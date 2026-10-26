// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/animation.h>
#include <openrayman/binary_reader.h>
#include <openrayman/error.h>
#include <openrayman/pointer_stream.h>

#include <format>

namespace openrayman
{
	namespace
	{
		// Where the bank's 12 array descriptions start. The fields before them aren't decoded yet,
		// but have the same size in every file: checked for Fix.gpt and all 70 level GPTs, whose
		// counts must continue exactly from Fix's.
		constexpr std::size_t fix_bank_offset = 0xCC0;
		constexpr std::size_t level_bank_offset = 0xF6C;

		// The 12 arrays, in file order, with element sizes. Each one starts 4-byte aligned.
		enum array_index
		{
			a_animations, a_vectors, a_quaternions, a_hierarchies, a_objects, a_frames,
			a_channels, a_ntto_choices, a_frame_keys, a_keyframes, a_events, a_morphs,
			array_count
		};
		constexpr std::size_t element_size[array_count] = { 0x38, 12, 8, 4, 6, 10, 16, 2, 4, 36, 12, 8 };

		struct stack
		{
			std::uint32_t count = 0;    // elements in use, Fix's and the level's
			std::uint32_t reserved = 0; // capacity the engine reserves
			std::uint32_t in_fix = 0;   // how many of them are Fix's
		};

		stack read_stack(binary_reader& r)
		{
			stack s;
			r.u32(); // usually count - 1; meaning unknown
			s.count = r.u32();
			s.reserved = r.u32();
			s.in_fix = r.u32();
			if(s.count > s.reserved || s.in_fix > s.count)
				throw format_error("animation bank array description is inconsistent");
			return s;
		}

		// Reads `count` elements of array `which` and appends them to the bank.
		void read_elements(animation_bank& bank, int which, binary_reader& r, std::size_t count)
		{
			for(std::size_t i = 0; i < count; i++)
			{
				switch(which)
				{
					case a_animations:
					{
						animation a;
						a.speed = r.u16();
						a.vectors = r.u16();
						a.quaternions = r.u16();
						a.hierarchies = r.u16();
						a.ntto = r.u16();
						a.ntto_per_channel = r.u16();
						a.channels = r.u16();
						a.frames = r.u16();
						a.keyframes = r.u16();
						r.u16();
						a.events = r.u16();
						r.skip(2 * 8); // unknowns, a second speed, second start indices, the morph count
						a.start_vectors = r.u16();
						a.start_quaternions = r.u16();
						a.start_hierarchies = r.u16();
						a.start_ntto = r.u16();
						a.start_frames = r.u16();
						a.start_channels = r.u16();
						a.start_events = r.u16();
						a.start_morphs = r.u16();
						r.u16(); // padding
						bank.animations.push_back(a);
						break;
					}
					case a_vectors:
					{
						float x = r.f32(), y = r.f32();
						bank.vectors.push_back({ x, y, r.f32() });
						break;
					}
					case a_quaternions:
					{
						std::array<float, 4> q;
						for(float& f : q)
							f = r.i16() / 32767.0f;
						bank.quaternions.push_back(q);
						break;
					}
					case a_hierarchies:
					{
						std::int16_t child = r.i16();
						bank.hierarchies.push_back({ child, r.i16() });
						break;
					}
					case a_objects:
					{
						animation_object o;
						o.flags = r.u16();
						o.object = r.u16();
						r.skip(2);
						bank.objects.push_back(o);
						break;
					}
					case a_frames:
					{
						animation_frame f;
						f.quaternion = r.u16();
						f.vector = r.u16();
						f.hierarchy_count = r.u16();
						f.hierarchy_start = r.u16();
						f.ntto = r.u16();
						bank.frames.push_back(f);
						break;
					}
					case a_channels:
					{
						animation_channel c;
						r.u16();
						c.id = r.i16();
						c.vector = r.u16();
						c.ntto = r.u16();
						c.frame_keys = r.u32();
						c.keyframe = r.u32();
						bank.channels.push_back(c);
						break;
					}
					case a_ntto_choices:
						bank.ntto_choices.push_back(r.u16());
						break;
					case a_frame_keys:
						bank.frame_keys.push_back(r.u32());
						break;
					case a_keyframes:
					{
						animation_keyframe k;
						r.skip(12); // a position, unused so far
						k.position_multiplier = r.f32();
						k.frame = r.u16();
						k.flags = r.u16();
						k.quaternion = r.u16();
						r.u16(); // a second quaternion
						k.scale_vector = r.u16();
						k.position_vector = r.u16();
						r.skip(8);
						bank.keyframes.push_back(k);
						break;
					}
					default:
						r.skip(element_size[which]);
						break;
				}
			}
		}
	}

