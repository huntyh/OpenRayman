#include "test_util.h"
#include "../src/renderer/backdrop.h"
#include "../src/renderer/transparent_order.h"

#include <openrayman/actor.h>
#include <openrayman/animation.h>
#include <openrayman/brain.h>
#include <openrayman/collision.h>
#include <openrayman/input.h>
#include <openrayman/game/animation_player.h>
#include <openrayman/game/handlers.h>
#include <openrayman/game/interpreter.h>
#include <openrayman/game/input.h>
#include <openrayman/game/live_memory.h>
#include <openrayman/game/sound_slots.h>
#include <openrayman/game/first_visit.h>
#include <openrayman/cnt_archive.h>
#include <openrayman/error.h>
#include <openrayman/game_data.h>
#include <openrayman/geometry.h>
#include <openrayman/gf_texture.h>
#include <openrayman/level_header.h>
#include <openrayman/light.h>
#include <openrayman/mask.h>
#include <openrayman/memory_image.h>
#include <openrayman/pointer_stream.h>
#include <openrayman/game/dialog.h>
#include <openrayman/relocation.h>
#include <openrayman/scene.h>
#include <openrayman/sna_file.h>
#include <openrayman/texture_table.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <cstdint>
#include <map>
#include <tuple>

using namespace openrayman;

TEST_CASE("transparent triangles use corner depth and reverse equal-depth submission")
{
	// A long triangle reaches behind the nearer one even though two of its corners are close.
	const auto spanning = transparent_sort_key::from_depths({2, 3, 100}, 0, false);
	const auto near = transparent_sort_key::from_depths({20, 20, 20}, 1, false);
	CHECK(spanning < near);
	// Coincident layers: the water is submitted first, the vortex second. The queue is a stack.
	const auto water = transparent_sort_key::from_depths({20, 25, 30}, 0, false);
	const auto vortex = transparent_sort_key::from_depths({20, 25, 30}, 1, false);
	CHECK(vortex < water);
	CHECK_FALSE(water < vortex);
	const auto screen = transparent_sort_key::from_depths({1000, 1000, 1000}, 2, true);
	CHECK(near < screen);
}

TEST_CASE("real data: pause swirl materials retain offsets, depth rules and saved grey colours")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto data = game_data::locate(*dir); REQUIRE(data);
	auto files = load_level_files(*data, "Learn_10");
	const auto* block = files.memory.find({5, 1}); REQUIRE(block);
	actor_object object;
	object.physical_object = memory_location{block, 0x65BF84};
	actor_set actors;
	std::vector<std::optional<std::size_t>> meshes;
	std::vector<std::optional<vec3>> scales;
	add_meshes(actors, files.memory, {object}, meshes, scales);
	REQUIRE(actors.meshes.size() == 1);
	const auto& mesh = actors.meshes[0];
	CHECK(mesh.lod_count == 1);
	CHECK(mesh.vertices.size() == 258);
	REQUIRE(mesh.colour_sets.size() == 1);
	REQUIRE(mesh.colour_sets[0].size() == 1);
	REQUIRE(mesh.colour_sets[0][0].size() == 258);
	CHECK((mesh.colour_sets[0][0][0] == saved_vertex_colour{109,109,109,255}));
	REQUIRE(mesh.elements.size() == 2);
	for(std::size_t i = 0; i < 2; ++i)
	{
		CHECK(mesh.elements[i].triangles.size() == 240);
		REQUIRE(mesh.elements[i].material);
		const auto& material = *mesh.elements[i].material;
		REQUIRE(material.texture);
		CHECK(material.flags == (i == 0 ? 0x45DC03FFu : 0x4DDC03FFu));
		CHECK(material.texture->flags == (i == 0 ? 0x80000048u : 0x80000008u));
		CHECK(bool(material.texture_draw_flags() & 0x08000000) == (i == 1));
		CHECK((material.texture_draw_flags() & 0x400) == 0);
		CHECK(material.scroll[0] == doctest::Approx(i == 0 ? 0.01f : 0.005f));
		CHECK(material.scroll_offset == material.scroll);
		CHECK(material.scroll[1] == 0);
	}
}

TEST_CASE("additive fullbright requires the texture and both material flags")
{
	visual_material m;
	m.texture.emplace();
	m.texture->flags = texture_flag::additive;
	m.flags = 0x4DDC03FF;
	CHECK(m.additive_fullbright());
	m.flags &= ~0x04000000u;
	CHECK_FALSE(m.additive_fullbright());
	m.flags = 0x4DDC03FF & ~8u;
	CHECK_FALSE(m.additive_fullbright());
	m.flags = 0x4DDC03FF;
	m.texture->flags = 0;
	CHECK_FALSE(m.additive_fullbright());
}

TEST_CASE("real data: Jail_10 far backdrop sector contains only static mountain scenery")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto data = game_data::locate(*dir);
	REQUIRE(data);
	auto files = load_level_files(*data, "Jail_10");
	const auto header = read_level_header(files);
	scene tree(files.memory, header);
	const auto lights = read_lights(files.memory, tree);
	REQUIRE(lights.sectors.size() == 9);
	CHECK(lights.sectors[7].node == 81);
	CHECK(lights.sectors[7].is_virtual);
	std::size_t objects = 0;
	for(const auto& placed : static_geometry(files.memory, tree))
	{
		if(lights.sector_of(tree, placed.node) != 7) continue;
		++objects;
		CHECK(placed.node == 82);
		CHECK(placed.visible);
		CHECK(placed.object.look == look_at::none);
		CHECK(placed.object.sprites.empty());
		CHECK(placed.object.vertices.size() == 31);
		CHECK(placed.object.elements.size() == 7);
	}
	CHECK(objects == 1);
	// The crack the intro camera emerges from remains real foreground rock.
	const auto geometry = static_geometry(files.memory, tree);
	const auto crack = std::ranges::find(geometry, 17u, &placed_geometry::node);
	REQUIRE(crack != geometry.end());
	CHECK(lights.sector_of(tree, crack->node) == 0);
	CHECK(crack->object.vertices.size() == 22);
	CHECK(crack->object.elements.size() == 2);
	for(std::size_t i = 0; i < tree.nodes().size(); ++i)
		if(tree.node(i).type == super_object_type::actor)
			CHECK(lights.sector_of(tree, i) != 7);
}

TEST_CASE("real data: Jail_20 backdrop recipe excludes the chute and solid scenery")
{
	const auto recipe = std::ranges::find(backdrop_recipes, std::string_view("Jail_20"), &backdrop_recipe::level);
	REQUIRE(recipe != std::end(backdrop_recipes));
	REQUIRE(recipe->sectors.size() == 1);
	CHECK(recipe->sectors[0] == 0);
	CHECK(recipe->skies.empty()); // the sky shell shares sector 0 with the mountains
	CHECK(recipe->anchor[0] == doctest::Approx(393.22f));
	CHECK(recipe->anchor[1] == doctest::Approx(321.303f));
	CHECK(recipe->anchor[2] == doctest::Approx(275.931f));
	CHECK(recipe->parallax == doctest::Approx(0.1f));
	auto dir = real_game_dir();
	if(!dir) return;
	auto data = game_data::locate(*dir);
	REQUIRE(data);
	auto files = load_level_files(*data, "Jail_20");
	scene tree(files.memory, read_level_header(files));
	const auto lights = read_lights(files.memory, tree);
	REQUIRE(lights.sectors.size() == 14);
	CHECK(lights.sectors[0].node == 34);
	std::vector<std::size_t> nodes;
	for(const auto& placed : static_geometry(files.memory, tree))
	{
		if(lights.sector_of(tree, placed.node) != 0) continue;
		nodes.push_back(placed.node);
		CHECK(placed.visible);
		CHECK(placed.object.look == look_at::none);
		CHECK(placed.object.sprites.empty());
		const auto& ipo = tree.node(placed.node);
		REQUIRE(ipo.data);
		const auto physical = files.memory.follow(*ipo.data);
		REQUIRE(physical);
		CHECK_FALSE(files.memory.follow(*physical + 4)); // no collision set on either object
		if(placed.node == 35)
		{
			CHECK(placed.object.vertices.size() == 11);
			CHECK(placed.object.elements.size() == 1);
		}
		else
		{
			CHECK(placed.node == 36);
			CHECK(placed.object.vertices.size() == 27);
			CHECK(placed.object.elements.size() == 7);
			for(const auto& e : placed.object.elements)
			{
				REQUIRE(e.material);
				REQUIRE(e.material->texture);
				CHECK(e.material->texture->name.starts_with("textures_vallee\\"));
			}
		}
	}
	CHECK(nodes == std::vector<std::size_t>{ 35, 36 });
	// The trigger/exit remains in ordinary sector 12; the landing actors likewise
	// still draw normally even when their live sector is the backdrop's sector 0.
	CHECK(lights.sector_at(recipe->anchor) == 12);
	CHECK(lights.sector_of(tree, 58) == 8);
	CHECK(lights.sector_of(tree, 13) == std::nullopt); // actors live in the dynamic tree
}

TEST_CASE("real data: Jail_20 cell hides its white panel and selects blue beam meshes")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto game = game_data::locate(*dir);
	REQUIRE(game);
	auto files = load_level_files(*game, "Jail_20");
	auto header = read_level_header(files);
	scene tree(files.memory, header);
	auto geometry = static_geometry(files.memory, tree);
	for(std::size_t node : { 44u, 45u, 46u })
	{
		auto found = std::ranges::find(geometry, node, &placed_geometry::node);
		REQUIRE(found != geometry.end());
		CHECK(found->visible == (node == 45));
	}
	// The actual beams are node 9; its startup script replaces Fix's red beam list.
	game::live_memory memory(files.memory);
	const auto self = tree.node(9).at.address();
	game::interpreter vm(memory, self);
	game::register_handlers(vm);
	vm.think(self);
	const auto data = memory.u32(memory.u32(self + 4));
	const auto* block = files.memory.find({ 0x04, 0x01 });
	REQUIRE(block);
	memory_location list{ block, 0x3EE0 };
	CHECK(memory.u32(data + 0x10) == list.address());
	auto objects = read_object_list(files.memory, list);
	REQUIRE_FALSE(objects.empty());
	REQUIRE(objects[0].physical_object);
	auto mesh_at = physical_object_geometry(files.memory, *objects[0].physical_object);
	REQUIRE(mesh_at);
	auto mesh = read_geometric_object(files.memory, *mesh_at);
	REQUIRE(mesh.elements.size() == 2);
	REQUIRE(mesh.elements[0].material);
	REQUIRE(mesh.elements[0].material->texture);
	CHECK(mesh.elements[0].material->texture->name == "effets_speciaux\\faisc2_txy_adnzw.tga");
	CHECK((mesh.elements[0].material->texture->flags & 0x20) != 0);
}

TEST_CASE("real data: Ubisoft arc saved vertex colours follow its physical object")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto game = game_data::locate(*dir);
	REQUIRE(game);
	auto files = load_level_files(*game, "Jail_10");
	const auto* block = files.memory.find({ 0x05, 0x02 });
	REQUIRE(block);
	actor_object object;
	object.physical_object = memory_location{ block, 0x61AC4 };
	actor_set set;
	std::vector<std::optional<std::size_t>> meshes;
	std::vector<std::optional<vec3>> scales;
	add_meshes(set, files.memory, { object }, meshes, scales);
	REQUIRE(meshes.size() == 1);
	REQUIRE(meshes[0]);
	const auto& sets = set.meshes[*meshes[0]].colour_sets;
	REQUIRE(sets.size() == 1);
	REQUIRE(sets[0].size() == 1);
	REQUIRE(sets[0][0].size() == 10);
	CHECK((sets[0][0][0] == saved_vertex_colour{ 255, 255, 36, 255 }));
}

TEST_CASE("real data: level startup music events")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto game = game_data::locate(*dir);
	REQUIRE(game);
	for(const auto& [level, event] : { std::pair{"Menu", 0x01160000u},
		std::pair{"Mapmonde", 0x01160000u}, std::pair{"Learn_10", 0x00090009u} })
	{
		auto header = read_level_header(load_level_files(*game, level));
		REQUIRE(header.startup_sound);
		CHECK(*header.startup_sound == event);
	}
}

TEST_CASE("real data: Menu Teensy hat morph records")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto game = game_data::locate(*dir);
	REQUIRE(game);
	auto files = load_level_files(*game, "Menu");
	auto bank = read_animation_bank(files);
	REQUIRE(bank.animations.size() > 316);
	const auto& a = bank.animations[316];
	CHECK(a.morphs == 10);
	for(unsigned frame : { 48u, 49u })
	{
		auto channels = pose(bank, 316, frame);
		auto channel = std::ranges::find_if(channels, [](const auto& c) { return c.id == 12; });
		REQUIRE(channel != channels.end());
		CHECK(channel->object == 11);
		REQUIRE(channel->morph);
		CHECK(channel->morph->target == 16);
		CHECK(channel->morph->amount == 42);
	}
	auto header = read_level_header(files);
	scene tree(files.memory, header);
	auto actors = read_actors(files.memory, tree, bank);
	auto actor = std::ranges::find_if(actors.actors, [](const auto& e) { return e.node == 29; });
	REQUIRE(actor != actors.actors.end());
	REQUIRE(actor->meshes.size() > 16);
	REQUIRE(actor->meshes[11]);
	REQUIRE(actor->meshes[16]);
	const auto& source = actors.meshes[*actor->meshes[11]];
	const auto& target = actors.meshes[*actor->meshes[16]];
	CHECK(source.vertices.size() == target.vertices.size());
	CHECK(source.normals.size() == target.normals.size());
	CHECK(source.elements.size() == target.elements.size());
	CHECK(source.sprites.empty());
	CHECK(target.sprites.empty());
	CHECK(source.skipped_types.empty());
	CHECK(target.skipped_types.empty());
	CHECK_FALSE(source.vertices.empty());
	auto posed_actor = *actor;
	posed_actor.animation = 316;
	std::vector<posed_part> parts;
	pose_actor(posed_actor, bank, 48, parts);
	CHECK(std::ranges::any_of(parts, [&](const auto& part) {
		return part.mesh == *actor->meshes[11] && part.morph_mesh == actor->meshes[16] && part.morph_amount == 42;
	}));
}

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

// One block 05/02 at 0x1000 holding a root super object with two children, at 0x00, 0x40, 0x80.
// `break_link` leaves out the second child's back link to the first.
static memory_image three_super_objects(bool break_link = false)
{
	const std::uint32_t base = 0x1000, root = base, a = base + 0x40, b = base + 0x80;
	std::vector<std::uint32_t> words(0xC0 / 4, 0);
	std::vector<std::pair<std::uint32_t, block_id>> pointers;
	auto set = [&](std::uint32_t node, std::uint32_t field, std::uint32_t value, bool pointer) {
		words[(node - base + field) / 4] = value;
		if(pointer)
			pointers.push_back({ node + field, { 0x05, 2 } });
	};
	set(root, 0x00, super_object_type::world, false);
	set(root, 0x08, a, true);
	set(root, 0x0C, b, true);
	set(root, 0x10, 2, false);
	set(a, 0x00, super_object_type::actor, false);
	set(a, 0x14, b, true);
	set(a, 0x1C, root, true);
	set(b, 0x00, super_object_type::actor, false);
	if(!break_link)
		set(b, 0x18, a, true);
	set(b, 0x1C, root, true);

	bytes data;
	for(std::uint32_t w : words)
		data.u32(w);
	sna_builder sna;
	sna.block(0x05, 2, base, base + 0x1000, data);
	bytes table;
	table.u8(1).u32(0);
	table_block(table, 0x05, 2, pointers);

	memory_image image;
	image.add(sna_file(sna.build()), parse_relocation_table(table.data));
	return image;
}

TEST_CASE("pointer stream: pointers consume relocation entries in order")
{
	memory_image image = three_super_objects();
	bytes stream;
	stream.u32(0x1040).u32(0).u32(7).u32(0x1080);
	bytes table;
	table.u8(1).u32(0);
	table_block(table, 0x0A, 0, { { 0x1040, { 0x05, 2 } }, { 0x1080, { 0x05, 2 } } });
	relocation_table parsed = parse_relocation_table(table.data);

	pointer_stream s(stream.data, parsed, image, 0);
	CHECK(s.scan() == std::vector<std::size_t>{ 0, 12 });
	CHECK(s.peek_pointer()->offset == 0x40);
	CHECK(s.pointer()->offset == 0x40);
	CHECK_FALSE(s.pointer().has_value()); // null takes no entry
	CHECK(s.u32() == 7);
	CHECK(s.pointer()->offset == 0x80);
	CHECK(s.pointers_left() == 0);

	// Reading a data word as a pointer gets out of step with the table and is caught.
	pointer_stream wrong(stream.data, parsed, image, 0);
	wrong.skip(8);
	CHECK_THROWS_AS(wrong.pointer(), format_error);
}

