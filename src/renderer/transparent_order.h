#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace openrayman
{
	// GliVd1vf.dll 0x10002151 / 0x100011B0: farthest corner, float-depth bucket,
	// and a stack within each bucket. Screen objects flush after the world.
	struct transparent_sort_key
	{
		std::uint32_t bucket;
		std::size_t submission;
		bool screen;

		static transparent_sort_key from_depths(const std::array<float, 3>& depths, std::size_t submission, bool screen)
		{
			const float farthest = std::max(1e-5f, *std::max_element(depths.begin(), depths.end()));
			return { ((std::bit_cast<std::uint32_t>(1.0f / farthest) >> 16) - 0x2C8u) & 0xFFFu, submission, screen };
		}
		bool operator<(const transparent_sort_key& other) const
		{
			if(screen != other.screen) return !screen;
			if(bucket != other.bucket) return bucket < other.bucket;
			return submission > other.submission;
		}
	};
}
