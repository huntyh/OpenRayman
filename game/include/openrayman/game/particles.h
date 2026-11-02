#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

namespace openrayman::game
{
	// The game's small particles: sprites that scripts start with procedure 503 (the effect spawner,
	// 0x463390) and that live on their own for a moment. Pool 0x4E5800, added by 0x4600C0, moved and
	// faded by 0x4608B0 / 0x461120 every frame. Only the effects Learn_10 shows ambiently are known so
	// far; see docs/formats/particles.md.
	class particles
	{
	public:
		using vec3 = std::array<float, 3>;

		struct particle
		{
			int type = 0;          // the exe's particle type (low byte of its flags)
			vec3 position{};
			vec3 motion{};         // type 11: a speed; type 14: the point it glides to
			float size = 0;        // half its width and height
			float alpha = 0;       // 0..255
			int fade = 0;          // 0 fading in, 1 holding, 2 fading out (type 11)
			double since = 0;      // when the current fade step began (seconds)
			float hold = 0;        // how long it stays fully visible (seconds)
			float wobble = 0;      // type 11: radius of its circling, and how fast it turns
			float turn = 0;
			std::uint32_t material = 0; // a game material
			// The water effects (kinds 7, 39, 60, 62) follow the exe's flag word (+0 of a pool entry):
			// the low byte is the type (0 stays, 1 falls, 3 drifts), 0xF00 how fast it grows, 0xF000
			// how fast it fades, 0x10000 "dies after `life` seconds", 0x20000 "turns into a ripple
			// when it falls to `floor`", 0x10000000 drawn flat on the ground, 0x8000000 drawn
			// standing on its point. 0 for the older effects (types 11 and 14 above).
			std::uint32_t flags = 0;
			float height = 0;      // half its height (size is half its width)
			double born = 0;
			float life = 0;        // +0x3C: seconds to live with 0x10000, the floor with 0x20000
			// How long its material's animated texture runs, 0 for a still one. A particle plays it
			// once from its birth and is gone when it ends (0x424C00 with 0x4A7278 set).
			float animation = 0;
		};
		struct screen_rectangle
		{
			vec3 first{}, second{}; // viewport percentages
			float alpha = 0;
			double born = 0;
		};
		struct screen_image
		{
			vec3 first{}, second{};
			std::uint32_t material = 0;
			int slot = 0;
			double born = 0;
		};

		// Kind 120's gauge slots (0x460530, table 0x4E0F20, drawn by 0x460670): two percent corners,
		// filled from the first (or from the bottom when taller than wide) with `material` up to
		// `fill` percent, the rest with `track`, the material kind 50 last named (0x4B9ADC).
		struct screen_bar
		{
			vec3 first{}, second{};
			float fill = 0;
			int alpha = 0;
			std::uint32_t material = 0, track = 0;
			int slot = 0;
		};

		// Procedure 503's arguments: effect kind, a point, a vector, a game material, a number.
		// `animation` gives how long a game material's animated texture runs (0 for a still one).
		void start(int kind, const vec3& point, const vec3& vector, std::uint32_t material, float number, double now);
		void set_animation_lengths(std::function<float(std::uint32_t)> lengths) { m_animation_length = std::move(lengths); }
		// One tick: moves and fades every particle, and drops those that are done.
		void update(double now, double seconds);

		const std::vector<particle>& all() const { return m_particles; }
		const std::vector<screen_rectangle>& screen_rectangles() const { return m_screen_rectangles; }
		const std::vector<screen_image>& screen_images() const { return m_screen_images; }
		const std::vector<screen_bar>& screen_bars() const { return m_screen_bars; }
		// The bars as the images 0x460670 paints: the filled part and, below 100 percent, the rest.
		std::vector<screen_image> screen_bar_images() const;
		// Where a particle shows now (type 11 circles around its position).
		vec3 shown_at(const particle& p, double now) const;

		// The game's pool holds 150 (and adds at most 35 per frame); ours is bigger, see
		// docs/IMPROVEMENTS.md.
		static constexpr std::size_t capacity = 2000;

	private:
		float random(float from, float to);

		std::vector<particle> m_particles;
		std::vector<screen_rectangle> m_screen_rectangles;
		std::vector<screen_image> m_screen_images;
		std::vector<screen_bar> m_screen_bars;
		std::uint32_t m_bar_track = 0; // kind 50's material, which the next bars use as their track
		double m_rising_clock = 0; // effect 28 starts at most one particle every 0.05 s
		double m_ripple_clock = 0; // effect 7 starts at most one ripple every 0.2 s (0x4B9B0C)
		std::uint32_t m_ripple_material = 0; // effect 7's material, which falling drops become (0x4B9AD8)
		bool m_fewer = false;      // effect 167's switch (0x4B9AF8): no drops, quieter ripples

		// Adds a particle the way 0x4600C0 does for the flag-word effects.
		void add(std::uint32_t flags, const vec3& point, const vec3& motion, float life, float size, std::uint32_t material, double now);
		// One tick of a flag-word particle (0x461120); false when it is done.
		void step(particle& p, double now, float dt);
		std::mt19937 m_random{ 1999 };
		std::function<float(std::uint32_t)> m_animation_length;
	};
}
