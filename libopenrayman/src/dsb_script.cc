#include <openrayman/dsb_script.h>
#include <openrayman/binary_reader.h>
#include <openrayman/error.h>

#include <format>

// A decoded DSB is a list of sections: a u32 section id, entries of the form
// (u32 entry id, arguments...), then a u32 0xFFFF terminator. Strings are a u16
// length (including the NUL) followed by the characters.

namespace openrayman
{
	constexpr std::uint32_t dsb_end = 0xFFFF;

	std::vector<std::byte> decode_dsb(std::span<const std::byte> data)
	{
		binary_reader reader(data);
		std::uint32_t key = reader.u32();

		std::vector<std::byte> result(data.begin() + 4, data.end());
		for(std::byte& b : result)
		{
			b ^= static_cast<std::byte>((key >> 8) & 0xFF);
			key = 16807 * (key ^ 0x75BD924u) - 0x7FFFFFFFu * ((key ^ 0x75BD924u) / 0x1F31Du);
		}
		return result;
	}

	static std::string read_string(binary_reader& reader)
	{
		return reader.string(reader.u16());
	}

	static std::array<float, 4> read_color(binary_reader& reader)
	{
		return { reader.f32(), reader.f32(), reader.f32(), reader.f32() };
	}

	[[noreturn]] static void unknown(const char* section, std::uint32_t id, std::size_t position)
	{
		throw format_error(std::format("unknown DSB {} entry 0x{:X} at offset 0x{:X}", section, id, position - 4));
	}

	// "Random<VigLoadLearn,1,1,VigLoadMisc,1,6>" -> { "VigLoadLearn", "VigLoadMisc" }
	static std::vector<std::string> expand_images(const std::string& spec)
	{
		const std::string prefix = "Random<";
		if(!spec.starts_with(prefix))
			return { spec };

		std::vector<std::string> parts;
		std::string inner = spec.substr(prefix.size(), spec.find('>') - prefix.size());
		std::size_t start = 0;
		while(start <= inner.size())
		{
			std::size_t comma = inner.find(',', start);
			if(comma == std::string::npos)
				comma = inner.size();
			parts.push_back(inner.substr(start, comma - start));
			start = comma + 1;
		}

		// Parts come in triplets of (name, min, max).
		std::vector<std::string> images;
		for(std::size_t n = 0; n < parts.size(); n += 3)
			images.push_back(parts[n]);
		return images;
	}

	static void parse_memory(binary_reader& r, dsb_script& script)
	{
		for(std::uint32_t id = r.u32(); id != dsb_end; id = r.u32())
		{
			if(id == 0x10)
			{
				// Two values for this pool, only the first one has been seen used.
				script.memory[id] = r.u32();
				r.u32();
			}
			else
				script.memory[id] = r.u32();
		}
	}

	static void parse_levels(binary_reader& r, dsb_script& script)
	{
		r.u32(); // level count, always 0 in practice
		for(std::uint32_t id = r.u32(); id != dsb_end; id = r.u32())
		{
			if(id != 0x1F)
				unknown("level list", id, r.position());
			script.levels.push_back(read_string(r));
		}
	}

	static void parse_directories(binary_reader& r, dsb_script& script)
	{
		for(std::uint32_t id = r.u32(); id != dsb_end; id = r.u32())
		{
			std::string path = read_string(r);
			switch(id)
			{
				case 0x29: script.directories["dll"] = path; break;
				case 0x2A: script.directories["root"] = path; break;
				case 0x2B: script.directories["world"] = path; break;
				case 0x2C: script.directories["levels"] = path; break;
				case 0x2D: script.directories["sound"] = path; break;
				case 0x2E: script.directories["saves"] = path; break;
				case 0x30: script.directories["vignette"] = path; break;
				case 0x31: script.directories["options"] = path; break;
				default: script.directories[std::format("0x{:X}", id)] = path; break;
			}
		}
	}

	static void parse_texture_archives(binary_reader& r, dsb_script& script)
	{
		for(std::uint32_t id = r.u32(); id != dsb_end; id = r.u32())
			script.texture_archives.push_back(read_string(r));
	}

	static void parse_vignette(binary_reader& r, dsb_script& script)
	{
		dsb_vignette& v = script.vignettes.emplace_back();
		for(std::uint32_t id = r.u32(); id != dsb_end; id = r.u32())
		{
			switch(id)
			{
				case 0x47:
				case 0x48:
					v.images = expand_images(read_string(r));
					break;
				case 0x4B: // display now
				case 0x50: // add the loading bar
					break;
				case 0x4C: v.bar_outside_color = read_color(r); break;
				case 0x4D: v.bar_inside_color = read_color(r); break;
				case 0x4E:
					for(auto& c : v.bar_corner_colors)
						c = read_color(r);
					break;
				case 0x4F:
					for(auto& n : v.bar_rect)
						n = r.i32();
					break;
				case 0x51: v.bar_max = r.i32(); break;
				default: unknown("vignette", id, r.position());
			}
		}
	}

	static void parse_options(binary_reader& r, dsb_script& script)
	{
		for(std::uint32_t id = r.u32(); id != dsb_end; id = r.u32())
		{
			switch(id)
			{
				case 0x65: script.default_options_file = read_string(r); break;
				case 0x66: script.current_options_file = read_string(r); break;
				case 0x67:
					// Three option strings, e.g. "ON", "2", "95". Meaning unknown.
					for(int n = 0; n < 3; n++)
						read_string(r);
					break;
				default: unknown("options", id, r.position());
			}
		}
	}

	static void parse_sound_banks(binary_reader& r, dsb_script& script)
	{
		for(std::uint32_t id = r.u32(); id != dsb_end; id = r.u32())
		{
			switch(id)
			{
				case 0x5D:
				{
					std::uint32_t count = r.u32();
					for(std::uint32_t n = 0; n < count; n++)
						script.sound_banks.push_back(r.u32());
					break;
				}
				case 0x5E: script.sound_banks.push_back(r.u32()); break;
				default: unknown("sound bank", id, r.position());
			}
		}
	}

	// Sections whose meaning isn't known yet: a flat list of u32 values until the terminator.
	static void skip_flat(binary_reader& r)
	{
		while(r.u32() != dsb_end) {}
	}

	dsb_script parse_dsb(std::span<const std::byte> data)
	{
		std::vector<std::byte> decoded = decode_dsb(data);
		binary_reader r(decoded);
		dsb_script script;

		while(r.remaining() >= 4)
		{
			std::uint32_t section = r.u32();
			switch(section)
			{
				case 0x00: parse_memory(r, script); break;
				case 0x1E: parse_levels(r, script); break;
				case 0x20: skip_flat(r); break;
				case 0x28: parse_directories(r, script); break;
				case 0x40: parse_texture_archives(r, script); break;
				case 0x46: parse_vignette(r, script); break;
				case 0x5A: skip_flat(r); break; // levels only: entry 0x5B with one value, e.g. 40
				case 0x5C: parse_sound_banks(r, script); break;
				case 0x64: parse_options(r, script); break;
				case 0x6E: skip_flat(r); break;
				case dsb_end: break; // stray terminator between sections
				default: unknown("section", section, r.position());
			}
		}
		return script;
	}
}
