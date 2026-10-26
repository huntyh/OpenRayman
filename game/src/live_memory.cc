#include <openrayman/game/live_memory.h>
#include <openrayman/error.h>

#include <format>

namespace openrayman::game
{
	namespace
	{
		// Runtime-only blocks are allocated up to this size; larger reservations are cut short.
		constexpr std::uint32_t max_runtime_block = 64 << 20;
	}

	live_memory::live_memory(const memory_image& image)
	{
		for(const sna_block* block : image.blocks())
		{
			region r;
			r.base = block->base;
			r.data = block->data;
			if(r.data.empty() && block->reserved_end > block->base)
				r.data.resize(std::min(block->reserved_end - block->base, max_runtime_block));
			if(r.data.empty())
				continue;
			if(auto next = m_regions.lower_bound(r.base); next != m_regions.end() && next->first < r.base + r.data.size())
				throw format_error(std::format("block {} overlaps another block in memory", block->id.to_string()));
			m_regions.emplace(r.base, std::move(r));
		}
		for(const auto& [slot, target] : image.pointers())
		{
			std::uint32_t value = target.address();
			if(image.read_u32(slot) != value)
				m_moved++;
			write(slot.address(), value);
			m_relocated++;
		}
	}

	const live_memory::region* live_memory::find(std::uint32_t address, std::size_t size) const
	{
		auto it = m_regions.upper_bound(address);
		if(it == m_regions.begin())
			return nullptr;
		--it;
		const region& r = it->second;
		if(std::uint64_t(address) + size > std::uint64_t(r.base) + r.data.size())
			return nullptr;
		return &r;
	}

	bool live_memory::contains(std::uint32_t address, std::size_t size) const
	{
		return find(address, size) != nullptr;
	}

	std::span<const std::byte> live_memory::span(std::uint32_t address, std::size_t size) const
	{
		const region* r = find(address, size);
		if(!r)
			throw format_error(std::format("game memory access outside every block: 0x{:08X}, {} bytes", address, size));
		return std::span(r->data).subspan(address - r->base, size);
	}

	std::span<std::byte> live_memory::span(std::uint32_t address, std::size_t size)
	{
		auto bytes = std::as_const(*this).span(address, size);
		return { const_cast<std::byte*>(bytes.data()), bytes.size() };
	}
}
