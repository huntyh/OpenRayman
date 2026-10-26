// ortool: inspects and exports Rayman 2 data, mainly for checking that the readers work.

#include <openrayman/apm_audio.h>
#include <openrayman/cnt_archive.h>
#include <openrayman/dsb_script.h>
#include <openrayman/file.h>
#include <openrayman/game_data.h>
#include <openrayman/gf_texture.h>
#include <openrayman/memory_image.h>
#include <openrayman/level_header.h>
#include <openrayman/pointer_stream.h>
#include <openrayman/scene.h>
#include <openrayman/texture_table.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
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
				for(const texture_info& t : level_textures)
					std::cout << std::format("  texture {:>4}  {:>4}x{:<4}  {:08X}  {}\n", t.slot, t.width, t.height, t.flags, t.name);
			}
			std::cout << std::format("{:<10} {:>7} pointers: {} to data, {} to runtime memory, {} dangling\n",
				level, stats.pointers, stats.into_data, stats.into_runtime, stats.dangling);
			std::cout << std::format("{:<10} scene: {} super objects, {} actors in the dynamic world, {} in the sector tree; {} + {} textures\n",
				"", tree.nodes().size(), tree.node(tree.dynamic_world()).children.size(), tree.subtree_size(tree.father_sector()),
				fix_textures.size(), level_textures.size());
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
