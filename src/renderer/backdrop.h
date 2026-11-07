#pragma once

#include <openrayman/geometry.h>

#include <span>
#include <string_view>

namespace openrayman
{
	// Renderer improvements, selected explicitly per level. These are not asset flags.
	struct backdrop_recipe
	{
		std::string_view level;
		std::span<const std::size_t> sectors, skies;
		vec3 anchor;
		float parallax;
	};

	inline constexpr std::size_t jail_10_backdrop[] = { 7 }, jail_10_skies[] = { 2, 3, 4 };
	inline constexpr std::size_t jail_20_backdrop[] = { 0 };
	inline constexpr backdrop_recipe backdrop_recipes[] = {
		{ "Jail_10", jail_10_backdrop, jail_10_skies, { -101.584f, 139.372f, 541.117f }, 0.1f },
		// Sector 0 contains only the sky shell and distant valley cards. The chute,
		// its exit and all actors remain in the normal pass.
		{ "Jail_20", jail_20_backdrop, {}, { 393.22f, 321.303f, 275.931f }, 0.1f },
	};
}
