#pragma once

#include <openrayman/bnm_bank.h>
#include <openrayman/game_data.h>
#include <SDL3/SDL_audio.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <map>
#include <mutex>
#include <random>
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
		void stop_source(std::uint32_t source); // every voice an actor started
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
			std::vector<music_segment> segments;
			music_segment tail;
		};
		music_segment load_music_segment(std::uint32_t id, unsigned depth = 0);
		void request_theme(std::uint32_t id, std::uint32_t event, float volume);
		void start_theme(theme next);
		void next_music_segment();
		void log_music(const music_segment& segment, int index);

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
		std::mt19937 m_random{ std::random_device{}() };
	};
}
