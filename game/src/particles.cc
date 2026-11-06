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
		if(kind == 125)
		{
			// 0x4605A0 stores the two percent corners, material and vector.z opacity
			// in a screen-image slot; 0x460600 draws it while its opacity is positive.
			int slot = int(number);
			if(slot >= 0 && slot < 36)
			{
				std::erase_if(m_screen_images, [slot](const screen_image& image) { return image.slot == slot; });
				if(material && vector[2] > 0) m_screen_images.push_back({point, vector, material, slot, now});
			}
			return;
		}
		if(kind == 170)
		{
			// 0x465850: tile `number` of the tiled picture gets the material.
			int tile = int(number);
			if(tile >= 0 && tile < int(m_tiles.size()))
				m_tiles[std::size_t(tile)] = material;
			return;
		}
		if(kind == 171)
		{
			// 0x465870: the picture's corners (point and vector x, y, in viewport percent) and its
			// opacity (the number).
			m_tiles_first = point;
			m_tiles_second = vector;
			m_tiles_alpha = number;
			return;
		}
		if(kind == 50)
		{
			// 0x463390 case 0x32: keeps the point, vector and number for other effects and makes
			// the material the track of the bars that follow (0x4B9ADC).
			m_bar_track = material;
			return;
		}
		if(kind == 120)
		{
			// 0x4648B0 -> 0x460530: gauge slot `number` (eight of them): corners from the point and
			// vector's x and y, the fill percent from point.z, the opacity from vector.z.
			int slot = int(number);
			if(slot >= 0 && slot < 8)
			{
				std::erase_if(m_screen_bars, [slot](const screen_bar& bar) { return bar.slot == slot; });
				m_screen_bars.push_back({point, vector, point[2], int(vector[2]), material, m_bar_track, slot});
			}
			return;
		}
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
		if(kind == 5)
		{
			// 0x461B10: four smoke puffs 0.6 above the point, each from its own offset and drifting
			// its own way (type 3), growing at 1.3 and fading at 100 a second (0x1103). A breaking
			// cage makes one. Each also gets a random 0.7..2 at +0x38 that only the draw reads (not
			// used here).
			struct puff { vec3 offset, motion; float size, height; };
			static constexpr puff puffs[] = {
				{ { 0, 0.5f, 1.4f }, { 1.0f, 0.56f, -0.53f }, 0.8f, 0.8f },
				{ { 0, -0.5f, 2.1f }, { -0.5f, -0.98f, 0.23f }, 0.5f, 0.6f },
				{ { 0, 0.5f, 0.1f }, { 0.5f, 0.16f, 0.13f }, 0.3f, 0.3f },
				{ { 0, -0.5f, 0.6f }, { 1.0f, -0.29f, -0.63f }, 1.0f, 1.0f },
			};
			for(const puff& f : puffs)
			{
				std::size_t before = m_particles.size();
				add(0x1103, { point[0] + f.offset[0], point[1] + f.offset[1], point[2] + f.offset[2] }, f.motion, number, f.size, material, now);
				if(m_particles.size() > before)
					m_particles.back().height = f.height;
			}
			return;
		}
		if(kind == 45)
		{
			// 0x463390 case 0x2D: nine sparks from the point that fall (type 1), shrink at 0.5 and
			// fade at 160 a second (0x2501), living `number` seconds; each flies up to 2 × number a
			// second sideways and -0.2 to 6 × number up, 0.16 to 0.8 × number big. A breaking cage
			// makes three, one per sparkle material: its golden burst.
			for(int i = 0; i < 9; i++)
			{
				vec3 v = { random(-2 * number, 2 * number), random(-2 * number, 2 * number), random(-0.2f * number, 6 * number) };
				add(0x2501, point, v, number, random(0.16f * number, 0.8f * number), material, now);
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

	std::vector<particles::screen_image> particles::screen_bar_images() const
	{
		std::vector<screen_image> out;
		for(const screen_bar& bar : m_screen_bars)
		{
			if(bar.alpha <= 0)
				continue;
			float x0 = bar.first[0], y0 = bar.first[1], x1 = bar.second[0], y1 = bar.second[1];
			float a = float(bar.alpha);
			auto image = [&](float ax, float ay, float bx, float by, std::uint32_t material) {
				if(material) out.push_back({ vec3{ ax, ay, 0 }, vec3{ bx, by, a }, material, bar.slot, 0 });
			};
			if(x1 - x0 <= y1 - y0)
			{
				// Taller than wide: fills up from the bottom.
				float top = y1 - (y1 - y0) * bar.fill * 0.01f;
				image(x0, top, x1, y1, bar.material);
				if(bar.fill < 100) image(x0, y0, x1, top, bar.track);
			}
			else
			{
				float right = x0 + (x1 - x0) * bar.fill * 0.01f;
				image(x0, y0, right, y1, bar.material);
				if(bar.fill < 100) image(right, y0, x1, y1, bar.track);
			}
		}
		return out;
	}

	std::vector<particles::screen_image> particles::screen_tile_images() const
	{
		// 0x4658B0: six columns and three rows between the corners, tile = column * 3 + row (so
		// logo_<column><row>), top to bottom in each column, at the opacity / 255 (0x42A300). The
		// exe rounds the corners and tile sizes down to whole pixels; ours stays in percent.
		std::vector<screen_image> out;
		if(m_tiles_alpha <= 0.01f)
			return out;
		float x0 = m_tiles_first[0], y0 = m_tiles_first[1];
		float w = (m_tiles_second[0] - x0) / 6, h = (m_tiles_second[1] - y0) / 3;
		float a = std::min(m_tiles_alpha, 255.0f);
		for(int column = 0; column < 6; column++)
			for(int row = 0; row < 3; row++)
				if(std::uint32_t material = m_tiles[std::size_t(column * 3 + row)])
					out.push_back({ vec3{ x0 + w * column, y0 + h * row, 0 }, vec3{ x0 + w * (column + 1), y0 + h * (row + 1), a }, material, 100 + column * 3 + row, 0, true });
		return out;
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
