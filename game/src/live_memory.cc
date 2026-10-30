#include <openrayman/game/live_memory.h>
#include <openrayman/error.h>

#include <algorithm>
#include <format>
#include <utility>

namespace openrayman::game
{
	namespace
	{
		// Runtime-only blocks are allocated up to this size; larger reservations are cut short.
		constexpr std::uint32_t max_runtime_block = 64 << 20;
		// What allocate() can hand out in all.
		constexpr std::uint32_t heap_size = 4 << 20;
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
		auto inside = [&](const region& r) {
			return address >= r.base && std::uint64_t(address) + size <= std::uint64_t(r.base) + r.data.size();
		};
		if(m_last && inside(*m_last))
			return m_last;
		auto it = m_regions.upper_bound(address);
		if(it == m_regions.begin())
			return nullptr;
		--it;
		const region& r = it->second;
		if(!inside(r))
			return nullptr;
		m_last = &r;
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

	std::uint32_t live_memory::allocate(std::size_t size)
	{
		if(!m_heap)
		{
			std::uint64_t end = 0;
			for(const auto& [base, r] : m_regions)
				end = std::max(end, std::uint64_t(base) + r.data.size());
			end = (end + 0xFFFFF) & ~std::uint64_t(0xFFFFF);
			if(end + heap_size > 0xFFFFFFFFull)
				throw format_error("no address space left for run-time allocations");
			region r;
			r.base = std::uint32_t(end);
			r.data.resize(heap_size);
			m_heap = r.base;
			m_regions.emplace(r.base, std::move(r));
			m_last = nullptr;
		}
		std::uint32_t at = m_heap + m_heap_used;
		std::size_t rounded = (size + 15) & ~std::size_t(15);
		if(m_heap_used + rounded > heap_size)
			throw format_error(std::format("run-time allocation of {} bytes doesn't fit", size));
		m_heap_used += std::uint32_t(rounded);
		return at;
	}
}
