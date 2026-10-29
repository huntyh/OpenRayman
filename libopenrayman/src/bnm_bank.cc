// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/bnm_bank.h>
#include <openrayman/binary_reader.h>
#include <openrayman/error.h>
#include <openrayman/file.h>

#include <algorithm>
#include <format>

// Layout and semantics: docs/formats/sound.md. Addresses in the comments are the exe's.
//
//   0x00  header, 11 u32
//   0x2C  events, 0x20 each
//         resources, 0x5C each
//         group and theme entries (the "extra area")
//         sample regions: MPEG, MIDI, PCM, ADPCM (only PCM is used in the shipped banks)
//
// FUN_00453990 reads the file in that order; FUN_00453BF0 then resolves the ids inside events and
// group entries to pointers. Here everything stays an id, which is what the data actually stores.

namespace openrayman
{
	namespace
	{
		constexpr std::uint32_t header_size = 0x2C;
		constexpr std::uint32_t event_size = 0x20;
		constexpr std::uint32_t resource_size = 0x5C;
		constexpr std::uint32_t no_tail = 0xFFFFFFFF;

		// Region indices in bnm_header::regions.
		constexpr std::size_t region_mpeg = 0, region_midi = 1, region_pcm = 2, region_adpcm = 3, region_end = 4;

		// A resource's data area, as FUN_00453BF0 picks it.
		enum class data_area
		{
			none,
			sample,  // +0x34
			mpeg,    // +0x2C
			midi,    // +0x30
			adpcm,   // +0x38
			extra,   // +0x28, the group and theme entries
		};

		data_area area_of(const bnm_resource& resource)
		{
			switch(resource.type)
			{
				case bnm_resource_type::sample:
					if(resource.external)
						return data_area::none;
					switch(resource.data_format)
					{
						case bnm_data_format::pcm: return data_area::sample;
						case bnm_data_format::mpeg: return data_area::mpeg;
						case bnm_data_format::apm: return data_area::adpcm;
						default: return data_area::none;
					}
				case 2: return data_area::midi;
				case 3: return data_area::sample;
				case 4:
				case 6:
				case 8:
				case 10:
				case 11: return data_area::extra;
				default: return data_area::none;
			}
		}

		void require_range(std::size_t begin, std::size_t end, std::size_t limit, const char* what)
		{
			if(begin > end || end > limit)
				throw format_error(std::format("BNM {} is out of range", what));
		}
	}

	bool bnm_event::plays() const
	{
		switch(type)
		{
			case bnm_event_type::play:
			case 6:
			case 7:
			case 8:
			case 10: return true;
			default: return false;
		}
	}

	bnm_bank::bnm_bank(std::vector<std::byte> data) : m_data(std::move(data))
	{
		parse(m_data);
	}

	bnm_bank::bnm_bank(std::span<const std::byte> data)
	{
		m_data.assign(data.begin(), data.end());
		parse(m_data);
	}

	bnm_bank bnm_bank::load(const std::filesystem::path& path)
	{
		return bnm_bank(read_file(path));
	}

	void bnm_bank::parse(std::span<const std::byte> data)
	{
		if(data.size() < header_size)
			throw format_error("BNM file is shorter than its header");

		binary_reader reader(data);
		m_header.unused = reader.u32();
		m_header.event_offset = reader.u32();
		m_header.event_count = reader.u32();
		m_header.resource_offset = reader.u32();
		m_header.resource_count = reader.u32();
		for(std::uint32_t& region : m_header.regions)
			region = reader.u32();
		m_header.file_size = reader.u32();

		// The shipped banks hold all of these; a violated one means this isn't a bank at all.
		if(m_header.event_offset != header_size)
			throw format_error("not a BNM bank (bad header size)");
		if(m_header.resource_offset != header_size + std::uint64_t(m_header.event_count) * event_size)
			throw format_error("not a BNM bank (resource table does not follow the events)");
		if(m_header.file_size != data.size())
			throw format_error("not a BNM bank (size field does not match the file)");

		const auto& r = m_header.regions;
		if(!(r[region_mpeg] <= r[region_midi] && r[region_midi] <= r[region_pcm] &&
			r[region_pcm] <= r[region_adpcm] && r[region_adpcm] <= r[region_end]))
			throw format_error("BNM data regions are not in ascending order");
		if(r[region_end] > data.size())
			throw format_error("BNM data regions run past the end of the file");

		m_extra_begin = std::size_t(m_header.resource_offset) + std::size_t(m_header.resource_count) * resource_size;
		if(m_extra_begin > r[region_pcm])
			throw format_error("BNM resource table runs into the sample data");

		reader.seek(m_header.event_offset);
		m_events.reserve(m_header.event_count);
		for(std::uint32_t i = 0; i < m_header.event_count; i++)
		{
			bnm_event event;
			event.editor_id = reader.u32();
			event.type = reader.u32();
			event.target = reader.u32();
			event.pitch = reader.u32();
			for(std::uint32_t& word : event.raw)
				word = reader.u32();
			m_events.push_back(std::move(event));
		}

		reader.seek(m_header.resource_offset);
		m_resources.reserve(m_header.resource_count);
		for(std::uint32_t i = 0; i < m_header.resource_count; i++)
		{
			bnm_resource resource;
			resource.editor_id = reader.u32();
			resource.type = reader.u32();
			resource.embedded = reader.u32();
			resource.size = reader.u32();
			resource.offset = reader.u32();
			resource.volume = reader.u32();
			resource.group10_count = reader.u32();
			resource.gain = reader.u32();
			resource.group8_count = reader.u32();
			resource.theme_count = reader.u32();
			resource.group4_count = reader.u32();
			resource.external = reader.u32();
			resource.flag30 = reader.u32();
			resource.unknown34 = reader.u32();
			resource.size_again = reader.u32();
			resource.rate = reader.u32();
			resource.format = reader.u32();
			resource.data_format = reader.u32();
			resource.name = reader.string(16);
			reader.skip(4); // padding up to the next record

			if(area_of(resource) == data_area::extra)
			{
				const std::size_t stride = entry_stride(resource.type);
				const std::size_t count = entry_count(resource);
				const std::size_t begin = m_extra_begin + resource.offset;
				if(begin > r[region_pcm] || count * stride * 4 > r[region_pcm] - begin)
					throw format_error("BNM group entries run past the extra area");
			}
			m_resources.push_back(std::move(resource));
		}
	}

