#include <openrayman/game/particles.h>

#include <algorithm>
#include <cmath>

namespace openrayman::game
{
	float particles::random(float from, float to)
	{
		return std::uniform_real_distribution<float>(from, to)(m_random);
	}

	void particles::add(std::uint32_t flags, const vec3& point, const vec3& motion, float life, float size, std::uint32_t material, double now)
	{
		if(m_particles.size() >= capacity)
			return;
		particle p;
		p.type = int(flags & 0xFF);
		p.flags = flags;
		p.position = point;
		p.motion = motion;
		p.life = life;
		p.size = size;
		p.height = size;
		p.material = material;
		p.born = p.since = now;
		p.animation = m_animation_length ? m_animation_length(material) : 0.0f;
		// Fully visible, except the ripple, which starts at the strength it was given.
		p.alpha = flags == 0x10003300 ? motion[0] : 255;
		m_particles.push_back(p);
	}

	void particles::start(int kind, const vec3& point, const vec3& vector, std::uint32_t material, float number, double now)
	{
		if(kind == 110)
		{
			// 0x4648B0 makes type 0x11; 0x460A10 / 0x429820 draw a black
			// percent-of-viewport rectangle once, at the opacity supplied by the script.
			if(material)
				m_screen_rectangles.push_back({point, vector, std::clamp(number, 0.0f, 255.0f), now});
			return;
		}
		if(kind == 7)
		{
			// 0x46348E: a ripple lying on the water, growing and fading; one every 0.2 s at most.
			// The vector is (strength, -, size): strength over 300 becomes 250.
			m_ripple_material = material;
			if(now < m_ripple_clock)
				return;
			m_ripple_clock = now + 0.2;
			vec3 v = vector;
			if(v[0] > 300)
				v[0] = 250;
			add(m_fewer ? 0x10003100 : 0x10003300, point, v, v[0], v[2], material, now);
			return;
		}
		if(kind == 39)
		{
			// 0x464188: a splash standing on the point, the vector's Y × 0.5 big, slowly growing
			// and fading over 2.5 s.
			add(0x8001100, point, {}, 0.01f, vector[1] * 0.5f, material, now);
			return;
		}
		if(kind == 60)
		{
			// 0x462460: ten drops thrown up and out (±3 across, 2 to 6 up), 0.03 to 0.05 big, that
			// fall back and become ripples on the point's height.
			if(m_fewer)
				return;
			for(int i = 0; i < 10; i++)
			{
				vec3 v = { random(-3, 3), random(-3, 3), random(2, 6) };
				add(0x21001, point, v, point[2], random(0.03f, 0.05f), material, now);
			}
			return;
		}
		if(kind == 62)
		{
			// 0x462580: ten specks of foam drifting out flat (±1 a second) for 1 to 3 seconds,
			// 0.01 to 0.10 big.
			for(int i = 0; i < 10; i++)
			{
				vec3 v = { random(-1, 1), random(-1, 1), 0 };
				float size = random(0.01f, 0.10f);
				add(0x10003, point, v, m_fewer ? random(0.2f, 1.0f) : random(1, 3), size, material, now);
			}
			return;
		}
		if(kind == 9)
		{
			// 0x463569: a burst at the point: a flash that shrinks (0x500) and fades fast (0x4000),
			// 0.5 to 0.8 big, and eight sparks that fall (type 1) and fade, flying up to 2 a second
			// sideways and -0.2 to 6 up, 0.19 to 0.35 big. The portal makes three when it starts.
			float flash = random(0.5f, 0.8f);
			add(0x4500, point, vec3{}, number, flash, material, now);
			for(int i = 0; i < 8; i++)
			{
				vec3 v = { random(-2, 2), random(-2, 2), random(-0.2f, 6.0f) };
				add(0x3001, point, v, number, random(0.19f, 0.35f), material, now);
			}
			return;
		}
		if(kind == 55)
		{
			// 0x46469A: vector.y + 1 motes drifting (type 3) from the point, each axis up to `number`
			// a second either way, vector.x big, fading at 260 a second. The spinning portal asks
			// for two every frame: the swirl of sparkles around it.
			int count = int(vector[1]) + 1;
			for(int i = 0; i < count; i++)
			{
				vec3 v = { random(-number, number), random(-number, number), random(-number, number) };
				add(0x3003, point, v, 1.0f, vector[0], material, now);
			}
			return;
		}
		if(kind == 175)
		{
			// 0x4648B0 case 0xAF: twelve sparks drawn from around the vector's point (Rayman's chest,
			// for the portal) into the point (the portal). Each starts number × 0.5 away, in a
			// direction mixed from two axes square to the line between the points (0x4656E0) with
			// a random -8..8 of each per axis, 0.05 to 0.30 big, and travels a ninth of its gap
			// each frame for eight frames (type 0x12, 0x4600C0 / 0x461120).
			vec3 d{};
			float length = 0;
			for(int i = 0; i < 3; i++)
			{
				d[i] = vector[i] - point[i];
				length += d[i] * d[i];
			}
			if(number == 0 || length == 0)
				return;
			length = std::sqrt(length);
			for(float& x : d)
				x /= length;
			// Two axes square to d: the largest of d's three plain perpendiculars, and d × that.
			vec3 u = { -d[1], d[0], 0 };
			float best = u[0] * u[0] + u[1] * u[1];
			if(float l = d[2] * d[2] + d[1] * d[1]; l > best)
				u = { 0, -d[2], d[1] }, best = l;
			if(float l = d[2] * d[2] + d[0] * d[0]; l > best)
				u = { -d[2], 0, d[0] }, best = l;
			for(float& x : u)
				x /= std::sqrt(best);
			vec3 w = { d[1] * u[2] - d[2] * u[1], d[2] * u[0] - d[0] * u[2], d[0] * u[1] - d[1] * u[0] };
			for(int n = 0; n < 12; n++)
			{
				vec3 at{};
				float l = 0;
				for(int i = 0; i < 3; i++)
				{
					at[i] = random(-8, 8) * u[i] + random(-8, 8) * w[i];
					l += at[i] * at[i];
				}
				vec3 motion{};
				for(int i = 0; i < 3; i++)
				{
					if(l != 0)
						at[i] = at[i] / std::sqrt(l) * number * 0.5f;
					at[i] += vector[i];
					motion[i] = (point[i] - at[i]) * (1.0f / 9.0f);
				}
				add(0x12, at, motion, 0, random(0.05f, 0.30f), material, now);
			}
			return;
		}
		if(kind == 167)
		{
			// 0x4648B0 case 0xA7: fewer water effects while the number is over 0.01.
			m_fewer = number > 0.01f;
			return;
		}
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
		p.since = p.born = now;
		p.animation = m_animation_length ? m_animation_length(material) : 0.0f;
		m_particles.push_back(p);
	}

