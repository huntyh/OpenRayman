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
#include <openrayman/light.h>
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
		"  lights <game folder> <level> [x,y,z] Sectors (box, light kinds), every static light, each actor's sector;\n"
		"                                      with a point: the static vertices near it and their light\n"
		"  peek <game folder> <level> <block> <hex offset> <words>\n"
		"                                      Print memory words, e.g. peek ... Learn_10 05/02 300 16\n"
		"  motion <game folder> <level>       For each actor: its animation and how far its parts move\n"
		"  states <game folder> <level> <node>\n"
		"                                      The states of an actor's family and their animations\n"
		"  anim <game folder> <level> <index> <frame>\n"
		"                                      Print the objects an animation shows at a frame\n"
		"  refs <game folder> <level> <block> <hex offset>\n"
		"                                      List the pointers that lead to a place in memory\n"
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
			std::vector<placed_geometry> static_geo = static_geometry(image, tree);
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
					std::cout << std::format("  texture {:>4}  {} +0x{:06X}  {:>4}x{:<4}  {:08X} {:02X}  {}\n", t.slot, t.at.block->id.to_string(), t.at.offset, t.width, t.height, t.flags, t.tiling, t.name);
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
			std::size_t sprites = 0;
			for(const auto* list : { &actors, &static_geo })
			{
				for(const placed_geometry& p : *list)
				{
					for(const sprite& s : p.object.sprites)
					{
						sprites++;
						if(levels.size() == 1)
						{
							vec3 c = p.world.apply(p.object.vertices[s.vertex]);
							std::cout << std::format("  sprite at ({:.1f}, {:.1f}, {:.1f}) half size {:.2f} x {:.2f}  {}\n", c[0], c[1], c[2],
								s.half_size[0] * p.world.uniform_scale(), s.half_size[1] * p.world.uniform_scale(), s.material && s.material->texture ? s.material->texture->name : "untextured");
							if(s.material)
							{
								const visual_material& m = *s.material;
								std::cout << std::format("    material {:08X} ambient {:.2f} {:.2f} {:.2f} {:.2f} diffuse {:.2f} {:.2f} {:.2f} {:.2f} specular {:.2f} {:.2f} {:.2f} {:.2f}\n",
									m.flags, m.ambient[0], m.ambient[1], m.ambient[2], m.ambient[3], m.diffuse[0], m.diffuse[1], m.diffuse[2], m.diffuse[3],
									m.specular[0], m.specular[1], m.specular[2], m.specular[3]);
							}
						}
					}
				}
			}
			std::cout << std::format("{:<10} sprites: {}\n", "", sprites);
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
					{
						std::cout << std::format(", {} skipped (type {})", g.skipped_types.size(), g.skipped_types.front());
						if(auto list = files.memory.follow(*geo + 0x14))
							if(auto element = files.memory.follow(*list))
								std::cout << std::format(" first element at {} +0x{:X}", element->block->id.to_string(), element->offset);
					}
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

// Prints which objects each channel of an animation shows at a frame (an index into the actor's
// object list; "-" means hidden).
static int cmd_anim(const fs::path& folder, const std::string& name, const std::string& index, const std::string& frame)
{
	game_data game = require_game(folder);
	level_files files = load_level_files(game, name);
	animation_bank bank = read_animation_bank(files);
	std::size_t a = std::stoul(index);
	if(a >= bank.animations.size())
		throw format_error(std::format("animation {} is beyond the bank's {}", a, bank.animations.size()));
	std::cout << std::format("animation {}: {} frames, {} channels\n", a, bank.animations[a].frames, bank.animations[a].channels);
	for(const channel_pose& c : pose(bank, a, std::uint32_t(std::stoul(frame))))
	{
		const auto& m = c.local.m;
		std::cout << std::format("  channel {:>3} parent {:>3} object {:>3}  pos ({:.2f}, {:.2f}, {:.2f})\n", c.id,
			c.parent ? std::to_string(*c.parent) : "-", c.object ? std::to_string(*c.object) : "-", m[0][3], m[1][3], m[2][3]);
	}
	return EXIT_SUCCESS;
}

// Lists every pointer in memory that leads to <block> <hex offset>, to find what uses a structure.
static int cmd_refs(const fs::path& folder, const std::string& name, const std::string& block, const std::string& offset)
{
	game_data game = require_game(folder);
	memory_image memory = load_level_memory(game, name);
	block_id id{ std::uint8_t(std::stoul(block.substr(0, 2), nullptr, 16)), std::uint8_t(std::stoul(block.substr(3, 2), nullptr, 16)) };
	const sna_block* target = memory.find(id);
	if(!target)
		throw format_error("no block " + block);
	std::uint32_t wanted = std::uint32_t(std::stoul(offset, nullptr, 16));
	for(const sna_block* b : memory.blocks())
	{
		for(std::uint32_t o = 0; o + 4 <= b->data.size(); o += 4)
		{
			auto to = memory.follow({ b, o });
			if(to && to->block == target && to->offset == wanted)
				std::cout << std::format("  {} +0x{:06X}\n", b->id.to_string(), o);
		}
	}
	return EXIT_SUCCESS;
}