TEST_CASE("scene: the super object tree is rebuilt and its links cross-checked")
{
	memory_image image = three_super_objects();
	const sna_block* block = image.find({ 0x05, 2 });
	REQUIRE(block);
	level_header header;
	header.actual_world = { block, 0x00 };
	header.dynamic_world = { block, 0x40 };
	header.inactive_dynamic_world = { block, 0x80 };
	header.father_sector = { block, 0x80 };

	scene tree(image, header);
	REQUIRE(tree.nodes().size() == 3);
	CHECK(tree.node(tree.actual_world()).children == std::vector<std::size_t>{ 1, 2 });
	CHECK(tree.node(tree.dynamic_world()).type == super_object_type::actor);
	CHECK(tree.node(2).parent == std::optional<std::size_t>(0));
	CHECK(tree.subtree_size(0) == 3);

	memory_image broken = three_super_objects(true);
	const sna_block* broken_block = broken.find({ 0x05, 2 });
	header.actual_world = { broken_block, 0x00 };
	CHECK_THROWS_AS(scene(broken, header), format_error);
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

	// Texture sizes from the GF files, to check what the PTX entries claim against.
	std::vector<cnt_archive> archives;
	for(const auto& path : game->texture_archives())
		archives.emplace_back(path);
	std::map<std::string, std::pair<std::uint32_t, std::uint32_t>> gf_sizes;
	auto gf_size = [&](const std::string& path) -> std::optional<std::pair<std::uint32_t, std::uint32_t>> {
		if(auto it = gf_sizes.find(path); it != gf_sizes.end())
			return it->second;
		for(const cnt_archive& archive : archives)
		{
			if(const cnt_entry* entry = archive.find(path))
			{
				gf_texture t = decode_gf(archive.read(*entry));
				return gf_sizes[path] = { t.width, t.height };
			}
		}
		return std::nullopt;
	};

	std::size_t references = 0, exact = 0;
	std::map<std::uint16_t, std::size_t> skipped_types;
	std::map<std::uint32_t, std::size_t> transform_types;
	std::pair<std::size_t, std::string> smallest_scene{ SIZE_MAX, "" };
	std::size_t levels_loaded = 0;
	for(const std::string& level : game->game_dsb().levels)
	{
		INFO(level);
		level_files files = load_level_files(*game, level);
		memory_image::statistics stats = files.memory.check();
		CHECK(stats.pointers > 800);
		CHECK(stats.dangling == 0);

		// The same five Fix actors join every level.
		level_header header = read_level_header(files);
		CHECK(header.fix_actors.size() == 10);
		CHECK(header.placements.size() == 5);
		REQUIRE(header.main_actor);
		CHECK(files.memory.read_u32(*header.main_actor) == 2);
		if(level == "Mapmonde")
			CHECK(*header.main_actor != header.placements.front().super_object);
		if(level == "Learn_10" || level == "Jail_20")
			CHECK(*header.main_actor == header.placements.front().super_object);

		scene tree(files.memory, header);
		smallest_scene = std::min(smallest_scene, std::pair(tree.nodes().size(), level));
		if(level == "Learn_10")
		{
			CHECK(tree.nodes().size() == 194);
			CHECK(tree.node(tree.dynamic_world()).children.size() == 83);
			CHECK(tree.subtree_size(tree.father_sector()) == 108);
		}
		CHECK(tree.node(tree.actual_world()).children.size() == 3);
		CHECK(tree.node(tree.dynamic_world()).children.size() >= header.placements.size());
		CHECK(tree.subtree_size(tree.father_sector()) >= 4);
		for(const scene_node& node : tree.nodes())
		{
			if(node.placement)
				CHECK(node.type == super_object_type::actor);
			if(!node.matrix)
				continue;
			// Transform types: 1 and 2 have identity matrices, 3 repeats its rotation in the
			// combined matrix (no scale), 4 differs.
			transform t = read_transform(files.memory, *node.matrix);
			constexpr std::array<float, 9> identity{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
			CHECK(t.type >= 1);
			CHECK(t.type <= 4);
			if(t.type <= 2)
				CHECK((t.rotation == identity && t.matrix == identity));
			if(t.type == 3)
				CHECK(t.rotation == t.matrix);
			if(t.type == 4)
			{
				// Each row of the combined matrix is the rotation's row times a scale: the game
				// multiplies row vectors, so the matrices are stored transposed (see affine::from).
				for(int r = 0; r < 3; r++)
				{
					float dot = 0, rr = 0;
					for(int k = 0; k < 3; k++)
						dot += t.matrix[r * 3 + k] * t.rotation[r * 3 + k], rr += t.rotation[r * 3 + k] * t.rotation[r * 3 + k];
					for(int k = 0; k < 3; k++)
						CHECK(std::abs(t.matrix[r * 3 + k] - dot / rr * t.rotation[r * 3 + k]) < 1e-3f);
				}
			}
			transform_types[t.type]++;
		}

		// Static geometry: every mesh decodes, with indices in range (checked while reading), and
		// every textured material names a texture that exists.
		std::size_t meshes = 0, level_triangles = 0;
		bool stream_scrolls = false;
		for(const placed_geometry& placed : static_geometry(files.memory, tree))
		{
			meshes++;
			for(std::uint16_t type : placed.object.skipped_types)
				skipped_types[type]++;
			for(const triangle_element& e : placed.object.elements)
			{
				level_triangles += e.triangles.size();
				CHECK(e.uv_triangles.size() == e.triangles.size());
				// The Woods of Light's stream slides along v (0.0065 texture heights per frame).
				if(level == "Learn_10" && e.material && e.material->texture && e.material->texture->name.find("eau_eau") != std::string::npos)
					stream_scrolls = stream_scrolls || (e.material->scroll[0] == 0 && e.material->scroll[1] == doctest::Approx(0.0065f));
				if(e.material && e.material->texture)
					CHECK(gf_size(e.material->texture->cnt_path()).has_value());
			}
		}
		CHECK(meshes > 0);
		CHECK(level_triangles > 0);
		if(level == "Learn_10")
			CHECK(stream_scrolls);
		if(level == "Learn_10")
		{
			// Recorded from the export checked by eye (top and side views of the whole level).
			CHECK(meshes == 87);
			CHECK(level_triangles == 2604);
		}

		// Collision: every level's static collision decodes (indices checked while reading) and
		// has some; normals are unit length.
		std::vector<collision_triangle> collision = static_collision(files.memory, tree);
		CHECK(!collision.empty());
		std::size_t with_material = 0;
		for(const collision_triangle& t : collision)
		{
			CHECK(std::abs(t.normal[0] * t.normal[0] + t.normal[1] * t.normal[1] + t.normal[2] * t.normal[2] - 1) < 1e-3f);
			with_material += t.has_material;
		}
		if(level == "Learn_10") // virtual sectors (lighting only) excluded
		{
			CHECK(collision.size() == 1307);
			CHECK(with_material == 192);
		}

		// Animations: Fix's 289 plus the level's. Reading actors cross-checks each one's animation
		// reference against the bank (frame and channel counts), and posing checks every index.
		animation_bank bank = read_animation_bank(files);
		CHECK(bank.fix_animations == 289);
		CHECK(bank.animations.size() >= bank.fix_animations);
		std::vector<placed_geometry> actors = actor_geometry(files.memory, tree, bank);
		std::size_t actor_triangles = 0, actor_sprites = 0;
		for(const placed_geometry& p : actors)
		{
			for(std::uint16_t type : p.object.skipped_types)
				skipped_types[type]++;
			for(const triangle_element& e : p.object.elements)
				actor_triangles += e.triangles.size();
			for(const sprite& s : p.object.sprites)
			{
				actor_sprites++;
				if(s.material && s.material->texture)
					CHECK(gf_size(s.material->texture->cnt_path()).has_value());
			}
		}
		if(level == "Learn_10")
			CHECK(actor_sprites == 31); // the Lums and other glows, checked by eye

		// Every frame of every actor's animation, and halfway to the next, poses without bad indices.
		actor_set set = read_actors(files.memory, tree, bank);
		std::size_t pose_failures = 0, posed_frames = 0;
		for(const actor_set::entry& actor : set.actors)
		{
			std::uint16_t frames = actor.animation ? bank.animations[*actor.animation].frames : 1;
			for(std::uint16_t f = 0; f < frames; f++)
			{
				for(double offset : { 0.0, 0.5 })
				{
					std::vector<posed_part> parts;
					try
					{
						pose_actor(actor, bank, f + offset, parts);
						posed_frames++;
					}
					catch(const format_error&)
					{
						pose_failures++;
					}
				}
			}
		}
		CHECK(pose_failures == 0);
		CHECK(posed_frames > 0);

		// Every actor with a family is in one of its states, and each state's animation reference
		// agrees with the bank (read_actors checks the counts).
		std::size_t without_state = 0;
		for(const actor_set::entry& actor : set.actors)
		{
			if(actor.family && !actor.state)
				without_state++;
		}
		CHECK(without_state == 0);
		if(level == "Learn_10")
		{
			// The Lums' family (in Fix): saved in a placeholder, state 4 is the yellow Lum with wings.
			auto lum = std::ranges::find(set.actors, std::size_t(12), &actor_set::entry::node);
			REQUIRE(lum != set.actors.end());
			CHECK(lum->states.size() == 15);
			CHECK(lum->state == std::optional<std::size_t>(0));
			CHECK(lum->states[4] == std::optional<std::uint16_t>(std::uint16_t(280)));

			// Lums flap their wings with an animated texture: 4 frames of 0.035 s.
			bool wings = false;
			for(const geometric_object& mesh : set.meshes)
			{
				for(const triangle_element& e : mesh.elements)
				{
					if(!e.material || e.material->frames.size() != 4 || e.material->frames[0].texture.name.find("lum_aile01") == std::string::npos)
						continue;
					wings = true;
					for(const texture_frame& f : e.material->frames)
						CHECK(f.duration == doctest::Approx(0.035f));
				}
			}
			CHECK(wings);
		}
		if(level == "Learn_10")
		{
			// Different physical objects may share the same geometry but have different colour sets.
			std::vector<memory_location> unique;
			for(const auto& at : set.mesh_at)
				if(std::ranges::find(unique, at) == unique.end()) unique.push_back(at);
			CHECK(unique.size() == 113);
		}
		if(level == "Learn_10")
		{
			// Recorded from a render checked by eye: Rayman, Lums and props correctly posed.
			CHECK(bank.animations.size() == 450);
			CHECK(actors.size() == 176);
			CHECK(actor_triangles == 3838);
		}

		CHECK(read_texture_table(files, false).size() == 257);
		std::vector<texture_info> textures = read_texture_table(files, true);
		CHECK(!textures.empty());
		if(level == "Learn_10")
			CHECK(textures.size() == 112);
		levels_loaded++;
		for(const texture_info& t : textures)
		{
			INFO(t.name);
			auto size = gf_size(t.cnt_path());
			REQUIRE(size.has_value());
			// Equal, or (for a few textures) the archive holds a version at twice or half the size.
			auto close = [](std::uint32_t a, std::uint32_t b) { return a == b || a == 2 * b || 2 * a == b; };
			CHECK(close(t.width, size->first));
			CHECK(close(t.height, size->second));
			references++;
			exact += t.width == size->first && t.height == size->second;
		}
	}
	CHECK(levels_loaded == 70);
	CHECK(smallest_scene == std::pair<std::size_t, std::string>(14, "Raycap"));
	CHECK(exact >= references * 95 / 100);
	CHECK(transform_types == std::map<std::uint32_t, std::size_t>{ { 1, 6951 }, { 2, 3109 }, { 3, 1256 }, { 4, 290 } });
	// Every element type in the game is decoded: triangles and sprites.
	CHECK(skipped_types.empty());
}

TEST_CASE("real data: every sector's static lights")
{
	auto dir = real_game_dir();
	if(!dir)
	{
		MESSAGE("OPENRAYMAN_GAME_DIR not set, skipping");
		return;
	}
	auto game = game_data::locate(*dir);
	REQUIRE(game.has_value());

	std::map<light_type, std::size_t> types;
	std::map<int, std::size_t> targets;
	std::size_t most_in_a_sector = 0, levels_with_fog = 0, vertices = 0, outside = 0;
	for(const std::string& level : game->game_dsb().levels)
	{
		INFO(level);
		level_files files = load_level_files(*game, level);
		level_header header = read_level_header(files);
		scene tree(files.memory, header);
		level_lights lights = read_lights(files.memory, tree);
		bool fog = false;
		for(const light& l : lights.lights)
		{
			types[l.type]++;
			targets[l.targets]++;
			fog = fog || l.type == light_type::fog;
		}
		levels_with_fog += fog;
		for(const sector& s : lights.sectors)
			most_in_a_sector = std::max(most_in_a_sector, s.lights.size());

		// Each piece of static geometry belongs to a sector, and lies (nearly) inside its box.
		for(const placed_geometry& placed : static_geometry(files.memory, tree))
		{
			auto s = lights.sector_of(tree, placed.node);
			REQUIRE(s.has_value());
			sector box = lights.sectors[*s];
			for(int axis = 0; axis < 3; axis++)
				box.min[axis] -= 1, box.max[axis] += 1;
			for(const vec3& v : placed.object.vertices)
			{
				vertices++;
				outside += !box.contains(placed.world.apply(v));
			}
		}
		if(level == "Learn_10")
		{
			CHECK(lights.sectors.size() == 20);
			CHECK(lights.lights.size() == 225);
		}
	}
	// Every light type in the game is known (the reader throws otherwise).
	CHECK(types == std::map<light_type, std::size_t>{ { light_type::parallel, 189 }, { light_type::spherical, 10214 },
		{ light_type::ambient, 198 }, { light_type::parallel_other, 11 }, { light_type::fog, 128 } });
	CHECK(targets == std::map<int, std::size_t>{ { 0, 10 }, { 1, 9727 }, { 2, 736 }, { 3, 267 } });
	CHECK(most_in_a_sector == 299);
	CHECK(levels_with_fog == 62);
	// The sector boxes: all but a few vertices (0.6%) lie inside their sector's box, give or take a unit.
	CHECK(vertices == 102016);
	CHECK(outside == 605);
}

TEST_CASE("real data: actor shadows")
{
	auto dir = real_game_dir();
	if(!dir)
	{
		MESSAGE("OPENRAYMAN_GAME_DIR not set, skipping");
		return;
	}
	auto game = game_data::locate(*dir);
	REQUIRE(game.has_value());

	std::size_t with_shadow = 0, actors = 0, shown = 0;
	std::map<std::string, std::size_t> textures;
	for(const std::string& level : game->game_dsb().levels)
	{
		INFO(level);
		level_files files = load_level_files(*game, level);
		level_header header = read_level_header(files);
		scene tree(files.memory, header);
		animation_bank bank = read_animation_bank(files);
		actor_set set = read_actors(files.memory, tree, bank);
		std::size_t here = 0;
		for(const actor_set::entry& a : set.actors)
		{
			actors++;
			if(!a.shadow)
				continue;
			here++;
			shown += a.shadow->shown;
			CHECK(a.shadow->width > 0);
			CHECK(a.shadow->length > 0);
			REQUIRE(a.shadow->material.texture.has_value());
			textures[a.shadow->material.texture->name]++;
		}
		with_shadow += here;
		if(level == "Learn_10")
			CHECK(here == 4); // three of the level's, and one of Fix's (Rayman's, 0.8 by 0.8)
	}
	// Every shadow uses the same blob texture from Fix.
	CHECK(with_shadow == 152);
	// None is switched on as saved: the scripts switch them on (Rayman's) or leave them off.
	CHECK(shown == 0);
	CHECK(actors == 5107);
	CHECK(textures == std::map<std::string, std::size_t>{ { "effets_speciaux\\shadow1_mxy_aaa.tga", 152 } });
}

TEST_CASE("real data: every actor's scripts")
{
	auto dir = real_game_dir();
	if(!dir)
	{
		MESSAGE("OPENRAYMAN_GAME_DIR not set, skipping");
		return;
	}
	auto game = game_data::locate(*dir);
	REQUIRE(game.has_value());

	// The exe's handler tables: keywords, conditions, operators, functions, procedures, meta-actions.
	constexpr std::uint32_t table_size[] = { 20, 149, 28, 314, 514, 12 };
	std::size_t actors = 0, models = 0, behaviours = 0, reflexes = 0, macros = 0, scripts = 0, nodes = 0, variables = 0;
	std::size_t empty_macros = 0;
	std::size_t unprinted = 0; // operators whose argument count doesn't fit their form
	for(const std::string& level : game->game_dsb().levels)
	{
		INFO(level);
		level_files files = load_level_files(*game, level);
		level_header header = read_level_header(files);
		scene tree(files.memory, header);
		level_brains brains = read_brains(files.memory, tree);
		actors += brains.actors.size();
		models += brains.models.size();
		for(const auto& [key, m] : brains.models)
		{
			behaviours += m.behaviours.size();
			reflexes += m.reflexes.size();
			macros += m.macros.size();
			variables += m.variables.size();
			for(const dsg_variable& v : m.variables)
				CHECK(v.offset < m.defaults.size());
			std::vector<const script*> all;
			for(const auto* list : { &m.behaviours, &m.reflexes })
				for(const behaviour& b : *list)
				{
					for(const script& s : b.rules)
						all.push_back(&s);
					if(b.schedule)
						all.push_back(&*b.schedule);
				}
			for(const macro& mc : m.macros)
				if(!mc.body.nodes.empty())
					all.push_back(&mc.body);
				else
					empty_macros++;
			for(const script* s : all)
			{
				scripts++;
				nodes += s->nodes.size();
				// Every script starts one indent in and ends with an end node at indent 0.
				REQUIRE(s->nodes.size() >= 1);
				CHECK(s->nodes.back().type == std::uint8_t(node_type::end));
				CHECK((s->nodes.size() == 1 || s->nodes.front().indent == 1));
				for(const script_node& n : s->nodes)
				{
					if(n.type < std::size(table_size))
						CHECK(n.param < table_size[n.type]);
					if(n.type == std::uint8_t(node_type::subroutine))
						CHECK(std::ranges::any_of(m.macros, [&](const macro& mc) { return n.target == mc.at; }));
				}
				for(const std::string& line : describe_script(*s, m, files.memory, tree))
					unprinted += line.find("operator_") != std::string::npos;
			}
		}
		if(level == "Learn_10")
		{
			CHECK(brains.actors.size() == 82);
			CHECK(brains.models.size() == 38);
			// Rayman, Fix's actor in node 84.
			auto rayman = std::ranges::find_if(brains.actors, [](const actor_brain& b) { return b.node == 84; });
			REQUIRE(rayman != brains.actors.end());
			const ai_model& m = brains.model(*rayman);
			CHECK(m.behaviours.size() == 46);
			CHECK(m.reflexes.size() == 16);
			CHECK(m.macros.size() == 65);
			CHECK(m.variables.size() == 115);
		}
	}
	CHECK(actors == 5020);
	CHECK(models == 1944);
	CHECK(behaviours == 15679);
	CHECK(reflexes == 2151);
	CHECK(macros == 6920);
	CHECK(scripts == 37845);
	CHECK(nodes == 3319729);
	CHECK(variables == 47915);
	CHECK(empty_macros == 1); // in Astro_10
	CHECK(unprinted == 0);
}

TEST_CASE("real data: Learn_10's scripts run")
{
	auto dir = real_game_dir();
	if(!dir)
	{
		MESSAGE("OPENRAYMAN_GAME_DIR not set, skipping");
		return;
	}
	auto game = game_data::locate(*dir);
	REQUIRE(game.has_value());
	level_files files = load_level_files(*game, "Learn_10");
	level_header header = read_level_header(files);
	scene tree(files.memory, header);
	game::live_memory memory(files.memory);
	game::interpreter vm(memory, header.placements.at(0).super_object.address());
	game::register_handlers(vm);
	// Five seconds of the game's 60 ticks per second: no actor's scripts may fail.
	std::vector<std::uint32_t> actors;
	for(const scene_node& n : tree.nodes())
		if(n.type == super_object_type::actor)
			actors.push_back(n.at.address());
	std::size_t errors = 0;
	for(int tick = 0; tick < 300; tick++)
		game::step(vm, actors, [&](std::uint32_t, const std::exception& e) {
			if(errors++ == 0)
				MESSAGE(std::format("tick {}, node 0x{:08X}: {}", tick, vm.last_node(), e.what()));
		});
	CHECK(errors == 0);
}

TEST_CASE("real data: cutscene bars follow the controller script")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto game = game_data::locate(*dir);
	REQUIRE(game.has_value());
	auto files = load_level_files(*game, "Learn_10");
	auto header = read_level_header(files);
	scene tree(files.memory, header);
	game::live_memory memory(files.memory);
	game::interpreter vm(memory, header.placements.at(0).super_object.address());
	game::register_handlers(vm);
	std::vector<std::uint32_t> actors;
	for(const auto& n : tree.nodes())
		if(n.type == super_object_type::actor) actors.push_back(n.at.address());
	auto controller = tree.nodes().at(6).at.address();
	auto tick = [&] { game::step(vm, actors, [&](std::uint32_t, const std::exception& e) { FAIL(e.what()); }); };
	tick(); tick();
	vm.set_variable(controller, 58, game::value::integer(1));
	tick();
	CHECK(vm.get_variable(controller, 60).as_float() == doctest::Approx(vm.frame_ms() * 0.4f));
	for(int i = 0; i < 40; ++i) tick();
	CHECK(vm.get_variable(controller, 60).as_float() == doctest::Approx(195));
	CHECK(vm.world_view_bounds().top == 45);
	CHECK(vm.world_view_bounds().bottom == 805);
	CHECK(vm.get_variable(controller, 58).as_int() == 0);
	vm.set_variable(controller, 59, game::value::integer(1));
	for(int i = 0; i < 40; ++i) tick();
	CHECK(vm.get_variable(controller, 60).as_float() == doctest::Approx(0));
	CHECK(vm.world_view_bounds().top == 0);
	CHECK(vm.world_view_bounds().bottom == 1000);
}

TEST_CASE("channel position uses the selected part and falls back to the actor")
{
	game::live_memory m(three_super_objects());
	auto so = m.allocate(0x48), actor = m.allocate(0x38), data = m.allocate(0x118);
	auto transform = m.allocate(0x58), part = m.allocate(0x48), part_transform = m.allocate(0x58);
	auto table = m.allocate(60), script = m.allocate(24);
	m.write<std::uint32_t>(so, 2);
	m.write(so + 4, actor);
	m.write(actor, data);
	m.write(so + 0x24, transform);
	m.write(transform + 4, vec3{ 1, 2, 3 });
	m.write(part + 0x24, part_transform);
	m.write(part_transform + 4, vec3{ 4, 5, 6 });
	m.write(data + 0x90, table);
	m.write(table + 20, part);
	m.write<std::uint32_t>(script, 210);
	m.write<std::uint8_t>(script + 7, std::uint8_t(node_type::function));
	m.write<std::uint8_t>(script + 14, 1);
	m.write<std::uint8_t>(script + 15, std::uint8_t(node_type::integer));
	game::interpreter vm(m, so);
	game::register_handlers(vm);
	auto check = [&](std::uint32_t index, vec3 expected) {
		m.write(script + 8, index);
		game::value result;
		CHECK(vm.eval(so, script, result) == script + 16);
		CHECK(result.words == game::value::vector(expected[0], expected[1], expected[2]).words);
	};
	check(1, { 4, 5, 6 });
	check(2, { 1, 2, 3 });
	check(255, { 0, 0, 0 });
	m.write<std::uint32_t>(data + 0x90, 0);
	check(1, { 1, 2, 3 });

	auto brain = m.allocate(12);
	m.write(actor + 0x0C, brain);
	m.write<std::uint32_t>(script, 237);
	m.write<std::uint8_t>(script + 7, std::uint8_t(node_type::procedure));
	m.write<std::uint32_t>(script + 8, 0x123);
	game::value result;
	CHECK(vm.eval(so, script, result) == script + 16);
	CHECK(m.u8(brain + 8) == 0x23);
	auto standard = m.allocate(0x34);
	m.write(actor + 4, standard);
	m.write<std::uint32_t>(script, 438);
	CHECK(vm.eval(so, script, result) == script + 16);
	CHECK(m.u8(standard + 0x33) == 0x23);
}

// Synthetic script calls retain value types and verify argument consumption as well as writes.
struct handler_fixture
{
	game::live_memory m{ three_super_objects() };
	std::uint32_t so = m.allocate(0x48), actor = m.allocate(0x38), data = m.allocate(0x118);
	std::uint32_t standard = m.allocate(0x34), dynamics = m.allocate(4), base = m.allocate(0x300);
	std::uint32_t collision = m.allocate(0x68), script = m.allocate(128);
	game::interpreter vm{ m, so };
	handler_fixture()
	{
		m.write<std::uint32_t>(so, 2); m.write(so + 4, actor);
		m.write(actor, data); m.write(actor + 4, standard); m.write(standard + 0xC, so);
		m.write(actor + 8, dynamics); m.write(dynamics, base); m.write(actor + 0x14, collision);
		game::register_handlers(vm);
	}
	game::value invoke(node_type kind, unsigned number, std::initializer_list<game::value> args, std::uint32_t target_actor = 0)
	{
		std::uint32_t start = script;
		if(target_actor)
		{
			m.write<std::uint32_t>(script, 24); m.write<std::uint8_t>(script + 6, 0);
			m.write<std::uint8_t>(script + 7, std::uint8_t(node_type::operator_));
			m.write(script + 8, target_actor); m.write<std::uint8_t>(script + 14, 1);
			m.write<std::uint8_t>(script + 15, std::uint8_t(node_type::actor));
			start += 16;
		}
		m.write(start, number); m.write<std::uint8_t>(start + 6, 1);
		m.write<std::uint8_t>(start + 7, std::uint8_t(kind));
		unsigned j = 0;
		for(auto v : args)
		{
			auto at = start + 8 + j * 8;
			m.write(at, 1000u + j); m.write<std::uint8_t>(at + 6, 2);
			m.write<std::uint8_t>(at + 7, std::uint8_t(node_type::function));
			vm.on_function(1000u + j++, [v](game::call& c, game::value& out) { out = v; return c.args; });
		}
		game::value result;
		CHECK(vm.eval(so, script, result) == start + 8 + unsigned(args.size()) * 8);
		CHECK(vm.missing.empty());
		return result;
	}
	game::value integer(int n) { return game::value::integer(n); }
	game::value me() { return game::value::of(game::value_type::actor, actor); }
	game::value object(std::uint32_t n) { return game::value::of(game::value_type::super_object, n); }
	void procedure(unsigned n, std::initializer_list<game::value> args) { invoke(node_type::procedure, n, args); }
	game::value function(unsigned n, std::initializer_list<game::value> args = {}) { return invoke(node_type::function, n, args); }
	bool condition(unsigned n, std::initializer_list<game::value> args = {}) { return invoke(node_type::condition, n, args).word() != 0; }
};

TEST_CASE("procedure 200 preserves a turned scaled actor when dynamics caches are stale")
{
	handler_fixture f;
	auto base = f.m.allocate(0x338), transform = f.m.allocate(0x58);
	f.m.write(f.dynamics, base);
	f.m.write(f.so + 0x24, transform);
	f.m.write<std::uint32_t>(base + 0xC, 4);
	affine before;
	before.m[0][0] = 0; before.m[1][0] = 2;
	before.m[0][1] = -3; before.m[1][1] = 0; before.m[2][2] = 4;
	game::set_global_placement(f.m, f.so, before);
	f.m.write<float>(base + 0x3C, 12);
	f.procedure(200, {f.me(), game::value::vector(10, 20, 30)});
	auto after = game::global_placement(f.m, f.so);
	REQUIRE(after);
	CHECK(after->apply({1, 1, 1}) == vec3{7, 22, 34});
	CHECK(f.m.f32(base + 0x3C) == 0);
	for(auto offset : {0x78u, 0xD0u, 0x288u, 0x2E0u})
	{
		CHECK(f.m.f32(base + offset + 4) == 10);
		CHECK(f.m.f32(base + offset + 0x38) == 2);
		CHECK(f.m.f32(base + offset + 0x40) == -3);
		CHECK(f.m.f32(base + offset + 0x54) == 4);
	}
}

TEST_CASE("procedure 218 places only the transform part and expires on the next tick")
{
	handler_fixture f;
	auto table = f.m.allocate(80), part = f.m.allocate(0x48), visible = f.m.allocate(0x48);
	f.m.write(f.data + 0x90, table);
	f.m.write(table + 40, part); f.m.write(table + 44, visible);
	auto transform = f.m.allocate(0x58), visible_transform = f.m.allocate(0x58), actor_transform = f.m.allocate(0x58);
	f.m.write(part + 0x24, transform); f.m.write(visible + 0x24, visible_transform); f.m.write(f.so + 0x24, actor_transform);
	affine a; a.m[0][0] = 2; a.m[1][1] = 3; a.m[2][2] = 4; a.m[0][3] = 100;
	game::set_global_placement(f.m, f.so, a);
	game::set_global_placement(f.m, part, a);
	f.procedure(218, {game::value::vector(1, 2, 3), f.integer(2), f.integer(0)});
	REQUIRE(f.vm.placed_channels(f.so).contains(2));
	CHECK(f.vm.placed_channels(f.so).at(2) == vec3{102, 6, 12});
	CHECK(f.m.f32(transform + 4) == doctest::Approx(102));
	CHECK(f.m.f32(transform + 0x34) == doctest::Approx(2));
	CHECK(f.m.f32(visible_transform + 4) == 0);
	CHECK(f.m.u8(f.data + 0xFC) == 1);
	f.procedure(218, {game::value::vector(4, 5, 6), f.integer(2), f.integer(1)});
	CHECK(f.vm.placed_channels(f.so).at(2) == vec3{4, 5, 6});
	// Missing entries do not fall back to the actor or to the visible object.
	f.procedure(218, {game::value::vector(9, 9, 9), f.integer(3), f.integer(1)});
	CHECK(f.vm.placed_channels(f.so).size() == 1);
	CHECK(f.m.f32(actor_transform + 4) == doctest::Approx(100));
	// 255 skips the remaining argument without evaluating its function.
	f.m.write<std::uint32_t>(f.script, 218); f.m.write<std::uint8_t>(f.script + 7, std::uint8_t(node_type::procedure));
	f.m.write<std::uint32_t>(f.script + 16, 255); f.m.write<std::uint8_t>(f.script + 23, std::uint8_t(node_type::integer));
	bool evaluated = false;
	f.vm.on_function(1002, [&](game::call& c, game::value&) { evaluated = true; return c.args; });
	game::value out; CHECK(f.vm.eval(f.so, f.script, out) == f.script + 32); CHECK_FALSE(evaluated);
	f.vm.next_tick();
	CHECK(f.vm.placed_channels(f.so).empty());
	f.procedure(218, {game::value::vector(7, 8, 9), f.integer(2), f.integer(1)});
	f.vm.reset_actor(f.so); CHECK(f.vm.placed_channels(f.so).empty());
	// Identity and translation-only transforms ignore the saved matrix rows.
	f.m.write<std::uint32_t>(actor_transform, 1);
	f.procedure(218, {game::value::vector(1, 2, 3), f.integer(2), f.integer(0)});
	CHECK(f.vm.placed_channels(f.so).at(2) == vec3{1, 2, 3});
	f.m.write<std::uint32_t>(actor_transform, 2);
	f.procedure(218, {game::value::vector(1, 2, 3), f.integer(2), f.integer(0)});
	CHECK(f.vm.placed_channels(f.so).at(2) == vec3{101, 2, 3});
	f.vm.remove(f.so); f.vm.restore(f.so); CHECK(f.vm.placed_channels(f.so).empty());
}

