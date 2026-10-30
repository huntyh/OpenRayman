#include "test_util.h"

#include <openrayman/bnm_bank.h>
#include <openrayman/error.h>
#include <openrayman/file.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <map>

using namespace openrayman;

namespace
{
	// Assembles a bank the way the shipped ones are laid out: header, events, resources, entries,
	// then the sample data. See docs/formats/sound.md.
	struct bank_builder
	{
		bytes out;
		std::vector<std::array<std::uint32_t, 8>> events;
		std::vector<std::array<std::uint32_t, 18>> resources;
		std::vector<std::uint32_t> extra;    // group/theme entry words
		std::vector<std::uint8_t> samples;   // the PCM region

		// event: editor id, type, target, pitch
		void event(std::uint32_t id, std::uint32_t type, std::uint32_t target, std::uint32_t pitch = 0)
		{
			std::array<std::uint32_t, 8> e{};
			e[0] = id;
			e[1] = type;
			e[2] = target;
			e[3] = pitch;
			e[4] = 0;
			e[5] = 0;
			e[6] = 1;
			e[7] = 1;
			events.push_back(e);
		}

		// A resource. Every field is explicit so the tests read like the format doc.
		struct resource_spec
		{
			std::uint32_t id = 0, type = 1, size = 0, offset = 0, volume = 127;
			std::uint32_t w18 = 0, w1c = 0, w20 = 0, w24 = 0, w28 = 0;
			std::uint32_t external = 0, flag30 = 0, size_again = 0;
			std::uint32_t rate = 22050, format = 0x00010010, data_format = 1;
			std::string name;
		};

		std::uint32_t resource(const resource_spec& spec)
		{
			std::array<std::uint32_t, 18> r{};
			r[0] = spec.id;
			r[1] = spec.type;
			r[2] = spec.external ? 0 : 1;
			r[3] = spec.size;
			r[4] = spec.offset;
			r[5] = spec.volume;
			r[6] = spec.w18;
			r[7] = spec.w1c;
			r[8] = spec.w20;
			r[9] = spec.w24;
			r[10] = spec.w28;
			r[11] = spec.external;
			r[12] = spec.flag30;
			r[13] = 0;
			r[14] = spec.size_again;
			r[15] = spec.rate;
			r[16] = spec.format;
			r[17] = spec.data_format;
			resources.push_back(r);
			names.push_back(spec.name);
			return std::uint32_t(resources.size() - 1);
		}

		std::vector<std::string> names;

		// Appends sample bytes and returns their offset in the PCM region.
		std::uint32_t sample(const std::vector<std::uint8_t>& data)
		{
			std::uint32_t offset = static_cast<std::uint32_t>(samples.size());
			samples.insert(samples.end(), data.begin(), data.end());
			while(samples.size() % 4)
				samples.push_back(0);
			return offset;
		}

		std::vector<std::byte> build() const
		{			const std::uint32_t event_offset = 0x2C;
			const std::uint32_t resource_offset = event_offset + std::uint32_t(events.size()) * 0x20;
			const std::uint32_t extra_offset = resource_offset + std::uint32_t(resources.size()) * 0x5C;
			const std::uint32_t extra_end = extra_offset + std::uint32_t(extra.size()) * 4;
			const std::uint32_t sample_end = extra_end + std::uint32_t(samples.size());

			bytes b;
			b.u32(0).u32(event_offset).u32(std::uint32_t(events.size())).u32(resource_offset)
				.u32(std::uint32_t(resources.size()));
			b.u32(extra_end).u32(extra_end).u32(extra_end).u32(sample_end).u32(sample_end); // regions
			b.u32(sample_end);                                                             // file size

			for(const auto& e : events)
				for(std::uint32_t w : e)
					b.u32(w);
			for(std::size_t i = 0; i < resources.size(); i++)
			{
				for(std::uint32_t w : resources[i])
					b.u32(w);
				b.str(names[i]).pad_to(b.data.size() + (16 - names[i].size()) + 4);
			}
			for(std::uint32_t w : extra)
				b.u32(w);
			for(std::uint8_t byte : samples)
				b.u8(byte);
			return b.data;
		}
	};
}

