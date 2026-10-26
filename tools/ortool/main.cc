// ortool: inspects and exports Rayman 2 data, mainly for checking that the readers work.

#include <openrayman/actor.h>
#include <openrayman/animation.h>
#include <openrayman/apm_audio.h>
#include <openrayman/cnt_archive.h>
#include <openrayman/dsb_script.h>
#include <openrayman/file.h>
#include <openrayman/game_data.h>
#include <openrayman/geometry.h>
#include <openrayman/gf_texture.h>
#include <openrayman/memory_image.h>
#include <openrayman/level_header.h>
#include <openrayman/pointer_stream.h>
#include <openrayman/scene.h>
#include <openrayman/texture_table.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace openrayman;

static void usage()
{
	std::cout <<
		"usage: ortool <command> [arguments]\n"
		"\n"
		"  info <game folder>                  Summarize a Rayman 2 installation\n"
		"  dsb <file.dsb>                      Print the contents of a DSB script\n"
		"  cnt <file.cnt>                      List the files in a CNT archive\n"
		"  textures <game folder> <out folder> Export every texture as PNG\n"
		"  music <game folder> <out folder>    Export every APM music file as WAV\n"
		"  level <game folder> <level|all>     Load a level's memory and check its pointers\n"
		"  gpt <game folder> <level>           List the pointers in Fix.gpt and a level's GPT\n"
		"  obj <game folder> <level> <out>     Export a level's static geometry as OBJ with textures\n"
		"  scene <game folder> <level>         Print a level's super object tree\n"
		"  peek <game folder> <level> <block> <hex offset> <words>\n"
		"                                      Print memory words, e.g. peek ... Learn_10 05/02 300 16\n"
		"\n"
		"<game folder> is the install folder (e.g. \"C:\\GOG Games\\Rayman 2\") or its Data folder.\n";
}

static game_data require_game(const fs::path& folder)
{
	auto game = game_data::locate(folder);
	if(!game)
		throw std::runtime_error("no Game.dsb found in " + folder.string() + " or its Data folder");
	return std::move(*game);
}

static void write_wav(const fs::path& path, const std::vector<std::int16_t>& pcm, std::uint16_t channels, std::uint32_t rate)
{
	std::ofstream out(path, std::ios::binary);
	auto u32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
	auto u16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };

	std::uint32_t data_size = static_cast<std::uint32_t>(pcm.size() * 2);
	out.write("RIFF", 4); u32(36 + data_size); out.write("WAVE", 4);
	out.write("fmt ", 4); u32(16); u16(1); u16(channels); u32(rate); u32(rate * channels * 2); u16(static_cast<std::uint16_t>(channels * 2)); u16(16);
	out.write("data", 4); u32(data_size);
	out.write(reinterpret_cast<const char*>(pcm.data()), data_size);
	if(!out)
		throw std::runtime_error("could not write " + path.string());
}

