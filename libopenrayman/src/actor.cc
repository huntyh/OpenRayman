// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/actor.h>
#include <openrayman/binary_reader.h>
#include <openrayman/error.h>

#include <algorithm>
#include <format>

namespace openrayman
{
	namespace
	{
		// actor -> 3D data -> current state -> animation reference.
		constexpr std::uint32_t actor_3d_data = 0x00;
		constexpr std::uint32_t actor_standard_game = 0x04;
		constexpr std::uint32_t standard_game_shadow_byte = 0x29; // bit 0x04: draw the shadow
		constexpr std::uint32_t data_shadow_quality = 0xDC;      // u16, 0: no shadow
		constexpr std::uint32_t draw_flag_no_shadow = 0x20;      // in the super object's draw flags
		constexpr std::uint32_t data_current_state = 0x04;
		constexpr std::uint32_t data_object_list = 0x0C;
		constexpr std::uint32_t data_family = 0x14;
		constexpr std::uint32_t data_shadow_material = 0xCC; // a game material
		constexpr std::uint32_t data_shadow_size = 0xD4;     // 2 floats
		constexpr std::uint32_t family_first_state = 0x10;
		constexpr std::uint32_t family_state_count = 0x18; // u32
		constexpr std::uint32_t state_next = 0x00;
		constexpr std::uint32_t state_animation = 0x04;
		constexpr std::uint32_t object_list_entries = 0x04;
		constexpr std::uint32_t object_list_count = 0x0C; // u16
		constexpr std::uint32_t object_entry_size = 0x14; // scale, physical object, 3 unknown words

		std::string describe(const memory_location& at)
		{
			return std::format("{} +0x{:X}", at.block->id.to_string(), at.offset);
		}

		binary_reader reader_at(const memory_location& at, std::size_t size)
		{
			if(std::size_t(at.offset) + size > at.block->data.size())
				throw format_error("data at " + describe(at) + " runs past the loaded block");
			return binary_reader(std::span(at.block->data).subspan(at.offset, size));
		}
	}

	actor read_actor(const memory_image& memory, const scene& tree, const animation_bank& bank, std::size_t node)
	{
		actor result;
		result.node = node;
		const scene_node& n = tree.node(node);
		if(!n.data)
			return result;
		auto data = memory.follow(*n.data + actor_3d_data);
		if(!data)
			return result;

		if(auto list = memory.follow(*data + data_object_list))
		{
			std::uint16_t count = reader_at(*list + object_list_count, 2).u16();
			if(count)
			{
				memory_location entries = *memory.follow(*list + object_list_entries);
				reader_at(entries, std::size_t(count) * object_entry_size); // bounds check
				for(std::uint16_t i = 0; i < count; i++)
				{
					memory_location entry = entries + i * object_entry_size;
					actor_object object;
					object.physical_object = memory.follow(entry + 4);
					if(auto scale = memory.follow(entry))
					{
						binary_reader r = reader_at(*scale, 12);
						float x = r.f32(), y = r.f32();
						object.scale = vec3{ x, y, r.f32() };
					}
					result.objects.push_back(object);
				}
			}
		}

		// The animation a state plays, if it has one.
		auto animation_of = [&](const memory_location& state) -> std::optional<std::uint16_t> {
			auto reference = memory.follow(state + state_animation);
			if(!reference)
				return std::nullopt;
			// u16 frames, u8 speed, u8 channels, events, 3 floats, morph data, u16 animation index
			binary_reader r = reader_at(*reference, 0x1C);
			std::uint16_t frames = r.u16();
			r.u8();
			std::uint8_t channels = r.u8();
			r.skip(0x14);
			std::uint16_t index = r.u16();
			if(index >= bank.animations.size())
				throw format_error(std::format("actor at {} plays animation {}, beyond the bank's {}", describe(n.at), index, bank.animations.size()));
			// The reference repeats the animation's own counts: a good check that both are right.
			const animation& a = bank.animations[index];
			if(a.frames != frames || a.channels != channels)
				throw format_error(std::format("actor at {}: animation {} has {} frames and {} channels, its reference says {} and {}",
					describe(n.at), index, a.frames, a.channels, frames, channels));
			return index;
		};

		auto current = memory.follow(*data + data_current_state);
		if(current)
			result.animation = animation_of(*current);

		// Its shadow: a game material (whose first field is the visual material) and a size.
		if(auto game_material = memory.follow(*data + data_shadow_material))
		{
			if(auto visual = memory.follow(*game_material))
			{
				actor_shadow shadow;
				shadow.material = read_visual_material(memory, *visual);
				binary_reader size = reader_at(*data + data_shadow_size, 8);
				shadow.width = size.f32();
				shadow.length = size.f32();
				// Whether the game draws it now (0x44C570 in Rayman2.exe): scripts can switch it on.
				std::uint16_t quality = reader_at(*data + data_shadow_quality, 2).u16();
				bool switched_on = false;
				if(auto standard_game = memory.follow(*n.data + actor_standard_game))
					switched_on = (reader_at(*standard_game + standard_game_shadow_byte, 1).u8() & 0x04) != 0;
				shadow.shown = quality != 0 && switched_on && !(n.draw_flags & draw_flag_no_shadow);
				result.shadow = shadow;
			}
		}

		// The family's states: a list linked through each state's first word.
		if(auto family = memory.follow(*data + data_family))
		{
			result.family = family;
			std::uint32_t count = reader_at(*family + family_state_count, 4).u32();
			auto state = memory.follow(*family + family_first_state);
			for(std::uint32_t i = 0; state && i < count; i++, state = memory.follow(*state + state_next))
			{
				if(current && *state == *current)
					result.state = result.states.size();
				result.states.push_back(animation_of(*state));
				result.state_at.push_back(*state);
			}
		}
		return result;
	}