TEST_CASE("conditions 19 and 20 test kind-2 zones, material masks and the target's world flag")
{
	handler_fixture f;
	auto make_zone = [&]() {
		auto zone = f.m.allocate(0x30), vertices = f.m.allocate(12), kinds = f.m.allocate(2), elements = f.m.allocate(4);
		auto element = f.m.allocate(8), sphere = f.m.allocate(12), material = f.m.allocate(16), collide = f.m.allocate(4);
		f.m.write(zone, vertices); f.m.write(zone + 0x10, kinds); f.m.write(zone + 0x14, elements);
		f.m.write<std::uint16_t>(zone + 0x2E, 1); f.m.write<std::uint16_t>(kinds, 7);
		f.m.write(elements, element); f.m.write(element, sphere); f.m.write<std::uint16_t>(element + 4, 1);
		f.m.write<float>(sphere, 1); f.m.write(sphere + 4, material); f.m.write(material + 0xC, collide);
		f.m.write<std::uint16_t>(collide + 2, 5);
		return std::pair{zone, collide};
	};
	auto [zone, collide] = make_zone();
	auto head = f.m.allocate(12), link = f.m.allocate(8);
	f.m.write(f.collision + 4, head); f.m.write(head, link); f.m.write<std::uint32_t>(head + 8, 1);
	f.m.write(link + 4, zone); f.m.write<std::uint32_t>(f.collision + 0x34, 1);
	auto other = f.m.allocate(0x48), actor = f.m.allocate(0x38), standard = f.m.allocate(0x34);
	f.m.write<std::uint32_t>(other, 2); f.m.write(other + 4, actor); f.m.write(actor + 4, standard); f.m.write(actor + 0x14, f.collision);
	auto ta = f.m.allocate(0x58), tb = f.m.allocate(0x58);
	f.m.write(f.so + 0x24, ta); f.m.write(other + 0x24, tb);
	affine identity; game::set_global_placement(f.m, f.so, identity); game::set_global_placement(f.m, other, identity);
	auto test = [&](unsigned condition, int number, int mask) {
		return f.condition(condition, {f.me(), f.integer(number), f.object(other), f.integer(mask)});
	};
	CHECK_FALSE(test(19, 1, 0));
	f.m.write<std::uint8_t>(standard + 0x29, 4);
	CHECK(test(19, 1, 0)); CHECK(test(19, 1, 5)); CHECK_FALSE(test(19, 1, 3));
	CHECK_FALSE(test(19, 0, 0)); CHECK_FALSE(test(19, 2, 0));
	f.m.write<std::uint32_t>(f.collision + 0x34, 2); CHECK_FALSE(test(19, 1, 0));
	f.m.write<std::uint32_t>(f.collision + 0x34, 1);
	f.m.write<float>(tb + 4, 3); CHECK_FALSE(test(19, 1, 0)); f.m.write<float>(tb + 4, 0);
	// Selector 1 uses channel zero directly and geometry +8, not the kind-0 slot.
	auto table = f.m.allocate(20), part = f.m.allocate(0x48), object = f.m.allocate(8), geometry = f.m.allocate(16);
	f.m.write(f.data + 0x90, table); f.m.write(table, part); f.m.write<std::uint32_t>(part, 0x10);
	f.m.write(part + 4, object); f.m.write(object + 4, geometry); f.m.write(geometry + 8, zone); f.m.write(part + 0x24, ta);
	CHECK(test(20, 0, 1)); CHECK_FALSE(test(20, 1, 1));
	// A fallback provides geometry, but the original channel still provides its frame.
	auto large_table = f.m.allocate(0x8001 * 20);
	auto fallback = f.m.allocate(0x48), far_frame = f.m.allocate(0x58);
	f.m.write(f.data + 0x90, large_table); f.m.write(large_table, part);
	f.m.write(large_table + 0x8000 * 20, fallback); f.m.write<std::uint32_t>(part, 0);
	f.m.write(fallback + 4, object); f.m.write(fallback + 0x24, far_frame);
	f.m.write<float>(far_frame + 4, 100);
	CHECK(test(20, 0, 1));
	// B may expose only a collision-bearing visible part, without a collision set.
	f.m.write<std::uint32_t>(actor + 0x14, 0);
	auto bdata = f.m.allocate(0x118), list = f.m.allocate(0x14), active_part = f.m.allocate(12);
	f.m.write(actor, bdata); f.m.write(bdata + 0x10, list); f.m.write<std::uint8_t>(list + 0xE, 2);
	f.m.write(bdata + 0x98, active_part); f.m.write(active_part + 4, part); f.m.write<std::uint32_t>(part, 0x10);
	CHECK(test(19, 1, 1));
	f.m.write<std::uint8_t>(list + 0xE, 0); CHECK_FALSE(test(19, 1, 1));
	f.m.write<std::uint8_t>(list + 0xE, 2);
	f.m.write<std::uint8_t>(standard + 0x29, 0); CHECK_FALSE(test(20, 0, 0));
}

TEST_CASE("real data: Learn_31 dust creature places six distinct trailing channels")
{
	auto dir = real_game_dir(); if(!dir) return;
	auto data = game_data::locate(*dir); REQUIRE(data);
	auto files = load_level_files(*data, "Learn_31");
	auto header = read_level_header(files); scene tree(files.memory, header);
	auto bank = read_animation_bank(files); game::live_memory m(files.memory);
	auto rayman = header.placements.at(0).super_object.address();
	game::interpreter vm(m, rayman); game::register_handlers(vm); game::register_dialog_handlers(vm);
	game::set_dialog_resolver(vm, files); vm.set_animations(&bank);
	vm.set_world(game::world_collision(static_collision(files.memory, tree))); game::install_level_events(m, bank);
	std::vector<std::uint32_t> actors;
	for(const auto& node : tree.nodes()) if(node.type == super_object_type::actor) actors.push_back(node.at.address());
	vm.set_actors(actors);
	auto posed_actors = read_actors(files.memory, tree, bank);
	for(const auto& actor : posed_actors.actors) game::set_global_placement(m, tree.node(actor.node).at.address(), actor.world);
	for(auto bit : game::revisit_story_bits) REQUIRE(vm.set_story_bit(actors, bit));
	auto creature = tree.node(8).at.address();
	game::input keys(m, read_input_actions(files));
	bool placed = false, distinct = false;
	for(int tick = 0; tick < 600; ++tick)
	{
		auto head = game::global_placement(m, creature); REQUIRE(head);
		game::write_transform(m, rayman, {head->m[0][3] + 5, head->m[1][3], head->m[2][3]}, {1,0,0,0,1,0,0,0,1});
		keys.step(); game::step(vm, actors);
		auto standard = m.u32(vm.actor_of(creature) + 4);
		m.write<std::uint32_t>(standard + 0x24, m.u32(standard + 0x24) & ~0x800u); // in view
		const auto& positions = vm.placed_channels(creature);
		if(positions.size() != 6) continue;
		placed = true;
		std::set<vec3> unique;
		for(std::int16_t channel = 3; channel <= 8; ++channel)
		{
			REQUIRE(positions.contains(channel)); unique.insert(positions.at(channel));
		}
		if(tick > 60 && unique.size() == 6)
		{
			distinct = true;
			// Exercise the same bank pose path used by main.cc, not just the stored placements.
			auto entry = std::ranges::find(posed_actors.actors, std::size_t(8), &actor_set::entry::node);
			REQUIRE(entry != posed_actors.actors.end());
			auto actor = *entry;
			auto state = m.u32(m.u32(vm.actor_of(creature)) + 4), reference = m.u32(state + 4);
			actor.animation = m.read<std::uint16_t>(reference + 0x18);
			actor.world = *game::global_placement(m, creature);
			std::vector<posed_part> rendered;
			pose_actor(actor, bank, 0, rendered, positions);
			auto channels = pose(bank, *actor.animation, 0);
			for(const auto& [channel, position] : positions)
			{
				auto found = std::ranges::find(channels, channel, &channel_pose::id);
				REQUIRE(found != channels.end()); REQUIRE(found->object);
				auto mesh = actor.meshes.at(*found->object); REQUIRE(mesh);
				CHECK(std::ranges::any_of(rendered, [&](const auto& part) {
					return part.mesh == *mesh && std::abs(part.world.m[0][3] - position[0]) < 0.0001f
						&& std::abs(part.world.m[1][3] - position[1]) < 0.0001f && std::abs(part.world.m[2][3] - position[2]) < 0.0001f;
				}));
			}
			break;
		}
	}
	CHECK(placed); CHECK(distinct);
	for(const auto& [missing, count] : vm.missing)
	{
		CHECK_FALSE((std::string(missing.first) == "procedure" && missing.second == 218));
		CHECK_FALSE((std::string(missing.first) == "condition" && missing.second == 19));
	}
}

TEST_CASE("handler census: object and collision flags preserve neighboring bits")
{
	handler_fixture f;
	f.m.write<std::uint32_t>(f.so + 0x2C, 0x80000001);
	f.procedure(62, { f.object(f.so), f.integer(31), f.integer(2) });
	CHECK(f.m.u32(f.so + 0x2C) == 1);
	f.procedure(62, { f.object(f.so), f.integer(32), f.integer(1) });
	f.procedure(62, { f.object(f.so), f.integer(0), f.integer(3) });
	CHECK(f.m.u32(f.so + 0x2C) == 1);
	f.procedure(311, { f.object(f.so), f.integer(63), f.integer(-1) });
	CHECK(f.m.u32(f.so + 0x30) == 0x80000000);
	f.procedure(311, { f.object(f.so), f.integer(31), f.integer(0) });
	CHECK(f.m.u32(f.so + 0x30) == 0);
	const unsigned masks[] = { 1, 2, 4, 8, 0x20, 0x40 };
	for(unsigned j = 0; j < 6; ++j)
	{
		f.m.write<std::uint8_t>(f.collision + 0x42, 0x90);
		f.procedure(334 + j, { f.integer(257) });
		CHECK(f.m.u8(f.collision + 0x42) == (0x90 | masks[j]));
		f.procedure(334 + j, { f.integer(256) });
		CHECK(f.m.u8(f.collision + 0x42) == 0x90);
	}
	f.procedure(439, { f.integer(0x123), f.integer(-2) });
	CHECK(f.m.u8(f.standard + 0x2A) == 0x23);
	CHECK(f.m.u8(f.standard + 0x2B) == 0xFE);
	CHECK(f.function(271).as_int() == 0x23); CHECK(f.function(272).as_int() == 0xFE);
	f.procedure(438, {f.integer(0x180)}); CHECK(f.function(270).as_int() == 0x80);
	f.procedure(66, {});
	f.vm.menu_request[3] = 0;
	CHECK_FALSE(f.condition(124));
	f.vm.menu_request[3] = 7;
	CHECK(f.condition(124));
}

TEST_CASE("handler census: dynamics fields and linked selections")
{
	handler_fixture f;
	const std::pair<unsigned, unsigned> fields[] = {
		{264,0x1C},{265,0x20},{267,0x154},{268,0x158},{269,0x15C},{271,0x1DC},
		{272,0x1E0},{273,0x1E4},{275,0x160},{277,0x164},{293,0x168},{294,0x16C},{295,0x170}
	};
	for(auto [procedure, offset] : fields)
	{
		f.procedure(procedure, { game::value::real(-0.25f) });
		CHECK(f.m.f32(f.base + offset) == -0.25f);
	}
	f.procedure(266, { game::value::real(45) });
	CHECK(f.m.f32(f.base + 0x14) == doctest::Approx(1.0f));
	f.procedure(270, { game::value::real(0.25f), game::value::real(0.5f), game::value::real(1) });
	CHECK(f.m.f32(f.base + 0x154) == 0.25f);
	CHECK(f.m.f32(f.base + 0x158) == 0.5f);
	CHECK(f.m.f32(f.base + 0x15C) == 1);
	f.procedure(276, { game::value::vector(1, 2, 3) });
	f.procedure(278, { game::value::vector(4, -2, 1) });
	CHECK(f.m.read<vec3>(f.base + 0x184) == vec3{5, 0, 4});
	f.m.write<std::uint32_t>(f.base, 1);
	f.procedure(146, { f.me(), f.integer(0) });
	CHECK(f.m.u32(f.base) == 0x400001);
	f.procedure(146, { f.me(), f.integer(1) });
	CHECK(f.m.u32(f.base) == 1);
	f.procedure(305, { f.integer(257) });
	CHECK(f.m.u32(f.base + 8) == 0x40000000);
	f.procedure(305, { f.integer(256) });
	CHECK(f.m.u32(f.base + 8) == 0);
	auto head = f.m.allocate(12), second = f.m.allocate(12);
	f.m.write(f.base + 0x64, head); f.m.write(head + 8, second);
	f.procedure(150, { f.me(), f.integer(2) });
	CHECK(f.m.u32(f.base + 0x68) == second);
	for(int n : { 0, -1, 3 })
	{
		f.procedure(150, { f.me(), f.integer(n) });
		CHECK(f.m.u32(f.base + 0x68) == head);
	}
}

TEST_CASE("handler census: graph cursor wraps, clamps, and preserves failed moves")
{
	handler_fixture f;
	auto cursor = f.m.allocate(12), graph = f.m.allocate(12), n0 = f.m.allocate(16), n1 = f.m.allocate(16), n2 = f.m.allocate(16);
	f.m.write(f.actor + 0x18, cursor); f.m.write(cursor, graph); f.m.write(graph, n0);
	f.m.write<std::uint32_t>(graph + 8, 3); f.m.write(n0, n1); f.m.write(n1, n2);
	f.m.write<std::uint32_t>(n0 + 12, 100); f.m.write<std::uint32_t>(n1 + 12, 200); f.m.write<std::uint32_t>(n2 + 12, 300);
	CHECK(f.function(128).as_int() == 0);
	CHECK(f.function(129).as_int() == 2);
	CHECK(f.function(127, { f.integer(99) }).as_int() == 2);
	CHECK(f.function(130, { f.integer(1) }).as_int() == -1);
	CHECK(f.function(126).as_int() == 2);
	CHECK(f.function(131, { f.integer(2) }).as_int() == 0);
	CHECK(f.function(132, { f.integer(0) }).word() == 300);
	CHECK(f.function(132, { f.integer(3) }).word() == 0);
	CHECK(f.function(137, { f.integer(100) }).as_int() == 2);
	CHECK(f.function(138, { f.integer(200) }).as_int() == 1);
	CHECK(f.function(138, { f.integer(999) }).as_int() == -1);
	CHECK(f.function(126).as_int() == 1);
	f.m.write<std::uint8_t>(cursor + 8, 1);
	CHECK(f.function(127, { f.integer(-1) }).as_int() == 2);
	CHECK(f.function(130, { f.integer(2) }).as_int() == 1);
	CHECK(f.function(131, { f.integer(5) }).as_int() == 2);
	CHECK(f.function(132, { f.integer(-1) }).word() == 100);
}

TEST_CASE("handler census: list mutation and membership keep stable order")
{
	handler_fixture f;
	auto list = f.m.allocate(20), other = f.m.allocate(0x48), other_actor = f.m.allocate(0x38), other_standard = f.m.allocate(0x34);
	f.m.write<std::uint8_t>(list + 1, 4);
	f.m.write<std::uint32_t>(other, 2); f.m.write(other + 4, other_actor);
	f.m.write(other_actor + 4, other_standard); f.m.write(other_standard + 0xC, other);
	auto l = game::value::of(game::value_type::buffer, list);
	f.procedure(25, { l, f.me() });
	f.procedure(25, { l, f.object(other) });
	f.procedure(25, { l, f.me() });
	CHECK(f.m.u8(list) == 3);
	f.procedure(28, { l, f.me() });
	CHECK(f.m.u8(list) == 2);
	CHECK(f.m.u32(list + 4) == other);
	CHECK(f.m.u32(list + 8) == f.so);
	f.procedure(27, { l, f.integer(255) });
	CHECK(f.m.u8(list) == 1);
	f.procedure(27, { l, f.integer(9) });
	CHECK(f.m.u8(list) == 1);
	f.procedure(27, { l, f.integer(255) });
	f.procedure(27, { l, f.integer(255) });
	CHECK(f.m.u8(list) == 0);
	f.m.write(f.so + 8, other);
	f.procedure(303, { l });
	CHECK(f.m.u8(list) == 1);
	CHECK(f.m.u32(list + 4) == other);
}

TEST_CASE("handler census: health byte conversion and carried-by alternatives")
{
	handler_fixture f;
	f.m.write<std::uint8_t>(f.standard + 0x21, 5); f.m.write<std::uint8_t>(f.standard + 0x22, 10);
	f.procedure(1, { f.me(), f.integer(0x123) });
	CHECK(f.m.u8(f.standard + 0x30) == 0x23);
	f.procedure(4, { f.me(), game::value::real(258.9f) });
	CHECK(f.m.u8(f.standard + 0x21) == 7);
	f.procedure(4, { f.me(), f.integer(9) });
	CHECK(f.m.u8(f.standard + 0x21) == 10);
	f.procedure(5, { f.me(), f.integer(20) });
	CHECK(f.m.u8(f.standard + 0x21) == 0);
	auto carrier = f.m.allocate(0x48);
	f.m.write<std::uint32_t>(f.base + 0xC, 4); f.m.write(f.base + 0x284, carrier);
	CHECK(f.condition(104, { f.object(carrier) }));
	f.m.write<std::uint32_t>(f.base + 0x284, 0);
	CHECK_FALSE(f.condition(104, { f.object(carrier) }));
	f.m.write(carrier + 8, f.so); f.m.write<std::uint32_t>(f.base + 0xC, 0);
	CHECK(f.condition(104, { f.object(carrier) }));
	f.m.write<std::uint32_t>(carrier + 8, 0);
	CHECK_FALSE(f.condition(104, { f.object(carrier) }));
	auto brain = f.m.allocate(12), mind = f.m.allocate(20), reflex = f.m.allocate(32);
	f.m.write(f.actor + 0xC, brain); f.m.write(brain, mind); f.m.write(mind + 8, reflex);
	f.m.write<std::uint32_t>(reflex + 8, 1234);
	CHECK(f.condition(69, { f.me(), f.integer(1234) }));
	CHECK_FALSE(f.condition(69, { f.me(), f.integer(1235) }));
}

TEST_CASE("handler census: zone edits affect queries and invalidate cached surfaces")
{
	handler_fixture f;
	auto head = f.m.allocate(12), node = f.m.allocate(8), zone = f.m.allocate(0x50);
	auto vertices = f.m.allocate(24), kinds = f.m.allocate(4), elements = f.m.allocate(4), element = f.m.allocate(4), entries = f.m.allocate(12);
	f.m.write(f.collision + 8, head); f.m.write(f.collision + 4, head); f.m.write(f.collision, head);
	f.m.write(head, node); f.m.write<std::uint32_t>(head + 8, 1); f.m.write(node + 4, zone);
	f.m.write(zone, vertices); f.m.write(zone + 0x10, kinds); f.m.write(zone + 0x14, elements);
	f.m.write(elements, element); f.m.write(element, entries);
	f.m.write<std::int16_t>(kinds, 7); f.m.write<float>(entries, 1);
	f.procedure(307, { f.me(), f.integer(1), game::value::vector(2, 4, 6) });
	CHECK(f.m.read<vec3>(vertices) == vec3{2, 4, 6});
	for(unsigned fn : {169, 170, 171})
		CHECK(f.function(fn, { f.integer(1) }).words == game::value::vector(2, 4, 6).words);
	f.vm.surface_cache().actors[f.so] = {};
	f.procedure(420, { f.integer(1), game::value::real(2.5f) });
	CHECK(f.m.f32(entries) == 2.5f);
	CHECK(f.vm.surface_cache().actors.empty());
	CHECK(f.function(175, { f.integer(1) }).words == game::value::vector(2.5f, 2.5f, 2.5f).words);
	f.m.write<std::int16_t>(kinds, 8);
	f.m.write<std::int16_t>(entries, 0); f.m.write<std::int16_t>(entries + 2, 1);
	f.m.write(vertices, vec3{1, 2, 3}); f.m.write(vertices + 12, vec3{5, 8, 11});
	f.procedure(308, { f.me(), f.integer(1), game::value::vector(10, 20, 30) });
	CHECK(f.m.read<vec3>(vertices) == vec3{10, 20, 30});
	CHECK(f.m.read<vec3>(vertices + 12) == vec3{14, 26, 38});
	CHECK(f.function(171, { f.integer(1) }).words == game::value::vector(12, 23, 34).words);
	f.procedure(309, { f.me(), f.integer(1), game::value::vector(0, 0, 0) });
	CHECK(f.m.read<vec3>(vertices + 12) == vec3{4, 6, 8});
	f.procedure(340, { f.integer(0x123) });
	CHECK(f.m.u8(f.collision + 0x40) == 0x23);
}

TEST_CASE("handler census: part positions, scale and direction use the chosen transform")
{
	handler_fixture f;
	auto world = f.m.allocate(0x58), local = f.m.allocate(0x58), part = f.m.allocate(0x48), table = f.m.allocate(40);
	f.m.write(f.data + 0x90, table); f.m.write(table + 20, part);
	f.m.write(part + 0x24, world); f.m.write(part + 0x20, local); f.m.write(f.so + 0x24, world);
	f.m.write(world + 4, vec3{10, 20, 30}); f.m.write(local + 4, vec3{1, 2, 3});
	f.m.write<std::uint32_t>(world, 4);
	f.m.write(world + 0x10, vec3{0, 1, 0}); f.m.write(world + 0x1C, vec3{-1, 0, 0}); f.m.write(world + 0x28, vec3{0, 0, 1});
	f.m.write(world + 0x34, vec3{0, 2, 0}); f.m.write(world + 0x40, vec3{-3, 0, 0}); f.m.write(world + 0x4C, vec3{0, 0, 4});
	CHECK(f.function(1).words == game::value::vector(10, 20, 30).words);
	CHECK(f.function(210, { f.integer(1) }).words == game::value::vector(10, 20, 30).words);
	CHECK(f.function(211, { f.integer(1) }).words == game::value::vector(1, 2, 3).words);
	CHECK(f.function(212, { f.integer(1) }).words == game::value::vector(2, 3, 4).words);
	auto forward = f.function(213, { f.integer(1) });
	CHECK(forward.component(0) == 1); CHECK(forward.component(1) == 0); CHECK(forward.component(2) == 0);
	for(unsigned n : {210, 211, 212, 213}) CHECK(f.function(n, { f.integer(255) }).words == game::value::vector(0, 0, 0).words);
}

TEST_CASE("handler census: projection and ballistic calculations have exact simple cases")
{
	handler_fixture f;
	auto v = [](float x, float y, float z) { return game::value::vector(x,y,z); };
	CHECK(f.function(178, { v(3,4,0), v(0,0,0), v(1,0,0) }).words == v(0,-4,0).words);
	CHECK(f.function(178, { v(3,4,0), v(0,0,0), v(0,0,0) }).words == v(0,0,0).words);
	CHECK(f.function(179, { v(3,4,0), v(0,0,0), v(2,0,0) }).words == v(-1,-4,0).words);
	CHECK(f.function(179, { v(-1,4,0), v(0,0,0), v(2,0,0) }).words == v(1,-4,0).words);
	CHECK(f.function(179, { v(3,4,0), v(0,0,0), v(0,0,0) }).words == v(-3,-4,0).words);
	f.m.write<float>(f.base + 0x10, 8);
	CHECK(f.function(238, { v(0,0,0), v(8,4,0), game::value::real(4) }).words == v(4,2,8).words);
	CHECK(f.function(238, { v(0,0,0), v(8,4,0), game::value::real(-1) }).words == v(0,0,0).words);
	f.m.write<float>(f.base + 0x10, 0);
	CHECK(f.function(238, { v(0,0,0), v(8,4,0), game::value::real(4) }).words == v(0,0,0).words);
}

TEST_CASE("state procedure installs gravity before the next script function")
{
	handler_fixture f;
	auto state = f.m.allocate(0x50), mechanics = f.m.allocate(0x3C);
	f.m.write(state + 0x24, mechanics);
	f.m.write<std::uint32_t>(mechanics, 1);
	f.m.write<float>(mechanics + 8, 8);
	f.procedure(72, { f.integer(int(state)) });
	CHECK(f.m.f32(f.base + 0x10) == 8);
	auto v = [](float x, float y, float z) { return game::value::vector(x, y, z); };
	CHECK(f.function(238, { v(0, 0, 0), v(8, 4, 0), game::value::real(4) }).words == v(4, 2, 8).words);
}

TEST_CASE("handler census: directed graph fields and reset restore saved defaults")
{
	handler_fixture f;
	auto graph = f.m.allocate(12), first = f.m.allocate(28), last = f.m.allocate(28), links = f.m.allocate(12), link = f.m.allocate(32);
	f.m.write(graph, first); f.m.write(first, last); f.m.write<std::uint32_t>(first + 12, 100); f.m.write<std::uint32_t>(last + 12, 200);
	f.m.write(first + 0x18, links); f.m.write(links, link); f.m.write(link + 12, last);
	f.m.write<std::uint32_t>(first + 0x14, 8); f.m.write<std::uint32_t>(link + 0x14, 3); f.m.write<std::uint32_t>(link + 0x1C, 9);
	f.procedure(400, { f.integer(int(graph)) });
	CHECK(f.function(144, {f.integer(int(graph)), f.integer(100)}).word() == 8);
	CHECK(f.function(140, {f.integer(int(graph)), f.integer(100), f.integer(200)}).word() == 3);
	CHECK(f.function(142, {f.integer(int(graph)), f.integer(100), f.integer(200)}).word() == 9);
	CHECK(f.function(141, {f.integer(int(graph)), f.integer(100), f.integer(200), f.integer(4), f.integer(0)}).as_int() == 0);
	CHECK(f.m.u32(link + 0x10) == 7);
	CHECK(f.function(141, {f.integer(int(graph)), f.integer(100), f.integer(200), f.integer(3), f.integer(4)}).as_int() == 0);
	CHECK(f.m.u32(link + 0x10) == 4);
	CHECK(f.function(143, {f.integer(int(graph)), f.integer(100), f.integer(200), f.integer(-8)}).as_int() == 0);
	CHECK(f.m.read<std::int32_t>(link + 0x18) == -8);
	CHECK(f.function(142, {f.integer(int(graph)), f.integer(200), f.integer(100)}).as_int() == -1);
	f.procedure(400, { f.integer(int(graph)) });
	CHECK(f.m.u32(link + 0x10) == 3); CHECK(f.m.u32(link + 0x18) == 9);
}

