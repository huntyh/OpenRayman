// OpenRayman is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License, version 3. See LICENSE_openrayman for the full license.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace openrayman::game
{
	// 0x459C90 / 0x459D30: the default distance curve uses an eightfold saturation
	// distance as its zero, then holds the value reached at the fade distance.
	// Snd_Cpa.ini supplies 8 and 30. The intermediate ratio is 16.16.
	inline std::uint8_t sound_distance_volume(float distance, float saturation = 8, float fade = 30)
	{
		if(!std::isfinite(distance) || !(saturation > 0) || !std::isfinite(saturation) || !std::isfinite(fade)) return 0;
		const double near = saturation;
		const double end = near * 8;
		// A fade below saturation disables the cap (0x459CB5).
		const double cap = fade >= saturation ? fade : end;
		const double at = std::clamp(double(distance), near, std::min(cap, end));
		const auto ratio = std::int64_t((end - at) / (end - near) * 65536);
		return std::uint8_t((127 * ratio) >> 16);
	}
}