// For each actor: its animation, and how far its parts travel over one loop (to find what moves).
static int cmd_motion(const fs::path& folder, const std::string& name)
{
	game_data game = require_game(folder);
	level_files files = load_level_files(game, name);
	level_header header = read_level_header(files);
	scene tree(files.memory, header);
	animation_bank bank = read_animation_bank(files);
	actor_set set = read_actors(files.memory, tree, bank);
	for(const actor_set::entry& actor : set.actors)
	{
		const auto& p = actor.world.m;
		if(!actor.animation)
		{
			std::vector<posed_part> parts;
			pose_actor(actor, bank, 0, parts);
			std::cout << std::format("node {:>4} at ({:7.1f}, {:7.1f}, {:6.1f}) no animation, {} objects, {} parts\n",
				actor.node, p[0][3], p[1][3], p[2][3], actor.meshes.size(), parts.size());
			continue;
		}
		const animation& a = bank.animations[*actor.animation];
		std::vector<posed_part> first;
		pose_actor(actor, bank, 0, first);
		float most = 0;
		for(std::uint32_t f = 1; f < a.frames; f++)
		{
			std::vector<posed_part> parts;
			pose_actor(actor, bank, f, parts);
			for(std::size_t i = 0; i < std::min(parts.size(), first.size()); i++)
			{
				for(int r = 0; r < 3; r++)
					most = std::max(most, std::abs(parts[i].world.m[r][3] - first[i].world.m[r][3]));
			}
		}
		std::cout << std::format("node {:>4} at ({:7.1f}, {:7.1f}, {:6.1f}) animation {:>3}: {:>3} frames at {:>2}/s, {:>2} parts, moves up to {:.3f}\n",
			actor.node, p[0][3], p[1][3], p[2][3], *actor.animation, a.frames, a.speed, first.size(), most);
	}
	return EXIT_SUCCESS;
}

// The states of an actor's family, in order, with the animation each one plays.
static int cmd_states(const fs::path& folder, const std::string& name, const std::string& node_text)
{
	game_data game = require_game(folder);
	level_files files = load_level_files(game, name);
	level_header header = read_level_header(files);
	scene tree(files.memory, header);
	animation_bank bank = read_animation_bank(files);
	const memory_image& memory = files.memory;
	const scene_node& n = tree.node(std::stoul(node_text));
	auto data = n.data ? memory.follow(*n.data) : std::nullopt;               // 3D data
	auto current = data ? memory.follow(*data + 4) : std::nullopt;
	auto family = data ? memory.follow(*data + 0x14) : std::nullopt;
	if(!family)
		throw format_error("this super object has no actor family");
	auto state = memory.follow(*family + 0x10);
	std::uint32_t count = 0;
	std::memcpy(&count, family->block->data.data() + family->offset + 0x18, 4);
	for(std::uint32_t i = 0; state && i < count; i++, state = memory.follow(*state))
	{
		std::cout << std::format("state {:>2} at {} +0x{:X}{}", i, state->block->id.to_string(), state->offset, current && *current == *state ? " (current)" : "");
		if(auto reference = memory.follow(*state + 4))
		{
			std::uint16_t index = 0;
			std::memcpy(&index, reference->block->data.data() + reference->offset + 0x18, 2);
			if(index < bank.animations.size())
				std::cout << std::format(": animation {}, {} frames, {} channels", index, bank.animations[index].frames, bank.animations[index].channels);
		}
		std::cout << '\n';
	}
	return EXIT_SUCCESS;
}

