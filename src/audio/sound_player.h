#pragma once

#include <openrayman/bnm_bank.h>
#include <openrayman/game_data.h>
#include <SDL3/SDL_audio.h>

#include <array>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <string>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <thread>
#include <vector>

namespace openrayman
{
	class sound_player
	{
	public:
		explicit sound_player(const game_data& game);
		~sound_player();
		sound_player(const sound_player&) = delete;
		sound_player& operator=(const sound_player&) = delete;

		bool available() const { return m_stream != nullptr; }
		// `selector` picks among a variant set by the source's ground (-1: the set's default).
		void request(std::uint32_t id, std::uint32_t source, float gain, float pan, std::int32_t selector = -1);
		void place(std::uint32_t source, float gain, float pan);
		void stop_all();
		void stop_source(std::uint32_t source); // the looping voices an actor started
		void set_paused(bool paused);

	private:
		struct voice
		{
			std::vector<std::int16_t> samples;
			std::uint32_t event = 0;
			std::uint32_t source = 0;
			unsigned channels = 0;
			double position = 0;
			double step = 1;
			float volume = 1;
			float gain = 1;
			float pan = 0;
			bool loop = false;
		};

		struct music_segment
		{
			std::vector<std::int16_t> samples;
			std::string name;
			unsigned channels = 0;
			unsigned rate = 0;
			float volume = 1;
		};
		struct theme
		{
			std::uint32_t id = 0, event = 0, restart = 0, cycles = 0;
			float volume = 1;
			std::vector<std::array<std::uint32_t, 6>> entries;
			std::vector<std::shared_ptr<const music_segment>> segments; // null: could not be loaded
			std::shared_ptr<const music_segment> tail;
		};
		struct segment_ref
		{
			std::string name;
			float volume = 1;
		};
		// A theme waiting for the loader thread: its segments named, not decoded yet.
		struct theme_job
		{
			theme next;
			std::vector<std::optional<segment_ref>> segments;
			std::optional<segment_ref> tail;
			std::uint64_t serial = 0;
		};
		std::optional<segment_ref> segment_of(std::uint32_t id, unsigned depth = 0);
		std::shared_ptr<const music_segment> load_music_segment(const std::optional<segment_ref>& ref);
		void load_themes();
		void request_theme(std::uint32_t id, std::uint32_t event, float volume);
		void install_theme(theme next);
		void start_theme(theme next);
		void next_music_segment();
		void log_music(const std::shared_ptr<const music_segment>& segment, int index);

		const bnm_bank* bank(std::uint32_t id);
		void resolve(std::uint32_t id, std::uint32_t event, std::uint32_t source,
			float gain, float pan, float volume, double pitch, std::int32_t selector, unsigned depth);
		static void SDLCALL feed(void* self, SDL_AudioStream* stream, int additional, int total);

		const game_data& m_game;
		SDL_AudioStream* m_stream = nullptr;
		std::map<std::uint32_t, bnm_bank> m_banks;
		std::mutex m_mutex;
		std::vector<voice> m_voices;
		theme m_theme;
		std::unique_ptr<theme> m_pending_theme;
		int m_music_index = -1, m_repeat_index = -1;
		std::uint32_t m_repeat = 0, m_cycles = 0;
		double m_music_position = 0;
		std::uint64_t m_music_frames = 0;
		bool m_music_tail = false, m_music_log = false;
		// The loader thread and its one waiting job (a newer request replaces it); the serial drops
		// themes that a stop or a newer request made stale while they were decoding.
		std::thread m_loader;
		std::mutex m_job_mutex;
		std::condition_variable m_job_ready;
		std::optional<theme_job> m_job;
		bool m_quit = false;
		std::uint64_t m_theme_serial = 0;
		std::uint32_t m_loading_theme = 0, m_loading_event = 0;
		std::map<std::string, std::shared_ptr<const music_segment>> m_segment_cache;
		std::mt19937 m_random{ std::random_device{}() };
	};
}
