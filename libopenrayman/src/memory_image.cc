#include <openrayman/memory_image.h>
#include <openrayman/error.h>
#include <openrayman/file.h>
#include <openrayman/game_data.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>

namespace openrayman
{
	void memory_image::add(sna_file sna, const relocation_table& table)
	{
		std::size_t source = m_files++;

		for(const sna_block& block : sna.blocks())
		{
			auto it = m_by_id.find(block.id);
			if(it != m_by_id.end() && !it->second->data.empty())
				throw format_error("block " + block.id.to_string() + " is loaded twice");
			if(it != m_by_id.end())
			{
				// Replaces a runtime-only placeholder announced by an earlier file.
				*it->second = block;
			}
			else
			{
				auto owned = std::make_unique<sna_block>(block);
				m_by_id[block.id] = owned.get();
				m_blocks.push_back(std::move(owned));
			}
			m_source[block.id] = source;
		}

		for(const sna_region& region : sna.regions())
		{
			m_views[{ source, region.id }] = region;
			if(!m_by_id.contains(region.id))
			{
				// Announced but never loaded with data: memory the game allocates at runtime.
				auto placeholder = std::make_unique<sna_block>();
				placeholder->id = region.id;
				placeholder->base = region.base;
				placeholder->reserved_end = region.reserved_end;
				m_by_id[region.id] = placeholder.get();
				m_source[region.id] = source;
				m_blocks.push_back(std::move(placeholder));
			}
		}

		for(const auto& [id, list] : table.blocks)
		{
			const sna_block* block = find(id);
			if(!block || m_source.at(id) != source)
				throw format_error("relocation table refers to block " + id.to_string() + ", which this file doesn't load");
			for(const relocation& reloc : list)
			{
				if(reloc.address < block->base || reloc.address + 4 > block->base + block->data.size())
					throw format_error(std::format("pointer slot 0x{:08X} lies outside block {}", reloc.address, id.to_string()));
				m_pointers[key(id, reloc.address)] = reloc.target;
			}
		}
	}

	const sna_block* memory_image::find(block_id id) const
	{
		auto it = m_by_id.find(id);
		return it == m_by_id.end() ? nullptr : it->second;
	}

	std::vector<const sna_block*> memory_image::blocks() const
	{
		std::vector<const sna_block*> result;
		for(const auto& block : m_blocks)
			result.push_back(block.get());
		return result;
	}

	std::uint32_t memory_image::read_u32(const memory_location& at) const
	{
		if(std::size_t(at.offset) + 4 > at.block->data.size())
			throw format_error("read past the end of block " + at.block->id.to_string());
		std::uint32_t value;
		std::memcpy(&value, at.block->data.data() + at.offset, 4);
		return value;
	}

	bool memory_image::is_pointer(const memory_location& at) const
	{
		return m_pointers.contains(key(at.block->id, at.address()));
	}

	std::optional<memory_location> memory_image::resolve(std::size_t source, block_id target_id, std::uint32_t value) const
	{
		const sna_block* target = find(target_id);
		if(value == 0 || !target)
			return std::nullopt;

		// Use the address the writing file announced for the target, not the target's own.
		std::uint32_t base = target->base;
		std::uint32_t end = target->reserved_end;
		if(auto view = m_views.find({ source, target_id }); view != m_views.end())
		{
			base = view->second.base;
			end = view->second.reserved_end;
		}
		if(value < base || value > end)
			return std::nullopt;
		return memory_location{ target, value - base };
	}

	std::optional<memory_location> memory_image::follow(const memory_location& at) const
	{
		auto it = m_pointers.find(key(at.block->id, at.address()));
		if(it == m_pointers.end())
			return std::nullopt;
		return resolve(m_source.at(at.block->id), it->second, read_u32(at));
	}

	memory_image::statistics memory_image::check() const
	{
		statistics stats;
		for(const auto& [slot, target_id] : m_pointers)
		{
			stats.pointers++;
			block_id source{ std::uint8_t(slot >> 40), std::uint8_t(slot >> 32) };
			const sna_block* block = find(source);
			memory_location at{ block, std::uint32_t(slot) - block->base };

			auto target = resolve(m_source.at(source), target_id, read_u32(at));
			if(!target)
				stats.dangling++;
			else if(target->offset < target->block->data.size())
				stats.into_data++;
			else
				stats.into_runtime++;
		}
		return stats;
	}

	static std::filesystem::path require(const std::optional<std::filesystem::path>& path, const std::string& what)
	{
		if(!path)
			throw format_error(what + " not found");
		return *path;
	}

	memory_image load_level_memory(const game_data& game, const std::string& level)
	{
		const auto& levels = game.game_dsb().levels;
		auto it = std::ranges::find_if(levels, [&](const std::string& name) {
			return std::ranges::equal(name, level, [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
		});
		if(it == levels.end())
			throw format_error("unknown level " + level);
		auto index = static_cast<std::uint32_t>(it - levels.begin());

		std::filesystem::path levels_dir = require(game.directory("levels"), "levels directory");

		memory_image image;
		image.add(sna_file(read_file(require(find_case_insensitive(levels_dir, "Fix.sna"), "Fix.sna"))),
			parse_relocation_table(read_file(require(find_case_insensitive(levels_dir, "Fix.rtb"), "Fix.rtb"))));

		levels_dat dat(require(find_case_insensitive(levels_dir, "LEVELS0.DAT"), "LEVELS0.DAT"));
		std::filesystem::path sna = require(find_case_insensitive(levels_dir, *it + "/" + *it + ".sna"), *it + ".sna");
		image.add(sna_file(read_file(sna)), dat.read(index, relocation_type::sna));
		return image;
	}
}
