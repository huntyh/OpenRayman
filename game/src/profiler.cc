#include <openrayman/game/profiler.h>

#include <algorithm>
#include <utility>

namespace openrayman::game::profiler
{
	namespace
	{
		constexpr std::size_t zones = std::size_t(zone::count), counters = std::size_t(counter::count);

		struct state
		{
			bool on = false;
			std::array<std::int64_t, zones> frame{};       // this frame's sums, nanoseconds
			std::array<std::uint64_t, counters> frame_counts{};
			std::array<std::int64_t, zones> total{}, worst{};
			std::array<std::uint64_t, counters> counts{};
			int frames = 0;
			std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
			std::optional<report> ready;
		};

		state& current()
		{
			static state s;
			return s;
		}
	}

	std::string_view name(zone z)
	{
		static constexpr std::array<std::string_view, zones> names = {
			"frame", "scripts", "brains", "animation", "mechanics", "actor floor", "camera", "particles",
			"posing", "visibility", "draw", "gpu", "swap", "limiter",
		};
		return names[std::size_t(z)];
	}

	std::string_view name(counter c)
	{
		static constexpr std::array<std::string_view, counters> names = {
			"draw calls", "triangles", "ticks", "actors", "collision gathers",
		};
		return names[std::size_t(c)];
	}

	bool enabled() { return current().on; }

	void set_enabled(bool on)
	{
		state& s = current();
		if(on && !s.on)
		{
			s = state{};
			s.on = true;
		}
		s.on = on;
	}

	void add(zone z, std::chrono::nanoseconds time)
	{
		state& s = current();
		if(s.on)
			s.frame[std::size_t(z)] += time.count();
	}

	void add(counter c, std::uint64_t n)
	{
		state& s = current();
		if(s.on)
			s.frame_counts[std::size_t(c)] += n;
	}

	void end_frame()
	{
		state& s = current();
		if(!s.on)
			return;
		for(std::size_t i = 0; i < zones; i++)
		{
			s.total[i] += s.frame[i];
			s.worst[i] = std::max(s.worst[i], s.frame[i]);
		}
		for(std::size_t i = 0; i < counters; i++)
			s.counts[i] += s.frame_counts[i];
		s.frame = {};
		s.frame_counts = {};
		s.frames++;
		auto now = std::chrono::steady_clock::now();
		double seconds = std::chrono::duration<double>(now - s.since).count();
		if(seconds < 1.0)
			return;
		report r;
		r.seconds = seconds;
		r.frames = s.frames;
		for(std::size_t i = 0; i < zones; i++)
		{
			r.average_ms[i] = double(s.total[i]) / 1e6 / s.frames;
			r.worst_ms[i] = double(s.worst[i]) / 1e6;
		}
		for(std::size_t i = 0; i < counters; i++)
			r.per_frame[i] = double(s.counts[i]) / s.frames;
		s.ready = r;
		s.total = {};
		s.worst = {};
		s.counts = {};
		s.frames = 0;
		s.since = now;
	}

	std::optional<report> take_report()
	{
		return std::exchange(current().ready, std::nullopt);
	}
}