TEST_CASE("BNM: header, events and resources")
{
	bank_builder builder;
	builder.event(0x3000F, 1, 0);              // play resource 0
	builder.event(0x30000, 2, 0x10000);        // stop the voices event 0x10000 started
	builder.event(0x30001, 1, 1, 0x1999);      // play resource 1, pitch jitter

	bank_builder::resource_spec pcm;
	pcm.id = 0x4000F;
	pcm.size = 8;
	pcm.size_again = 8;
	pcm.name = "couine.wav";
	std::uint32_t first = builder.resource(pcm);

	bank_builder::resource_spec apm;
	apm.id = 0x40016;
	apm.size = 12345;
	apm.external = 1;
	apm.data_format = 4;
	apm.format = 0x00020010;
	apm.name = "JINGLE8.apm";
	std::uint32_t second = builder.resource(apm);

	std::uint32_t offset = builder.sample({ 1, 2, 3, 4, 5, 6, 7, 8 });
	builder.resources[first][4] = offset;

	bnm_bank bank(builder.build());

	CHECK(bank.header().event_count == 3);
	CHECK(bank.header().resource_count == 2);
	CHECK(bank.header().event_offset == 0x2C);
	CHECK(bank.header().resource_offset == 0x2C + 3 * 0x20);

	REQUIRE(bank.events().size() == 3);
	CHECK(bank.events()[0].type == bnm_event_type::play);
	CHECK(bank.events()[0].plays());
	CHECK(bank.events()[0].target == 0);
	CHECK(bank.events()[1].type == bnm_event_type::stop);
	CHECK_FALSE(bank.events()[1].plays());
	CHECK(bank.events()[2].pitch == 0x1999);

	REQUIRE(bank.resources().size() == 2);
	CHECK(bank.resources()[first].name == "couine.wav");
	CHECK(bank.resources()[first].type == bnm_resource_type::sample);
	CHECK(bank.resources()[first].rate == 22050);
	CHECK(bank.resources()[first].channels() == 1);
	CHECK(bank.resources()[first].bits() == 16);
	CHECK_FALSE(bnm_bank::is_external(bank.resources()[first]));

	CHECK(bank.resources()[second].name == "JINGLE8.apm");
	CHECK(bank.resources()[second].data_format == bnm_data_format::apm);
	CHECK(bank.resources()[second].channels() == 2);
	CHECK(bnm_bank::is_external(bank.resources()[second]));

	// Ids are bank << 16 | index.
	bnm_id id = bnm_id::of(0x00030001);
	CHECK(id.bank == 3);
	CHECK(id.index == 1);
	CHECK(id.value() == 0x00030001);
}

TEST_CASE("BNM: embedded samples come from the PCM region, external ones have no payload")
{
	bank_builder builder;
	builder.event(0x30000, 1, 0);

	bank_builder::resource_spec pcm;
	pcm.size = 8;
	pcm.size_again = 8;
	pcm.name = "tone.wav";
	std::uint32_t index = builder.resource(pcm);
	builder.resources[index][4] = builder.sample({ 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80 });

	bnm_bank bank(builder.build());

	CHECK(bank.pcm().size() == 8);
	std::span<const std::byte> payload = bank.sample(bank.resources()[index]);
	REQUIRE(payload.size() == 8);
	CHECK(std::to_integer<int>(payload[0]) == 0x10);
	CHECK(std::to_integer<int>(payload[7]) == 0x80);

	bank_builder::resource_spec apm;
	apm.external = 1;
	apm.data_format = 4;
	apm.name = "LOOP.apm";
	apm.size = 999;
	bank_builder with_apm;
	with_apm.event(0x30000, 1, 0);
	std::uint32_t apm_index = with_apm.resource(apm);
	bnm_bank second(with_apm.build());
	CHECK(second.sample(second.resources()[apm_index]).empty());
	CHECK(second.pcm().empty());
}

TEST_CASE("BNM: group and theme entries")
{
	bank_builder builder;
	builder.event(0x30000, 1, 0);

	// A type 8 group: two entries of two words, the id first.
	bank_builder::resource_spec group8;
	group8.type = bnm_resource_type::group8;
	group8.w20 = 2;
	group8.size = 2 * 8;
	std::uint32_t g8 = builder.resource(group8);
	builder.extra = { 0x50000, 3, 0x50001, 7 };

	// A type 10 group: three-word entries, id first.
	std::vector<std::uint32_t> extra = builder.extra;
	extra.insert(extra.end(), { 0x60000, 90, 1 });
	bank_builder::resource_spec group10;
	group10.type = bnm_resource_type::group10;
	group10.w18 = 1;
	group10.w1c = 90;
	group10.size = 12;
	group10.offset = 16; // byte offset: right after the four type 8 entry words
	std::uint32_t g10 = builder.resource(group10);
	builder.extra = extra;

	// A type 4 group keeps its id in the last word of each four-word entry, unlike the others.
	bnm_bank bank(builder.build());

	CHECK(bnm_bank::entry_stride(bnm_resource_type::group8) == 2);
	CHECK(bnm_bank::entry_stride(bnm_resource_type::group10) == 3);
	CHECK(bnm_bank::entry_stride(bnm_resource_type::theme) == 6);
	CHECK(bnm_bank::entry_stride(bnm_resource_type::sample) == 0);
	CHECK(bnm_bank::entry_id_word(bnm_resource_type::group8) == 0);
	CHECK(bnm_bank::entry_id_word(4) == 3);

	auto entries8 = bank.entries(bank.resources()[g8]);
	REQUIRE(entries8.size() == 2);
	CHECK(entries8[0][0] == 0x50000);
	CHECK(entries8[0][1] == 3);
	CHECK(entries8[1][0] == 0x50001);

	auto entries10 = bank.entries(bank.resources()[g10]);
	REQUIRE(entries10.size() == 1);
	CHECK(entries10[0][0] == 0x60000);
	CHECK(entries10[0][1] == 90);
	CHECK(entries10[0][2] == 1);
}

