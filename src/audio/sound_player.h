#pragma once

#include <openrayman/bnm_bank.h>
#include <openrayman/game_data.h>
#include <SDL3/SDL_audio.h>

#include <cstdint>
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
		void request(std::uint32_t id, std::uint32_t source, float gain, float pan);
		void place(std::uint32_t source, float gain, float pan);
		void stop_all();
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

		const bnm_bank* bank(std::uint32_t id);
		void resolve(std::uint32_t id, std::uint32_t event, std::uint32_t source,
			float gain, float pan, float volume, double pitch, unsigned depth);
		static void SDLCALL feed(void* self, SDL_AudioStream* stream, int additional, int total);

		const game_data& m_game;
		SDL_AudioStream* m_stream = nullptr;
		std::map<std::uint32_t, bnm_bank> m_banks;
		std::mutex m_mutex;
		std::vector<voice> m_voices;
		std::mt19937 m_random{ std::random_device{}() };
	};
}
