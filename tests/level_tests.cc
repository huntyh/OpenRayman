#include "test_util.h"

#include <openrayman/error.h>
#include <openrayman/game_data.h>
#include <openrayman/mask.h>
#include <openrayman/memory_image.h>
#include <openrayman/relocation.h>
#include <openrayman/sna_file.h>

#include <doctest/doctest.h>

using namespace openrayman;

TEST_CASE("mask: decoding is its own inverse")
{
	std::vector<std::byte> data = { std::byte(1), std::byte(2), std::byte(3), std::byte(250) };
	auto original = data;
	mask(1234).decode(data);
	CHECK(data != original);
	mask(1234).decode(data);
	CHECK(data == original);
}

// Builds a masked SNA file from plain block descriptions.
struct sna_builder
{
	bytes plain;

	sna_builder& block(std::uint8_t module, std::uint8_t id, std::uint32_t base, std::uint32_t reserved_end, const bytes& data)
	{
		plain.u8(module).u8(id).u8(0).u32(base);
		plain.u32(reserved_end).u32(0).u32(0).u32(static_cast<std::uint32_t>(data.data.size()));
		plain.data.insert(plain.data.end(), data.data.begin(), data.data.end());
		return *this;
	}

	std::vector<std::byte> build() const
	{
		const std::uint32_t seed = 0x6AB5CC79;
		bytes file;
		file.u32(seed);
		std::vector<std::byte> masked = plain.data;
		mask(seed).decode(masked);
		file.data.insert(file.data.end(), masked.begin(), masked.end());
		return file.data;
	}
};

static bytes& table_block(bytes& b, std::uint8_t module, std::uint8_t id, std::vector<std::pair<std::uint32_t, block_id>> pointers)
{
	b.u8(module).u8(id).u32(static_cast<std::uint32_t>(pointers.size()));
	for(auto [address, target] : pointers)
		b.u32(address).u8(target.module).u8(target.id).u8(0xCD).u8(0xCD);
	return b;
}

TEST_CASE("SNA: announcements and data blocks")
{
	bytes payload;
	payload.u32(0x11111111).u32(0x22222222);

	sna_builder sna;
	sna.block(0x0A, 0, 0x5000, 0x6000, {});                // runtime-only region
	sna.block(0x04, 0, 0x1000, 0x2000, {});                // announcement
	sna.block(0x04, 0, 0x1000, 0x2000, payload);           // the data
	sna.plain.u8(0x99).u8(0).u8(0).u32(0xFFFFFFFF);        // skipped entry

	sna_file file(sna.build());
	REQUIRE(file.blocks().size() == 1);
	CHECK(file.blocks()[0].id == block_id{ 0x04, 0 });
	CHECK(file.blocks()[0].base == 0x1000);
	CHECK(file.blocks()[0].data.size() == 8);
	CHECK(file.regions().size() == 2);
}

TEST_CASE("relocation tables: parse and reject trailing garbage")
{
	bytes b;
	b.u8(1).u32(0);
	table_block(b, 0x04, 0, { { 0x1000, { 0x04, 0 } }, { 0x1004, { 0x0A, 0 } } });

	relocation_table table = parse_relocation_table(b.data);
	CHECK(table.pointer_count() == 2);
	CHECK(table.blocks.at({ 0x04, 0 })[1].target == block_id{ 0x0A, 0 });

	b.u8(0);
	CHECK_THROWS_AS(parse_relocation_table(b.data), format_error);
}

TEST_CASE("memory image: each file's pointers use the addresses that file announced")
{
	// Fix: block 04/00 at 0x1000, pointing to itself.
	bytes fix_data;
	fix_data.u32(0x1008).u32(0).u32(0xAAAA);
	sna_builder fix;
	fix.block(0x04, 0, 0x1000, 0x2000, fix_data);
	bytes fix_table;
	fix_table.u8(1).u32(0);
	table_block(fix_table, 0x04, 0, { { 0x1000, { 0x04, 0 } } });

	// Level: believes Fix's 04/00 starts 8 bytes later, like the real levels do.
	bytes level_data;
	level_data.u32(0x1010).u32(0x5004);
	sna_builder level;
	level.block(0x04, 0, 0x1008, 0x2008, {});
	level.block(0x0A, 0, 0x5000, 0x6000, {});
	level.block(0x04, 1, 0x9000, 0x9100, level_data);
	bytes level_table;
	level_table.u8(1).u32(0);
	table_block(level_table, 0x04, 1, { { 0x9000, { 0x04, 0 } }, { 0x9004, { 0x0A, 0 } } });

	memory_image image;
	image.add(sna_file(fix.build()), parse_relocation_table(fix_table.data));
	image.add(sna_file(level.build()), parse_relocation_table(level_table.data));

	const sna_block* fix_block = image.find({ 0x04, 0 });
	const sna_block* level_block = image.find({ 0x04, 1 });
	REQUIRE(fix_block);
	REQUIRE(level_block);

	// Fix's own pointer: 0x1008 with Fix's base 0x1000.
	auto a = image.follow({ fix_block, 0 });
	REQUIRE(a);
	CHECK(a->block == fix_block);
	CHECK(a->offset == 8);

	// The level's pointer 0x1010 with the level's view (base 0x1008) is the same spot.
	auto b = image.follow({ level_block, 0 });
	REQUIRE(b);
	CHECK(b->block == fix_block);
	CHECK(b->offset == 8);

	// Into memory that only exists at runtime.
	auto c = image.follow({ level_block, 4 });
	REQUIRE(c);
	CHECK(c->block->data.empty());
	CHECK(c->offset == 4);

	CHECK_FALSE(image.is_pointer({ fix_block, 4 }));
	CHECK(image.check().dangling == 0);
}

TEST_CASE("real data: every level loads with all pointers resolved")
{
	auto dir = real_game_dir();
	if(!dir)
	{
		MESSAGE("OPENRAYMAN_GAME_DIR not set, skipping");
		return;
	}
	auto game = game_data::locate(*dir);
	REQUIRE(game.has_value());

	for(const std::string& level : game->game_dsb().levels)
	{
		INFO(level);
		memory_image image = load_level_memory(*game, level);
		memory_image::statistics stats = image.check();
		CHECK(stats.pointers > 800);
		CHECK(stats.dangling == 0);
	}
}
