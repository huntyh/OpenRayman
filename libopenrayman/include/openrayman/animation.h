// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <openrayman/geometry.h>
#include <openrayman/memory_image.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace openrayman
{
	// Converts a quaternion (x, y, z, w), not necessarily normalized, to a row-major 3x3 rotation
	// matrix for column vectors. A zero quaternion gives the identity.
	inline std::array<float, 9> quaternion_to_matrix(float x, float y, float z, float w)
	{
		const float length_squared = x * x + y * y + z * z + w * w;
		if(length_squared == 0.0f)
			return { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
		const float inverse = 1.0f / std::sqrt(length_squared);
		x *= inverse;
		y *= inverse;
		z *= inverse;
		w *= inverse;
		return {
			1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w),
			2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
			2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y),
		};
	}

	// One animation ("A3D"): counts, and where its data starts in the bank's shared arrays.
	struct animation
	{
		std::uint16_t speed = 0;
		std::uint16_t vectors = 0, quaternions = 0, hierarchies = 0, ntto = 0, ntto_per_channel = 0;
		std::uint16_t channels = 0, frames = 0, keyframes = 0, events = 0;
		std::uint16_t start_vectors = 0, start_quaternions = 0, start_hierarchies = 0, start_ntto = 0;
		std::uint16_t start_frames = 0, start_channels = 0, start_events = 0, start_morphs = 0;
	};

	// Per animation frame: which channel hierarchy is active.
	struct animation_frame
	{
		std::uint16_t quaternion = 0, vector = 0;
		std::uint16_t hierarchy_count = 0, hierarchy_start = 0;
		std::uint16_t ntto = 0; // index into each channel's object choices
	};

	// A channel is one moving part.
	struct animation_channel
	{
		std::int16_t id = 0;
		std::uint16_t vector = 0;
		std::uint16_t ntto = 0;         // first of this channel's object choices (in ntto_choices)
		std::uint32_t frame_keys = 0;   // first of this channel's per-frame keyframe indices
		std::uint32_t keyframe = 0;
	};

	// Which object a channel shows ("NTTO"): an index into the actor's object list.
	struct animation_object
	{
		std::uint16_t flags = 0;
		std::uint16_t object = 0;
		bool invisible() const { return flags & 0x2; }
	};

	struct animation_keyframe
	{
		float position_multiplier = 1;
		std::uint16_t frame = 0, flags = 0;
		std::uint16_t quaternion = 0, scale_vector = 0, position_vector = 0;
		bool last() const { return flags & 0x80; }
	};

	// The animations of Fix and a level, in the engine's shared arrays: Fix's come first, the
	// level's are appended, and all indices are into the combined arrays. See
	// docs/formats/animation.md.
	struct animation_bank
	{
		std::vector<animation> animations;
		std::vector<vec3> vectors;
		std::vector<std::array<float, 4>> quaternions; // x, y, z, w
		std::vector<std::array<std::int16_t, 2>> hierarchies; // child channel id, parent channel id
		std::vector<animation_object> objects;
		std::vector<animation_frame> frames;
		std::vector<animation_channel> channels;
		std::vector<std::uint16_t> ntto_choices;
		std::vector<std::uint32_t> frame_keys;
		std::vector<animation_keyframe> keyframes;
		std::size_t fix_animations = 0;
	};

	// Reads Fix's bank (arrays in Fix's memory, located by Fix.gpt) and appends the level's
	// (inline in the level GPT). Throws format_error.
	animation_bank read_animation_bank(const level_files& files);

	// One channel of an animation at one frame.
	struct channel_pose
	{
		std::int16_t id = 0;
		std::optional<std::size_t> parent;   // index of the parent channel, if any
		std::optional<std::uint16_t> object; // index in the actor's object list; none if hidden
		affine local;                        // relative to the parent channel, or to the actor
	};

	// Evaluates an animation at a frame (without interpolating between keyframes). Throws
	// format_error on indices outside the bank.
	std::vector<channel_pose> pose(const animation_bank& bank, std::size_t animation_index, std::uint32_t frame);
}
