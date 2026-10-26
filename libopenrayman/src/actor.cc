// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/actor.h>
#include <openrayman/binary_reader.h>
#include <openrayman/error.h>

#include <format>

namespace openrayman
{
	namespace
	{
		// actor -> 3D data -> current state -> animation reference.
		constexpr std::uint32_t actor_3d_data = 0x00;
		constexpr std::uint32_t data_current_state = 0x04;
		constexpr std::uint32_t data_object_list = 0x0C;
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

		auto state = memory.follow(*data + data_current_state);
		auto reference = state ? memory.follow(*state + state_animation) : std::nullopt;
		if(reference)
		{
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
			result.animation = index;
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

	std::vector<placed_geometry> actor_geometry(const memory_image& memory, const scene& tree, const animation_bank& bank)
	{
		std::vector<placed_geometry> result;
		auto add = [&](std::size_t node, const actor_object& object, const affine& world) {
			if(!object.physical_object)
				return;
			auto at = physical_object_geometry(memory, *object.physical_object);
			if(!at)
				return;
			affine placed = world;
			if(object.scale)
			{
				for(int r = 0; r < 3; r++)
					for(int c = 0; c < 3; c++)
						placed.m[r][c] *= (*object.scale)[c];
			}
			result.push_back({ node, placed, read_geometric_object(memory, *at) });
		};

		for(std::size_t node : tree.node(tree.dynamic_world()).children)
		{
			if(tree.node(node).type != super_object_type::actor)
				continue;
			actor a = read_actor(memory, tree, bank, node);
			affine world = world_transform(memory, tree, node);
			if(!a.animation)
			{
				if(!a.objects.empty())
					add(node, a.objects.front(), world);
				continue;
			}

			std::vector<channel_pose> channels = pose(bank, *a.animation, 0);
			for(std::size_t c = 0; c < channels.size(); c++)
			{
				if(!channels[c].object || *channels[c].object >= a.objects.size())
					continue;
				// Chain up through parent channels; a channel count bounds the walk against cycles.
				affine local = channels[c].local;
				std::optional<std::size_t> parent = channels[c].parent;
				for(std::size_t steps = 0; parent && steps < channels.size(); steps++)
				{
					local = channels[*parent].local * local;
					parent = channels[*parent].parent;
				}
				add(node, a.objects[*channels[c].object], world * local);
			}
		}
		return result;
	}
}