	animation_bank read_animation_bank(const level_files& files)
	{
		animation_bank bank;
		const memory_image& memory = files.memory;

		// Fix: the descriptions, then one pointer per array into Fix's memory.
		pointer_stream fix(files.fix_gpt, files.fix_gpt_table, memory, 0);
		fix.skip_to(fix_bank_offset);
		stack fix_stacks[array_count];
		for(stack& s : fix_stacks)
			s = read_stack(fix);
		for(int which = 0; which < array_count; which++)
		{
			auto at = fix.pointer();
			if(!at)
			{
				if(fix_stacks[which].count)
					throw format_error("Fix animation array without data");
				continue;
			}
			std::size_t bytes = fix_stacks[which].count * element_size[which];
			if(std::size_t(at->offset) + bytes > at->block->data.size())
				throw format_error("Fix animation array runs past its memory block");
			binary_reader r(std::span(at->block->data).subspan(at->offset, bytes));
			read_elements(bank, which, r, fix_stacks[which].count);
		}
		bank.fix_animations = bank.animations.size();

		// Level: its descriptions, then the elements it adds, inline.
		pointer_stream level(files.level_gpt, files.level_gpt_table, memory, 1);
		level.skip_to(level_bank_offset);
		stack level_stacks[array_count];
		for(int which = 0; which < array_count; which++)
		{
			level_stacks[which] = read_stack(level);
			if(level_stacks[which].in_fix != fix_stacks[which].count)
				throw format_error(std::format("level animation array {} doesn't continue from Fix's", which));
		}
		// Events (they hold pointers) and morph data come after this and aren't needed yet.
		for(int which = 0; which < a_events; which++)
		{
			std::size_t count = level_stacks[which].count - level_stacks[which].in_fix;
			if(count && level.position() % 4)
				level.skip(4 - level.position() % 4);
			read_elements(bank, which, level, count);
		}
		return bank;
	}

	std::vector<channel_pose> pose(const animation_bank& bank, std::size_t animation_index, std::uint32_t frame)
	{
		auto at = [](const auto& list, std::size_t index, const char* what) -> const auto& {
			if(index >= list.size())
				throw format_error(std::format("animation {} index {} is outside the bank ({})", what, index, list.size()));
			return list[index];
		};

		const animation& a = at(bank.animations, animation_index, "number");
		std::vector<channel_pose> result;
		if(a.frames == 0)
			return result;
		frame %= a.frames;
		const animation_frame& f = at(bank.frames, std::size_t(a.start_frames) + frame, "frame");

		for(std::size_t c = 0; c < a.channels; c++)
		{
			const animation_channel& channel = at(bank.channels, std::size_t(a.start_channels) + c, "channel");
			const animation_keyframe& key = at(bank.keyframes, at(bank.frame_keys, channel.frame_keys + frame, "frame key"), "keyframe");
			const vec3& position = at(bank.vectors, key.position_vector, "position");
			const vec3& scale = at(bank.vectors, key.scale_vector, "scale");
			const std::array<float, 4>& q = at(bank.quaternions, key.quaternion, "rotation");
			const animation_object& object = at(bank.objects, at(bank.ntto_choices, std::size_t(channel.ntto) + f.ntto, "object choice"), "object");

			channel_pose p;
			p.id = channel.id;
			if(!object.invisible())
				p.object = object.object;
			std::array<float, 9> rotation = quaternion_to_matrix(q[0], q[1], q[2], q[3]);
			for(int r = 0; r < 3; r++)
			{
				for(int k = 0; k < 3; k++)
					p.local.m[r][k] = rotation[r * 3 + k] * scale[k];
				p.local.m[r][3] = position[r] * key.position_multiplier;
			}
			result.push_back(p);
		}

		// The hierarchy can change per frame. It links channels by id.
		auto by_id = [&](std::int16_t id) -> std::optional<std::size_t> {
			for(std::size_t c = 0; c < result.size(); c++)
			{
				if(result[c].id == id)
					return c;
			}
			return std::nullopt;
		};
		for(std::size_t h = 0; h < f.hierarchy_count; h++)
		{
			const auto& link = at(bank.hierarchies, std::size_t(f.hierarchy_start) + h, "hierarchy");
			auto child = by_id(link[0]);
			auto parent = by_id(link[1]);
			if(child && parent && child != parent)
				result[*child].parent = parent;
		}
		return result;
	}
}
