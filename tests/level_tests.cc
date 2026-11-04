#include "test_util.h"

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

using namespace openrayman;

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
		m.write<float>(part_transform + 0x34 + i * 16, 1);
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

TEST_CASE("screen text inline colours retain the glyph and change the tint")
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
	CHECK(layout.glyphs[1].index == *game::dialog_glyph_index('B'));
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
	CHECK(describe(10) == "any(any(key A, key Num8), pad 0 button 11 (kind 26))");
	CHECK(describe(11) == "any(any(key Space, key Num1), pad 0 button 10 (kind 26))");
	CHECK(describe(13) == "any(any(key LCtrl, key RCtrl), held action 12)");
	CHECK(describe(16) == "any(any(key W, key PgDn), pad 0 button 13 (kind 26))");

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
	auto dynamics = m.u32(camera_actor + 8);
	auto base = m.u32(dynamics);
	auto report = m.u32(base + 0x150);
	REQUIRE(m.contains(report + 0x168, 1));
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
	// The five placed Lums, the end cage and Teensies, and the kids' director.
	constexpr std::size_t absent[] = { 3, 11, 12, 13, 36, 53, 56, 57, 58, 59, 60, 61, 70, 71 };
	for(auto node : absent) CHECK_FALSE(vm.removed(tree.node(node).at.address()));
	game::apply_revisit_state(vm, tree, "Learn_10");
	game::input keys(memory, read_input_actions(files));
	std::size_t errors = 0;
	for(int tick = 0; tick < 390; ++tick)
	{
		if(tick == 240) game::write_transform(memory, rayman, { -61.53f, 204.99f, 0.16f }, { 0, 1, 0, -1, 0, 0, 0, 0, 1 });
		if(tick == 330) game::write_transform(memory, rayman, { 32.605f, 106.09f, 33.868f }, { 0, 1, 0, -1, 0, 0, 0, 0, 1 });
		keys.step();
		game::step(vm, actors, [&](std::uint32_t, const std::exception&) { ++errors; });
		CHECK(vm.removed(tree.node(3).at.address()));
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
	bool revisit = false, collect = false;
	SUBCASE("closed trapdoor matches the recording") {}
	SUBCASE("revisit has an open trapdoor") { revisit = true; }
	SUBCASE("the tutorial releases the door and the Lum opens it") { collect = true; }
	for(std::uint32_t bit : revisit ? std::span<const std::uint32_t>(game::revisit_story_bits) : std::span<const std::uint32_t>(game::first_visit_story_bits))
		REQUIRE(vm.set_story_bit(actors, bit));
	if(!revisit) REQUIRE(game::apply_first_visit_hand_over(m, vm, tree, "Learn_10", rayman));
	game::input keys(m, read_input_actions(files));
	const std::uint32_t door = tree.node(66).at.address();
	std::uint32_t rayman_base = m.u32(m.u32(vm.actor_of(rayman) + 8));
	std::uint32_t report = m.u32(rayman_base + 0x150);
	std::size_t errors = 0;
	for(int tick = 0; tick <= 420; tick++)
	{
		if(tick == 240)
			game::write_transform(m, rayman, { -25, 291.4f, -9 }, { 0, 1, 0, -1, 0, 0, 0, 0, 1 });
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
		keys.step();
		game::step(vm, actors, [&](std::uint32_t, const std::exception& e) {
			if(errors++ == 0) MESSAGE(std::format("tick {}: {}", tick, e.what()));
		});
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
		auto at = *game::global_placement(m, door);
		CHECK(at.m[0][3] > -20); // a sideways slide, not a lowered or switched-off floor
		CHECK(at.m[2][3] == doctest::Approx(-10.8240f).epsilon(0.00001));
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
