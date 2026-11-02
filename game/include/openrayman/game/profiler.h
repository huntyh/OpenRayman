#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>

// Where a frame's time goes: named zones timed on the CPU, the GPU's own time, and counters, summed
// per frame and reported once a second (average and worst frame). The engine shows the report with
// F8 and logs it with OPENRAYMAN_PROFILE=<file.csv>. Single-threaded: only the main loop records.
// When nothing asks for it, a zone costs one test of a flag.
namespace openrayman::game::profiler
{
	enum class zone : std::uint8_t
	{
		frame,       // the whole frame, from one start to the next
		scripts,     // the 60 Hz ticks: everything below up to particles
		brains,      // the actors' scripts (behaviours, reflexes)
		animation,   // playing animations, their events and motion
		mechanics,   // the actors' movement and collision
		actor_floor, // of which: other actors' surfaces (standing on actors)
		camera,      // the game camera's mechanics
		particles,
		posing,      // the actors' parts placed for drawing
		visibility,  // which actors are in view
		draw,        // building and sending the draw calls (CPU)
		gpu,         // the GPU's time for the level (timer queries, a few frames late)
		swap,        // presenting the frame; with vsync, mostly waiting for the monitor
		limiter,     // waiting for the frame-rate cap
		count,
	};

	enum class counter : std::uint8_t
	{
		draw_calls,
		triangles,
		ticks,         // 60 Hz ticks run
		actors,        // actors that thought
		collision_gathers, // actor-surface searches (one per moving, colliding actor)
		count,
	};

	std::string_view name(zone z);
	std::string_view name(counter c);

	bool enabled();
	void set_enabled(bool on);

	void add(zone z, std::chrono::nanoseconds time);
	void add(counter c, std::uint64_t n = 1);

	// Closes the frame: its sums go into the second being collected.
	void end_frame();

	struct report
	{
		double seconds = 0; // how long it covers
		int frames = 0;
		std::array<double, std::size_t(zone::count)> average_ms{}, worst_ms{};
		std::array<double, std::size_t(counter::count)> per_frame{};
	};
	// The last full second, once per second (nullopt in between).
	std::optional<report> take_report();

	// Times its own lifetime into a zone.
	class scope
	{
	public:
		explicit scope(zone z) : m_zone(z), m_on(enabled())
		{
			if(m_on)
				m_start = std::chrono::steady_clock::now();
		}
		~scope()
		{
			if(m_on)
				add(m_zone, std::chrono::steady_clock::now() - m_start);
		}
		scope(const scope&) = delete;
		scope& operator=(const scope&) = delete;

	private:
		zone m_zone;
		bool m_on;
		std::chrono::steady_clock::time_point m_start;
	};
}