// Every sector with its box and lights, and which sector each actor stands in.
static int cmd_lights(const fs::path& folder, const std::string& name, std::optional<vec3> probe)
{
	game_data game = require_game(folder);
	level_files files = load_level_files(game, name);
	level_header header = read_level_header(files);
	scene tree(files.memory, header);
	level_lights lights = read_lights(files.memory, tree);
	for(std::size_t s = 0; s < lights.sectors.size(); s++)
	{
		const sector& c = lights.sectors[s];
		std::cout << std::format("sector {:>2} node {:>3}{} ({:.0f},{:.0f},{:.0f})..({:.0f},{:.0f},{:.0f})", s, c.node, c.is_virtual ? " virtual" : "",
			c.min[0], c.min[1], c.min[2], c.max[0], c.max[1], c.max[2]);
		std::map<std::pair<int, int>, int> kinds;
		for(std::size_t i : c.lights)
			kinds[{ int(lights.lights[i].type), lights.lights[i].targets }]++;
		for(auto [k, n] : kinds)
			std::cout << std::format(" t{}/{}x{}", k.first, k.second, n);
		std::cout << '\n';
	}
	for(std::size_t i = 0; i < lights.lights.size(); i++)
	{
		const light& l = lights.lights[i];
		vec3 p = l.position();
		std::cout << std::format("light {:>3} type {} targets {} on {} painting {} channels {} ({:.1f},{:.1f},{:.1f}) near {:.2f} far {:.2f} color {:.2f},{:.2f},{:.2f},{:.2f}\n", i, int(l.type), l.targets, l.on, l.painting, int(l.channels),
			p[0], p[1], p[2], l.near, l.far, l.color[0], l.color[1], l.color[2], l.color[3]);
	}
	for(std::size_t n = 0; n < tree.nodes().size(); n++)
	{
		if(tree.node(n).type != super_object_type::actor)
			continue;
		affine world = world_transform(files.memory, tree, n);
		vec3 p{ world.m[0][3], world.m[1][3], world.m[2][3] };
		auto s = lights.sector_at(p);
		std::cout << std::format("actor node {:>3} at ({:.1f},{:.1f},{:.1f}) sector {}\n", n, p[0], p[1], p[2], s ? std::to_string(*s) : "none");
	}
	// With a point: the static vertices within 3 units of it, their sector and the light on them.
	if(probe)
	{
		for(const placed_geometry& placed : static_geometry(files.memory, tree))
		{
			auto s = lights.sector_of(tree, placed.node);
			for(std::size_t v = 0; v < placed.object.vertices.size(); v++)
			{
				vec3 p = placed.world.apply(placed.object.vertices[v]);
				float d = std::sqrt((p[0] - (*probe)[0]) * (p[0] - (*probe)[0]) + (p[1] - (*probe)[1]) * (p[1] - (*probe)[1]) + (p[2] - (*probe)[2]) * (p[2] - (*probe)[2]));
				if(d > 3 || !s)
					continue;
				std::vector<const light*> list;
				for(std::size_t i : lights.sectors[*s].lights)
					list.push_back(&lights.lights[i]);
				vec3 n = v < placed.object.normals.size() ? placed.object.normals[v] : vec3{ 0, 0, 1 };
				rgba sum = light_at(list, light_target::scenery, p, n);
				std::cout << std::format("vertex node {} ({:.2f},{:.2f},{:.2f}) d {:.2f} sector {} normal ({:.2f},{:.2f},{:.2f}) light {:.2f},{:.2f},{:.2f},{:.2f}\n",
					placed.node, p[0], p[1], p[2], d, *s, n[0], n[1], n[2], sum[0], sum[1], sum[2], sum[3]);
			}
		}
		return EXIT_SUCCESS;
	}
	// The static geometry's material colours, which the lights are combined with.
	std::map<std::string, std::size_t> colors;
	for(const placed_geometry& placed : static_geometry(files.memory, tree))
	{
		for(const triangle_element& e : placed.object.elements)
		{
			if(e.material)
			{
				const visual_material& m = *e.material;
				colors[std::format("ambient {:.2f},{:.2f},{:.2f},{:.2f} diffuse {:.2f},{:.2f},{:.2f},{:.2f}", m.ambient[0], m.ambient[1], m.ambient[2], m.ambient[3],
					m.diffuse[0], m.diffuse[1], m.diffuse[2], m.diffuse[3])] += e.triangles.size();
			}
		}
	}
	for(const auto& [text, triangles] : colors)
		std::cout << "material " << text << ": " << triangles << " triangles\n";
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
		if(cmd == "motion" && args.size() == 3)
			return cmd_motion(args[1], args[2]);
		if(cmd == "states" && args.size() == 4)
			return cmd_states(args[1], args[2], args[3]);
		if(cmd == "anim" && args.size() == 5)
			return cmd_anim(args[1], args[2], args[3], args[4]);
		if(cmd == "refs" && args.size() == 5)
			return cmd_refs(args[1], args[2], args[3], args[4]);
		if(cmd == "lights" && args.size() == 3)
			return cmd_lights(args[1], args[2], std::nullopt);
		if(cmd == "lights" && args.size() == 4)
		{
			vec3 p{};
			std::sscanf(args[3].c_str(), "%f,%f,%f", &p[0], &p[1], &p[2]);
			return cmd_lights(args[1], args[2], p);
		}
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
