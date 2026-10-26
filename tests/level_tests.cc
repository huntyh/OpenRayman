#include "test_util.h"

#include <openrayman/actor.h>
#include <openrayman/animation.h>
#include <openrayman/cnt_archive.h>
#include <openrayman/error.h>
#include <openrayman/game_data.h>
#include <openrayman/geometry.h>
#include <openrayman/gf_texture.h>
#include <openrayman/level_header.h>
#include <openrayman/mask.h>
#include <openrayman/memory_image.h>
#include <openrayman/pointer_stream.h>
#include <openrayman/relocation.h>
#include <openrayman/scene.h>
#include <openrayman/sna_file.h>
#include <openrayman/texture_table.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>

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
		for(const placed_geometry& placed : static_geometry(files.memory, tree))
		{
			meshes++;
			for(std::uint16_t type : placed.object.skipped_types)
				skipped_types[type]++;
			for(const triangle_element& e : placed.object.elements)
			{
				level_triangles += e.triangles.size();
				CHECK(e.uv_triangles.size() == e.triangles.size());
				if(e.material && e.material->texture)
					CHECK(gf_size(e.material->texture->cnt_path()).has_value());
			}
		}
		CHECK(meshes > 0);
		CHECK(level_triangles > 0);
		if(level == "Learn_10")
		{
			// Recorded from the export checked by eye (top and side views of the whole level).
			CHECK(meshes == 87);
			CHECK(level_triangles == 2604);
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
		}
		if(level == "Learn_10")
			CHECK(set.meshes.size() == 113); // distinct meshes, shared between actors
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
