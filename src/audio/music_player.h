#pragma once

#include <openrayman/apm_audio.h>

#include <SDL3/SDL_audio.h>

#include <filesystem>
#include <memory>
#include <string>

namespace openrayman
{
	// Plays one APM track on a loop through the default audio device.
	class music_player
	{
	public:
		music_player();
		~music_player();
		music_player(const music_player&) = delete;
		music_player& operator=(const music_player&) = delete;

		// False if no audio device could be opened; play() then does nothing.
		bool available() const { return m_stream != nullptr; }

		// Starts playing a file from the beginning. Throws if it can't be decoded.
		void play(const std::filesystem::path& file);
		void stop();

		void set_paused(bool paused);
		bool paused() const { return m_paused; }

		// Seconds played of the current track, and its total length.
		double position() const;
		double length() const;

	private:
		static void SDLCALL feed(void* self, SDL_AudioStream* stream, int additional, int total);

		SDL_AudioStream* m_stream = nullptr;
		std::unique_ptr<apm_decoder> m_decoder;
		bool m_paused = false;
	};
}