TEST_CASE("handler census: mechanics reset, update stamp and contact actor ancestor")
{
	handler_fixture f;
	auto state = f.m.allocate(0x30), mechanics = f.m.allocate(0x3C), report = f.m.allocate(0x100), part = f.m.allocate(0x48);
	f.m.write(f.data + 4, state); f.m.write(state + 0x24, mechanics);
	f.m.write<std::uint32_t>(mechanics, 1); f.m.write<std::uint32_t>(mechanics + 4, 4); f.m.write<float>(mechanics + 8, 25);
	f.procedure(80, {}); CHECK(f.m.u32(f.base + 4) == mechanics); CHECK(f.m.f32(f.base + 0x10) == 25);
	f.m.write<float>(f.base + 0x10, 3);
	f.procedure(80, {}); CHECK(f.m.f32(f.base + 0x10) == 25);
	f.vm.next_tick(); CHECK_FALSE(f.condition(56, { f.me() }));
	f.m.write(f.standard + 0x18, f.vm.frame_number()); CHECK(f.condition(56, { f.me() }));
	f.vm.next_tick(); CHECK_FALSE(f.condition(56, { f.me() }));
	f.m.write(f.base + 0x150, report); f.m.write<std::uint32_t>(report + 4, 0x10000);
	f.m.write(report + 0xA4, part); f.m.write(part + 0x1C, f.so);
	CHECK(f.condition(33, { f.me() }));
	f.m.write<std::uint32_t>(report + 4, 0); CHECK_FALSE(f.condition(33, { f.me() }));
}

TEST_CASE("handler census: scalar speeds set the appropriate axes and preserve other fields")
{
	handler_fixture f;
	for(auto [number, offset, flags] : std::initializer_list<std::tuple<unsigned,unsigned,unsigned>>{
		{185,0x2C,0x84000}, {187,0x24,0xE4000}, {189,0x30,0x100000}, {191,0x38,0x400000},
		{192,0x30,0x300000}, {193,0x30,0x700000}, {195,0x30,0x108000}, {196,0x34,0x208000},
		{197,0x38,0x408000}, {199,0x30,0x708000}, {202,0x190,0x800000}, {203,0x194,0x1000000},
		{204,0x198,0x2000000}, {205,0x190,0x1800000}, {206,0x190,0x3800000},
		{208,0x24,0x810000}, {209,0x28,0x1010000}, {210,0x2C,0x2010000},
		{211,0x24,0x1810000}, {212,0x24,0x3810000}, {281,0x19C,0x4000000},
		{282,0x1A0,0x8000000}, {283,0x1A4,0x10000000}, {284,0x19C,0x1C000000}})
	{
		f.m.write<std::uint32_t>(f.base + 8, 1);
		if(number == 187 || number == 193 || number == 199 || number == 206 || number == 212 || number == 284)
			f.procedure(number, {game::value::real(2),game::value::real(3),game::value::real(4)});
		else if(number == 192 || number == 205 || number == 211)
			f.procedure(number, {game::value::real(2),game::value::real(3)});
		else f.procedure(number, {game::value::real(2)});
		CHECK(f.m.f32(f.base + offset) == 2);
		CHECK(f.m.u32(f.base + 8) == (flags | 1));
	}
	f.m.write<std::uint32_t>(f.base + 8, 0x1C000001);
	f.procedure(285, {}); CHECK(f.m.u32(f.base + 8) == 0x18000001);
	f.procedure(286, {}); CHECK(f.m.u32(f.base + 8) == 0x10000001);
	f.procedure(287, {}); CHECK(f.m.u32(f.base + 8) == 1);
	f.procedure(262, {f.integer(1)}); CHECK(f.m.u32(f.base + 8) == 0x2001);
	f.procedure(262, {f.integer(256)}); CHECK(f.m.u32(f.base + 8) == 1);
}

TEST_CASE("handler census: percentage channel packing and native crossed addition")
{
	handler_fixture f;
	auto packed = game::value::of(0x27, 0x001E140A);
	CHECK(f.function(76, {f.integer(10),f.integer(20),f.integer(30)}).word() == packed.word());
	CHECK(f.function(75, {f.integer(-1),game::value::real(25.9f),f.integer(200),f.integer(100)}).word() == 0x64641900);
	CHECK(f.function(70, {packed,game::value::of(0x27,0x04030201)}).word() == 0x0420170B);
	CHECK(f.function(71, {packed,f.integer(5)}).word() == 0x001E140F);
	CHECK(f.function(72, {packed,f.integer(5)}).word() == 0x0023140A);
	CHECK(f.function(73, {packed,f.integer(5)}).word() == 0x001E190A);
	CHECK(f.function(74, {packed,f.integer(5)}).word() == 0x051E140A);
	for(unsigned j = 0; j < 4; ++j)
		CHECK(f.function(77 + j, {game::value::of(0x27, 0xFF1E140A)}).word() == std::array<unsigned,4>{10,20,30,100}[j]);
	CHECK(f.function(71, {packed,f.integer(-1)}).word() == 0x001E1464);
}

TEST_CASE("handler census: secondary dynamics and high-word masks")
{
	handler_fixture f;
	auto block = f.m.allocate(0x20), masks = f.m.allocate(8), dynamics = f.m.allocate(8);
	f.m.write(f.actor + 8, dynamics); f.m.write(dynamics + 4, block); f.m.write(block + 0x10, vec3{1,2,3});
	CHECK(f.function(311).words == game::value::vector(1,2,3).words);
	f.m.write(f.actor + 0x2C, masks);
	f.procedure(364, {f.integer(0x12345678), f.integer(-1)});
	CHECK(f.m.u32(masks) == 0x56780000); CHECK(f.m.u32(masks + 4) == 0xFFFF0000);
}

TEST_CASE("handler census: shortest paths honor capability bits and cheaper queued routes")
{
	handler_fixture f;
	auto graph = f.m.allocate(12), cursor = f.m.allocate(12), a = f.m.allocate(28), b = f.m.allocate(28), c = f.m.allocate(28);
	auto ab_list = f.m.allocate(12), b_list = f.m.allocate(12), ab = f.m.allocate(32), ac = f.m.allocate(32), bc = f.m.allocate(32);
	f.m.write(f.actor + 0x18, cursor); f.m.write(graph, a); f.m.write<std::uint32_t>(graph + 8, 3);
	f.m.write(a, b); f.m.write(b, c); f.m.write<std::uint32_t>(a + 12, 100); f.m.write<std::uint32_t>(b + 12, 200); f.m.write<std::uint32_t>(c + 12, 300);
	f.m.write(a + 0x18, ab_list); f.m.write(ab_list, ac); f.m.write(ac, ab); f.m.write(ac + 12, c);
	f.m.write(ab + 12, b); f.m.write(b + 0x18, b_list); f.m.write(b_list, bc); f.m.write(bc + 12, c);
	f.m.write<std::uint32_t>(ac + 0x10, 2); f.m.write<std::uint32_t>(ac + 0x18, 10);
	f.m.write<std::uint32_t>(ab + 0x10, 1); f.m.write<std::uint32_t>(ab + 0x18, 2);
	f.m.write<std::uint32_t>(bc + 0x10, 1); f.m.write<std::uint32_t>(bc + 0x18, 3);
	f.m.write<std::uint32_t>(f.standard + 0x1C, 3);
	CHECK(f.function(121, {f.integer(int(graph)),f.integer(100),f.integer(300)}).as_int() == 3);
	CHECK(f.function(126).as_int() == 0);
	CHECK(f.function(132, {f.integer(0)}).word() == 100);
	CHECK(f.function(132, {f.integer(1)}).word() == 200);
	CHECK(f.function(132, {f.integer(2)}).word() == 300);
	CHECK(f.function(133, {f.integer(0)}).word() == 1);
	CHECK(f.function(127, {f.integer(99)}).as_int() == 2);
	CHECK(f.function(131, {f.integer(1)}).as_int() == 1);
	CHECK(f.function(138, {f.integer(100)}).as_int() == 0);
	f.m.write<std::uint32_t>(f.standard + 0x1C, 2);
	CHECK(f.function(121, {f.integer(int(graph)),f.integer(100),f.integer(300)}).as_int() == 2);
	CHECK(f.function(132, {f.integer(1)}).word() == 300);
	CHECK(f.function(133, {f.integer(0)}).word() == 2);
	f.m.write<std::uint32_t>(f.standard + 0x1C, 0);
	CHECK(f.function(121, {f.integer(int(graph)),f.integer(100),f.integer(300)}).as_int() == 0);
	CHECK(f.function(126).as_int() == -1);
	CHECK(f.function(132, {f.integer(0)}).word() == 0);
}

TEST_CASE("nearest eligible waypoint honors capability and exclusions")
{
	handler_fixture f;
	auto graph = f.m.allocate(12), a = f.m.allocate(28), b = f.m.allocate(28), c = f.m.allocate(28);
	auto wa = f.m.allocate(20), wb = f.m.allocate(20), wc = f.m.allocate(20);
	f.m.write(graph, a); f.m.write<std::uint32_t>(graph + 8, 3);
	f.m.write(a, b); f.m.write(b, c);
	f.m.write(a + 12, wa); f.m.write(b + 12, wb); f.m.write(c + 12, wc);
	f.m.write<std::uint32_t>(a + 16, 1); f.m.write<std::uint32_t>(b + 16, 3); f.m.write<std::uint32_t>(c + 16, 3);
	f.m.write(wa, vec3{1, 0, 0}); f.m.write(wb, vec3{2, 0, 0}); f.m.write(wc, vec3{3, 0, 0});
	auto query = [&](int mask, std::uint32_t x, std::uint32_t y) {
		return f.function(115, {f.integer(int(graph)), f.integer(0), f.integer(mask), f.integer(int(x)), f.integer(int(y))}).word();
	};
	CHECK(query(3, 0, 0) == wb);
	CHECK(query(3, wb, 0) == wc);
	CHECK(query(3, wb, wc) == 0);
	CHECK(query(1, 0, 0) == wa);
	CHECK(f.function(116, {f.integer(int(graph)), f.integer(0), game::value::vector(1,0,0)}).word() == wa);
	CHECK(f.function(117, {f.integer(int(graph)), f.integer(0), f.integer(3), f.integer(int(wb)), f.integer(0), game::value::vector(1,0,0)}).word() == wc);
	collision_triangle wall;
	wall.corners = {vec3{1.5f,-4,-4}, vec3{1.5f,4,-4}, vec3{1.5f,0,4}};
	f.vm.set_world(game::world_collision({wall}));
	CHECK(f.function(115, {f.integer(int(graph)), f.integer(1), f.integer(3), f.integer(0), f.integer(0)}).word() == 0);
	CHECK(f.function(115, {f.integer(int(graph)), f.integer(1), f.integer(1), f.integer(0), f.integer(0)}).word() == wa);
}

TEST_CASE("procedure 78 exits the loop opened by procedure 76")
{
	handler_fixture f;
	auto node = [&](unsigned offset, unsigned parameter, node_type type, unsigned indent) {
		f.m.write<std::uint32_t>(f.script + offset, parameter);
		f.m.write<std::uint8_t>(f.script + offset + 6, std::uint8_t(indent));
		f.m.write<std::uint8_t>(f.script + offset + 7, std::uint8_t(type));
	};
	node(0, 76, node_type::procedure, 1); node(8, 30, node_type::integer, 2);
	node(16, 78, node_type::procedure, 1); node(24, 0, node_type::integer, 2);
	node(32, 77, node_type::procedure, 1); node(40, 0, node_type::end, 0);
	game::value result;
	CHECK(f.vm.eval(f.so, f.script, result) == f.script + 16);
	REQUIRE(f.vm.loops.size() == 1);
	CHECK(f.vm.eval(f.so, f.script + 16, result) == f.script + 40);
	CHECK(f.vm.loops.empty());
}

TEST_CASE("handler census: list model and family filters distinguish retaining from dropping")
{
	handler_fixture f;
	auto brain = f.m.allocate(12), mind = f.m.allocate(20), list = f.m.allocate(12);
	f.m.write(f.actor + 0xC, brain); f.m.write(brain, mind); f.m.write<std::uint32_t>(mind, 123);
	f.m.write<std::uint32_t>(f.data + 0x14, 456);
	f.m.write<std::uint8_t>(list + 1, 2);
	auto l = game::value::of(game::value_type::buffer, list);
	f.procedure(25, {l, f.me()});
	CHECK(f.condition(38, {l, f.me()}));
	CHECK(f.condition(39, {l, f.integer(123)}));
	CHECK_FALSE(f.condition(39, {l, f.integer(124)}));
	CHECK(f.condition(40, {l, f.integer(456)}));
	f.procedure(32, {l, f.integer(456)}); CHECK(f.m.u8(list) == 1);
	f.procedure(33, {l, f.integer(123)}); CHECK(f.m.u8(list) == 1);
	f.procedure(36, {l, f.integer(456)}); CHECK(f.m.u8(list) == 0);
	f.procedure(25, {l, f.me()});
	f.procedure(37, {l, f.integer(123)}); CHECK(f.m.u8(list) == 0);
}

TEST_CASE("handler census: redirected byte setters and position queries choose the right actor")
{
	handler_fixture f;
	auto so = f.m.allocate(0x48), actor = f.m.allocate(0x38), standard = f.m.allocate(0x34), brain = f.m.allocate(12), data = f.m.allocate(0x118);
	auto world = f.m.allocate(0x58), own_world = f.m.allocate(0x58);
	f.m.write<std::uint32_t>(so, 2); f.m.write(so + 4, actor); f.m.write(actor + 4, standard); f.m.write(standard + 0xC, so);
	f.m.write(actor + 0xC, brain); f.m.write(actor, data); f.m.write(so + 0x24, world); f.m.write(f.so + 0x24, own_world);
	f.m.write(world + 4, vec3{4,5,6}); f.m.write(own_world + 4, vec3{1,2,3});
	f.invoke(node_type::procedure, 237, {f.integer(0x123)}, actor); CHECK(f.m.u8(brain + 8) == 0x23);
	f.invoke(node_type::procedure, 438, {f.integer(0x124)}, actor); CHECK(f.m.u8(standard + 0x33) == 0x24);
	CHECK(f.m.u8(f.standard + 0x33) == 0);
	CHECK(f.invoke(node_type::function, 210, {f.integer(0)}, actor).words == game::value::vector(4,5,6).words);
	CHECK(f.invoke(node_type::function, 1, {}, actor).words == game::value::vector(1,2,3).words);
	// Function 1 leaves the pending target alone, as its native handler does.
	CHECK(f.function(0).words == game::value::vector(4,5,6).words);
}

TEST_CASE("handler census: the actor tick writes the update stamp only for active actors")
{
	handler_fixture f;
	f.m.write<std::uint8_t>(f.standard + 0x29, 2);
	std::array<std::uint32_t,1> actors{f.so};
	game::step(f.vm, actors);
	CHECK(f.m.u32(f.standard + 0x18) == f.vm.frame_number());
	CHECK(f.condition(56, {f.me()}));
	f.m.write<std::uint8_t>(f.standard + 0x29, 0);
	game::step(f.vm, actors);
	CHECK_FALSE(f.condition(56, {f.me()}));
}

TEST_CASE("handler census: channel bytes and animation frame count")
{
	handler_fixture f;
	auto table = f.m.allocate(60), state = f.m.allocate(0x30), animation = f.m.allocate(0x20);
	f.m.write(f.data + 0x90, table); f.m.write<std::uint32_t>(f.data + 0x94, 3);
	f.procedure(97, {f.integer(1)}); CHECK(f.m.u8(table + 36) == 1); CHECK(f.m.u8(table + 16) == 0);
	f.procedure(98, {f.integer(2)}); CHECK(f.m.u8(table + 16) == 1); CHECK(f.m.u8(table + 56) == 0);
	f.procedure(99, {f.integer(1)}); CHECK(f.m.u8(table + 36) == 0);
	f.procedure(97, {f.integer(0xFFFF)}); CHECK(f.m.u8(table + 16) == 1); CHECK(f.m.u8(table + 36) == 1); CHECK(f.m.u8(table + 56) == 1);
	f.procedure(216, {}); CHECK(f.m.u8(table + 16) == 0); CHECK(f.m.u8(table + 36) == 0); CHECK(f.m.u8(table + 56) == 0);
	f.m.write(f.data + 4, state); f.m.write(state + 4, animation); f.m.write<std::uint16_t>(animation, 456);
	CHECK(f.function(148).as_int() == 456);
}

TEST_CASE("handler census: graph neighbor choice and node capability modes")
{
	handler_fixture f;
	auto graph = f.m.allocate(12), a = f.m.allocate(28), b = f.m.allocate(28), c = f.m.allocate(28), list = f.m.allocate(12), ab = f.m.allocate(32), ac = f.m.allocate(32);
	f.m.write(graph, a); f.m.write(a, b); f.m.write(b, c);
	f.m.write<std::uint32_t>(a + 12, 100); f.m.write<std::uint32_t>(b + 12, 200); f.m.write<std::uint32_t>(c + 12, 300);
	f.m.write(a + 0x18, list); f.m.write(list, ab); f.m.write(ab, ac); f.m.write(ab + 12, b); f.m.write(ac + 12, c);
	f.m.write<std::uint32_t>(ab + 0x10, 1); f.m.write<std::uint32_t>(ac + 0x10, 2);
	f.m.write<std::uint32_t>(ab + 0x18, 9); f.m.write<std::uint32_t>(ac + 0x18, 5);
	CHECK(f.function(118, {f.integer(int(graph)),f.integer(100),f.integer(3)}).word() == 300);
	CHECK(f.function(118, {f.integer(int(graph)),f.integer(100),f.integer(1)}).word() == 200);
	CHECK(f.function(118, {f.integer(int(graph)),f.integer(100),f.integer(0)}).word() == 0);
	f.m.write<std::uint32_t>(a + 0x10, 7); f.m.write<std::uint32_t>(b + 0x10, 7);
	f.procedure(395, {f.integer(int(graph)),f.integer(100),f.integer(2),f.integer(2)});
	CHECK(f.m.u32(a + 0x10) == 7); CHECK(f.m.u32(b + 0x10) == 5);
	f.procedure(395, {f.integer(int(graph)),f.integer(100),f.integer(1),f.integer(4)});
	CHECK(f.m.u32(a + 0x10) == 0); // native mode 4 clears everything via fallthrough
	f.procedure(395, {f.integer(int(graph)),f.integer(200),f.integer(8),f.integer(3)});
	CHECK(f.m.u32(a + 0x10) == 0); CHECK(f.m.u32(b + 0x10) == 8);
}

TEST_CASE("handler census: newly reached capability tests and animation motion")
{
	handler_fixture f;
	CHECK(f.condition(107, {f.integer(7),f.integer(3)}));
	CHECK_FALSE(f.condition(107, {f.integer(1),f.integer(3)}));
	CHECK(f.condition(108, {f.integer(1),f.integer(3)}));
	CHECK_FALSE(f.condition(108, {f.integer(0),f.integer(3)}));
	f.m.write<std::uint32_t>(f.standard + 0x1C, 5);
	CHECK(f.condition(110, {f.integer(5)})); CHECK_FALSE(f.condition(110, {f.integer(3)}));
	CHECK(f.condition(111, {f.integer(3)})); CHECK(f.condition(112, {f.integer(2)}));
	CHECK_FALSE(f.condition(112, {f.integer(32)}));
	f.m.write(f.base + 0x54, vec3{2,4,6});
	CHECK(f.function(239).words == game::value::vector(2,4,6).words);
}

TEST_CASE("homing ranks live actors only in the current and linked sectors")
{
	game::live_memory m(three_super_objects());
	auto make_actor = [&](vec3 position) {
		std::uint32_t so = m.allocate(0x48), actor = m.allocate(0x38), standard = m.allocate(0x34), transform = m.allocate(0x58);
		m.write<std::uint32_t>(so, 2);
		m.write(so + 4, actor);
		m.write<std::uint32_t>(so + 0x1C, 0x1000);
		m.write(so + 0x24, transform);
		m.write(actor + 4, standard);
		m.write(standard + 0xC, so);
		m.write<std::uint32_t>(standard + 0x24, 32);
		m.write<std::uint32_t>(transform, 3);
		m.write(transform + 4, position);
		for(std::uint32_t i = 0; i < 3; i++)
		{
			m.write<float>(transform + 0x10 + i * 16, 1);
			m.write<float>(transform + 0x34 + i * 16, 1);
		}
		return so;
	};
	auto make_sector = [&](float low, float high) {
		std::uint32_t so = m.allocate(0x48), data = m.allocate(0x88);
		m.write<std::uint32_t>(so, 4);
		m.write(so + 4, data);
		m.write(data + 0x60, vec3{ low, -20, -40 });
		m.write(data + 0x6C, vec3{ high, 20, 40 });
		return so;
	};
	std::uint32_t self = make_actor({ 0, 0, 0 }), near = make_actor({ 5, 2, 0 }), far = make_actor({ 15, 1, 0 });
	std::uint32_t a = make_sector(-10, 10), b = make_sector(10, 30);
	m.write(0x1008, a);
	m.write(a + 0x14, b);
	game::interpreter vm(m, self);
	game::register_handlers(vm);
	vm.set_actors({ self, near, far });
	CHECK(vm.sector_at({ 15, 1, 0 }) == b);
	std::uint32_t script = m.allocate(8 * 8), direction = m.allocate(12);
	m.write(direction, vec3{ 1, 0, 0 });
	auto node = [&](std::uint32_t at, node_type type, std::uint32_t param) {
		m.write(at, param);
		m.write<std::uint8_t>(at + 6, at == script ? 0 : 1);
		m.write<std::uint8_t>(at + 7, std::uint8_t(type));
	};
	node(script, node_type::function, 113);
	node(script + 8, node_type::integer, 32);
	node(script + 16, node_type::integer, 1); // lateral weight
	node(script + 24, node_type::integer, 0);
	node(script + 32, node_type::integer, 0);
	node(script + 40, node_type::constant_vector, direction);
	node(script + 48, node_type::integer, 35);
	node(script + 56, node_type::integer, 35);
	auto search = [&] {
		game::value out;
		CHECK(vm.eval(self, script, out) == script + 64);
		CHECK(out.type == game::value_type::actor);
		return out.word();
	};
	CHECK(search() == vm.actor_of(near));
	// The more distant actor wins on lateral alignment once its sector becomes reachable.
	std::uint32_t link = m.allocate(12), sector_data = m.u32(a + 4);
	m.write(link + 4, b);
	m.write(sector_data + 0x24, link);
	m.write<std::uint32_t>(sector_data + 0x2C, 1);
	CHECK(search() == vm.actor_of(far));
	node(script + 16, node_type::integer, 0);
	node(script + 24, node_type::integer, 1); // horizontal distance instead of lateral alignment
	CHECK(search() == vm.actor_of(near));
	node(script + 24, node_type::integer, 0);
	node(script + 32, node_type::integer, 1); // full distance
	CHECK(search() == vm.actor_of(near));
	node(script + 32, node_type::integer, 0);
	node(script + 16, node_type::integer, 1);
	std::uint32_t standard = m.u32(vm.actor_of(far) + 4), transform = m.u32(far + 0x24);
	m.write<std::uint8_t>(standard + 0x29, 1);
	CHECK(search() == vm.actor_of(near));
	m.write<std::uint8_t>(standard + 0x29, 0);
	m.write<std::uint32_t>(standard + 0x24, 0);
	CHECK(search() == vm.actor_of(near));
	m.write<std::uint32_t>(standard + 0x24, 32);
	m.write(transform + 4, vec3{ 15, 1, 30 });
	CHECK(search() == vm.actor_of(near)); // above the elevation limit
	m.write(transform + 4, vec3{ 15, 1, -30 });
	CHECK(search() == vm.actor_of(far)); // the exe's elevation limit is signed
	m.write(transform + 4, vec3{ 5, 0, 0 });
	m.write<std::uint32_t>(sector_data + 0x2C, 0);
	CHECK(search() == vm.actor_of(far)); // live move across the sector boundary; exact axis
	vm.remove(far);
	CHECK(search() == vm.actor_of(near));
	m.write(direction, vec3{ 1, 0, 1 });
	CHECK(search() == vm.actor_of(near)); // transpose of the non-unit frame, not its matrix inverse
	m.write(direction, vec3{ -1, 0, 0 });
	CHECK(search() == 0);

	// A scaled and rotated box centre must be an offset in the target's rotation axes, not a
	// world position. Its active state and material mask both matter (function 154).
	std::uint32_t actor = vm.actor_of(near), set = m.allocate(0x68), head = m.allocate(12), entry = m.allocate(8);
	std::uint32_t zone = m.allocate(0x30), vertices = m.allocate(24), kinds = m.allocate(4), elements = m.allocate(4);
	std::uint32_t element = m.allocate(8), box = m.allocate(8), material = m.allocate(16), collide = m.allocate(4);
	m.write(actor + 0x14, set);
	m.write(set + 4, head);
	m.write<std::uint32_t>(set + 0x34, 1);
	m.write(head, entry);
	m.write<std::uint32_t>(head + 8, 1);
	m.write(entry + 4, zone);
	m.write(zone, vertices);
	m.write(zone + 0x10, kinds);
	m.write(zone + 0x14, elements);
	m.write<std::uint16_t>(zone + 0x2E, 1);
	m.write(vertices, vec3{ 0, 0, 0 });
	m.write(vertices + 12, vec3{ 2, 4, 6 });
	m.write<std::uint16_t>(kinds, 8);
	m.write(elements, element);
	m.write(element, box);
	m.write<std::uint16_t>(element + 4, 1);
	m.write<std::uint16_t>(box + 2, 1);
	m.write(box + 4, material);
	m.write(material + 0xC, collide);
	m.write<std::uint16_t>(collide + 2, 1);
	std::uint32_t t = m.u32(near + 0x24);
	m.write<std::uint32_t>(t, 4);
	m.write(t + 0x10, vec3{ 0, 1, 0 });
	m.write(t + 0x1C, vec3{ -1, 0, 0 });
	m.write(t + 0x34, vec3{ 0, 2, 0 });
	m.write(t + 0x40, vec3{ -3, 0, 0 });
	m.write(t + 0x4C, vec3{ 0, 0, 4 });
	node(script, node_type::function, 154);
	node(script + 8, node_type::actor, actor);
	node(script + 16, node_type::mask, 1);
	game::value centre;
	CHECK(vm.eval(self, script, centre) == script + 24);
	CHECK(centre.component(0) == doctest::Approx(2));
	CHECK(centre.component(1) == doctest::Approx(6));
	CHECK(centre.component(2) == doctest::Approx(12));
	m.write<std::uint16_t>(collide + 2, 2);
	vm.eval(self, script, centre);
	CHECK(centre.words == game::value::vector(0, 0, 0).words);
	m.write<std::uint16_t>(collide + 2, 1);
	m.write<std::uint32_t>(set + 0x34, 2);
	vm.eval(self, script, centre);
	CHECK(centre.words == game::value::vector(0, 0, 0).words);

	// With direct zones off, a collision-bearing part supplies a world centre, converted back
	// through the full actor matrix. Hiding that part restores the zero-offset fallback.
	std::uint32_t data = m.allocate(0x118), part_link = m.allocate(12), part = m.allocate(0x48);
	std::uint32_t object = m.allocate(8), physical = m.allocate(16), part_transform = m.allocate(0x58);
	m.write(actor, data);
	m.write(data + 0x98, part_link);
	m.write(part_link, part);
	m.write<std::uint32_t>(part, 0x10);
	m.write(part + 4, object);
	m.write(object + 4, physical);
	m.write(physical + 8, zone);
	m.write(part + 0x24, part_transform);
	m.write<std::uint32_t>(part_transform, 3);
	m.write(part_transform + 4, vec3{ 5, 2, 0 });
	for(std::uint32_t i = 0; i < 3; i++)
	{
		m.write<float>(part_transform + 0x10 + i * 16, 1);
		m.write<float>(part_transform + 0x34 + i * 16, 1);
	}
	vm.eval(self, script, centre);
	CHECK(centre.component(0) == doctest::Approx(1));
	CHECK(centre.component(1) == doctest::Approx(-1.0f / 3));
	CHECK(centre.component(2) == doctest::Approx(0.75f));
	m.write<std::uint32_t>(part + 0x30, 2);
	vm.eval(self, script, centre);
	CHECK(centre.words == game::value::vector(0, 0, 0).words);
}

