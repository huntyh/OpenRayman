#pragma once

#include <array>
#include <cstdint>
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
		};

		// Procedure 503's arguments: effect kind, a point, a vector, a game material, a number.
		void start(int kind, const vec3& point, const vec3& vector, std::uint32_t material, float number, double now);
		// One tick: moves and fades every particle, and drops those that are done.
		void update(double now, double seconds);

		const std::vector<particle>& all() const { return m_particles; }
		// Where a particle shows now (type 11 circles around its position).
		vec3 shown_at(const particle& p, double now) const;

		// The game's pool holds 150 (and adds at most 35 per frame); ours is bigger, see
		// docs/IMPROVEMENTS.md.
		static constexpr std::size_t capacity = 2000;

	private:
		float random(float from, float to);

		std::vector<particle> m_particles;
		double m_rising_clock = 0; // effect 28 starts at most one particle every 0.05 s
		std::mt19937 m_random{ 1999 };
	};
}
