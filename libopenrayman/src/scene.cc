// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/scene.h>
#include <openrayman/error.h>

#include <cstring>
#include <format>

namespace openrayman
{
	// Super object layout, worked out by checking that the links agree across every level.
	namespace field
	{
		constexpr std::uint32_t type = 0x00;
		constexpr std::uint32_t data = 0x04;
		constexpr std::uint32_t first_child = 0x08;
		constexpr std::uint32_t last_child = 0x0C;
		constexpr std::uint32_t child_count = 0x10;
		constexpr std::uint32_t next = 0x14;
		constexpr std::uint32_t previous = 0x18;
		constexpr std::uint32_t parent = 0x1C;
		constexpr std::uint32_t matrix = 0x20;
		constexpr std::uint32_t draw_flags = 0x30;
		constexpr std::uint32_t transparency = 0x38;
		constexpr std::uint32_t size = 0x3C;
	}

	constexpr int max_depth = 64;

	static std::string describe(const memory_location& at)
	{
		return std::format("{} +0x{:X}", at.block->id.to_string(), at.offset);
	}

	static std::string describe(const std::optional<memory_location>& at)
	{
		return at ? describe(*at) : "null";
	}

	scene::scene(const memory_image& memory, const level_header& header) : m_memory(memory), m_header(header)
	{
		m_actual_world = walk(header.actual_world, std::nullopt, 0);

		auto require = [&](const memory_location& at, const char* what) {
			auto index = find(at);
			if(!index)
				throw format_error(std::string("the ") + what + " isn't part of the actual world");
			return *index;
		};
		m_dynamic_world = require(header.dynamic_world, "dynamic world");
		m_inactive_dynamic_world = require(header.inactive_dynamic_world, "inactive dynamic world");
		m_father_sector = require(header.father_sector, "father sector");
	}

	std::optional<std::size_t> scene::find(const memory_location& at) const
	{
		auto it = m_index.find({ at.block, at.offset });
		return it == m_index.end() ? std::nullopt : std::optional(it->second);
	}

	std::size_t scene::subtree_size(std::size_t index) const
	{
		std::size_t size = 1;
		for(std::size_t child : m_nodes[index].children)
			size += subtree_size(child);
		return size;
	}

	std::size_t scene::walk(const memory_location& at, std::optional<std::size_t> parent, int depth)
	{
		if(depth > max_depth)
			throw format_error("super object tree is too deep, probably a cycle");
		if(at.offset + field::size > at.block->data.size())
			throw format_error("super object at " + describe(at) + " lies outside loaded data");
		if(m_index.contains({ at.block, at.offset }))
			throw format_error("super object at " + describe(at) + " appears twice in the tree");

		std::size_t index = m_nodes.size();
		m_index[{ at.block, at.offset }] = index;
		{
			scene_node node;
			node.at = at;
			node.type = m_memory.read_u32(at + field::type);
			node.data = m_memory.follow(at + field::data);
			node.matrix = m_memory.follow(at + field::matrix);
			node.draw_flags = m_memory.read_u32(at + field::draw_flags);
			std::uint32_t transparency = m_memory.read_u32(at + field::transparency);
			std::memcpy(&node.transparency, &transparency, 4);
			node.placement = m_header.placement_of(at);
			node.parent = parent;
			m_nodes.push_back(node);
		}

		std::uint32_t count = m_memory.read_u32(at + field::child_count);
		auto child = m_memory.follow(at + field::first_child);
		std::optional<memory_location> previous;
		for(std::uint32_t i = 0; i < count; i++)
		{
			if(!child)
				throw format_error(std::format("super object at {} lists {} children but only links {}", describe(at), count, i));

			// Fix's actors get their links from the level header; everything else links itself.
			std::optional<memory_location> next, back, up;
			if(const fix_actor_placement* placement = m_header.placement_of(*child))
			{
				next = placement->next;
				back = placement->previous;
				up = placement->parent;
			}
			else
			{
				next = m_memory.follow(*child + field::next);
				back = m_memory.follow(*child + field::previous);
				up = m_memory.follow(*child + field::parent);
			}
			if(up != at || back != previous)
				throw format_error(std::format("child {} of {} at {} links back to parent {} and previous {}",
					i, describe(at), describe(*child), describe(up), describe(back)));

			std::size_t child_index = walk(*child, index, depth + 1);
			m_nodes[index].children.push_back(child_index);
			previous = child;
			child = next;
		}
		if(child)
			throw format_error(std::format("super object at {} has more than its {} children", describe(at), count));
		if(m_memory.follow(at + field::last_child) != previous)
			throw format_error("last child of super object at " + describe(at) + " doesn't match its child list");
		return index;
	}
}