TEST_CASE("real data: an off-axis shot hits Learn_10's last cage")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto game = game_data::locate(*dir);
	REQUIRE(game);
	level_files files = load_level_files(*game, "Learn_10");
	level_header header = read_level_header(files);
	scene tree(files.memory, header);
	animation_bank bank = read_animation_bank(files);
	game::live_memory m(files.memory);
	std::uint32_t rayman = header.placements.at(0).super_object.address();
	game::interpreter vm(m, rayman);
	game::register_handlers(vm);
	game::register_dialog_handlers(vm);
	game::set_dialog_resolver(vm, files);
	vm.set_world(game::world_collision(static_collision(files.memory, tree)));
	vm.set_animations(&bank);
	game::install_level_events(m, bank);
	std::vector<std::uint32_t> actors;
	for(const scene_node& n : tree.nodes())
		if(n.type == super_object_type::actor)
			actors.push_back(n.at.address());
	vm.set_actors(actors);
	CHECK(vm.set_story_bit(actors, 1133));
	actor_set set = read_actors(files.memory, tree, bank);
	for(const actor_set::entry& e : set.actors)
		game::set_global_placement(m, tree.node(e.node).at.address(), e.world);
	level_lights lights = read_lights(files.memory, tree);
	for(std::uint32_t so : actors)
		if(auto placement = game::global_placement(m, so))
		{
			vec3 p{ placement->m[0][3], placement->m[1][3], placement->m[2][3] };
			auto sector = lights.sector_at(p);
			REQUIRE(sector);
			CHECK(vm.sector_at(p) == tree.node(lights.sectors[*sector].node).at.address());
		}
	bool homing = true, away = false;
	SUBCASE("a 23 degree miss is corrected into the cage") {}
	SUBCASE("without the target search the same shot misses")
	{
		homing = false;
		vm.on_function(113, [](game::call& c, game::value& out) {
			for(int i = 0; i < 7; i++) c.args = c.vm.skip(c.args);
			out = game::value::of(game::value_type::actor, 0);
			return c.args;
		});
	}
	SUBCASE("nothing in the cone leaves a straight shot") { away = true; }
	game::input keys(m, read_input_actions(files));
	std::uint32_t cage_so = tree.node(53).at.address(), cage = vm.actor_of(cage_so);
	std::uint32_t cage_set = m.u32(cage + 0x14), cage_data = m.u32(cage);
	int made = -1, hit = -1, reaction = -1;
	std::uint32_t waiting_state = 0;
	std::size_t errors = 0;
	for(int tick = 0; tick < 370; tick++)
	{
		if(tick == 240)
		{
			float sign = away ? -1.0f : 1.0f;
			game::write_transform(m, rayman, { 24.8f, 125, 24.25f }, { sign, 0, 0, 0, sign, 0, 0, 0, 1 });
		}
		keys.set_key(0x39, tick >= 300 && tick < 305); // Space
		keys.step();
		game::step(vm, actors, [&](std::uint32_t, const std::exception& e) {
			if(errors++ == 0) MESSAGE(e.what());
		});
		// Headless test: the target is in view. The viewer's render pass clears
		// fresh-wake bit 0x800 after this update (0x45C450).
		const auto cage_standard = m.u32(cage + 4);
		m.write<std::uint32_t>(cage_standard + 0x24, m.u32(cage_standard + 0x24) & ~0x800u);
		if(tick == 308) waiting_state = m.u32(cage_data + 4);
		if(tick > 308 && hit < 0 && m.u8(cage_set + 0x65))
		{
			hit = tick;
			CHECK(m.u8(cage_set + 0x65) == 10);
			CHECK(m.f32(cage_set + 0x5C) == doctest::Approx(1));
		}
		if(tick > 308 && reaction < 0 && m.u32(cage_data + 4) != waiting_state)
			reaction = tick;
		for(const auto& slot : vm.generated().slots())
			if(slot.creator == rayman && !vm.removed(slot.super_object))
			{
				if(made < 0) made = tick;
				auto target = vm.get_variable(slot.super_object, 22);
				if(tick == 309)
					CHECK(target.word() == (homing && !away ? cage : 0));
				if(away && tick <= 330)
				{
					CHECK(target.word() == 0);
					auto velocity = vm.get_variable(slot.super_object, 13);
					CHECK(velocity.component(0) == doctest::Approx(0).epsilon(0.0001));
					CHECK(velocity.component(1) == doctest::Approx(25));
					CHECK(velocity.component(2) == doctest::Approx(0));
				}
			}
	}
	CHECK(errors == 0);
	CHECK(made == 309);
	CHECK(hit == (homing && !away ? 352 : -1));
	CHECK(reaction == (homing && !away ? 353 : -1));
}

TEST_CASE("real data: English dialog table")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto game = game_data::locate(*dir);
	REQUIRE(game.has_value());
	level_files files = load_level_files(*game, "Learn_10");
	game::live_memory memory(files.memory);
	auto resolve = game::make_dialog_resolver(memory, files);
	struct sample { int id, length, sum; };
	for(auto [id, length, sum] : { sample{0,51,3268}, sample{1,114,8442}, sample{2,85,6330},
		sample{100,4,224}, sample{665,45,3563} })
	{
		auto s = resolve(id);
		REQUIRE(s.has_value());
		int actual = 0;
		for(unsigned char c : *s) actual += c;
		CHECK(s->size() == length);
		CHECK(actual == sum);
	}
	CHECK_FALSE(resolve(666).has_value());
	// The main menu's "Site Rayman2" entry (id 0x16A) names us instead, its format codes kept.
	auto site = resolve(0x16A);
	REQUIRE(site.has_value());
	CHECK(site->ends_with("OpenRayman " OPENRAYMAN_TEST_VERSION));
	game::interpreter vm(memory, 0);
	game::register_dialog_handlers(vm);
	game::set_dialog_resolver(vm, files);
	auto id = game::value::of(game::value_type::id, 0);
	for(unsigned language = 0; language < 5; ++language)
	{
		vm.language = static_cast<std::uint8_t>(language);
		CHECK(game::resolve_dialog_value(vm, id) == game::make_dialog_resolver(memory, files, language)(0));
	}
}

TEST_CASE("dialog glyph byte mapping")
{
	CHECK_FALSE(game::dialog_glyph_index(0x1f).has_value());
	CHECK(game::dialog_glyph_index('A') == 33);
	CHECK(game::dialog_glyph_index('a') == 33);
	CHECK(game::dialog_glyph_index(0x9f) == 127);
	CHECK(game::dialog_glyph_index(0xa0) == 96);
	CHECK(game::dialog_glyph_index('A', 800) == 833);
}

TEST_CASE("dialog layout measures the whole block and reveals only its prefix")
{
	game::dialog_text text;
	text.widths.fill(1);
	text.line_height = 0.5f;
	text.prepared = "/C:/W500:/Z2000:AB/H250:/L:/O800:CDE/E9:";
	text.revealed = 2;
	auto layout = game::layout_dialog(text);
	CHECK(layout.centred);
	REQUIRE(layout.glyphs.size() == 5);
	CHECK(layout.width == doctest::Approx(3));
	CHECK(layout.glyphs[1].x == doctest::Approx(1));
	CHECK(layout.glyphs[2].x == doctest::Approx(0));
	CHECK(layout.glyphs[2].y == doctest::Approx(0.25));
	CHECK(layout.glyphs[2].index == 835);
	CHECK(layout.glyphs[0].revealed);
	CHECK(layout.glyphs[1].revealed);
	CHECK_FALSE(layout.glyphs[2].revealed);
}

TEST_CASE("screen text inline colours select a texture bank and gradient")
{
	game::dialog_text text;
	text.screen = true;
	text.tint = std::array<float, 4>{1, 1, 1, 1};
	text.widths.fill(1);
	text.prepared = "A/O200:B/O400:C/O999:D/O600:E";
	text.revealed = 5;
	auto layout = game::layout_dialog(text);
	REQUIRE(layout.glyphs.size() == 5);
	CHECK(layout.glyphs[0].screen_colour == -1);
	CHECK(layout.glyphs[1].screen_colour == 200);
	CHECK(layout.glyphs[2].screen_colour == 400);
	CHECK(layout.glyphs[3].screen_colour == 400);
	CHECK(layout.glyphs[4].screen_colour == 600);
	CHECK(layout.glyphs[1].index == *game::dialog_glyph_index('B') + 200);
	CHECK(layout.glyphs[3].index == *game::dialog_glyph_index('D') + 999);
}

TEST_CASE("dialog screen projection uses the fixed camera and reciprocal depth")
{
	// Derived independently from 0x422940 / 0x422AB0: unproject through the fixed
	// camera, then project its result. Screen text must not use the world camera's FOV.
	auto p = game::project_dialog_screen(60.9f, 850.9f, 16.9f);
	CHECK(p.x == doctest::Approx(42.8346457));
	CHECK(p.y == doctest::Approx(405.354331));
	CHECK(p.unit_x == doctest::Approx(50.3937008));
	CHECK(p.unit_y == doctest::Approx(55.8500014));
	auto centre = game::project_dialog_screen(500, 500, 32);
	CHECK(centre.x == doctest::Approx(320));
	CHECK(centre.y == doctest::Approx(240));
	CHECK(centre.unit_x > p.unit_x);
}

TEST_CASE("real data: intro counter increments request the click and keep the recorded slot layout")
{
	auto dir = real_game_dir(); if(!dir) return;
	auto data = game_data::locate(*dir); REQUIRE(data);
	auto files = load_level_files(*data, "Jail_10");
	auto header = read_level_header(files);
	game::live_memory m(files.memory);
	const auto rayman = header.placements.at(0).super_object.address();
	game::interpreter vm(m, rayman);
	game::register_handlers(vm); game::register_dialog_handlers(vm);
	game::set_dialog_resolver(vm, files);
	game::apply_sound_slots(m, *data, "Jail_10");
	const auto* templates = files.memory.find({4, 1}); REQUIRE(templates);
	const auto counter = vm.generated().create(m, templates->base + 0x680, rayman, {0,0,0}, vm.clock());
	REQUIRE(counter);
	const int values[] = {700,620,20,2,3,18900,0,0,255,0};
	for(unsigned i = 0; i < 10; ++i) vm.set_variable(counter, i, game::value::integer(values[i]));
	const auto standard = m.u32(vm.actor_of(counter) + 4);
	m.write<std::uint32_t>(standard + 0x24, m.u32(standard + 0x24) | (1u << 27));
	bool clicked = false;
	for(int tick = 0; tick < 10; ++tick)
	{
		vm.next_tick(); vm.think(counter);
		for(const auto& sound : vm.take_sounds())
		{
			CHECK(sound.event == 0x008A0000);
			CHECK(sound.source == counter);
			CHECK(sound.procedure == 115);
			clicked = true;
		}
	}
	CHECK(clicked);
	CHECK(vm.get_variable(counter, 5).as_int() > 18900);
	const auto& slot = vm.screen_text().at(26);
	CHECK(slot.used); CHECK(slot.x == 700); CHECK(slot.y == 620); CHECK(slot.size == 20);
	CHECK(slot.flags == 0); CHECK(slot.colour == 0);
}

TEST_CASE("real data: dialog template glyph objects")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto game = game_data::locate(*dir);
	REQUIRE(game.has_value());
	auto files = load_level_files(*game, "Learn_10");
	auto block = files.memory.find({4, 0});
	REQUIRE(block != nullptr);
	CHECK(game::screen_slot_font_actor(files) == block->base + 0x15ca8);
	auto data = files.memory.follow({block, 0x15ca8});
	REQUIRE(data.has_value());
	auto list = files.memory.follow(*data + 0x10);
	REQUIRE(list.has_value());
	auto objects = read_object_list(files.memory, *list);
	CHECK(objects.size() == 0x372);
	auto index = game::dialog_glyph_index('A');
	REQUIRE(index.has_value());
	REQUIRE(objects.size() > std::size_t(*index));
	REQUIRE(objects[*index].physical_object.has_value());
	auto geometry = physical_object_geometry(files.memory, *objects[*index].physical_object);
	REQUIRE(geometry.has_value());
	auto glyph = read_geometric_object(files.memory, *geometry);
	CHECK(glyph.vertices.size() == 4);
	REQUIRE(glyph.elements.size() == 1);
	CHECK(glyph.elements[0].triangles.size() == 2);
	REQUIRE(glyph.elements[0].material.has_value());
	CHECK(glyph.elements[0].material->texture.has_value());
	// Murfy's /O400 selects a painted A. Its colon entry is intentionally empty.
	REQUIRE(objects.size() > 433);
	CHECK_FALSE(objects[426].physical_object.has_value());
	REQUIRE(objects[433].physical_object.has_value());
	auto visual = files.memory.follow(*objects[433].physical_object);
	REQUIRE(visual);
	auto blocks = files.memory.follow(*visual + 0x10);
	REQUIRE(blocks);
	auto first = files.memory.follow(*blocks);
	REQUIRE(first);
	auto colours = files.memory.follow(*first + 4);
	REQUIRE(colours);
	CHECK(files.memory.read_u32(*colours) == 4);
	auto colour_data = files.memory.follow(*colours + 4);
	REQUIRE(colour_data);
	CHECK(files.memory.read_u32(*colour_data) == 0x00BB00FF);
	// The live font pointers at 0x4A727C / entries 33,233,433,633 (2026-11-07)
	// select these same objects. All four palettes share a material, so a frame
	// sample naming that material does not identify the palette being drawn.
	const saved_vertex_colour first_colours[] = {
		{125,94,201,255}, {96,5,33,255}, {255,187,60,255}, {125,94,201,255}
	};
	for(unsigned bank = 0; bank < 4; ++bank)
	{
		const auto& physical = objects[33 + bank * 200].physical_object;
		REQUIRE(physical);
		auto sets = physical_object_colour_sets(files.memory, *physical);
		REQUIRE(sets.size() == 1); REQUIRE(sets[0].size() == 1); REQUIRE(sets[0][0].size() == 4);
		CHECK(sets[0][0][0] == first_colours[bank]);
		auto at = physical_object_geometry(files.memory, *physical); REQUIRE(at);
		auto mesh = read_geometric_object(files.memory, *at);
		REQUIRE(mesh.elements.size() == 1); REQUIRE(mesh.elements[0].material);
		const auto& material = *mesh.elements[0].material;
		CHECK(material.at == glyph.elements[0].material->at);
		CHECK((material.ambient == rgba{0,0,0,1}));
		CHECK((material.diffuse == rgba{1,1,1,1}));
	}
}

// A level made only of what the input evaluator needs: one block 0x06/00 with 138 actions (all
// disabled but the ones `actions` names) and a pool of expressions the actions point at. Each
// expression is a list of 8-byte slots, the first byte a kind and the rest its operands.
struct input_fixture
{
	static constexpr std::uint32_t base = 0x1000;
	static constexpr std::uint32_t count = 138;
	static constexpr std::uint32_t expressions = base + count * input_action_size;
	static constexpr std::uint32_t pool_size = 0x400;

	std::array<std::vector<std::uint32_t>, count> nodes; // per action: its slots, 2 words each

	// Slot helper: the kind's low byte in the first word, as the files have it.
	static std::uint32_t kind(std::uint8_t k) { return 0xF000u | k; }

	// A pad button test (kinds 12..15, 26): slots kind, pad, button; the button slot's bytes +5/+6
	// hold the tick range (node +0x15/+0x16, 0x42EFE0).
	static std::vector<std::uint32_t> pad(std::uint8_t k, std::uint32_t pad_number, std::uint32_t button, std::uint8_t lo = 0, std::uint8_t hi = 0)
	{
		return { kind(k), 0, pad_number, 0, button, std::uint32_t(lo) << 8 | std::uint32_t(hi) << 16 };
	}

	// A pad axis test (kind 16): slots kind, pad, axis, min, max.
	static std::vector<std::uint32_t> axis(std::uint32_t pad_number, std::uint32_t axis_number, std::int32_t min, std::int32_t max)
	{
		return { kind(16), 0, pad_number, 0, axis_number, 0, std::uint32_t(min), 0, std::uint32_t(max), 0 };
	}

	// Points action `index` at the expression given as 32-bit slot words.
	input_fixture& set(std::size_t index, std::vector<std::uint32_t> words)
	{
		nodes[index] = std::move(words);
		return *this;
	}

	memory_image build() const
	{
		// The block: 138 actions, then each enabled action's expression one after the other.
		std::vector<std::uint8_t> block(count * input_action_size + pool_size, 0);
		auto put = [&](std::uint32_t address, std::uint32_t value) {
			std::uint32_t at = address - base;
			for(int i = 0; i < 4; i++)
				block[at + i] = std::uint8_t(value >> (8 * i) & 0xFF);
		};
		std::vector<std::pair<std::uint32_t, block_id>> pointers;
		std::uint32_t at = expressions;
		for(std::size_t i = 0; i < count; i++)
		{
			if(nodes[i].empty())
				continue;
			std::uint32_t action = base + std::uint32_t(i) * input_action_size;
			put(action + input_action::slots, std::uint32_t(nodes[i].size() / 2));
			put(action + input_action::expression, at);
			put(action + input_action::counter, 0xFFFFFFFE); // -2, as the files have it
			block[action - base + input_action::enabled] = 1;
			for(std::uint32_t word : nodes[i])
			{
				put(at, word);
				at += 4;
			}
			pointers.push_back({ action + input_action::expression, { 0x06, 0 } });
		}
		bytes data;
		for(std::uint8_t byte : block)
			data.u8(byte);
		sna_builder sna;
		sna.block(0x06, 0, base, base + std::uint32_t(block.size()), data);
		bytes table;
		table.u8(1).u32(0);
		table_block(table, 0x06, 0, pointers);
		memory_image image;
		image.add(sna_file(sna.build()), parse_relocation_table(table.data));
		return image;
	}
};

TEST_CASE("input: joystick buttons and axes feed the actions")
{
	input_fixture fixture;
	// 10: held button 11 (kind 26), 11: button 12 went down, 12: button 13 went up,
	// 13: button 14 down for at least 3 frames, 14: button 15 up, 15: an axis in -110..110.
	fixture.set(10, input_fixture::pad(26, 0, 11, 0, 0xFF));
	fixture.set(11, input_fixture::pad(12, 0, 12));
	fixture.set(12, input_fixture::pad(13, 0, 13));
	fixture.set(13, input_fixture::pad(14, 0, 14, 3, 0xFF));
	fixture.set(14, input_fixture::pad(15, 0, 15, 0, 0xFF));
	fixture.set(15, input_fixture::axis(0, 4, -110, 110));
	memory_image image = fixture.build();
	game::live_memory memory(image);
	input_actions actions{ { image.find({ 0x06, 0 }), 0 }, input_fixture::count };
	game::input pad(memory, actions);
	auto counter = [&](std::size_t index) { return memory.read<std::int32_t>(pad.action(index) + input_action::counter); };
	auto analog = [&](std::size_t index) { return memory.read<float>(pad.action(index) + input_action::analog); };

	// Everything starts up and its counter counts down.
	pad.step();
	CHECK(counter(10) == -3);
	CHECK_FALSE(pad.pad_button(0, 11));

	// Kind 26 is the held test: button 11 down keeps action 10 down.
	pad.set_pad_button(0, 11, true);
	pad.step();
	CHECK(counter(10) == 1);
	CHECK(pad.pad_button(0, 11));
	pad.step();
	CHECK(counter(10) == 2);
	pad.set_pad_button(0, 11, false);
	pad.step();
	CHECK(counter(10) == -1);

	// Kind 12 is the press edge only: button 12 held gives a counter that stops at 1.
	pad.set_pad_button(0, 12, true);
	pad.step();
	CHECK(counter(11) == 1);
	pad.step();
	CHECK(counter(11) == -1);

	// Kind 13 is the release edge: nothing while it is held, then the release frame.
	pad.set_pad_button(0, 13, true);
	pad.step();
	CHECK(counter(12) < 0);
	pad.set_pad_button(0, 13, false);
	pad.step();
	CHECK(counter(12) == 1);

	// Kind 14 is down for between the operand's frames: 3..255 here (the press tick counts 1), so
	// the third tick qualifies.
	pad.set_pad_button(0, 14, true);
	pad.step();
	pad.step();
	CHECK(counter(13) < 0);
	pad.step();
	CHECK(counter(13) == 1);
	pad.set_pad_button(0, 14, false);
	pad.step();
	CHECK(counter(13) == -1);

	// Kind 15 is up for 0..255 frames: button 15 has been up all along, so action 14 is held.
	pad.step();
	CHECK(counter(14) > 1);
	pad.set_pad_button(0, 15, true);
	pad.step();
	CHECK(counter(14) == -1);
	pad.set_pad_button(0, 15, false);

	// Kind 16 reads the axis over -32768..32767 as -100..100 and keeps it as the analog value.
	pad.set_pad_axis(0, 4, 0);
	pad.step();
	CHECK(counter(15) > 0); // 0 is inside -110..110, as it has been since the start
	CHECK(analog(15) == doctest::Approx(0.0f));
	pad.set_pad_axis(0, 4, 16384);
	pad.step();
	CHECK(analog(15) == doctest::Approx(50.0f));
	pad.set_pad_axis(0, 4, -32768);
	pad.step();
	CHECK(analog(15) == doctest::Approx(-100.0f));
	// A full push the other way reads 99.997 (32767 / 32768), still inside the data's -110..110.
	pad.set_pad_axis(0, 4, 32767);
	pad.step();
	CHECK(counter(15) > 0);
	CHECK(analog(15) == doctest::Approx(100.0f).epsilon(0.001));

	// The other pad numbers are separate, and release_pad clears one of them.
	pad.set_pad_button(1, 11, true);
	pad.step();
	CHECK(pad.pad_button(1, 11));
	CHECK(counter(10) < 0); // pad 0's button is still up, so the action stays up
	pad.release_all();
	pad.step();
	CHECK_FALSE(pad.pad_button(1, 11));
}

TEST_CASE("input: a pad axis test outside its range leaves the analog value alone")
{
	input_fixture fixture;
	// Action 0: axis 6 in -110..-50 (the game's action 12 axis), action 1: the same axis in 1..1.
	fixture.set(0, input_fixture::axis(0, 6, -110, -50));
	fixture.set(1, input_fixture::axis(0, 6, 1, 1));
	memory_image image = fixture.build();
	game::live_memory memory(image);
	input_actions actions{ { image.find({ 0x06, 0 }), 0 }, input_fixture::count };
	game::input pad(memory, actions);
	auto analog = [&](std::size_t index) { return memory.read<float>(pad.action(index) + input_action::analog); };

	pad.set_pad_axis(0, 6, -32768); // -100: inside
	pad.step();
	CHECK(analog(0) == doctest::Approx(-100.0f));
	CHECK(analog(1) == doctest::Approx(0.0f));
	CHECK(memory.read<std::int32_t>(pad.action(1) + input_action::counter) < 0);

	pad.set_pad_axis(0, 6, 0); // 0: outside
	pad.step();
	CHECK(analog(0) == doctest::Approx(0.0f));
	CHECK(memory.read<std::int32_t>(pad.action(0) + input_action::counter) < 0);
}

