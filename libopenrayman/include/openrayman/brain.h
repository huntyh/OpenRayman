// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <openrayman/memory_image.h>
#include <openrayman/scene.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace openrayman
{
	// What a script node is: the interpreter in Rayman2.exe (0x480B90) jumps through a table of 41
	// handlers by this number. See docs/formats/scripts.md.
	enum class node_type : std::uint8_t
	{
		keyword = 0,         // if, then, else, me, ... (20 in the exe's table)
		condition = 1,       // true or false (149)
		operator_ = 2,       // arithmetic, assignment, vector parts, array elements (28)
		function = 3,        // returns a value (314)
		procedure = 4,       // does something (514)
		meta_action = 5,     // takes several frames (12)
		end = 8,             // closes a script (indent 0)
		field = 9,           // one of an actor's 18 fields
		variable_10 = 10,    // like `variable`, not used by the game's data
		variable = 11,       // a designer variable of the running actor, by index
		integer = 12,
		real = 13,           // a float
		input = 14,          // points at an input action (block 06/00)
		constant_vector = 15,// points at 3 floats, not used by the game's data
		vector = 16,         // built from its three arguments
		mask = 17,           // a bit mask, up to 0x4000
		small_id = 18,       // 0..48, not identified yet
		text = 20,           // points at a zero-terminated string (level names)
		family = 22,
		actor = 23,
		state = 24,          // a family state
		super_object = 25,
		waypoint = 26,       // a position and a radius
		id_27 = 27,          // mostly 0x5000..0x5100, not identified yet
		behaviour = 28,      // a behaviour or reflex of some AI model
		sound_event = 30,    // a sound event (procedure 115 plays it)
		reference_31 = 31,
		reference_32 = 32,   // points into the dynamic world's blocks, not identified yet
		model = 35,          // an AI model (to read another actor's variables)
		reference_36 = 36,
		flags_37 = 37,
		reference_38 = 38,
		subroutine = 39,     // runs a macro in place
		ignored = 40,        // skipped by the interpreter
	};
	constexpr std::uint8_t node_type_count = 41;

	// One node of a script. Scripts are trees written out in order: a node's arguments follow it,
	// one indent deeper.
	struct script_node
	{
		std::uint32_t param = 0; // a number, a float's bits, a table index or a pointer, by type
		std::optional<memory_location> target; // where param points, if it is a pointer
		std::uint8_t indent = 0;
		std::uint8_t type = 0; // node_type
	};

	struct script
	{
		memory_location at; // the script structure (its first word points to the nodes)
		std::vector<script_node> nodes; // up to and including the end node
	};

	// A behaviour ("comport"): rules checked every frame, and a schedule that may run over several
	// frames and resumes where it stopped.
	struct behaviour
	{
		memory_location at;
		std::vector<script> rules;
		std::optional<script> schedule;
	};

	// A script that other scripts run in place. The game runs a copy, which starts out the same.
	struct macro
	{
		memory_location at; // what subroutine nodes point at
		script body; // no nodes if the macro is empty
	};

	// A designer variable: a slot in the actor's variable buffer.
	struct dsg_variable
	{
		std::uint32_t offset = 0; // in the buffer
		std::uint32_t type = 0;
	};

	// What every actor of one kind shares: behaviours, reflexes, macros and variable layout.
	struct ai_model
	{
		memory_location at;
		std::vector<behaviour> behaviours; // the normal ones; the actor runs one at a time
		std::vector<behaviour> reflexes;   // checked before the normal behaviour
		std::vector<macro> macros;         // run in place by subroutine nodes
		std::vector<dsg_variable> variables;
		std::vector<std::byte> defaults; // the variables' initial values
	};

	// An actor's own brain: its model, and where it stands in it as saved.
	struct actor_brain
	{
		std::size_t node = 0;
		memory_location model;                    // key into level_brains::models
		std::optional<std::size_t> behaviour;     // current normal behaviour, as saved
		std::optional<std::size_t> reflex;        // current reflex, as saved
		std::vector<std::byte> variables;         // this actor's variable buffer, as saved
	};

	struct level_brains
	{
		std::map<std::uint64_t, ai_model> models; // by address in memory (see model_key)
		std::vector<actor_brain> actors;

		static std::uint64_t model_key(const memory_location& at)
		{
			return std::uint64_t(at.block->id.module) << 40 | std::uint64_t(at.block->id.id) << 32 | at.offset;
		}
		const ai_model& model(const actor_brain& b) const { return models.at(model_key(b.model)); }
	};

	// The script as readable lines, one statement or argument per line. Functions, conditions and
	// procedures appear by number until they are identified from the exe.
	std::vector<std::string> describe_script(const script& s, const ai_model& model, const memory_image& memory, const scene& tree);

	// Reads the brains of every actor in the scene that has one. Throws format_error.
	level_brains read_brains(const memory_image& memory, const scene& tree);
}
