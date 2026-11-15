#pragma once

#include <algorithm>
#include <cstdint>

namespace openrayman
{
	// A linear approximation of the APM transition DLL's untraced gain curve. The one-second
	// duration matches Bast_20's recorded 0x459950 transition to the silence theme.
	inline constexpr std::uint32_t music_fade_frames = 22050;
	inline float music_fade_gain(std::uint32_t remaining)
	{
		return static_cast<float>(std::min(remaining, music_fade_frames)) / music_fade_frames;
	}
}
