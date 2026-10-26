#include "test_util.h"

#include <openrayman/apm_audio.h>
#include <openrayman/cnt_archive.h>
#include <openrayman/dsb_script.h>
#include <openrayman/error.h>
#include <openrayman/gf_texture.h>

#include <doctest/doctest.h>

using namespace openrayman;

TEST_CASE("GF: run-length planes decode to top-down RGBA")
{
	const std::uint8_t rle = 0xAA;
	bytes b;
	b.u32(888).u32(2).u32(2).u8(3).u8(rle);
	b.u8(rle).u8(10).u8(4);             // blue: 10 x4
	b.u8(1).u8(2).u8(3).u8(4);          // green: literal
	b.u8(rle).u8(7).u8(2).u8(8).u8(9);  // red: 7 7 8 9

	gf_texture t = decode_gf(b.data);
	REQUIRE(t.width == 2);
	REQUIRE(t.height == 2);
	CHECK(t.channels == 3);

	// GF rows are bottom-up, so file pixel 2 ends up top left.
	const std::vector<std::uint8_t> expected = {
		8, 3, 10, 255,   9, 4, 10, 255,
		7, 1, 10, 255,   7, 2, 10, 255,
	};
	CHECK(t.rgba == expected);
}

TEST_CASE("GF: bad input is rejected")
{
	bytes wrong_format;
	wrong_format.u32(1234).u32(1).u32(1).u8(3).u8(0);
	CHECK_THROWS_AS(decode_gf(wrong_format.data), format_error);

	bytes truncated;
	truncated.u32(8888).u32(4).u32(4).u8(4).u8(0xAA).u8(1);
	CHECK_THROWS_AS(decode_gf(truncated.data), format_error);
}

static bytes apm_header(std::uint16_t channels, std::int32_t first_predictor, std::int32_t second_predictor)
{
	bytes b;
	b.u16(0x2000).u16(channels).u32(22050);
	b.pad_to(0x14).str("vs12");
	b.pad_to(0x2C).u32(static_cast<std::uint32_t>(first_predictor)).u32(0);
	b.pad_to(0x38).u32(static_cast<std::uint32_t>(second_predictor)).u32(0);
	b.pad_to(0x60).str("DATA");
	return b;
}

TEST_CASE("APM: stereo IMA ADPCM, (left, right) byte pairs, high nibble first")
{
	// The right channel's state comes first in the header (0x2C), the left one second (0x38).
	bytes b = apm_header(2, 100, -100);
	b.u8(0x77).u8(0x08);

	apm_decoder decoder(b.data);
	CHECK(decoder.channels() == 2);
	CHECK(decoder.sample_rate() == 22050);
	REQUIRE(decoder.frames() == 2);

	// Left: nibble 7 at step 7 adds 11, the index jumps by 8 (step 16), then adds 30.
	// Right: nibble 0 and 8 at step 7 both round to no change.
	const std::vector<std::int16_t> expected = { -89, 100, -59, 100 };
	CHECK(decoder.decode_all() == expected);
	CHECK(decoder.frames_left() == 0);

	decoder.reset();
	CHECK(decoder.frames_left() == 2);
}

TEST_CASE("APM: mono files decode two samples per byte")
{
	bytes b = apm_header(1, 100, 0);
	b.u8(0x77);

	apm_decoder decoder(b.data);
	CHECK(decoder.channels() == 1);
	REQUIRE(decoder.frames() == 2);
	CHECK(decoder.decode_all() == std::vector<std::int16_t>{ 111, 141 });
}

TEST_CASE("APM: non-APM data is rejected")
{
	bytes b;
	b.u16(1).u16(2).u32(44100).pad_to(0x70);
	CHECK_THROWS_AS(apm_decoder(b.data), format_error);
}

// Applies the DSB obfuscation to plain data, producing a file decode_dsb() turns back into it.
static std::vector<std::byte> encode_dsb(const std::vector<std::byte>& plain)
{
	bytes keyed;
	keyed.u32(0x6AB5CC79);
	keyed.data.insert(keyed.data.end(), plain.begin(), plain.end());
	std::vector<std::byte> masked = decode_dsb(keyed.data); // XOR is its own inverse
	std::vector<std::byte> file(keyed.data.begin(), keyed.data.begin() + 4);
	file.insert(file.end(), masked.begin(), masked.end());
	return file;
}

static bytes& dsb_string(bytes& b, const std::string& s)
{
	return b.u16(static_cast<std::uint16_t>(s.size() + 1)).str(s).u8(0);
}