TEST_CASE("real data: input actions follow the keys")
{
	auto dir = real_game_dir();
	if(!dir)
	{
		MESSAGE("OPENRAYMAN_GAME_DIR not set, skipping");
		return;
	}
	auto game = game_data::locate(*dir);
	REQUIRE(game.has_value());
	level_files files = load_level_files(*game, "Learn_10");
	input_actions actions = read_input_actions(files);
	CHECK(actions.count == 138);
	auto describe = [&](std::uint32_t i) {
		return describe_input_action(files.memory, actions, { actions.first.block, actions.first.offset + i * input_action_size });
	};
	// The game's keyboard controls: arrows move, A jumps, Space shoots, LShift walks, Ctrl puts
	// the camera back, Q and W turn it.
	CHECK(describe(2) == "key Up");
	CHECK(describe(5) == "key Left");
	CHECK(describe(6) == "any(key LShift, key Num7)");
	CHECK(describe(10) == "any(any(key A, key Num8), pad 0 button 11 held)");
	CHECK(describe(11) == "any(any(key Space, key Num1), pad 0 button 10 held)");
	CHECK(describe(13) == "any(any(key LCtrl, key RCtrl), held action 12)");
	CHECK(describe(16) == "any(any(key W, key PgDn), pad 0 button 13 held)");

	// The counter's timeline (0x42E710): 1 on the press tick, then up; -1 on release, then down.
	game::live_memory memory(files.memory);
	game::input keys(memory, actions);
	std::uint32_t jump = keys.action(10);
	auto counter = [&] { return memory.read<std::int32_t>(jump + input_action::counter); };
	CHECK(counter() == -2);
	keys.step();
	CHECK(counter() == -3);
	keys.set_key(0x1E, true); // A
	keys.step();
	CHECK(counter() == 1);
	keys.step();
	CHECK(counter() == 2);
	keys.set_key(0x1E, false);
	keys.step();
	CHECK(counter() == -1);
	keys.step();
	CHECK(counter() == -2);
	// Ctrl (action 13) through "held action 12" stays off with no joystick.
	keys.set_key(0x1D, true);
	keys.step();
	CHECK(memory.read<std::int32_t>(keys.action(13) + input_action::counter) == 1);
}

TEST_CASE("real data: Learn_10 portal camera lands with a clear view")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto data = game_data::locate(*dir);
	REQUIRE(data);
	level_files files = load_level_files(*data, "Learn_10");
	level_header header = read_level_header(files);
	scene tree(files.memory, header);
	animation_bank bank = read_animation_bank(files);
	game::live_memory m(files.memory);
	std::uint32_t rayman = header.placements.at(0).super_object.address();
	game::interpreter vm(m, rayman);
	game::register_handlers(vm);
	game::register_dialog_handlers(vm);
	game::set_dialog_resolver(vm, files);
	vm.set_world(game::world_collision(static_collision(files.memory, tree)));
	vm.set_animations(&bank);
	game::install_level_events(m, bank);
	std::vector<std::uint32_t> actors;
	for(const scene_node& n : tree.nodes())
		if(n.type == super_object_type::actor) actors.push_back(n.at.address());
	vm.set_actors(actors);
	for(const actor_set::entry& e : read_actors(files.memory, tree, bank).actors)
	{
		auto so = tree.node(e.node).at.address();
		auto placed = game::global_placement(m, so);
		if(!placed) continue;
		float difference = 0;
		for(int r = 0; r < 3; r++)
			for(int c = 0; c < 4; c++)
				difference = std::max(difference, std::abs(placed->m[r][c] - e.world.m[r][c]));
		if(difference > 1e-3f) game::set_global_placement(m, so, e.world);
	}
	for(std::uint32_t bit : game::revisit_story_bits) REQUIRE(vm.set_story_bit(actors, bit));
	game::input keys(m, read_input_actions(files));
	bool saw_startup_cover = false;
	for(int tick = 0; tick < 120; tick++)
	{
		keys.step();
		game::step(vm, actors);
		if(tick < 3)
			for(const auto& rectangle : vm.particles().screen_rectangles())
				saw_startup_cover |= rectangle.first[0] <= 0 && rectangle.first[1] <= 0 &&
					rectangle.second[0] >= 100 && rectangle.second[1] >= 100 && rectangle.alpha > 200;
		if(tick >= 3)
			for(const auto& rectangle : vm.particles().screen_rectangles())
			{
				bool covers_screen = rectangle.first[0] <= 0 && rectangle.first[1] <= 0 &&
					rectangle.second[0] >= 100 && rectangle.second[1] >= 100;
				INFO("tick ", tick, " full-screen alpha ", rectangle.alpha);
				CHECK_FALSE((covers_screen && rectangle.alpha > 32));
			}
		if(tick == 1 || tick == 119)
		{
			auto view = game::global_placement(m, vm.camera_motion().super_object);
			REQUIRE(view);
			std::array<float, 3> eye{ view->m[0][3], view->m[1][3], view->m[2][3] };
			auto aim = vm.camera_motion().look_at;
			float distance = 0;
			for(int i = 0; i < 3; i++) distance += (eye[i] - aim[i]) * (eye[i] - aim[i]);
			INFO("tick ", tick, " eye ", eye[0], ",", eye[1], ",", eye[2], " aim ", aim[0], ",", aim[1], ",", aim[2]);
			CHECK(distance < 144.0f);
			CHECK_FALSE(vm.world().first_hit(aim, eye).has_value());
		}
	}
	CHECK(saw_startup_cover);

	// Exercise the executable's swing decisions on the real camera object and block. Keep the
	// geometry empty so this isolates the settle-distance check and the report gate.
	vm.set_world(game::world_collision{});
	auto camera_so = vm.camera_motion().super_object;
	auto camera_actor = m.u32(camera_so + 4);
	auto dynamics = m.u32(camera_actor + 8);
	auto base = m.u32(dynamics);
	auto report = m.u32(base + 0x150);
	REQUIRE(m.contains(report + 0x168, 1));
	m.write<std::uint8_t>(report + 0x168, 0); // isolate swing geometry from the startup collision

	auto camera_object = m.u32(camera_actor + 0x10);
	auto block = m.u32(camera_object + 4);
	m.write<std::uint8_t>(camera_object + 0x74, 0);
	affine target_pose{}, camera_pose{};
	for(int i = 0; i < 3; ++i) { target_pose.m[i][i] = 1; camera_pose.m[i][i] = 1; }
	camera_pose.m[1][3] = 8;
	game::set_global_placement(m, rayman, target_pose);
	game::set_global_placement(m, camera_so, camera_pose);
	for(unsigned at = 0; at < 0x18; at += 4) m.write<float>(block + at, 0);
	m.write<float>(block + 0x18, 8); m.write<float>(block + 0x1C, 8);
	m.write<float>(block + 0x20, 8);
	m.write<float>(block + 0x28, 3.14159265f); m.write<float>(block + 0x2C, 0);
	m.write<float>(block + 0x30, 0); m.write<float>(block + 0x34, 0);
	m.write<float>(block + 0x60, -100); m.write<float>(block + 0x64, 100);
	m.write<std::uint32_t>(block + 0x68, rayman);
	m.write<std::uint16_t>(block + 0x72, 2);
	auto& camera = vm.camera_motion();
	camera.tracked_target = rayman;
	camera.swing = 0;
	game::solve_camera(vm, camera_so);
	CHECK(camera.trace.candidates == 2);
	CHECK(camera.trace.rejected[0] == 1);
	CHECK(camera.trace.rejected[1] == 1);
	CHECK(camera.swing == -2);
	// The blocked latch persists until the window gap is small.
	m.write<float>(block + 0x20, 3);
	game::solve_camera(vm, camera_so);
	CHECK(camera.trace.candidates == 0);
	CHECK(camera.swing == -2);
	m.write<float>(block + 0x28, 0);
	game::solve_camera(vm, camera_so);
	CHECK(camera.swing == 0);
	m.write<float>(block + 0x28, 3.14159265f);
	game::solve_camera(vm, camera_so);
	CHECK(camera.trace.candidates == 1);
	CHECK(camera.trace.rejected[0] == 0);
	CHECK(camera.swing != 0);

	// A collision report blocks a new swing unless global option bit 0 allows it.
	m.write<std::uint8_t>(report + 0x168, 1);
	camera.swing = 0;
	game::solve_camera(vm, camera_so);
	CHECK(camera.swing == -2);
	CHECK(camera.trace.candidates == 0);
	camera.swing = 0;
	vm.camera().values[0xB0] |= 1;
	game::solve_camera(vm, camera_so);
	CHECK(camera.trace.candidates == 1);
	CHECK(camera.swing != -2);

	// Procedure 497's fixed reference, used for Q/W in open space, still permits a swing.
	m.write<std::uint8_t>(report + 0x168, 0);
	m.write<std::uint16_t>(block + 0x72, 0x4002);
	m.write<float>(block + 0x28, 0);
	for(unsigned i = 0; i < 3; ++i) {
		m.write<float>(camera_object + 0x40 + i * 4, i == 0 ? 1.0f : 0.0f);
		m.write<float>(camera_object + 0x4C + i * 4, i == 2 ? 1.0f : 0.0f);
	}
	camera.swing = 0;
	game::solve_camera(vm, camera_so);
	CHECK(camera.trace.candidates == 1);
	CHECK(camera.trace.rejected[0] == 0);
	CHECK(camera.swing != 0);

	// A fixed camera destination skips the mechanics orbit (0x474420), even when the
	// script did not request that option itself. The first movement must be straight.
	game::set_global_placement(m, camera_so, camera_pose);
	m.write<std::uint16_t>(block + 0x70, 0x40);
	m.write<std::uint16_t>(block + 0x72, 0x2082);
	m.write<float>(camera_object + 0x34, 10);
	m.write<float>(camera_object + 0x38, 8);
	m.write<float>(camera_object + 0x3C, 0);
	m.write<float>(block + 0x38, 6);
	m.write<float>(block + 0x3C, 1);
	m.write<float>(block + 0x40, 0);
	camera.previous_move = {};
	camera.valid = true;
	game::solve_camera(vm, camera_so);
	CHECK((camera.flags & 4) != 0);
	game::move_camera(m, camera_so, camera, 100, vm.world());
	auto straight = game::global_placement(m, camera_so);
	REQUIRE(straight);
	CHECK(straight->m[0][3] == doctest::Approx(0.6f));
	CHECK(straight->m[1][3] == doctest::Approx(8.0f));
	CHECK(straight->m[2][3] == doctest::Approx(0.0f));
}

TEST_CASE("real data: Bast_20 director cuts back to Rayman at the scene handoff")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto data = game_data::locate(*dir); REQUIRE(data);
	auto files = load_level_files(*data, "Bast_20");
	auto header = read_level_header(files);
	scene tree(files.memory, header);
	auto bank = read_animation_bank(files);
	game::live_memory m(files.memory);
	auto rayman = header.placements.at(0).super_object.address();
	game::interpreter vm(m, rayman);
	game::register_handlers(vm);
	game::register_dialog_handlers(vm);
	game::set_dialog_resolver(vm, files);
	vm.set_world(game::world_collision(static_collision(files.memory, tree)));
	vm.set_animations(&bank);
	game::install_level_events(m, bank);
	std::vector<std::uint32_t> actors;
	for(const auto& n : tree.nodes())
		if(n.type == super_object_type::actor) actors.push_back(n.at.address());
	vm.set_actors(actors);
	for(const auto& e : read_actors(files.memory, tree, bank).actors)
		game::set_global_placement(m, tree.node(e.node).at.address(), e.world);
	for(auto bit : game::first_visit_story_bits) REQUIRE(vm.set_story_bit(actors, bit));
	game::input keys(m, read_input_actions(files));
	auto camera_so = tree.node(3).at.address();
	auto cam = m.u32(vm.actor_of(camera_so) + 0x10);
	int handoff = -1, jump = -1, transition_frames = 0;
	vec3 previous{};
	auto distance = [](const vec3& a, const vec3& b) {
		return std::sqrt((a[0]-b[0])*(a[0]-b[0]) + (a[1]-b[1])*(a[1]-b[1]) + (a[2]-b[2])*(a[2]-b[2]));
	};
	for(int tick = 0; tick < 610; ++tick)
	{
		keys.step();
		game::step(vm, actors);
		auto pose = *game::global_placement(m, camera_so);
		vec3 eye{pose.m[0][3], pose.m[1][3], pose.m[2][3]};
		// The director clears custom bit 30 as it releases the fixed shot.
		auto standard = m.u32(vm.actor_of(camera_so) + 4);
		if(tick > 500 && handoff < 0 && !(m.u32(standard + 0x24) & 0x20000000)) handoff = tick;
		if(handoff >= 0 && distance(eye, previous) > 35)
		{
			jump = tick;
			auto hero = *game::global_placement(m, rayman);
			vec3 at{hero.m[0][3], hero.m[1][3], hero.m[2][3]};
			CHECK(distance(eye, at) < 10);
			CHECK(distance(vm.camera_motion().aim, at) < 2);
			CHECK(m.u32(cam + 0x60) == 1);
			CHECK(m.u8(cam + 0x17C) == 0);
		}
		if(handoff >= 0 && m.u32(cam + 0x60) == 1) ++transition_frames;
		previous = eye;
	}
	REQUIRE(handoff >= 0);
	REQUIRE(jump >= 0);
	CHECK(jump - handoff <= 2);
	CHECK(transition_frames == 1);
	CHECK(m.u32(cam + 0x60) == 0);
	// rec_bast20cam1 has a 40-unit jump, 8.4 units from Rayman, and
	// exactly one sampled mode-1 frame. Candidate azimuth remains approximate.

	// The fight-region controller raises the aim by 2.5 units and uses a
	// 12-unit boom. Its ordinary floor fitting must also run: without it
	// this view settles about 1.7 units too high (rec_bast20cam2 endpoint).
	for(int tick = 610; tick < 1000; ++tick)
	{
		if(tick == 700)
		{
			game::write_transform(m, rayman, {32.824f,70.048f,-58.963f},
				{0.5f,0.8660254f,0,-0.8660254f,0.5f,0,0,0,1});
			vm.camera_motion().tracked_target = 0;
		}
		keys.step();
		game::step(vm, actors);
	}
	auto settled = *game::global_placement(m, camera_so);
	vec3 eye{settled.m[0][3], settled.m[1][3], settled.m[2][3]};
	CHECK(distance(eye, {23.157f,76.820f,-59.707f}) < 0.2f);

	// 0x473660 uses the valid ground slot (1), without remembering a floor
	// normal for airborne frames. Slot 0 can describe a different first contact.
	auto block = m.u32(cam + 4);
	auto report = m.u32(m.u32(m.u32(vm.actor_of(rayman) + 8)) + 0x150);
	REQUIRE((m.u32(report + 4) & 1) != 0);
	vm.set_world(game::world_collision{});
	m.write<std::uint8_t>(cam + 0x74, 0);
	m.write<std::uint8_t>(cam + 0x75, 0);
	m.write<std::uint16_t>(block + 0x72, 0x4002 | 4 | 0x40);
	game::solve_camera(vm, camera_so);
	auto grounded_wanted = vm.camera_motion().trace.wanted;
	for(unsigned k = 0; k < 3; ++k) m.write<float>(report + 0x0C + 4*k, k == 2 ? 1.0f : 0.0f);
	game::solve_camera(vm, camera_so);
	CHECK(distance(grounded_wanted, vm.camera_motion().trace.wanted) < 0.001f);
	m.write<std::uint32_t>(report + 4, 0);
	game::solve_camera(vm, camera_so);
	auto airborne_wanted = vm.camera_motion().trace.wanted;
	CHECK(distance(grounded_wanted, airborne_wanted) > 1);
	m.write<std::uint32_t>(report + 4, 1);
	m.write<std::uint16_t>(block + 0x72, 0x4002 | 4 | 0x40 | 0x20);
	game::solve_camera(vm, camera_so);
	CHECK(distance(airborne_wanted, vm.camera_motion().trace.wanted) < 0.001f);
}

TEST_CASE("real data: camera fallback relocates across an unreachable wall and restores follow")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto data = game_data::locate(*dir);
	REQUIRE(data);
	auto files = load_level_files(*data, "Learn_10");
	auto header = read_level_header(files);
	scene tree(files.memory, header);
	game::live_memory m(files.memory);
	auto rayman = header.placements.at(0).super_object.address();
	auto camera_so = tree.node(5).at.address();
	game::interpreter vm(m, rayman);
	auto actor = m.u32(camera_so + 4), cam = m.u32(actor + 0x10);
	REQUIRE(cam);
	auto block = m.u32(cam + 4);
	std::memcpy(m.span(block, 0x7C).data(), m.span(m.u32(cam), 0x7C).data(), 0x7C);
	for(unsigned at = 0; at < 0x18; at += 4) m.write<float>(block + at, 0);
	m.write<float>(block + 0x18, 4); m.write<float>(block + 0x1C, 4);
	m.write<float>(block + 0x20, 1);
	for(unsigned at : { 0x28u, 0x2Cu, 0x30u, 0x34u }) m.write<float>(block + at, 0);
	m.write<float>(block + 0x60, -100); m.write<float>(block + 0x64, 100);
	m.write<std::uint32_t>(block + 0x68, rayman);
	m.write<std::uint16_t>(block + 0x70, 4);
	m.write<std::uint16_t>(block + 0x72, 2 | 4 | 0x40);
	m.write<std::uint8_t>(cam + 0x74, 0); m.write<std::uint8_t>(cam + 0x75, 0);
	m.write<int>(cam + 0x60, 0);
	m.write<float>(cam + 0x68, 0); m.write<float>(cam + 0x6C, 0);
	affine target{}, eye{};
	target.m[0][3] = 2; eye.m[0][3] = -2;
	game::set_global_placement(m, rayman, target);
	game::set_global_placement(m, camera_so, eye);
	collision_triangle lower, upper;
	lower.corners = { vec3{ 0, -2, -4 }, vec3{ 0, 2, -4 }, vec3{ 0, 2, 4 } };
	upper.corners = { vec3{ 0, -2, -4 }, vec3{ 0, 2, 4 }, vec3{ 0, -2, 4 } };
	lower.normal = upper.normal = { 1, 0, 0 };
	vm.set_world(game::world_collision({ lower, upper }));
	auto& state = vm.camera_motion();
	state.valid = true;
	state.look_at = { 2, 0, 0 };
	state.previous_move = { 0, 0.01f, 0 }; // do not suppress static tests as stationary/parallel
	for(int tick = 0; tick < 3; ++tick) {
		game::solve_camera(vm, camera_so);
		CHECK(m.u32(cam + 0x60) == 0); // the four-frame route timer has not expired
	}
	game::solve_camera(vm, camera_so);
	CHECK(m.u32(cam + 0x60) == 1);
	CHECK((m.u8(cam + 0x75) & 2) != 0);
	CHECK(state.trace.relocated);
	CHECK(state.trace.search_mask == 5);
	CHECK(state.trace.search_candidates == 40); // eight yaws, four pitches and a half-distance sample
	auto landed = *game::global_placement(m, camera_so);
	vec3 p{ landed.m[0][3], landed.m[1][3], landed.m[2][3] };
	CHECK_FALSE(vm.world().first_hit({ 2, 0, 0 }, p));
	CHECK(vm.world().slide_sphere({ -2, 0, 0 }, p, 0.35f) != p);
	// The ordinary speed law is retained; there is no invented transition speed.
	CHECK(state.speed <= 10);
	// 0x485810's moving-target arrival gate restores the spare follow block.
	for(unsigned i = 0; i < 8; ++i) {
		m.write<float>(cam + 0x78 + i * 12, 1);
		m.write<float>(cam + 0x138 + i * 4, 0.03f);
	}
	vm.camera().values[0xB0] = 0x10;
	game::solve_camera(vm, camera_so);
	CHECK(m.u32(cam + 0x60) == 0);
	CHECK((m.u8(cam + 0x75) & 2) == 0);
	CHECK(m.f32(block + 0x28) == 0);
}

TEST_CASE("real data: Learn_10 cave follow does not circle at the first clearance waypoint")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto data = game_data::locate(*dir);
	REQUIRE(data);
	level_files files = load_level_files(*data, "Learn_10");
	level_header header = read_level_header(files);
	scene tree(files.memory, header);
	animation_bank bank = read_animation_bank(files);
	game::live_memory memory(files.memory);
	const auto rayman = header.placements.at(0).super_object.address();
	game::interpreter vm(memory, rayman);
	game::register_handlers(vm);
	game::register_dialog_handlers(vm);
	game::set_dialog_resolver(vm, files);
	vm.set_world(game::world_collision(static_collision(files.memory, tree)));
	vm.set_animations(&bank);
	game::install_level_events(memory, bank);
	game::apply_sound_slots(memory, *data, "Learn_10");
	std::vector<std::uint32_t> actors, fix;
	for(const auto& placed : header.placements) fix.push_back(placed.super_object.address());
	for(const auto& node : tree.nodes())
		if(node.type == super_object_type::actor) actors.push_back(node.at.address());
	vm.set_actors(actors);
	for(auto bit : game::revisit_story_bits) vm.set_story_bit(fix, bit);
	// With the silver shot too (1095), node 4 removes the kids' director: this replay is about the
	// camera in their cave, without the scene (which replays when only Learn_10 is cleared).
	vm.set_story_bit(fix, 1095);
	for(const auto& entry : read_actors(files.memory, tree, bank).actors)
	{
		auto so = tree.node(entry.node).at.address();
		auto placed = game::global_placement(memory, so);
		if(!placed) continue;
		float difference = 0;
		for(int r = 0; r < 3; ++r)
			for(int c = 0; c < 4; ++c)
				difference = std::max(difference, std::abs(placed->m[r][c] - entry.world.m[r][c]));
		if(difference > 1e-3f) game::set_global_placement(memory, so, entry.world);
	}
	game::apply_revisit_state(vm, tree, "Learn_10");
	game::input keys(memory, read_input_actions(files));
	for(int tick = 0; tick <= 3300; ++tick)
	{
		if(tick == 300 || tick == 3100)
		{
			auto transform = memory.u32(rayman + 0x24);
			const std::array<float, 3> pos = tick == 300 ? vec3{ -62, 204, 0.5f } : vec3{ -65, 204, 0 };
			const float angle = tick == 300 ? 0.0f : 90.0f * 0.01745329f;
			float fx = std::cos(angle), fy = std::sin(angle);
			const float axes[] = { -fy, fx, 0, -fx, -fy, 0, 0, 0, 1 };
			for(unsigned i = 0; i < 3; ++i) memory.write<float>(transform + 4 + i * 4, pos[i]);
			for(unsigned i = 0; i < 9; ++i) {
				memory.write<float>(transform + 0x10 + i * 4, axes[i]);
				memory.write<float>(transform + 0x34 + i * 4, axes[i]);
			}
		}
		keys.set_key(0x39, tick >= 400 && tick < 3004 && (tick - 400) % 60 < 3);
		keys.set_key(0xD0, tick >= 3200);
		keys.step();
		game::step(vm, actors);
	}
	auto position = game::global_placement(memory, rayman);
	REQUIRE(position);
	// Before the obstruction gate, he was circling at y=193.2 under a fixed camera.
	CHECK(position->m[1][3] < 191.0f);
	CHECK_FALSE(vm.camera_motion().trace.focus);
}