	void particles::step(particle& p, double now, float dt)
	{
		// 0x4607E0: past its time.
		if((p.flags & 0x10000) && p.life < now - p.born)
		{
			p.alpha = 0;
			return;
		}
		// Moving (0x461120 and 0x4610F0): type 1 falls at 7 units/s², types 1 and 3 move.
		if(p.type == 1)
			p.motion[2] -= 7.0f * dt;
		if(p.type == 1 || p.type == 3)
			for(int i = 0; i < 3; i++)
				p.position[i] += p.motion[i] * dt;
		// Type 0x12 moves its step every frame and is gone after eight (counted in `life`); frames
		// of the game's 60 a second here.
		if(p.type == 0x12)
		{
			for(int i = 0; i < 3; i++)
				p.position[i] += p.motion[i] * dt * 60;
			if((p.life += dt * 60) >= 8)
				p.alpha = 0;
		}
		// A drop that falls to its floor becomes a small ripple there (only once a ripple material
		// was given), which grows and fades like the others.
		if((p.flags & 0x20000) && m_ripple_material && !(p.flags & 0x80000) && p.position[2] <= p.life)
		{
			p.flags = 0x10003100;
			p.type = 0;
			p.position[2] = p.life;
			p.size = p.height = 0.1f;
			p.alpha = 250;
			p.born = now;
			p.material = m_ripple_material;
			p.animation = m_animation_length ? m_animation_length(p.material) : 0.0f;
		}
		// Growing: 0x100 at 1.3 a second, 0x200 3.7, 0x300 6.5; 0x500 shrinks at 0.5.
		switch(p.flags & 0xF00)
		{
		case 0x100: p.size += 1.3f * dt; p.height += 1.3f * dt; break;
		case 0x200: p.size += 3.7f * dt; p.height += 3.7f * dt; break;
		case 0x300: p.size += 6.5f * dt; p.height += 6.5f * dt; break;
		case 0x500: p.size -= 0.5f * dt; p.height -= 0.5f * dt; break;
		}
		// Shrunk to nothing: kept at 0.01 (and flagged 0x1000000 by the exe).
		p.size = std::max(p.size, 0.01f);
		p.height = std::max(p.height, 0.01f);
		// Fading: 0x1000 at 100 a second, 0x2000 160, 0x3000 260, 0x4000 350.
		switch(p.flags & 0xF000)
		{
		case 0x1000: p.alpha -= 100 * dt; break;
		case 0x2000: p.alpha -= 160 * dt; break;
		case 0x3000: p.alpha -= 260 * dt; break;
		case 0x4000: p.alpha -= 350 * dt; break;
		}
	}

	void particles::update(double now, double seconds)
	{
		std::erase_if(m_screen_rectangles, [now](const screen_rectangle& r) { return r.born < now; });
		const float dt = float(seconds);
		for(particle& p : m_particles)
		{
			if(p.animation > 0 && now - p.born >= p.animation)
				p.alpha = 0;
			else if(p.flags)
				step(p, now, dt);
			else if(p.type == 14)
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