	affine world_transform(const memory_image& memory, const scene& tree, std::size_t node)
	{
		affine world;
		for(std::optional<std::size_t> at = node; at; at = tree.node(*at).parent)
		{
			const scene_node& n = tree.node(*at);
			affine local;
			if(n.placement)
				local = affine::from(n.placement->matrix);
			else if(n.matrix && n.matrix->offset + 0x58 <= n.matrix->block->data.size())
				local = affine::from(read_transform(memory, *n.matrix));
			world = local * world;
		}
		return world;
	}

	actor_set read_actors(const memory_image& memory, const scene& tree, const animation_bank& bank)
	{
		actor_set result;
		std::vector<std::pair<memory_location, std::size_t>> read; // geometric object -> mesh index
		for(std::size_t node : tree.node(tree.dynamic_world()).children)
		{
			if(tree.node(node).type != super_object_type::actor)
				continue;
			actor a = read_actor(memory, tree, bank, node);
			actor_set::entry e;
			e.node = node;
			e.world = world_transform(memory, tree, node);
			e.animation = a.animation;
			e.family = a.family;
			e.states = a.states;
			e.state_at = a.state_at;
			e.state = a.state;
			e.shadow = a.shadow;
			for(const actor_object& object : a.objects)
			{
				std::optional<std::size_t> mesh;
				auto at = object.physical_object ? physical_object_geometry(memory, *object.physical_object) : std::nullopt;
				if(at)
				{
					auto found = std::ranges::find(read, *at, &std::pair<memory_location, std::size_t>::first);
					if(found == read.end())
					{
						read.push_back({ *at, result.meshes.size() });
						result.meshes.push_back(read_geometric_object(memory, *at));
						found = read.end() - 1;
					}
					mesh = found->second;
				}
				e.meshes.push_back(mesh);
				e.scales.push_back(object.scale);
			}
			result.actors.push_back(std::move(e));
		}
		return result;
	}

	void pose_actor(const actor_set::entry& actor, const animation_bank& bank, double frame, std::vector<posed_part>& out)
	{
		auto add = [&](std::size_t object, const affine& world) {
			if(object >= actor.meshes.size() || !actor.meshes[object])
				return;
			affine placed = world;
			if(const auto& scale = actor.scales[object])
			{
				for(int r = 0; r < 3; r++)
					for(int c = 0; c < 3; c++)
						placed.m[r][c] *= (*scale)[c];
			}
			out.push_back({ actor.node, *actor.meshes[object], placed });
		};

		if(!actor.animation)
		{
			add(0, actor.world);
			return;
		}
		std::vector<channel_pose> channels = pose(bank, *actor.animation, frame);
		for(std::size_t c = 0; c < channels.size(); c++)
		{
			if(!channels[c].object)
				continue;
			// Chain up through parent channels; a channel count bounds the walk against cycles.
			affine local = channels[c].local;
			std::optional<std::size_t> parent = channels[c].parent;
			for(std::size_t steps = 0; parent && steps < channels.size(); steps++)
			{
				local = channels[*parent].local * local;
				parent = channels[*parent].parent;
			}
			add(*channels[c].object, actor.world * local);
		}
	}

	std::vector<placed_geometry> actor_geometry(const memory_image& memory, const scene& tree, const animation_bank& bank)
	{
		actor_set set = read_actors(memory, tree, bank);
		std::vector<posed_part> parts;
		for(const actor_set::entry& actor : set.actors)
			pose_actor(actor, bank, 0, parts);
		std::vector<placed_geometry> result;
		for(const posed_part& part : parts)
			result.push_back({ part.node, part.world, set.meshes[part.mesh] });
		return result;
	}
}