TEST_CASE("real data: completed Learn_10 revisit keeps its actors and counters")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto data = game_data::locate(*dir);
	REQUIRE(data);
	level_files files = load_level_files(*data, "Learn_10");
	level_header header = read_level_header(files);
	scene tree(files.memory, header);
	animation_bank bank = read_animation_bank(files);
	game::live_memory memory(files.memory);
	const auto rayman = header.placements.at(0).super_object.address();
	game::interpreter vm(memory, rayman);
	game::register_handlers(vm);
	game::register_dialog_handlers(vm);
	game::set_dialog_resolver(vm, files);
	vm.set_world(game::world_collision(static_collision(files.memory, tree)));
	vm.set_animations(&bank);
	std::vector<std::uint32_t> actors;
	for(const scene_node& node : tree.nodes())
		if(node.type == super_object_type::actor) actors.push_back(node.at.address());
	vm.set_actors(actors);
	for(const auto& e : read_actors(files.memory, tree, bank).actors)
		game::set_global_placement(memory, tree.node(e.node).at.address(), e.world);
	for(auto bit : game::revisit_story_bits) REQUIRE(vm.set_story_bit(actors, bit));
	// The five placed Lums, the end cage and Teensies. Not the kids' director (node 3): the level's
	// own rule (node 4, story bits 1133 and 1095) takes it out, so with only Learn_10 cleared the
	// kids' scene replays, as in the game (card 78).
	constexpr std::size_t absent[] = { 11, 12, 13, 36, 53, 56, 57, 58, 59, 60, 61, 70, 71 };
	for(auto node : absent) CHECK_FALSE(vm.removed(tree.node(node).at.address()));
	bool silver_shot = false;
	SUBCASE("only Learn_10 cleared: the kids' director wakes when Rayman walks up") {}
	SUBCASE("with the silver shot, node 4 removes the kids' director") { silver_shot = true; }
	if(silver_shot) REQUIRE(vm.set_story_bit(actors, 1095));
	const auto director = tree.node(3).at.address();
	game::apply_revisit_state(vm, tree, "Learn_10");
	CHECK_FALSE(vm.removed(director));
	game::input keys(memory, read_input_actions(files));
	std::size_t errors = 0;
	for(int tick = 0; tick < 390; ++tick)
	{
		if(tick == 240) game::write_transform(memory, rayman, { -61.53f, 204.99f, 0.16f }, { 0, 1, 0, -1, 0, 0, 0, 0, 1 });
		if(tick == 330) game::write_transform(memory, rayman, { 32.605f, 106.09f, 33.868f }, { 0, 1, 0, -1, 0, 0, 0, 0, 1 });
		keys.step();
		game::step(vm, actors, [&](std::uint32_t, const std::exception&) { ++errors; });
		CHECK(vm.removed(director) == silver_shot);
		// Asleep until Rayman is put on the kids' spot (tick 240); there it runs its scene.
		if(tick < 240 || silver_shot) CHECK_FALSE(vm.active(director));
		if(tick == 329) CHECK(vm.active(director) == !silver_shot);
		if(tick == 270)
		{
			auto at = game::global_placement(memory, rayman);
			REQUIRE(at);
			CHECK(at->m[0][3] == doctest::Approx(-61.53f).epsilon(0.001));
			CHECK(at->m[1][3] == doctest::Approx(204.99f).epsilon(0.001));
		}
	}
	for(auto node : absent)
	{
		const auto so = tree.node(node).at.address();
		CHECK(vm.removed(so));
		const auto standard = memory.u32(vm.actor_of(so) + 4);
		CHECK((memory.u8(standard + 0x29) & 1) == 1);
	}
	CHECK(errors == 0);
	const auto globals = tree.node(6).at.address();
	CHECK(vm.get_variable(globals, 4).as_int() == 2);
	CHECK(vm.get_variable(globals, 5).as_int() == 2);
	CHECK(vm.get_variable(globals, 44).as_int() == 5);
	CHECK(vm.get_variable(globals, 43).as_int() == 0);
}

TEST_CASE("real data: Rayman stands on Learn_10's closed tutorial trapdoor")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto game = game_data::locate(*dir);
	REQUIRE(game);
	level_files files = load_level_files(*game, "Learn_10");
	level_header header = read_level_header(files);
	scene tree(files.memory, header);
	animation_bank bank = read_animation_bank(files);
	game::live_memory m(files.memory);
	std::uint32_t rayman = header.placements.at(0).super_object.address();
	game::interpreter vm(m, rayman);
	game::register_handlers(vm);
	game::register_dialog_handlers(vm);
	game::set_dialog_resolver(vm, files);
	vm.set_world(game::world_collision(static_collision(files.memory, tree)));
	vm.set_animations(&bank);
	game::install_level_events(m, bank);
	std::vector<std::uint32_t> actors;
	for(const scene_node& n : tree.nodes())
		if(n.type == super_object_type::actor) actors.push_back(n.at.address());
	vm.set_actors(actors);
	for(const actor_set::entry& e : read_actors(files.memory, tree, bank).actors)
		game::set_global_placement(m, tree.node(e.node).at.address(), e.world);
	bool revisit = false, collect = false, camera_drop = false;
	SUBCASE("closed trapdoor matches the recording") {}
	SUBCASE("revisit has an open trapdoor") { revisit = true; }
	SUBCASE("the tutorial releases the door and the Lum opens it") { collect = true; }
	SUBCASE("camera recovers below the sliding door") { collect = camera_drop = true; }
	for(std::uint32_t bit : revisit ? std::span<const std::uint32_t>(game::revisit_story_bits) : std::span<const std::uint32_t>(game::first_visit_story_bits))
		REQUIRE(vm.set_story_bit(actors, bit));
	if(!revisit) REQUIRE(game::apply_first_visit_hand_over(m, vm, tree, "Learn_10", rayman));
	game::input keys(m, read_input_actions(files));
	const std::uint32_t door = tree.node(66).at.address();
	std::uint32_t rayman_base = m.u32(m.u32(vm.actor_of(rayman) + 8));
	std::uint32_t report = m.u32(rayman_base + 0x150);
	std::size_t errors = 0;
	const auto camera_so = tree.node(5).at.address();
	const auto cam = m.u32(vm.actor_of(camera_so) + 0x10);
	int drop_tick = -1, transition_tick = -1, restored_tick = -1;
	float recovery_distance = 100, recovery_height = 0;
	for(int tick = 0; tick <= 600; tick++)
	{
		if(tick == 240)
		{
			if(camera_drop) game::write_transform(m, rayman, { -26.3f, 291.4f, -9 }, { 0, -1, 0, 1, 0, 0, 0, 0, 1 });
			else game::write_transform(m, rayman, { -25, 291.4f, -9 }, { 0, 1, 0, -1, 0, 0, 0, 0, 1 });
			if(camera_drop) vm.camera_motion().tracked_target = 0;
		}
		if(collect && tick == 300)
		{
			// Node 66 watches the Lum's custom bit 28. Set exactly that signal; the door's
			// own scripts perform the move, without changing its state or collision ourselves.
			std::uint32_t lum = vm.get_variable(door, 1).word();
			std::uint32_t standard = m.u32(lum + 4);
			REQUIRE(m.contains(standard + 0x24, 4));
			m.write<std::uint32_t>(standard + 0x24, m.u32(standard + 0x24) | (1u << 27));
			// The tutorial freezes the door's brain during its dialogue. Its release clears
			// custom bit 17 (run4 actor 64, clock 44225) before the collected signal is acted on.
			standard = m.u32(vm.actor_of(door) + 4);
			m.write<std::uint32_t>(standard + 0x24, m.u32(standard + 0x24) & ~(1u << 16));
		}
		if(camera_drop && tick == 300)
		{
			// Seed the approach eye/aim from rec_camsnap; let scripts open the door,
			// choose behaviour 8, and release it themselves. Never force a camera mode.
			game::write_transform(m, camera_so, { -18.787f, 292.758f, -6.671f },
				{ 0.1779f, -0.9840f, 0, 0.9257f, 0.1673f, 0.3392f, -0.3338f, -0.0603f, 0.9407f });
			auto& motion = vm.camera_motion();
			motion.aim = motion.look_at = motion.off = { -26.3f, 291.4f, -9.424f };
			motion.previous_move = motion.previous_aim = {};
			motion.carry_angle = 0;
		}
		if(camera_drop) keys.set_key(0xC8, tick >= 340 && tick < 580);
		keys.step();
		game::step(vm, actors, [&](std::uint32_t, const std::exception& e) {
			if(errors++ == 0) MESSAGE(std::format("tick {}: {}", tick, e.what()));
		});
		if(camera_drop && tick >= 300)
		{
			auto ray = *game::global_placement(m, rayman);
			if(drop_tick < 0 && ray.m[2][3] < -11.8f) drop_tick = tick;
			if(transition_tick < 0 && m.u32(cam + 0x60) == 1)
			{
				transition_tick = tick;
				// A relocation resets the mechanics aim too, so the first new view
				// looks down towards Rayman rather than back up through the door.
				CHECK(vm.camera_motion().aim[2] == doctest::Approx(vm.camera_motion().off[2]));
				auto eye = *game::global_placement(m, camera_so);
				CHECK(vm.camera_motion().aim[2] < eye.m[2][3]);
			}
			if(transition_tick >= 0 && restored_tick < 0 && m.u32(cam + 0x60) == 0)
			{
				restored_tick = tick;
				auto eye = *game::global_placement(m, camera_so);
				recovery_height = eye.m[2][3];
				float squared = 0;
				for(unsigned k = 0; k < 3; ++k) { float d = eye.m[k][3] - ray.m[k][3]; squared += d * d; }
				recovery_distance = std::sqrt(squared);
			}
		}
		if(tick == 270 || tick == 299 || tick == 420)
		{
			auto placement = game::global_placement(m, rayman);
			REQUIRE(placement);
			const float z = placement->m[2][3];
			INFO("tick ", tick, ", Z ", z);
			if(!revisit && (!collect || tick < 300))
			{
				CHECK(z == doctest::Approx(-10.8240f).epsilon(0.00001));
				CHECK((m.u32(report + 4) & 1) == 1);
				CHECK(m.u32(report + 8 + 0x28 + 0x24) == door);
			}
			else CHECK(z < -11.0f);
		}
	}
	if(collect)
	{
		// The door slides out from under him and he drops straight down the shaft onto the floor
		// below (user, the real game), not shoved sideways past it (#73).
		auto fell = *game::global_placement(m, rayman);
		if(!camera_drop) CHECK(fell.m[0][3] == doctest::Approx(-25.0f).epsilon(0.02));
		CHECK(fell.m[2][3] > -30.0f);
		CHECK(fell.m[2][3] < -27.0f);
		auto at = *game::global_placement(m, door);
		CHECK(at.m[0][3] > -20); // a sideways slide, not a lowered or switched-off floor
		CHECK(at.m[2][3] == doctest::Approx(-10.8240f).epsilon(0.00001));
	}
	if(camera_drop)
	{
		INFO("drop ", drop_tick, ", mode 1 ", transition_tick, ", mode 0 ", restored_tick,
			", recovery distance ", recovery_distance, ", height ", recovery_height);
		REQUIRE(drop_tick >= 0);
		REQUIRE(transition_tick >= drop_tick);
		CHECK(transition_tick - drop_tick <= 39); // about 0.6 s from the behaviour-8 fall threshold
		REQUIRE(restored_tick > transition_tick);
		CHECK(recovery_distance <= 6);
		CHECK(recovery_height < -20);
	}
	CHECK(errors == 0);
}

TEST_CASE("real data: Murfy's tablet tip comes back each time Rayman returns")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto game = game_data::locate(*dir);
	REQUIRE(game);
	level_files files = load_level_files(*game, "Learn_10");
	level_header header = read_level_header(files);
	scene tree(files.memory, header);
	animation_bank bank = read_animation_bank(files);
	game::live_memory m(files.memory);
	std::uint32_t rayman = header.placements.at(0).super_object.address();
	game::interpreter vm(m, rayman);
	game::register_handlers(vm);
	game::register_dialog_handlers(vm);
	game::set_dialog_resolver(vm, files);
	vm.set_world(game::world_collision(static_collision(files.memory, tree)));
	vm.set_animations(&bank);
	game::install_level_events(m, bank);
	std::vector<std::uint32_t> actors;
	for(const scene_node& n : tree.nodes())
		if(n.type == super_object_type::actor) actors.push_back(n.at.address());
	vm.set_actors(actors);
	for(const actor_set::entry& e : read_actors(files.memory, tree, bank).actors)
		game::set_global_placement(m, tree.node(e.node).at.address(), e.world);
	for(std::uint32_t bit : game::revisit_story_bits) REQUIRE(vm.set_story_bit(actors, bit));
	game::input keys(m, read_input_actions(files));
	// Node 42 shows the tip when Rayman stands by the tablet, fades it when he leaves, and
	// procedure 139 resets it (0x4062A0, standard type 1) to wait for him again.
	const std::uint32_t tip = tree.node(42).at.address();
	const std::uint32_t normal = m.u32(m.u32(m.u32(vm.actor_of(tip) + 0x0C)) + 4);
	const auto start = game::global_placement(m, tip);
	REQUIRE(start);
	std::uint32_t showing = 0, previous = m.u32(normal + 8);
	const std::uint32_t standard = m.u32(vm.actor_of(tip) + 4);
	std::uint32_t waiting = 0;
	bool back = false;
	const std::uint32_t custom = m.u32(standard + 0x24);
	int shown = 0;
	bool replay = false;
	SUBCASE("walking away and back") {}
	// Holding F1 on the tablet replays Murfy's whole scene; Space pages through it. Afterwards
	// the reset puts his custom bits back (0x4132A0), or he stays out waving (user, 2026-11-02).
	SUBCASE("F1 replays the scene") { replay = true; }
	for(int tick = 0; tick <= (replay ? 1400 : 760); tick++)
	{
		if(tick == 260 || (!replay && tick == 700))
			game::write_transform(m, rayman, { 7.4f, 311.9f, -6.9f }, { 0, 1, 0, -1, 0, 0, 0, 0, 1 });
		if(!replay && tick == 520)
			game::write_transform(m, rayman, { 30, 295, -7 }, { 0, 1, 0, -1, 0, 0, 0, 0, 1 });
		keys.set_key(0x3B, replay && tick >= 300 && tick < 420); // F1
		keys.set_key(0x39, replay && tick >= 700 && (tick - 700) % 40 < 4); // Space
		keys.step();
		game::step(vm, actors);
		std::uint32_t now = m.u32(normal + 8);
		if(tick == 1) waiting = now;
		if(tick > 1000 && now == waiting) back = true;
		if(tick == 270) showing = now;
		if(!replay && tick == 535) // reset: back where the level put it
		{
			auto back = game::global_placement(m, tip);
			REQUIRE(back);
			CHECK(back->m[0][3] == doctest::Approx(start->m[0][3]));
			CHECK(back->m[1][3] == doctest::Approx(start->m[1][3]));
		}
		if(now != previous && showing && now == showing) shown++;
		previous = now;
	}
	CHECK_FALSE(vm.removed(tip));
	if(replay)
	{
		CHECK(m.u32(standard + 0x24) == custom);
		CHECK(back); // reset after the scene, waiting by the tablet again
	}
	else CHECK(shown == 1); // the second visit (the first set `showing`)
}

TEST_CASE("actor floors carry a rider through translation and rotation and release it")
{
	game::live_memory m(three_super_objects());
	auto make_actor = [&](vec3 position) {
		std::uint32_t so = m.allocate(0x48), actor = m.allocate(0x38), standard = m.allocate(0x34), transform = m.allocate(0x58);
		m.write<std::uint32_t>(so, 2);
		m.write(so + 4, actor);
		m.write(so + 0x24, transform);
		m.write(actor + 4, standard);
		m.write(standard + 0xC, so);
		game::set_global_placement(m, so, affine{});
		m.write(transform + 4, position);
		return so;
	};
	std::uint32_t rider = make_actor({ 1, 0, 2 }), platform = make_actor({ 0, 0, 0 });
	std::uint32_t actor = m.u32(rider + 4), dynamics = m.allocate(4), base = m.allocate(0x288), report = m.allocate(0x16C);
	m.write(actor + 8, dynamics);
	m.write(dynamics, base);
	m.write<std::uint32_t>(base + 8, 0x406); // collision, gravity and inertia
	m.write<std::uint32_t>(base + 0xC, 4); // full dynamics, including support selection
	m.write(base + 0x154, vec3{ 0.4f, 0.4f, 1 });
	m.write<float>(base + 0x10, 25);
	m.write(base + 0x150, report);
	m.write(base + 0x7C, vec3{ 1, 0, 2 });
	std::uint32_t platform_actor = m.u32(platform + 4), standard = m.u32(platform_actor + 4);
	m.write<std::uint8_t>(standard + 0x28, 1); // an eligible moving support (0x413730)
	std::uint32_t set = m.allocate(0x68), head = m.allocate(12), link = m.allocate(8), zone = m.allocate(0x30);
	std::uint32_t vertices = m.allocate(48), kinds = m.allocate(2), elements = m.allocate(4), element = m.allocate(0x14), indices = m.allocate(12);
	m.write(platform_actor + 0x14, set);
	m.write(set + 0xC, head);
	m.write<std::uint32_t>(set + 0x3C, 1);
	m.write(head, link);
	m.write(link + 4, zone);
	m.write(zone, vertices);
	m.write(zone + 0x10, kinds);
	m.write(zone + 0x14, elements);
	m.write<std::uint16_t>(zone + 0x2C, 4);
	m.write<std::uint16_t>(zone + 0x2E, 1);
	m.write<std::uint16_t>(kinds, 1);
	m.write(elements, element);
	m.write<std::uint16_t>(element + 4, 2);
	m.write(element + 8, indices);
	const vec3 corners[] = { { -3, -3, 0 }, { 3, -3, 0 }, { 3, 3, 0 }, { -3, 3, 0 } };
	for(std::uint32_t i = 0; i < 4; i++) m.write(vertices + i * 12, corners[i]);
	const std::uint16_t faces[] = { 0, 1, 2, 0, 2, 3 };
	for(std::uint32_t i = 0; i < 6; i++) m.write(indices + i * 2, faces[i]);
	game::interpreter vm(m, rider);
	vm.set_actors({ platform, rider });
	auto move = [&] { game::move_actor(m, rider, actor, 1000.0 / 60, vm.world(), &vm); };
	for(int i = 0; i < 60; i++) move();
	CHECK(game::global_placement(m, rider)->m[2][3] == doctest::Approx(0));
	CHECK(m.u32(base + 0x284) == platform);
	CHECK(m.u32(report + 8 + 0x28 + 0x24) == platform);
	// A platform moves up and sideways, then turns 90 degrees. The off-centre rider must
	// follow the arc and turn with it; a stationary platform must add no subsequent drift.
	affine moved;
	moved.m[0][3] = 4; moved.m[1][3] = 2; moved.m[2][3] = 1;
	game::set_global_placement(m, platform, moved);
	move();
	auto at = *game::global_placement(m, rider);
	CHECK(at.m[0][3] == doctest::Approx(5));
	CHECK(at.m[1][3] == doctest::Approx(2));
	CHECK(at.m[2][3] == doctest::Approx(1));
	moved.m[0][0] = 0; moved.m[0][1] = -1;
	moved.m[1][0] = 1; moved.m[1][1] = 0;
	game::set_global_placement(m, platform, moved);
	move();
	for(int i = 0; i < 10; i++) move();
	at = *game::global_placement(m, rider);
	CHECK(at.m[0][3] == doctest::Approx(4));
	CHECK(at.m[1][3] == doctest::Approx(3));
	CHECK(at.m[2][3] == doctest::Approx(1));
	CHECK(at.m[1][0] == doctest::Approx(1));
	CHECK(game::actor_collision_triangles(vm, platform).empty()); // no self-collision
	// The selected group enables mode 0, while priority and collision exclusions can reject it.
	m.write<std::uint32_t>(set + 0x3C, 0);
	CHECK(game::actor_collision_triangles(vm, rider).empty());
	std::uint32_t group = m.allocate(12), group_link = m.allocate(8);
	m.write(set + 0x28, group);
	m.write(group, group_link);
	m.write<std::uint32_t>(group + 8, 1);
	CHECK(game::actor_collision_triangles(vm, rider).size() == 2);
	m.write<std::uint8_t>(set + 0x41, 1);
	CHECK(game::actor_collision_triangles(vm, rider).empty());
	m.write<std::uint8_t>(set + 0x41, 0);
	m.write<std::uint8_t>(set + 0x42, 8); // exclude the main actor
	CHECK(game::actor_collision_triangles(vm, rider).empty());
	m.write<std::uint8_t>(set + 0x42, 0);
	// A state override disables the receiving zone: the rider must fall and lose its support.
	m.write<std::uint32_t>(set + 0x3C, 2);
	move();
	CHECK(m.u32(base + 0x284) == 0);
	CHECK(game::global_placement(m, rider)->m[2][3] < 1);
	m.write<std::uint32_t>(set + 0x3C, 1);
	vm.remove(platform);
	CHECK(game::actor_collision_triangles(vm, rider).empty());
}

TEST_CASE("Learn_10 opening recognizes the completed story bit")
{
	std::vector<std::uint32_t> words(35);
	CHECK(game::learn_10_opening("learn_10"));
	CHECK_FALSE(game::learn_10_opening("Jail_20"));
	CHECK_FALSE(game::story_has(words, 1091));
	words[(1091 - 1) / 32] |= 1u << ((1091 - 1) % 32);
	CHECK(game::story_has(words, 1091));
	CHECK_FALSE(game::story_has(words, 0));
	CHECK_FALSE(game::story_has(words, 1121));
}

TEST_CASE("real data: sector wake delays the kids and story removal prevents their first thought")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto game_data = game_data::locate(*dir);
	REQUIRE(game_data);
	auto files = load_level_files(*game_data, "Learn_10");
	auto header = read_level_header(files);
	scene tree(files.memory, header);
	auto bank = read_animation_bank(files);
	game::live_memory m(files.memory);
	auto rayman = header.placements.at(0).super_object.address();
	game::interpreter vm(m, rayman);
	game::register_handlers(vm);
	game::register_dialog_handlers(vm);
	game::set_dialog_resolver(vm, files);
	std::vector<std::uint32_t> actors;
	for(const auto& node : tree.nodes())
		if(node.type == super_object_type::actor) actors.push_back(node.at.address());
	vm.set_actors(actors);
	for(const auto& e : read_actors(files.memory, tree, bank).actors)
		game::set_global_placement(m, tree.node(e.node).at.address(), e.world);
	for(auto bit : game::first_visit_story_bits) REQUIRE(vm.set_story_bit(actors, bit));
	REQUIRE(game::apply_first_visit_hand_over(m, vm, tree, "Learn_10", rayman));
	bool completed = false;
	SUBCASE("first visit waits for the cave approach") {}
	SUBCASE("the level script removes the completed director before he wakes") { completed = true; }
	if(completed)
	{
		REQUIRE(vm.set_story_bit(actors, 1133));
		REQUIRE(vm.set_story_bit(actors, 1095));
	}
	// Do not apply_revisit_state: node 4 must perform this removal itself.
	auto director = tree.node(3).at.address();
	auto standard = m.u32(vm.actor_of(director) + 4);
	auto data = m.u32(vm.actor_of(director));
	auto before = m.read<std::uint16_t>(data + 0x100);
	game::input keys(m, read_input_actions(files));
	for(int tick = 0; tick < 6; ++tick)
	{
		keys.step();
		game::step(vm, actors);
		CHECK_FALSE(vm.active(director));
		CHECK(m.u32(standard + 0x18) == 0);
		CHECK(m.read<std::uint16_t>(data + 0x100) == before);
	}
	CHECK(vm.removed(director) == completed);
	// run4: the director wakes at clock 60289, as Rayman reaches this approach
	// (last recorded point at 60257). The warp isolates activation from route/input timing.
	game::write_transform(m, rayman, { -98.75f, 243.19f, -7.51f }, { 0, 1, 0, -1, 0, 0, 0, 0, 1 });
	keys.step();
	game::step(vm, actors);
	CHECK(vm.active(director) == !completed);
	if(completed)
	{
		CHECK(m.read<std::uint16_t>(data + 0x100) == before);
		CHECK(m.u8(standard + 0x29) == 0x0B);
		CHECK(m.u32(standard + 0x18) == 0);
	}
	else
	{
		CHECK(m.u32(standard + 0x18) == vm.frame_number());
		CHECK(m.read<std::uint16_t>(data + 0x100) != before);
	}
}

TEST_CASE("real data: Bast_20 piranhas only catch Rayman in their wake region")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto data = game_data::locate(*dir);
	REQUIRE(data);
	auto files = load_level_files(*data, "Bast_20");
	auto header = read_level_header(files);
	scene tree(files.memory, header);
	auto bank = read_animation_bank(files);
	game::live_memory m(files.memory);
	auto rayman = header.placements.at(0).super_object.address();
	game::interpreter vm(m, rayman);
	game::register_handlers(vm);
	game::register_dialog_handlers(vm);
	game::set_dialog_resolver(vm, files);
	auto triangles = static_collision(files.memory, tree);
	// This level's piranha script supplies the hazard, not a water/kill triangle.
	CHECK(std::ranges::none_of(triangles, [](const auto& t) { return (t.flags & 0x4400) != 0; }));
	vm.set_world(game::world_collision(std::move(triangles)));
	vm.set_animations(&bank);
	game::install_level_events(m, bank);
	std::vector<std::uint32_t> actors;
	for(const auto& node : tree.nodes())
		if(node.type == super_object_type::actor) actors.push_back(node.at.address());
	vm.set_actors(actors);
	for(const auto& e : read_actors(files.memory, tree, bank).actors)
		game::set_global_placement(m, tree.node(e.node).at.address(), e.world);
	for(auto bit : game::revisit_story_bits) REQUIRE(vm.set_story_bit(actors, bit));
	bool pond = false, unfiltered = false;
	SUBCASE("falling near the reported camera does not wake the distant piranhas") {}
	SUBCASE("the actual pond still catches Rayman") { pond = true; }
	SUBCASE("running the sleeping script reproduces the remote mid-air capture") { unfiltered = true; }
	game::write_transform(m, rayman, { pond ? 118.0f : 13.0f, 67, -57 }, { 0, 1, 0, -1, 0, 0, 0, 0, 1 });
	auto fish = tree.node(24).at.address(), fish_actor = vm.actor_of(fish);
	auto fish_standard = m.u32(fish_actor + 4), fish_data = m.u32(fish_actor);
	auto fish_state = m.u32(fish_data + 4);
	auto ray_actor = vm.actor_of(rayman);
	auto contacts = m.u32(m.u32(m.u32(ray_actor + 8)) + 0x150);
	REQUIRE(contacts);
	game::input keys(m, read_input_actions(files));
	bool caught = false;
	for(int tick = 0; tick < 90; ++tick)
	{
		keys.step();
		game::step(vm, actors);
		if(unfiltered)
		{
			// Negative control: the pre-activation scheduler ran every placed actor.
			// Node 24 tests only Z; 0x40B620/0x40B4C0 must supply its spatial gate.
			vm.think(fish);
		}
		CHECK((m.u32(contacts + 4) & 0x40) == 0);
		if(m.u32(fish_data + 4) != fish_state)
		{
			caught = true;
			CHECK(vm.get_variable(rayman, 8).as_int() == 9);
			CHECK(game::global_placement(m, rayman)->m[2][3] == doctest::Approx(-57.8498f));
			break;
		}
		if(!pond && !unfiltered)
		{
			CHECK_FALSE(vm.active(fish));
			CHECK(m.u32(fish_standard + 0x18) == 0);
		}
	}
	CHECK(caught == (pond || unfiltered));
	if(!pond && !unfiltered)
	{
		CHECK(vm.get_variable(rayman, 9).as_int() != 28); // the scripted capture mode
		CHECK(game::global_placement(m, rayman)->m[2][3] < -61);
	}
}