TEST_CASE("BNM: a theme lists segments and an optional tail")
{
	// The tail offset is relative to the theme's own entry block, so the test puts the theme
	// after another one: with a base-relative offset it would read into the first theme's
	// entries instead (that is the mistake FUN_00459840's pointer arithmetic guards against).
	bank_builder builder;
	builder.event(0x30000, 1, 0);

	bank_builder::resource_spec first;
	first.type = bnm_resource_type::theme;
	first.w24 = 2;
	first.w28 = 0xFFFFFFFF;
	first.size = 2 * 0x18;
	builder.extra = { 0x71000, 0, 0, 1, 0, 0, 0x71001, 0, 0, 1, 0, 0 };

	bank_builder::resource_spec theme;
	theme.type = bnm_resource_type::theme;
	theme.w24 = 1;
	theme.offset = 2 * 0x18;  // byte offset of this theme's block
	theme.w28 = 1 * 0x18;     // the tail id follows its one entry
	theme.size = 1 * 0x18 + 4;
	std::uint32_t with_tail = builder.resource(theme);
	builder.extra.insert(builder.extra.end(), { 0x70001, 0, 0, 1, 0, 0, 0x70002 });

	bnm_bank bank(builder.build());

	const bnm_resource& resource = bank.resources()[with_tail];
	CHECK(resource.offset == 2 * 0x18);
	CHECK(bnm_bank::entry_count(resource) == 1);
	auto entries = bank.entries(resource);
	REQUIRE(entries.size() == 1);
	CHECK(entries[0][0] == 0x70001);
	CHECK(bank.theme_tail(resource) == 0x70002);

	// A theme without a tail.
	bank_builder plain;
	plain.event(0x30000, 1, 0);
	bank_builder::resource_spec bare;
	bare.type = bnm_resource_type::theme;
	bare.w24 = 1;
	bare.w28 = 0xFFFFFFFF;
	bare.size = 0x18;
	plain.extra = { 0x70000, 0, 0, 0, 0, 0 };
	std::uint32_t without_tail = plain.resource(bare);
	bnm_bank second(plain.build());
	CHECK(second.theme_tail(second.resources()[without_tail]) == 0);
}

TEST_CASE("BNM: malformed banks are rejected")
{
	// The file size field has to match the file, as it does in every shipped bank.
	bank_builder builder;
	builder.event(0x30000, 1, 0);
	bytes good;
	good.data = builder.build();

	SUBCASE("too short for the header")
	{
		std::vector<std::byte> truncated(good.data.begin(), good.data.begin() + 16);
		CHECK_THROWS_AS((bnm_bank(truncated)), format_error);
	}
	SUBCASE("header size field is not 0x2C")
	{
		bytes broken = good;
		broken.data[4] = std::byte{ 0x30 };
		CHECK_THROWS_AS(bnm_bank(broken.data), format_error);
	}
	SUBCASE("resource table does not follow the events")
	{
		bytes broken = good;
		broken.data[0x0C] = std::byte{ 0xFF };
		CHECK_THROWS_AS(bnm_bank(broken.data), format_error);
	}
	SUBCASE("size field does not match the file")
	{
		bytes broken = good;
		broken.u32(0); // a trailing word changes the length but not the header
		CHECK_THROWS_AS(bnm_bank(broken.data), format_error);
	}
	SUBCASE("data regions out of order")
	{
		bytes broken = good;
		// regions are at 0x14..0x24; make the PCM start smaller than the MIDI start
		broken.data[0x1C] = std::byte{ 0x00 };
		broken.data[0x1D] = std::byte{ 0x00 };
		CHECK_THROWS_AS(bnm_bank(broken.data), format_error);
	}
}