TEST_CASE("DSB: decode round trip and section parsing")
{
	bytes plain;
	plain.u32(0x00).u32(0x08).u32(1000).u32(0x10).u32(5).u32(6).u32(0xFFFF);
	plain.u32(0x28).u32(0x2D);
	dsb_string(plain, "\\World\\Sound").u32(0xFFFF);
	plain.u32(0x40).u32(0x42);
	dsb_string(plain, "\\Textures.cnt").u32(0xFFFF);
	plain.u32(0x1E).u32(0).u32(0x1F);
	dsb_string(plain, "Menu").u32(0x1F);
	dsb_string(plain, "Jail_10").u32(0xFFFF);
	plain.u32(0x46).u32(0x48);
	dsb_string(plain, "Random<VigA,1,1,VigB,1,6>").u32(0x4C).f32(1).f32(0.5f).f32(0).f32(1).u32(0x51).u32(80).u32(0xFFFF);
	plain.u32(0x5C).u32(0x5D).u32(2).u32(3).u32(9).u32(0xFFFF);
	plain.u32(0x5A).u32(0x5B).u32(40).u32(0xFFFF);

	std::vector<std::byte> file = encode_dsb(plain.data);
	CHECK(decode_dsb(file) == plain.data);

	dsb_script dsb = parse_dsb(file);
	CHECK(dsb.memory.at(0x08) == 1000);
	CHECK(dsb.memory.at(0x10) == 5);
	CHECK(dsb.directories.at("sound") == "\\World\\Sound");
	CHECK(dsb.texture_archives == std::vector<std::string>{ "\\Textures.cnt" });
	CHECK(dsb.levels == std::vector<std::string>{ "Menu", "Jail_10" });
	REQUIRE(dsb.vignettes.size() == 1);
	CHECK(dsb.vignettes[0].images == std::vector<std::string>{ "VigA", "VigB" });
	CHECK(dsb.vignettes[0].bar_outside_color[1] == 0.5f);
	CHECK(dsb.vignettes[0].bar_max == 80);
	CHECK(dsb.sound_banks == std::vector<std::uint32_t>{ 3, 9 });
}

TEST_CASE("DSB: unknown sections are reported, not skipped")
{
	bytes plain;
	plain.u32(0x99).u32(0xFFFF);
	CHECK_THROWS_AS(parse_dsb(encode_dsb(plain.data)), format_error);
}

TEST_CASE("CNT: directory, name obfuscation and content XOR")
{
	const std::uint8_t key = 0x04;
	auto xored = [&](const std::string& s) {
		std::string r = s;
		for(char& c : r)
			c = static_cast<char>(c ^ key);
		return r;
	};
	const std::uint8_t file_key[4] = { 1, 2, 3, 4 };
	const std::uint8_t content[6] = { 10, 20, 30, 40, 50, 60 };

	bytes b;
	b.u32(1).u32(2).u8(1).u8(1).u8(key);
	b.u32(9).str(xored("textures\\"));
	b.u8(0); // directory checksum

	std::size_t offset_field[2];
	for(int n = 0; n < 2; n++)
	{
		std::string name = n == 0 ? "Wall.gf" : "root.gf";
		b.u32(n == 0 ? 0 : 0xFFFFFFFF).u32(static_cast<std::uint32_t>(name.size())).str(xored(name));
		for(auto k : file_key)
			b.u8(k);
		b.u32(0); // checksum
		offset_field[n] = b.data.size();
		b.u32(0).u32(6);
	}
	for(int n = 0; n < 2; n++)
	{
		auto offset = static_cast<std::uint32_t>(b.data.size());
		std::memcpy(b.data.data() + offset_field[n], &offset, 4);
		// The first size - size % 4 bytes are XORed, the tail is stored as is.
		for(int i = 0; i < 6; i++)
			b.u8(static_cast<std::uint8_t>(i < 4 ? content[i] ^ file_key[i] : content[i]));
	}

	cnt_archive archive(write_temp("archive.cnt", b.data));
	REQUIRE(archive.entries().size() == 2);
	CHECK(archive.entries()[0].path == "textures/Wall.gf");
	CHECK(archive.entries()[1].path == "root.gf");

	const cnt_entry* entry = archive.find("TEXTURES\\wall.GF");
	REQUIRE(entry != nullptr);
	std::vector<std::byte> data = archive.read(*entry);
	REQUIRE(data.size() == 6);
	for(int i = 0; i < 6; i++)
		CHECK(static_cast<std::uint8_t>(data[i]) == content[i]);

	CHECK(archive.find("missing.gf") == nullptr);
}