TEST_CASE("real data: Bast_20 pond capture completes the death and return route")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto data = game_data::locate(*dir);
	REQUIRE(data);
	auto files = load_level_files(*data, "Bast_20");
	auto header = read_level_header(files);
	scene tree(files.memory, header);
	auto bank = read_animation_bank(files);
	auto brains = read_brains(files.memory, tree);
	auto ray_brain = std::ranges::find(brains.actors, 114, &actor_brain::node);
	REQUIRE(ray_brain != brains.actors.end());
	const auto& ray_model = brains.model(*ray_brain);
	game::live_memory m(files.memory);
	auto rayman = header.placements.at(0).super_object.address();
	game::interpreter vm(m, rayman);
	game::register_handlers(vm);
	game::register_dialog_handlers(vm);
	game::set_dialog_resolver(vm, files);
	vm.set_world(game::world_collision(static_collision(files.memory, tree)));
	vm.set_animations(&bank);
	game::install_level_events(m, bank);
	std::vector<std::uint32_t> actors;
	for(const auto& node : tree.nodes())
		if(node.type == super_object_type::actor) actors.push_back(node.at.address());
	vm.set_actors(actors);
	for(const auto& e : read_actors(files.memory, tree, bank).actors)
		game::set_global_placement(m, tree.node(e.node).at.address(), e.world);
	for(auto bit : game::revisit_story_bits) REQUIRE(vm.set_story_bit(actors, bit));
	auto fish = tree.node(24).at.address(), fish_actor = vm.actor_of(fish);
	auto fish_data = m.u32(fish_actor);
	auto home = game::global_placement(m, fish);
	REQUIRE(home);
	auto ray_actor = vm.actor_of(rayman), standard = m.u32(ray_actor + 4);
	auto mind = m.u32(m.u32(ray_actor + 0xC)), normal = m.u32(mind + 4);
	auto collision = m.u32(ray_actor + 0x14);
	game::input keys(m, read_input_actions(files));
	for(int tick = 0; tick < 120; ++tick) { keys.step(); game::step(vm, actors); }
	bool fall = false, rim = false;
	SUBCASE("placement below the trigger") {}
	SUBCASE("falling from above the pond") { fall = true; }
	SUBCASE("walking off the reported rim") { fall = true; rim = true; }
	game::write_transform(m, rayman, { fall ? 132.8f : 118.0f, rim ? 47.8f : (fall ? 60.0f : 67.0f), fall ? -51.7f : -57.0f },
		{ -1, 0, 0, 0, -1, 0, 0, 0, 1 });
	if(fall)
	{
		// Isolate playing from the unrelated portal-arrival script: its ordinary
		// var8=0 hand-over reinstalls idle/falling mechanics through behaviour 9.
		vm.set_variable(rayman, 8, game::value::integer(0));
		vm.change_behaviour(normal, ray_model.behaviours[9].at.address(), true);
		vm.change_behaviour(m.u32(mind + 8), ray_model.reflexes[13].at.address(), true);
	}
	const auto* fix_states = files.memory.find({ 0x04, 0x00 });
	REQUIRE(fix_states);
	const auto return_state = memory_location{ fix_states, 0x7338 }.address();
	bool caught = false, held = false, dying = false, returned = false;
	int capture_tick = -1, return_tick = -1;
	for(int tick = 0; tick < 420; ++tick)
	{
		keys.set_key(0xCB, rim && !caught); // Left, towards the pond through the game's input/camera path
		keys.step();
		game::step(vm, actors);
		auto behaviour = m.u32(normal + 8);
		if(!caught && vm.get_variable(rayman, 8).as_int() == 9)
		{
			caught = true; capture_tick = tick;
			CHECK(game::global_placement(m, rayman)->m[2][3] == doctest::Approx(-57.8498f));
			CHECK(m.f32(fish + 0x38) == 255);
		}
		held |= caught && (behaviour == ray_model.behaviours[9].at.address() || behaviour == ray_model.behaviours[10].at.address());
		if(behaviour == ray_model.behaviours[21].at.address())
		{
			dying = true;
			CHECK(vm.get_variable(rayman, 68).as_int() == 52);
			CHECK(m.u8(standard + 0x21) == 10); // the first event's damage: 12 - 2
			CHECK(m.u32(m.u32(ray_actor) + game::data_3d::requested_state) == return_state);
		}
		if(vm.restart_requested)
		{
			returned = true; return_tick = tick;
			CHECK(dying);
			CHECK(m.u8(collision + 0x64) == 50);
			break;
		}
	}
	INFO("fall ", fall, " rim ", rim, " capture tick ", capture_tick, " return tick ", return_tick);
	auto final_position = game::global_placement(m, rayman);
	INFO("position ", final_position->m[0][3], ",", final_position->m[1][3], ",", final_position->m[2][3],
		" var8 ", vm.get_variable(rayman, 8).as_int(), " var9 ", vm.get_variable(rayman, 9).as_int(),
		" normal ", m.u32(normal + 8), " state ", m.u32(m.u32(ray_actor) + 4));
	CHECK(caught); CHECK(held); CHECK(dying); CHECK(returned);
	if(returned) CHECK(return_tick - capture_tick <= 101);
	if(returned)
	{
		keys.set_key(0xCB, false); keys.step(); game::step(vm, actors);
		CHECK_FALSE(vm.restart_requested);
		CHECK(m.u32(normal + 8) != ray_model.behaviours[21].at.address());
		CHECK(game::global_placement(m, fish)->m[0][3] == doctest::Approx(home->m[0][3]));
		CHECK(game::global_placement(m, fish)->m[1][3] == doctest::Approx(home->m[1][3]));
		CHECK(m.u32(fish_data + 4) == m.u32(fish_data));
		CHECK(vm.generated().alive().empty());
	}
	for(const auto& [handler, count] : vm.missing)
		CHECK_FALSE((std::string_view(handler.first) == "procedure" && handler.second == 79));
}

TEST_CASE("procedure 79 requests a deferred return and its 0x467770 siblings exist")
{
	handler_fixture f;
	CHECK_FALSE(f.vm.restart_requested);
	f.procedure(79, {});
	CHECK(f.vm.restart_requested);
	CHECK(f.vm.stop_sounds_requested);
	f.procedure(80, {});
	f.procedure(81, {});
	f.vm.cast_ignore_mask = 0x40;
	f.procedure(82, {});
	CHECK(f.vm.cast_ignore_mask == 0);
}

TEST_CASE("handler census: procedure 139 writes placed actor removal flags")
{
	handler_fixture f;
	f.m.write<std::uint32_t>(f.standard + 0x14, 3);
	f.m.write<std::uint8_t>(f.standard + 0x29, 0x46);
	f.procedure(139, { f.me() });
	CHECK(f.vm.removed(f.so));
	CHECK(f.m.u8(f.standard + 0x29) == 0x43);
}

TEST_CASE("sector wake uses eligibility, fresh entry and moving-head order")
{
	handler_fixture f;
	auto root = f.m.allocate(0x48);
	auto a = f.m.allocate(0x48), b = f.m.allocate(0x48);
	auto ad = f.m.allocate(0x88), bd = f.m.allocate(0x88);
	f.m.write<std::uint32_t>(a, 4); f.m.write(a + 4, ad);
	f.m.write<std::uint32_t>(b, 4); f.m.write(b + 4, bd);
	f.m.write(root + 8, a); f.m.write(a + 0x14, b);
	f.m.write(a + 0x1C, root); f.m.write(b + 0x1C, root);
	f.m.write(f.so + 0x1C, root);
	for(auto d : { ad, bd })
	{
		f.m.write(d + 0x60, vec3{ d == ad ? 0.0f : 10.0f, -5, -5 });
		f.m.write(d + 0x6C, vec3{ d == ad ? 9.0f : 20.0f, 5, 5 });
	}
	auto transform = f.m.allocate(0x58);
	f.m.write(f.so + 0x24, transform);
	auto place = [&](float x) {
		affine p; p.m[0][3] = x;
		game::set_global_placement(f.m, f.so, p);
	};
	place(1);
	f.m.write<std::uint8_t>(f.standard + 0x29, 2);
	auto target = f.m.allocate(0x48), actor = f.m.allocate(0x38), standard = f.m.allocate(0x34);
	f.m.write<std::uint32_t>(target, 2); f.m.write(target + 4, actor); f.m.write(actor + 4, standard);
	f.m.write<std::uint8_t>(standard + 0x29, 3);
	f.m.write<std::uint32_t>(standard + 0x14, 3);
	bool eligible = false, overlap = false, reset = false;
	SUBCASE("removed type 3 never wakes") {}
	SUBCASE("removed type 1 wakes on a fresh entry") { eligible = reset = true; f.m.write<std::uint32_t>(standard + 0x14, 1); }
	SUBCASE("negative standard type uses the executable's signed comparison") { eligible = true; f.m.write<std::int32_t>(standard + 0x14, -1); }
	SUBCASE("a shared sector is not fresh") { overlap = true; f.m.write<std::uint32_t>(standard + 0x14, 1); }
	SUBCASE("mask 2 clear prevents wake without entry reset") { f.m.write<std::uint8_t>(standard + 0x29, 0); f.m.write<std::uint32_t>(standard + 0x10, 1); }
	SUBCASE("moving actor enters the head") { eligible = reset = true; f.m.write<std::uint8_t>(standard + 0x29, 2); f.m.write<std::uint8_t>(standard + 0x28, 1); }
	SUBCASE("present actor with entry reset disabled keeps its custom flags") { eligible = true; f.m.write<std::uint8_t>(standard + 0x29, 2); f.m.write<std::uint32_t>(standard + 0x10, 1); }
	SUBCASE("standard bit 0x10 exempts an actor from entry reset") { eligible = true; f.m.write<std::uint8_t>(standard + 0x29, 0x12); }
	f.m.write<std::uint32_t>(standard + 0x24, 0x10000);
	f.m.write<std::uint32_t>(standard + 0x2C, 0x20);
	auto fixed = f.m.allocate(0x48), fixed_actor = f.m.allocate(0x38), fixed_standard = f.m.allocate(0x34);
	f.m.write<std::uint32_t>(fixed, 2); f.m.write(fixed + 4, fixed_actor); f.m.write(fixed_actor + 4, fixed_standard);
	f.m.write<std::uint8_t>(fixed_standard + 0x29, 2);
	auto entry = f.m.allocate(0x10), fixed_entry = f.m.allocate(0x10);
	f.m.write(entry, target); f.m.write(fixed_entry, fixed); f.m.write(fixed_entry + 4, entry);
	f.m.write(bd, fixed_entry); f.m.write<std::uint32_t>(bd + 8, 2);
	if(overlap)
	{
		auto link = f.m.allocate(8);
		f.m.write(link, b); f.m.write(ad + 0x3C, link); f.m.write<std::uint32_t>(ad + 0x44, 1);
	}
	std::vector<std::uint32_t> actors{f.so, target, fixed};
	f.vm.set_actors(actors);
	f.vm.update_activation(actors);
	CHECK(f.vm.active(f.so));
	CHECK(f.vm.active(target) == overlap); // first entry is fresh
	CHECK((f.m.u32(standard + 0x24) & ~0x800u) == 0x10000); // startup never resets
	if(overlap) f.vm.remove(target);
	place(12);
	f.vm.update_activation(actors);
	CHECK(f.vm.active(f.so));
	CHECK(f.vm.active(target) == eligible);
	CHECK((f.m.u32(standard + 0x24) & ~0x800u) == (reset ? 0x20u : 0x10000u));
	if(eligible)
	{
		CHECK((f.m.u32(standard + 0x24) & 0x800) != 0);
		CHECK(f.vm.run_order.front() == (f.m.u8(standard + 0x28) ? target : fixed));
	}
	place(1);
	f.vm.update_activation(actors);
	CHECK_FALSE(f.vm.active(target));
}

TEST_CASE("real data: Learn_10 kids reset on every return from the cave")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto data = game_data::locate(*dir);
	REQUIRE(data);
	auto files = load_level_files(*data, "Learn_10");
	auto header = read_level_header(files);
	scene tree(files.memory, header);
	auto bank = read_animation_bank(files);
	game::live_memory m(files.memory);
	auto rayman = header.placements.at(0).super_object.address();
	game::interpreter vm(m, rayman);
	game::register_handlers(vm);
	vm.set_animations(&bank);
	std::vector<std::uint32_t> actors;
	for(const auto& n : tree.nodes())
		if(n.type == super_object_type::actor) actors.push_back(n.at.address());
	vm.set_actors(actors);
	for(const auto& e : read_actors(files.memory, tree, bank).actors)
		game::set_global_placement(m, tree.node(e.node).at.address(), e.world);
	REQUIRE(vm.set_story_bit(actors, 1092)); // after the scene: behaviour 0 chooses crying
	auto kids = tree.node(39).at.address(), actor = vm.actor_of(kids);
	auto standard = m.u32(actor + 4), anim = m.u32(actor);
	auto normal = m.u32(m.u32(m.u32(actor + 0x0C)) + 4);
	auto place = [&](vec3 p) {
		game::write_transform(m, rayman, {p[0], p[1], p[2]}, {0, 1, 0, -1, 0, 0, 0, 0, 1});
		vm.update_activation(actors);
	};
	place({-63, 202, 0});
	REQUIRE(vm.active(kids));
	vm.think(kids);
	auto crying = m.u32(anim + 4);
	REQUIRE(crying != m.u32(anim));
	REQUIRE(m.u32(normal + 8) != m.u32(normal + 0x14));
	for(int visit = 0; visit < 3; ++visit)
	{
		place({-40, 181, 1.6f});
		CHECK_FALSE(vm.active(kids));
		CHECK(m.u32(anim + 4) == crying); // sleeping itself does not reset
		place({-63, 202, 0});
		CHECK(vm.active(kids));
		CHECK(m.u32(normal + 8) == m.u32(normal + 0x14));
		CHECK(m.u32(anim + 4) == m.u32(anim));
		CHECK(m.u32(standard + 0x24) == (m.u32(standard + 0x2C) | 0x800u));
		vm.think(kids);
		CHECK(m.u32(anim + 4) == crying);
		CHECK(m.u32(normal + 8) != m.u32(normal + 0x14));
		CHECK((m.u32(standard + 0x24) & 0x10000) != 0);
	}
}

TEST_CASE("real data: static objects carry their bounding-sphere centre (0x42AF70 culling)")
{
	auto dir = real_game_dir();
	if(!dir) return;
	auto data = game_data::locate(*dir);
	REQUIRE(data);
	auto files = load_level_files(*data, "Jail_10");
	scene tree(files.memory, read_level_header(files));
	std::size_t with = 0, total = 0;
	for(const auto& placed : static_geometry(files.memory, tree))
	{
		total++;
		if(placed.object.bounding_centre) with++;
	}
	CHECK(total > 0);
	CHECK(with == total);
}

TEST_CASE("real data: Jail_20 cell scene: the pirate carrying Globox wakes and walks in on cue")
{
	// The user's recording of the real game (2026-11-07): the combined actor (actor 5 there, node 7 here)
	// leaves behaviour 0 at once, changes state at 16.0 s and steps into the cell at 24.4 s,
	// reaching (709.071, 130.369). It switches itself off first (procedure_85(me), which must not
	// end the rule) and the director's cue 1301/1410 switches it back on (procedure 84 relinks it
	// into the active world) and sends it message 10.
	auto dir = real_game_dir();
	if(!dir) return;
	auto data = game_data::locate(*dir);
	REQUIRE(data);
	auto files = load_level_files(*data, "Jail_20");
	auto header = read_level_header(files);
	scene tree(files.memory, header);
	auto bank = read_animation_bank(files);
	game::live_memory m(files.memory);
	auto rayman = header.placements.at(0).super_object.address();
	game::interpreter vm(m, rayman);
	game::register_handlers(vm);
	game::register_dialog_handlers(vm);
	game::set_dialog_resolver(vm, files);
	vm.set_world(game::world_collision(static_collision(files.memory, tree)));
	vm.set_animations(&bank);
	game::install_level_events(m, bank);
	std::vector<std::uint32_t> actors;
	for(const auto& node : tree.nodes())
		if(node.type == super_object_type::actor) actors.push_back(node.at.address());
	vm.set_actors(actors);
	for(const auto& e : read_actors(files.memory, tree, bank).actors)
		game::set_global_placement(m, tree.node(e.node).at.address(), e.world);
	for(auto bit : game::first_visit_story_bits) REQUIRE(vm.set_story_bit(actors, bit));
	auto pirate = tree.node(7).at.address();
	for(int tick = 0; tick < 1240; ++tick)
		game::step(vm, actors);
	CHECK(vm.active(pirate));
	auto at = game::global_placement(m, pirate);
	REQUIRE(at);
	CHECK(at->m[0][3] == doctest::Approx(709.071f).epsilon(0.001));
	CHECK(at->m[1][3] == doctest::Approx(130.369f).epsilon(0.001));
	CHECK(at->uniform_scale() == doctest::Approx(1));

	// rec_jail20_3: Rayman settles at load, before the scene freezes mechanics;
	// handing back control does not cause another descent.
	auto settled = game::global_placement(m, rayman);
	REQUIRE(settled);
	CHECK(settled->m[2][3] == doctest::Approx(455.482f).epsilon(0.00001));
	for(int tick = 1240; tick < 5750; ++tick)
		game::step(vm, actors);
	auto handed_back = game::global_placement(m, rayman);
	REQUIRE(handed_back);
	CHECK(handed_back->m[2][3] == doctest::Approx(settled->m[2][3]).epsilon(0.000001));
	CHECK(vm.active(rayman));
	CHECK_FALSE((m.u32(rayman + 0x30) & 2) != 0);
	CHECK(handed_back->uniform_scale() == doctest::Approx(1));

	// 0x4061B0 clears activity, but leaves the actor in the active world's list.
	// The original renderer's 0x20 last-draw bit is absent in this headless VM.
	auto cell_globox = tree.node(14).at.address();
	auto standard = m.u32(vm.actor_of(cell_globox) + 4);
	CHECK((m.u8(standard + 0x29) & 0x0F) == 0x08);
	CHECK_FALSE(vm.active(cell_globox));
	CHECK_FALSE(vm.removed(cell_globox));
	CHECK(std::ranges::find(vm.run_order, cell_globox) != vm.run_order.end());

	// The visible gameplay Globox uses the combined actor's other AI model (3/12),
	// not the switched-off cutscene model (2/16). Its object bank contains Globox.
	CHECK(vm.active(pirate));
	CHECK_FALSE((m.u32(pirate + 0x30) & 2) != 0);
	auto drawable = read_actors(files.memory, tree, bank);
	auto body = std::ranges::find(drawable.actors, std::size_t(7), &actor_set::entry::node);
	REQUIRE(body != drawable.actors.end());
	bool globox_skin = false;
	for(const auto& mesh : body->meshes)
		if(mesh)
			for(const auto& element : drawable.meshes[*mesh].elements)
				if(element.material && element.material->texture)
					globox_skin |= element.material->texture->name.find("globo_peau") != std::string::npos;
	CHECK(globox_skin);
	auto live_state = m.u32(m.u32(vm.actor_of(pirate)) + 4);
	auto reference = m.u32(live_state + 4);
	body->animation = m.read<std::uint16_t>(reference + 0x18);
	body->world = *game::global_placement(m, pirate);
	CHECK(body->world.uniform_scale() == doctest::Approx(1));
	std::vector<posed_part> visible_parts;
	pose_actor(*body, bank, 76.5, visible_parts);
	CHECK(visible_parts.size() == 11);
}

TEST_CASE("real data: Hall arrival, Murfy, locked links and Fairy Glade exit")
{
	auto dir = real_game_dir(); if(!dir) return;
	auto data = game_data::locate(*dir); REQUIRE(data);
	auto files = load_level_files(*data, "Mapmonde");
	auto header = read_level_header(files);
	scene tree(files.memory, header);
	auto bank = read_animation_bank(files);
	game::live_memory m(files.memory);
	REQUIRE(header.main_actor);
	game::interpreter vm(m, header.main_actor->address());
	game::register_handlers(vm);
	game::register_dialog_handlers(vm);
	game::set_dialog_resolver(vm, files);
	vm.set_world(game::world_collision(static_collision(files.memory, tree)));
	vm.set_animations(&bank);
	game::install_level_events(m, bank);
	std::vector<std::uint32_t> actors, fix;
	for(const auto& node : tree.nodes())
		if(node.type == super_object_type::actor) actors.push_back(node.at.address());
	for(const auto& p : header.placements) fix.push_back(p.super_object.address());
	vm.set_actors(actors);
	for(const auto& e : read_actors(files.memory, tree, bank).actors)
		game::set_global_placement(m, tree.node(e.node).at.address(), e.world);
	for(auto bit : game::revisit_story_bits) REQUIRE(vm.set_story_bit(fix, bit));
	vm.menu_request = {3, 1, 0, 0}; // Learn_10's completed exit unlocks door 1.
	vm.save_folder.clear(); // isolated new session: no save prompt
	game::input keys(m, read_input_actions(files));
	auto selected = [&] { return vm.get_variable(vm.main_actor(), 5).word(); };
	auto fairy = vm.actor_of(tree.node(8).at.address());
	for(int tick = 0; tick < 2100 && vm.level_request.empty(); ++tick)
	{
		// Down: Woods -> Fairy Glade. Later try continuing beyond the locked next door.
		keys.set_key(0xD0, (tick >= 100 && tick < 200) || (tick >= 1100 && tick < 1150));
		keys.set_key(0x39, tick >= 600 && tick <= 900 && tick % 60 == 0);
		keys.set_key(0x1E, tick == 1250); // A enters the chosen door after the explanation.
		keys.step();
		game::step(vm, actors);
		// ortool's headless draw pass: a newly woken visible actor clears bit 12.
		for(auto so : actors)
			if(vm.active(so))
			{
				auto standard = m.u32(vm.actor_of(so) + 4);
				m.write<std::uint32_t>(standard + 0x24, m.u32(standard + 0x24) & ~0x800u);
			}
		if(tick == 500)
		{
			CHECK(selected() == fairy);
			CHECK(vm.get_variable(tree.node(77).at.address(), 22).as_int() == 1);
			CHECK(vm.get_variable(vm.main_actor(), 7).as_int() == 0); // tutorial holds input
			auto bits = vm.story_words(fix);
			CHECK(game::story_has(bits, 960));
			CHECK(game::story_has(bits, 961));
			CHECK_FALSE(game::story_has(bits, 962));
			CHECK_FALSE(game::story_has(bits, 989));
		}
		if(tick == 1200)
		{
			CHECK(selected() == fairy);
			CHECK(game::story_has(vm.story_words(fix), 989));
			CHECK(vm.get_variable(vm.main_actor(), 7).as_int() == 1);
		}
	}
	CHECK(vm.level_request == "Learn_30");
	CHECK(vm.missing.empty());
}

TEST_CASE("real data: Learn_10 exit scene hands its completed story and entry pair to the Hall")
{
	auto dir = real_game_dir(); if(!dir) return;
	auto data = game_data::locate(*dir); REQUIRE(data);
	auto files = load_level_files(*data, "Learn_10");
	auto header = read_level_header(files);
	scene tree(files.memory, header);
	game::live_memory m(files.memory);
	game::interpreter vm(m, header.main_actor->address());
	game::register_handlers(vm);
	auto scene_rayman = tree.node(45).at.address();
	// The gate cutscene's final command is mailbox 99, consumed by this placed actor.
	// Exercise the installed ending script rather than manufacturing a level request.
	for(int tick = 0; tick < 4; ++tick) { vm.next_tick(); vm.think(scene_rayman); }
	auto standard = m.u32(vm.actor_of(scene_rayman) + 4);
	m.write<std::uint8_t>(standard + 0x22, 99);
	vm.next_tick(); vm.think(scene_rayman);
	CHECK(vm.level_request == "mapmonde");
	CHECK(vm.menu_request[0] == 3);
	CHECK(vm.menu_request[1] == 1);
	std::vector<std::uint32_t> fix;
	for(const auto& p : header.placements) fix.push_back(p.super_object.address());
	CHECK(game::story_has(vm.story_words(fix), 1133));
	CHECK(vm.missing.empty());
}
