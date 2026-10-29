// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace openrayman
{
	// A sound id as the game data stores it: bank << 16 | index. See docs/formats/sound.md.
	struct bnm_id
	{
		std::uint32_t bank = 0;
		std::uint32_t index = 0;

		static constexpr bnm_id of(std::uint32_t value) { return { value >> 16, value & 0xFFFF }; }
		constexpr std::uint32_t value() const { return bank << 16 | (index & 0xFFFF); }
		bool operator==(const bnm_id&) const = default;
	};

	// Event types. Only the first two occur in the shipped banks; the rest are the exe's other cases.
	namespace bnm_event_type
	{
		constexpr std::uint32_t play = 1;      // play the resource at +0x08
		constexpr std::uint32_t stop = 2;      // stop the voices this event started
		constexpr std::uint32_t stop_bank = 3; // stop every voice of the channel
		constexpr std::uint32_t sequence = 4;  // play +0x08, then +0x0C
		constexpr std::uint32_t theme = 9;     // music theme control
		constexpr std::uint32_t volume = 11;   // push a volume onto the channel
	}

	// Resource types.
	namespace bnm_resource_type
	{
		constexpr std::uint32_t sample = 1;  // PCM in the bank, or an external .apm
		constexpr std::uint32_t group4 = 4;  // group of samples, entries with the id last
		constexpr std::uint32_t group8 = 8;  // variants of one sound, selected by a value
		constexpr std::uint32_t group10 = 10; // layered samples
		constexpr std::uint32_t theme = 11;  // music theme: an ordered list of .apm segments
	}

	// Data format ids of a sample resource (+0x44).
	namespace bnm_data_format
	{
		constexpr std::uint32_t pcm = 1;    // 16-bit PCM in the bank's sample region
		constexpr std::uint32_t mpeg = 2;
		constexpr std::uint32_t apm = 4;    // external .apm file, IMA ADPCM
	}

	// The bank header: eleven u32 at the start of the file.
	struct bnm_header
	{
		std::uint32_t unused = 0;        // 0x00, always 0
		std::uint32_t event_offset = 0;  // 0x04, 0x2C
		std::uint32_t event_count = 0;   // 0x08
		std::uint32_t resource_offset = 0; // 0x0C, event_offset + event_count * 0x20
		std::uint32_t resource_count = 0;  // 0x10
		std::array<std::uint32_t, 5> regions{}; // 0x14..0x24: MPEG, MIDI, PCM, ADPCM starts and the end
		std::uint32_t file_size = 0;     // 0x28
	};

	// One event: what a sound id names. The named fields are the ones we could verify; raw keeps the rest.
	struct bnm_event
	{
		std::uint32_t editor_id = 0; // +0x00
		std::uint32_t type = 0;      // +0x04, see bnm_event_type
		std::uint32_t target = 0;    // +0x08: resource id for play, event id for stop/sequence
		std::uint32_t pitch = 0;     // +0x0C: random variation, 16.16 (0 = exact)
		std::array<std::uint32_t, 4> raw{}; // +0x10, +0x14, +0x18, +0x1C

		// True if this event plays its target (as opposed to stopping or steering).
		bool plays() const;
	};

	// One resource: a sound, or a list of them.
	struct bnm_resource
	{
		std::uint32_t editor_id = 0;   // +0x00
		std::uint32_t type = 0;        // +0x04, see bnm_resource_type
		std::uint32_t embedded = 0;    // +0x08: 1 = data in this bank
		std::uint32_t size = 0;        // +0x0C
		std::uint32_t offset = 0;      // +0x10, into the resource's data area
		std::uint32_t volume = 0;      // +0x14, 0..127
		std::uint32_t group10_count = 0; // +0x18
		std::uint32_t gain = 0;        // +0x1C: type 10 gain, type 11 start segment
		std::uint32_t group8_count = 0;  // +0x20
		std::uint32_t theme_count = 0;   // +0x24
		std::uint32_t group4_count = 0;  // +0x28; for a theme, the tail entry offset or 0xFFFFFFFF
		std::uint32_t external = 0;    // +0x2C: 1 = data lives outside the bank
		std::uint32_t flag30 = 0;      // +0x30, meaning unknown
		std::uint32_t unknown34 = 0;   // +0x34
		std::uint32_t size_again = 0;  // +0x38
		std::uint32_t rate = 0;        // +0x3C
		std::uint32_t format = 0;      // +0x40: 0x00010010 mono 16-bit, 0x00020010 stereo 16-bit
		std::uint32_t data_format = 0; // +0x44, see bnm_data_format
		std::string name;              // +0x48, NUL padded, may be empty

		std::uint16_t channels() const { return static_cast<std::uint16_t>(format >> 16); }
		std::uint16_t bits() const { return static_cast<std::uint16_t>(format & 0xFFFF); }
	};

	// A BNM sound bank (World/Sound/Bnk_<n>.bnm). Holds the whole file: the banks are at most a few MB.
	class bnm_bank
	{
	public:
		// Takes the file's contents. Throws format_error if it isn't a valid bank.
		explicit bnm_bank(std::vector<std::byte> data);
		explicit bnm_bank(std::span<const std::byte> data);

		// Reads and parses a bank file.
		static bnm_bank load(const std::filesystem::path& path);

		const bnm_header& header() const { return m_header; }

		const std::vector<bnm_event>& events() const { return m_events; }
		const std::vector<bnm_resource>& resources() const { return m_resources; }

		// Null (or out of range) lookup by index.
		const bnm_event* event(std::uint32_t index) const;
		const bnm_resource* resource(std::uint32_t index) const;

		// The sample data region (region 3). Empty in the 136 banks without one.
		std::span<const std::byte> pcm() const;

		// The payload of a type 1 resource as stored in the bank: headerless 16-bit PCM from the
		// sample region (or the MPEG/ADPCM region its sub-format names). Empty for external
		// resources, which have no payload here, and for everything that isn't a sample.
		std::span<const std::byte> sample(const bnm_resource& resource) const;

		// True for a sample whose data is a loose .apm file in the sound directory rather than a
		// payload in the bank.
		static bool is_external(const bnm_resource& resource);

		// The raw entry words of a group or theme resource: 2 per type 8 entry, 3 per type 10,
		// 4 per type 4 and 6 per type 11. The first word of a type 8/10 entry, and the last word
		// of a type 4 entry, is the id of the referenced resource.
		std::vector<std::array<std::uint32_t, 6>> entries(const bnm_resource& resource) const;

		// Entry stride in words for a group/theme resource, or 0 if it isn't one.
		static std::size_t entry_stride(std::uint32_t type);

		// How many entries a group/theme resource has.
		static std::uint32_t entry_count(const bnm_resource& resource);

		// Index of the id word inside an entry (type 4 keeps it last).
		static std::size_t entry_id_word(std::uint32_t type);

		// For a theme: the resource id stored at +0x28 words past this theme's own entries
		// (FUN_00459840 resolves that one word and plays it when the list runs out), or 0 when
		// the theme has no tail entry (+0x28 = 0xFFFFFFFF). The offset is relative to the entry
		// block, not to the start of the extra area.
		std::uint32_t theme_tail(const bnm_resource& resource) const;

	private:
		void parse(std::span<const std::byte> data);
		std::span<const std::byte> area(std::size_t begin, std::size_t end) const;

		std::vector<std::byte> m_data;
		bnm_header m_header;
		std::vector<bnm_event> m_events;
		std::vector<bnm_resource> m_resources;
		// Where the group/theme entries start: past the resource table.
		std::size_t m_extra_begin = 0;
	};
}