// The real data: every shipped bank has to parse, and the totals have to match the format notes.
TEST_CASE("BNM: all shipped banks parse")
{
	auto dir = real_game_dir();
	if(!dir)
	{
		MESSAGE("OPENRAYMAN_GAME_DIR is not set, skipping");
		return;
	}
	auto sound = find_case_insensitive(*dir, "Data/World/Sound");
	if(!sound)
		sound = find_case_insensitive(*dir, "World/Sound");
	REQUIRE(sound.has_value());

	std::vector<std::filesystem::path> files;
	for(const auto& entry : std::filesystem::directory_iterator(*sound))
	{
		if(entry.path().extension() == ".bnm")
			files.push_back(entry.path());
	}
	std::sort(files.begin(), files.end());
	REQUIRE(files.size() == 405);

	std::size_t events = 0, resources = 0, samples = 0, pcm_samples = 0, external = 0;
	std::size_t themes = 0, theme_segments = 0, groups = 0, unnamed = 0, tails = 0;
	std::map<std::uint32_t, std::size_t> event_types, resource_types;

	for(const auto& path : files)
	{
		INFO(path.filename().string());
		bnm_bank bank = bnm_bank::load(path);
		// "Bnk_<n>.bnm", the id the sound ids use for this bank.
		const std::string stem = path.stem().string();
		const auto bank_id = static_cast<std::uint32_t>(std::stoul(stem.substr(stem.find('_') + 1)));
		events += bank.events().size();
		resources += bank.resources().size();

		for(const bnm_event& event : bank.events())
			event_types[event.type]++;
		for(const bnm_resource& resource : bank.resources())
		{
			resource_types[resource.type]++;
			if(resource.name.empty())
				unnamed++;

			if(resource.type == bnm_resource_type::sample)
			{
				samples++;
				if(bnm_bank::is_external(resource))
				{
					external++;
					// The loose file has to exist with the size the bank records.
					auto loose = find_case_insensitive(*sound, resource.name);
					REQUIRE(loose.has_value());
					CHECK(std::filesystem::file_size(*loose) == resource.size);
					continue;
				}
				pcm_samples++;
				CHECK(resource.rate == 22050);
				CHECK(resource.channels() == 1);
				CHECK(resource.bits() == 16);
				CHECK(bank.sample(resource).size() == resource.size);
			}
			else if(resource.type == bnm_resource_type::theme)
			{
				themes++;
				auto entries = bank.entries(resource);
				CHECK(entries.size() == resource.theme_count);
				theme_segments += entries.size();
				for(const auto& entry : entries)
				{
					bnm_id target = bnm_id::of(entry[0]);
					CHECK(target.bank < 405);
				}
				// Every shipped theme with a tail names a real external .apm resource there.
				// The tail is a *resource* id, not an event id, so it is resolved as one.
				if(resource.group4_count != 0xFFFFFFFF)
				{
					tails++;
					bnm_id tail = bnm_id::of(bank.theme_tail(resource));
					CHECK(tail.bank < 405);
					const bnm_resource* piece = tail.bank == bank_id ? bank.resource(tail.index) : nullptr;
					std::optional<bnm_bank> other;
					if(!piece)
					{
						auto tail_path = find_case_insensitive(*sound, std::format("Bnk_{}.bnm", tail.bank));
						REQUIRE(tail_path.has_value());
						other.emplace(bnm_bank::load(*tail_path));
						piece = other->resource(tail.index);
					}
					REQUIRE(piece);
					CHECK(piece->type == bnm_resource_type::sample);
					CHECK(bnm_bank::is_external(*piece));
					CHECK_FALSE(piece->name.empty());
				}
			}
			else if(bnm_bank::entry_stride(resource.type))
			{
				groups++;
				CHECK(bank.entries(resource).size() == bnm_bank::entry_count(resource));
			}
		}

		// The embedded samples tile the region with 4-byte alignment and no gaps.
		if(!bank.pcm().empty())
			CHECK(bank.pcm().size() % 4 == 0);
	}

	CHECK(events == 1012);
	CHECK(resources == 2055);
	CHECK(samples == 1830);
	CHECK(pcm_samples == 740);
	CHECK(external == 1090);
	CHECK(themes == 147);
	CHECK(theme_segments == 2093);
	CHECK(tails == 9);
	CHECK(groups == 78);
	CHECK(unnamed == 225);
	CHECK(event_types[bnm_event_type::play] == 875);
	CHECK(event_types[bnm_event_type::stop] == 137);
	CHECK(resource_types[bnm_resource_type::sample] == 1830);
	CHECK(resource_types[bnm_resource_type::theme] == 147);
}
