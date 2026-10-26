// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <openrayman/level_header.h>
#include <openrayman/memory_image.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace openrayman
{
	// Super object types seen so far. Other values exist but aren't identified yet.
	namespace super_object_type
	{
		constexpr std::uint32_t world = 0x1; // the world roots
		constexpr std::uint32_t actor = 0x2; // "perso" in the engine
	}

	// One super object: a node of the level's scene tree. Each carries an engine object
	// (geometry, an actor, a sector, ...) and a transform relative to its parent.
	struct scene_node
	{
		memory_location at;
		std::uint32_t type = 0;
		std::optional<memory_location> data;
		std::optional<memory_location> matrix;
		std::uint32_t draw_flags = 0;
		float transparency = 0;

		// Set for actors stored in Fix, whose links come from the level header.
		const fix_actor_placement* placement = nullptr;

		std::optional<std::size_t> parent;
		std::vector<std::size_t> children;
	};

	// The scene tree of a loaded level, rebuilt from the super objects in memory.
	// See docs/formats/gpt.md.
	class scene
	{
	public:
		// Walks the tree from the actual world. Throws format_error if any link is inconsistent:
		// child counts, first and last child, sibling and parent links are all cross-checked.
		// Keeps references to both arguments, which must outlive the scene.
		scene(const memory_image& memory, const level_header& header);

		const std::vector<scene_node>& nodes() const { return m_nodes; }
		const scene_node& node(std::size_t index) const { return m_nodes.at(index); }

		std::size_t actual_world() const { return m_actual_world; }
		std::size_t dynamic_world() const { return m_dynamic_world; }
		std::size_t inactive_dynamic_world() const { return m_inactive_dynamic_world; }
		std::size_t father_sector() const { return m_father_sector; }

		// The node for a super object in memory, if it's part of the tree.
		std::optional<std::size_t> find(const memory_location& at) const;

		// Number of nodes in the subtree, including its root.
		std::size_t subtree_size(std::size_t index) const;

	private:
		std::size_t walk(const memory_location& at, std::optional<std::size_t> parent, int depth);

		const memory_image& m_memory;
		const level_header& m_header;
		std::vector<scene_node> m_nodes;
		std::map<std::pair<const sna_block*, std::uint32_t>, std::size_t> m_index;
		std::size_t m_actual_world = 0, m_dynamic_world = 0, m_inactive_dynamic_world = 0, m_father_sector = 0;
	};
}
