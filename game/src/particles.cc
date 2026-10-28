#include <openrayman/game/particles.h>

#include <algorithm>
#include <cmath>

namespace openrayman::game
{
	float particles::random(float from, float to)
	{
		return std::uniform_real_distribution<float>(from, to)(m_random);
	}

	void particles::start(int kind, const vec3& point, const vec3& vector, std::uint32_t material, float number, double now)
	{
		if(m_particles.size() >= capacity)
			return;
		particle p;
		p.material = material;
		if(kind == 23)
		{
			// 0x4639D7: one spark, up to a unit away on each axis, gliding into the point.
			p.type = 14;
			for(int i = 0; i < 3; i++)
				p.position[i] = point[i] + random(-1, 1);
			p.motion = point;
			p.size = random(0.10f, 0.15f);
			p.alpha = 255;
		}
		else if(kind == 28)
		{
			// 0x462C50: a speck in a slab 8 units ahead along the vector (the camera's), 4 to 5 units
			// down, drifting up. One every 0.05 s, however often the scripts ask.
			if(now < m_rising_clock)
				return;
			m_rising_clock = now + 0.05;
			p.type = 11;
			p.position = { point[0] + 8 * vector[0] + random(-4, 4), point[1] + 8 * vector[1] + random(-4, 4),
				point[2] + 8 * vector[2] + random(-5, -4) };
			p.motion = { random(-0.2f, 0.2f), random(-0.2f, 0.2f), random(2.5f, 4.5f) };
			p.size = number * random(0.20f, 0.25f);
			p.wobble = random(-1.5f, 1.5f);
			p.turn = random(1, 4);
			p.alpha = 5;
			p.hold = 0.5f;
		}
		else
			return;
		p.since = now;
		m_particles.push_back(p);
	}

	void particles::update(double now, double seconds)
	{
		const float dt = float(seconds);
		for(particle& p : m_particles)
		{
			if(p.type == 14)
			{
				// 0x4611A4: each axis closes 6.5 times its gap per second, and snaps there within 0.1.
				int there = 0;
				for(int i = 0; i < 3; i++)
				{
					float gap = p.motion[i] - p.position[i];
					float step = gap * dt * 6.5f;
					if(std::abs(gap) <= 0.1f || std::abs(step) >= std::abs(gap))
					{
						p.position[i] = p.motion[i];
						there++;
					}
					else
						p.position[i] += step;
				}
				if(there == 3)
					p.alpha = 0;
			}
			else if(p.type == 11)
			{
				// 0x4610F0 moves it; alpha class 0x7000 fades in at 350 a second, then (0x8000) it
				// holds for its time, then (0x3000) fades out at 260 a second.
				for(int i = 0; i < 3; i++)
					p.position[i] += p.motion[i] * dt;
				if(p.fade == 0 && (p.alpha += 350 * dt) >= 255)
				{
					p.alpha = 255;
					p.fade = 1;
					p.since = now;
				}
				else if(p.fade == 1 && now - p.since > p.hold)
					p.fade = 2;
				else if(p.fade == 2)
					p.alpha -= 260 * dt;
			}
		}
		std::erase_if(m_particles, [](const particle& p) { return p.alpha <= 0; });
	}

	particles::vec3 particles::shown_at(const particle& p, double now) const
	{
		if(p.type != 11)
			return p.position;
		// 0x460A60: drawn circling its position; the angle runs from its last fade step.
		float t = float(now - p.since) * p.turn;
		float s = std::sin(t), c = std::cos(t);
		return { p.position[0] + p.wobble * s, p.position[1] + p.wobble * c, p.position[2] + p.wobble * (s + c) };
	}
}