	const bnm_event* bnm_bank::event(std::uint32_t index) const
	{
		return index < m_events.size() ? &m_events[index] : nullptr;
	}

	const bnm_resource* bnm_bank::resource(std::uint32_t index) const
	{
		return index < m_resources.size() ? &m_resources[index] : nullptr;
	}

	std::span<const std::byte> bnm_bank::area(std::size_t begin, std::size_t end) const
	{
		if(begin > end || end > m_data.size())
			return {};
		return std::span<const std::byte>(m_data).subspan(begin, end - begin);
	}

	std::span<const std::byte> bnm_bank::pcm() const
	{
		const auto& r = m_header.regions;
		return area(r[region_pcm], r[region_adpcm]);
	}

	bool bnm_bank::is_external(const bnm_resource& resource)
	{
		return resource.type == bnm_resource_type::sample && resource.data_format == bnm_data_format::apm;
	}

	std::span<const std::byte> bnm_bank::sample(const bnm_resource& resource) const
	{
		if(resource.type != bnm_resource_type::sample)
			return {};

		const auto& r = m_header.regions;
		switch(area_of(resource))
		{
			case data_area::sample:
			{
				const std::size_t begin = std::size_t(r[region_pcm]) + resource.offset;
				if(begin > r[region_adpcm] || resource.size > r[region_adpcm] - begin)
					throw format_error("BNM sample runs past its data region");
				return area(begin, begin + resource.size);
			}
			case data_area::mpeg:
			{
				const std::size_t begin = std::size_t(r[region_mpeg]) + resource.offset;
				if(begin > r[region_midi] || resource.size > r[region_midi] - begin)
					throw format_error("BNM sample runs past its data region");
				return area(begin, begin + resource.size);
			}
			case data_area::adpcm:
			{
				const std::size_t begin = std::size_t(r[region_adpcm]) + resource.offset;
				if(begin > r[region_end] || resource.size > r[region_end] - begin)
					throw format_error("BNM sample runs past its data region");
				return area(begin, begin + resource.size);
			}
			default: return {};
		}
	}

	std::size_t bnm_bank::entry_stride(std::uint32_t type)
	{
		switch(type)
		{
			case 4: return 4;
			case bnm_resource_type::group8: return 2;
			case bnm_resource_type::group10: return 3;
			case bnm_resource_type::theme: return 6;
			default: return 0;
		}
	}

	std::uint32_t bnm_bank::entry_count(const bnm_resource& resource)
	{
		switch(resource.type)
		{
			case 4: return resource.group4_count;
			case bnm_resource_type::group8: return resource.group8_count;
			case bnm_resource_type::group10: return resource.group10_count;
			case bnm_resource_type::theme: return resource.theme_count;
			default: return 0;
		}
	}

	std::size_t bnm_bank::entry_id_word(std::uint32_t type)
	{
		return type == 4 ? 3 : 0;
	}

	std::vector<std::array<std::uint32_t, 6>> bnm_bank::entries(const bnm_resource& resource) const
	{
		std::vector<std::array<std::uint32_t, 6>> result;
		const std::size_t stride = entry_stride(resource.type);
		if(!stride || area_of(resource) != data_area::extra)
			return result;

		const std::size_t count = entry_count(resource);
		const std::size_t begin = m_extra_begin + resource.offset;
		const std::size_t bytes = count * stride * 4;
		if(begin > m_header.regions[region_pcm] || bytes > m_header.regions[region_pcm] - begin)
			throw format_error("BNM group entries run past the extra area");

		binary_reader reader(area(begin, begin + bytes));
		result.reserve(count);
		for(std::size_t i = 0; i < count; i++)
		{
			std::array<std::uint32_t, 6> entry{};
			for(std::size_t word = 0; word < stride; word++)
				entry[word] = reader.u32();
			result.push_back(entry);
		}
		return result;
	}

	std::uint32_t bnm_bank::theme_tail(const bnm_resource& resource) const
	{
		if(resource.type != bnm_resource_type::theme || resource.group4_count == no_tail)
			return 0;

		// FUN_00459840 takes the entry block as its base and adds the +0x28 offset to it, so the
		// tail follows this theme's own entries rather than the start of the extra area.
		const std::size_t begin = m_extra_begin + resource.offset + resource.group4_count;
		if(begin + 4 > m_header.regions[region_pcm])
			throw format_error("BNM theme tail is outside the extra area");
		return binary_reader(area(begin, begin + 4)).u32();
	}
}