static int cmd_info(const fs::path& folder)
{
	game_data game = require_game(folder);
	const dsb_script& dsb = game.game_dsb();

	std::cout << "Data folder: " << game.data_dir().string() << "\n\n";

	std::cout << "Directories:\n";
	for(const auto& [role, path] : dsb.directories)
		std::cout << "  " << role << " = " << path << (game.directory(role) ? "" : "  (not on disk, unused by the PC version)") << "\n";

	std::cout << "\nTexture archives:\n";
	for(const fs::path& path : game.texture_archives())
		std::cout << "  " << path.filename().string() << ": " << cnt_archive(path).entries().size() << " textures\n";

	std::cout << "\nLevels (" << dsb.levels.size() << "), first is " << (dsb.levels.empty() ? "none" : dsb.levels.front()) << ":\n";
	auto levels_dir = game.directory("levels");
	int missing = 0;
	for(const std::string& level : dsb.levels)
	{
		bool found = levels_dir && find_case_insensitive(*levels_dir, level + "/" + level + ".sna");
		missing += !found;
		std::cout << "  " << level << (found ? "" : "  (missing!)") << "\n";
	}

	std::cout << "\nMusic files: " << game.music_files().size() << "\n";
	std::cout << (missing ? "\nSome levels are missing.\n" : "\nEverything listed in Game.dsb was found.\n");
	return missing ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int cmd_dsb(const fs::path& file)
{
	dsb_script dsb = parse_dsb(read_file(file));
	std::cout << "Memory pools: " << dsb.memory.size() << "\n";
	for(const auto& [role, path] : dsb.directories)
		std::cout << "Directory " << role << " = " << path << "\n";
	for(const auto& archive : dsb.texture_archives)
		std::cout << "Texture archive " << archive << "\n";
	for(const auto& level : dsb.levels)
		std::cout << "Level " << level << "\n";
	for(const auto& v : dsb.vignettes)
	{
		std::cout << "Vignette";
		for(const auto& image : v.images)
			std::cout << " " << image;
		std::cout << "\n";
	}
	if(!dsb.sound_banks.empty())
	{
		std::cout << "Sound banks:";
		for(auto bank : dsb.sound_banks)
			std::cout << " " << bank;
		std::cout << "\n";
	}
	return EXIT_SUCCESS;
}

static int cmd_cnt(const fs::path& file)
{
	cnt_archive archive(file);
	for(const cnt_entry& entry : archive.entries())
		std::cout << entry.path << "  (" << entry.size << " bytes)\n";
	std::cout << archive.entries().size() << " files\n";
	return EXIT_SUCCESS;
}

static int cmd_textures(const fs::path& folder, const fs::path& out)
{
	game_data game = require_game(folder);
	int exported = 0, failed = 0;
	for(const fs::path& archive_path : game.texture_archives())
	{
		cnt_archive archive(archive_path);
		fs::path archive_out = out / archive_path.stem();
		for(const cnt_entry& entry : archive.entries())
		{
			try
			{
				gf_texture texture = decode_gf(archive.read(entry));
				fs::path target = archive_out / fs::path(entry.path).replace_extension(".png");
				fs::create_directories(target.parent_path());
				if(!stbi_write_png(target.string().c_str(), static_cast<int>(texture.width), static_cast<int>(texture.height),
						4, texture.rgba.data(), static_cast<int>(texture.width * 4)))
					throw std::runtime_error("could not write PNG");
				exported++;
			}
			catch(const std::exception& e)
			{
				std::cerr << "FAILED " << archive_path.filename().string() << ":" << entry.path << ": " << e.what() << "\n";
				failed++;
			}
		}
		std::cout << archive_path.filename().string() << " done\n";
	}
	std::cout << exported << " textures exported to " << out.string() << ", " << failed << " failed\n";
	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int cmd_music(const fs::path& folder, const fs::path& out)
{
	game_data game = require_game(folder);
	fs::create_directories(out);
	int exported = 0, failed = 0;
	for(const fs::path& file : game.music_files())
	{
		try
		{
			apm_decoder decoder(read_file(file));
			write_wav(out / file.filename().replace_extension(".wav"), decoder.decode_all(), decoder.channels(), decoder.sample_rate());
			exported++;
		}
		catch(const std::exception& e)
		{
			std::cerr << "FAILED " << file.filename().string() << ": " << e.what() << "\n";
			failed++;
		}
	}
	std::cout << exported << " music files exported to " << out.string() << ", " << failed << " failed\n";
	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int cmd_level(const fs::path& folder, const std::string& name)
{
	game_data game = require_game(folder);
	std::vector<std::string> levels = name == "all" ? game.game_dsb().levels : std::vector<std::string>{ name };

	int failed = 0;
	for(const std::string& level : levels)
	{
		try
		{
			level_files files = load_level_files(game, level);
			const memory_image& image = files.memory;
			memory_image::statistics stats = image.check();
			level_header header = read_level_header(files);
			scene tree(image, header);
			std::vector<texture_info> fix_textures = read_texture_table(files, false);
			std::vector<texture_info> level_textures = read_texture_table(files, true);

			if(levels.size() == 1)
			{
				for(const sna_block* block : image.blocks())
					std::cout << "  block " << block->id.to_string() << std::format("  0x{:08X}  {:>9} bytes\n", block->base, block->data.size());
				std::cout << std::format("  {} Fix actors, {} placed in this level\n", header.fix_actors.size(), header.placements.size());
				for(const fix_actor_placement& p : header.placements)
					std::cout << std::format("    actor at {} +0x{:X}: position {} {} {}\n", p.actor.block->id.to_string(), p.actor.offset,
						p.matrix.position[0], p.matrix.position[1], p.matrix.position[2]);
				for(const texture_info& t : level_textures)
					std::cout << std::format("  texture {:>4}  {:>4}x{:<4}  {:08X}  {}\n", t.slot, t.width, t.height, t.flags, t.name);
			}
			std::cout << std::format("{:<10} {:>7} pointers: {} to data, {} to runtime memory, {} dangling\n",
				level, stats.pointers, stats.into_data, stats.into_runtime, stats.dangling);
			std::cout << std::format("{:<10} scene: {} super objects, {} actors in the dynamic world, {} in the sector tree; {} + {} textures\n",
				"", tree.nodes().size(), tree.node(tree.dynamic_world()).children.size(), tree.subtree_size(tree.father_sector()),
				fix_textures.size(), level_textures.size());
			animation_bank bank = read_animation_bank(files);
			std::vector<placed_geometry> actors = actor_geometry(image, tree, bank);
			std::size_t actor_triangles = 0;
			for(const placed_geometry& p : actors)
				for(const triangle_element& e : p.object.elements)
					actor_triangles += e.triangles.size();
			std::cout << std::format("{:<10} actors: {} animations ({} in Fix), {} meshes and {} triangles at frame 0\n",
				"", bank.animations.size(), bank.fix_animations, actors.size(), actor_triangles);
			failed += stats.dangling > 0;
		}
		catch(const std::exception& e)
		{
			std::cout << level << ": FAILED: " << e.what() << "\n";
			failed++;
		}
	}
	if(levels.size() > 1)
		std::cout << (levels.size() - failed) << " of " << levels.size() << " levels loaded cleanly\n";
	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}

// Describes the first words of a structure: 'p' for a pointer, '.' for zero, 'x' for other data.
static std::string shape(const memory_image& memory, memory_location at, std::uint32_t size)
{
	std::string out;
	for(std::uint32_t o = 0; o < size && at.offset + o + 4 <= at.block->data.size(); o += 4)
	{
		memory_location word{ at.block, at.offset + o };
		out += memory.is_pointer(word) ? 'p' : memory.read_u32(word) == 0 ? '.' : 'x';
	}
	return out;
}

static void dump_gpt(const memory_image& memory, std::span<const std::byte> data, const relocation_table& table, std::size_t file)
{
	pointer_stream stream(data, table, memory, file);
	std::vector<std::size_t> offsets = stream.scan();
	for(std::size_t offset : offsets)
	{
		stream.seek(offset);
		memory_location target = *stream.pointer();
		std::cout << std::format("  0x{:06X} -> {} +0x{:06X}  {}\n", offset, target.block->id.to_string(), target.offset, shape(memory, target, 0x40));
	}
}

static int cmd_gpt(const fs::path& folder, const std::string& name)
{
	game_data game = require_game(folder);
	level_files files = load_level_files(game, name);
	std::cout << "Fix.gpt (" << files.fix_gpt.size() << " bytes)\n";
	dump_gpt(files.memory, files.fix_gpt, files.fix_gpt_table, 0);
	std::cout << files.name << ".gpt (" << files.level_gpt.size() << " bytes)\n";
	dump_gpt(files.memory, files.level_gpt, files.level_gpt_table, 1);
	return EXIT_SUCCESS;
}

static void dump_node(const memory_image& memory, const scene& tree, std::size_t index, int depth)
{
	const scene_node& node = tree.node(index);
	std::cout << std::format("{:{}}type 0x{:X} at {} +0x{:X}", "", depth * 2, node.type, node.at.block->id.to_string(), node.at.offset);
	if(node.data)
		std::cout << std::format("  data {} +0x{:X} {}", node.data->block->id.to_string(), node.data->offset, shape(memory, *node.data, 0x40));
	if(node.matrix && node.matrix->offset + 0x58 <= node.matrix->block->data.size())
	{
		transform t = read_transform(memory, *node.matrix);
		std::cout << std::format("  matrix {} +0x{:X} at ({:.1f}, {:.1f}, {:.1f})", node.matrix->block->id.to_string(), node.matrix->offset,
			t.position[0], t.position[1], t.position[2]);
	}
	else if(node.matrix)
		std::cout << std::format("  matrix {} +0x{:X} (runtime)", node.matrix->block->id.to_string(), node.matrix->offset);
	std::cout << "\n";
	for(std::size_t child : node.children)
		dump_node(memory, tree, child, depth + 1);
}

static int cmd_scene(const fs::path& folder, const std::string& name)
{
	game_data game = require_game(folder);
	level_files files = load_level_files(game, name);
	level_header header = read_level_header(files);
	scene tree(files.memory, header);
	dump_node(files.memory, tree, tree.actual_world(), 0);
	return EXIT_SUCCESS;
}

// Exports a level's static geometry (the sector tree) as Wavefront OBJ with textures as PNG.
static int cmd_obj(const fs::path& folder, const std::string& name, const fs::path& out)
{
	game_data game = require_game(folder);
	level_files files = load_level_files(game, name);
	level_header header = read_level_header(files);
	scene tree(files.memory, header);

	std::vector<cnt_archive> archives;
	for(const fs::path& path : game.texture_archives())
		archives.emplace_back(path);

	fs::create_directories(out / "textures");
	std::ofstream obj(out / (files.name + ".obj"));
	std::ofstream mtl(out / (files.name + ".mtl"));
	obj << "# " << files.name << " static geometry, exported by ortool. Z up in the game, Y up here.\n";
	obj << "mtllib " << files.name << ".mtl\n";

	std::map<std::uint32_t, std::string> materials; // by visual material address
	std::size_t vertex_base = 1, uv_base = 1, objects = 0, triangles = 0, skipped = 0, moved = 0, missing_textures = 0;

	auto material_name = [&](const std::optional<visual_material>& m) -> std::string {
		if(!m)
			return "none";
		if(auto it = materials.find(m->at.address()); it != materials.end())
			return it->second;
		std::string id = std::format("m{}", materials.size());
		materials[m->at.address()] = id;
		mtl << "newmtl " << id << "\n" << std::format("Kd {} {} {}\n", m->diffuse[0], m->diffuse[1], m->diffuse[2]);
		if(m->texture)
		{
			std::string png = "textures/" + fs::path(m->texture->cnt_path()).filename().replace_extension(".png").string();
			const cnt_entry* entry = nullptr;
			const cnt_archive* source = nullptr;
			for(const cnt_archive& a : archives)
			{
				if((entry = a.find(m->texture->cnt_path())))
				{
					source = &a;
					break;
				}
			}
			if(entry)
			{
				gf_texture t = decode_gf(source->read(*entry));
				stbi_write_png((out / png).string().c_str(), int(t.width), int(t.height), 4, t.rgba.data(), int(t.width * 4));
				mtl << "map_Kd " << png << "\n";
				if(t.channels == 4)
					mtl << "map_d " << png << "\n";
			}
			else
				missing_textures++;
		}
		return id;
	};

	// Scenery, then actors at the first frame of their animation.
	std::vector<placed_geometry> geometry = static_geometry(files.memory, tree);
	std::size_t scenery = geometry.size();
	std::vector<placed_geometry> actors = actor_geometry(files.memory, tree, read_animation_bank(files));
	geometry.insert(geometry.end(), std::make_move_iterator(actors.begin()), std::make_move_iterator(actors.end()));
	for(std::size_t index = 0; index < geometry.size(); index++)
	{
		const placed_geometry& placed = geometry[index];
		const geometric_object& geo = placed.object;
		moved += placed.world != affine{};
		obj << std::format("o {}_{:X}\n", index < scenery ? "ipo" : "actor", tree.node(placed.node).at.address());
		for(const vec3& v : geo.vertices)
		{
			vec3 p = placed.world.apply(v);
			obj << std::format("v {} {} {}\n", p[0], p[2], -p[1]);
		}
		std::size_t uvs = 0;
		for(const triangle_element& e : geo.elements)
		{
			for(const vec2& uv : e.uvs)
				obj << std::format("vt {} {}\n", uv[0], uv[1]); // OBJ counts v up from the bottom too
			obj << "usemtl " << material_name(e.material) << "\n";
			for(std::size_t t = 0; t < e.triangles.size(); t++)
			{
				obj << "f";
				for(int k = 0; k < 3; k++)
					obj << std::format(" {}/{}", vertex_base + e.triangles[t][k], uv_base + uvs + e.uv_triangles[t][k]);
				obj << "\n";
			}
			uvs += e.uvs.size();
			triangles += e.triangles.size();
		}
		vertex_base += geo.vertices.size();
		uv_base += uvs;
		skipped += geo.skipped_types.size();
		objects++;
	}

	std::cout << std::format("{} objects, {} triangles, {} materials, {} not at the origin, {} elements of other types skipped, {} textures missing\n",
		objects, triangles, materials.size(), moved, skipped, missing_textures);
	std::cout << "Written to " << (out / (files.name + ".obj")).string() << "\n";
	return EXIT_SUCCESS;
}

// Lists the level's actors with the pointers of their 3D data, for exploring.
static int cmd_actors(const fs::path& folder, const std::string& name)
{
	game_data game = require_game(folder);
	level_files files = load_level_files(game, name);
	level_header header = read_level_header(files);
	scene tree(files.memory, header);
	const memory_image& memory = files.memory;
	auto where = [](const std::optional<memory_location>& at) {
		return at ? std::format("{} +0x{:X}", at->block->id.to_string(), at->offset) : std::string("null");
	};
	for(std::size_t child : tree.node(tree.dynamic_world()).children)
	{
		const scene_node& node = tree.node(child);
		if(!node.data)
			continue;
		auto p3d = memory.follow(*node.data);
		std::cout << std::format("actor {} +0x{:X}  3d {}", node.at.block->id.to_string(), node.at.offset, where(p3d));
		if(p3d)
		{
			for(std::uint32_t o = 0; o < 0x20 && p3d->offset + o + 4 <= p3d->block->data.size(); o += 4)
				std::cout << "  " << where(memory.follow(*p3d + o));
		}
		std::cout << "\n";
	}
	return EXIT_SUCCESS;
}

// Prints each visible actor's animation and the pose of its channels at frame 0.
static int cmd_pose(const fs::path& folder, const std::string& name)
{
	game_data game = require_game(folder);
	level_files files = load_level_files(game, name);
	level_header header = read_level_header(files);
	scene tree(files.memory, header);
	animation_bank bank = read_animation_bank(files);
	for(std::size_t node : tree.node(tree.dynamic_world()).children)
	{
		if(tree.node(node).type != super_object_type::actor)
			continue;
		actor a = read_actor(files.memory, tree, bank, node);
		if(!a.animation || a.objects.empty())
			continue;
		vec3 at = world_transform(files.memory, tree, node).apply({ 0, 0, 0 });
		std::cout << std::format("actor 0x{:X} at ({:.1f}, {:.1f}, {:.1f}): animation {}, {} objects\n",
			tree.node(node).at.address(), at[0], at[1], at[2], *a.animation, a.objects.size());
		for(const channel_pose& c : pose(bank, *a.animation, 0))
		{
			const auto& m = c.local.m;
			float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
				+ m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
			// Rotation as axis and angle, scale as the column lengths.
			float sx = std::sqrt(m[0][0] * m[0][0] + m[1][0] * m[1][0] + m[2][0] * m[2][0]);
			float sy = std::sqrt(m[0][1] * m[0][1] + m[1][1] * m[1][1] + m[2][1] * m[2][1]);
			float sz = std::sqrt(m[0][2] * m[0][2] + m[1][2] * m[1][2] + m[2][2] * m[2][2]);
			float trace = m[0][0] / sx + m[1][1] / sy + m[2][2] / sz;
			float angle = std::acos(std::clamp((trace - 1) / 2, -1.0f, 1.0f)) * 57.29578f;
			float ax = m[2][1] / sy - m[1][2] / sz, ay = m[0][2] / sz - m[2][0] / sx, az = m[1][0] / sx - m[0][1] / sy;
			float al = std::sqrt(ax * ax + ay * ay + az * az);
			if(al > 0)
				ax /= al, ay /= al, az /= al;
			std::cout << std::format("  channel {:>3} parent {:>3} object {:>3}  pos ({:.2f}, {:.2f}, {:.2f})  det {:.2f}  rot {:.0f} deg about ({:.2f}, {:.2f}, {:.2f})  scale ({:.2f}, {:.2f}, {:.2f})",
				c.id, c.parent ? std::to_string(*c.parent) : "-", c.object ? std::to_string(*c.object) : "-",
				m[0][3], m[1][3], m[2][3], det, angle, ax, ay, az, sx, sy, sz);
			if(c.object && *c.object < a.objects.size() && a.objects[*c.object].physical_object)
			{
				if(auto geo = physical_object_geometry(files.memory, *a.objects[*c.object].physical_object))
				{
					geometric_object g = read_geometric_object(files.memory, *geo);
					std::cout << std::format("  {} vertices, {} elements", g.vertices.size(), g.elements.size());
					float lo = 1e9f, hi = -1e9f;
					for(const vec3& v : g.vertices)
						lo = std::min(lo, v[2]), hi = std::max(hi, v[2]);
					std::cout << std::format(" z {:.2f}..{:.2f}", lo, hi);
					for(const triangle_element& e : g.elements)
					{
						if(e.material)
							std::cout << std::format(" [{:08X} {}]", e.material->flags, e.material->texture ? e.material->texture->name : "untextured");
					}
					if(!g.skipped_types.empty())
						std::cout << std::format(", {} skipped (type {})", g.skipped_types.size(), g.skipped_types.front());
				}
				if(a.objects[*c.object].scale)
				{
					const vec3& s = *a.objects[*c.object].scale;
					std::cout << std::format("  entry scale ({:.2f}, {:.2f}, {:.2f})", s[0], s[1], s[2]);
				}
			}
			std::cout << "\n";
		}
	}
	return EXIT_SUCCESS;
}

static int cmd_peek(const fs::path& folder, const std::string& name, const std::string& block, const std::string& offset, const std::string& words)
{
	game_data game = require_game(folder);
	memory_image memory = load_level_memory(game, name);
	block_id id{ std::uint8_t(std::stoul(block.substr(0, 2), nullptr, 16)), std::uint8_t(std::stoul(block.substr(3, 2), nullptr, 16)) };
	const sna_block* found = memory.find(id);
	if(!found)
		throw format_error("no block " + block);
	memory_location at{ found, std::uint32_t(std::stoul(offset, nullptr, 16)) };
	for(unsigned long i = 0; i < std::stoul(words) && at.offset + 4 <= found->data.size(); i++, at.offset += 4)
	{
		std::uint32_t value = memory.read_u32(at);
		float f;
		std::memcpy(&f, &value, 4);
		std::cout << std::format("  +0x{:06X}  {:08X}  {:<12g}", at.offset, value, f);
		if(auto target = memory.follow(at))
			std::cout << std::format("  -> {} +0x{:06X}", target->block->id.to_string(), target->offset);
		std::cout << "\n";
	}
	return EXIT_SUCCESS;
}

int main(int argc, char** argv)
{
	std::vector<std::string> args(argv + 1, argv + argc);
	if(args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h")
	{
		usage();
		return args.empty() ? EXIT_FAILURE : EXIT_SUCCESS;
	}

	try
	{
		const std::string& cmd = args[0];
		if(cmd == "info" && args.size() == 2)
			return cmd_info(args[1]);
		if(cmd == "dsb" && args.size() == 2)
			return cmd_dsb(args[1]);
		if(cmd == "cnt" && args.size() == 2)
			return cmd_cnt(args[1]);
		if(cmd == "textures" && args.size() == 3)
			return cmd_textures(args[1], args[2]);
		if(cmd == "music" && args.size() == 3)
			return cmd_music(args[1], args[2]);
		if(cmd == "level" && args.size() == 3)
			return cmd_level(args[1], args[2]);
		if(cmd == "gpt" && args.size() == 3)
			return cmd_gpt(args[1], args[2]);
		if(cmd == "obj" && args.size() == 4)
			return cmd_obj(args[1], args[2], args[3]);
		if(cmd == "pose" && args.size() == 3)
			return cmd_pose(args[1], args[2]);
		if(cmd == "actors" && args.size() == 3)
			return cmd_actors(args[1], args[2]);
		if(cmd == "scene" && args.size() == 3)
			return cmd_scene(args[1], args[2]);
		if(cmd == "peek" && args.size() == 6)
			return cmd_peek(args[1], args[2], args[3], args[4], args[5]);
	}
	catch(const std::exception& e)
	{
		std::cerr << "error: " << e.what() << "\n";
		return EXIT_FAILURE;
	}

	usage();
	return EXIT_FAILURE;
}
