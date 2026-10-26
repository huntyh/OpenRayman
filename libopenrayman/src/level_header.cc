// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/level_header.h>
#include <openrayman/error.h>
#include <openrayman/pointer_stream.h>

#include <algorithm>

namespace openrayman
{
	// The level's view of the memory map: pointers in a level GPT are written by the level.
	constexpr std::size_t level_view = 1;

	static transform read_transform(binary_reader& reader)
	{
		transform t;
		t.type = reader.u32();
		for(float& f : t.position)
			f = reader.f32();
		for(float& f : t.rotation)
			f = reader.f32();
		for(float& f : t.matrix)
			f = reader.f32();
		return t;
	}

	const fix_actor_placement* level_header::placement_of(const memory_location& super_object) const
	{
		auto it = std::ranges::find(placements, super_object, &fix_actor_placement::super_object);
		return it == placements.end() ? nullptr : &*it;
	}

	level_header read_level_header(const level_files& files)
	{
		const memory_image& memory = files.memory;
		pointer_stream stream(files.level_gpt, files.level_gpt_table, memory, level_view);
		level_header header;

		std::uint32_t actors = stream.u32();
		if(actors > 1000)
			throw format_error("implausible number of Fix actors in the level header");
		for(std::uint32_t i = 0; i < actors; i++)
		{
			memory_location actor = stream.required_pointer();
			header.fix_actors.push_back(actor);

			// An actor placed in this level is followed by its super object, recognisable by its
			// data pointer (+4) leading back to the actor. Otherwise the next actor follows.
			auto next = stream.peek_pointer();
			if(!next || memory.follow(*next + 4) != actor)
				continue;

			fix_actor_placement placement;
			placement.actor = actor;
			placement.super_object = stream.required_pointer();
			placement.matrix = read_transform(stream);
			placement.draw_flags = stream.u32();
			placement.transparency = stream.f32();
			placement.next = stream.pointer();
			placement.previous = stream.pointer();
			placement.parent = stream.pointer();
			header.placements.push_back(placement);
		}

		header.actual_world = stream.required_pointer();
		header.dynamic_world = stream.required_pointer();
		header.inactive_dynamic_world = stream.required_pointer();
		header.father_sector = stream.required_pointer();
		return header;
	}
}
