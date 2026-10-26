#include <audio/music_player.h>

#include <openrayman/file.h>

#include <SDL3/SDL_log.h>

#include <vector>

namespace openrayman
{
	music_player::music_player()
	{
		// The source format changes per track, so open with a placeholder and set it in play().
		SDL_AudioSpec spec{ SDL_AUDIO_S16, 2, 22050 };
		m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &music_player::feed, this);
		if(!m_stream)
			SDL_Log("No audio device: %s", SDL_GetError());
	}

	music_player::~music_player()
	{
		if(m_stream)
			SDL_DestroyAudioStream(m_stream);
	}

	void music_player::play(const std::filesystem::path& file)
	{
		if(!m_stream)
			return;

		auto decoder = std::make_unique<apm_decoder>(read_file(file));
		SDL_AudioSpec spec{ SDL_AUDIO_S16, decoder->channels(), static_cast<int>(decoder->sample_rate()) };

		// The feed callback runs on the audio thread and uses m_decoder.
		SDL_LockAudioStream(m_stream);
		SDL_ClearAudioStream(m_stream);
		SDL_SetAudioStreamFormat(m_stream, &spec, nullptr);
		m_decoder = std::move(decoder);
		SDL_UnlockAudioStream(m_stream);

		set_paused(false);
	}

	void music_player::stop()
	{
		if(!m_stream)
			return;
		SDL_LockAudioStream(m_stream);
		SDL_ClearAudioStream(m_stream);
		m_decoder.reset();
		SDL_UnlockAudioStream(m_stream);
	}

	void music_player::set_paused(bool paused)
	{
		if(!m_stream)
			return;
		m_paused = paused;
		if(paused)
			SDL_PauseAudioStreamDevice(m_stream);
		else
			SDL_ResumeAudioStreamDevice(m_stream);
	}

	double music_player::position() const
	{
		if(!m_stream)
			return 0;
		SDL_LockAudioStream(m_stream);
		double result = m_decoder ? double(m_decoder->frames() - m_decoder->frames_left()) / m_decoder->sample_rate() : 0;
		SDL_UnlockAudioStream(m_stream);
		return result;
	}

	double music_player::length() const
	{
		if(!m_stream)
			return 0;
		SDL_LockAudioStream(m_stream);
		double result = m_decoder ? double(m_decoder->frames()) / m_decoder->sample_rate() : 0;
		SDL_UnlockAudioStream(m_stream);
		return result;
	}

	// Called by SDL with the stream locked whenever it needs more audio.
	void SDLCALL music_player::feed(void* self, SDL_AudioStream* stream, int additional, int)
	{
		auto& player = *static_cast<music_player*>(self);
		if(!player.m_decoder || additional <= 0)
			return;

		apm_decoder& decoder = *player.m_decoder;
		std::vector<std::int16_t> buffer(static_cast<std::size_t>(additional) / sizeof(std::int16_t));
		std::size_t filled = 0;
		while(filled < buffer.size())
		{
			if(decoder.frames_left() == 0)
				decoder.reset(); // loop
			std::size_t frames = decoder.decode(std::span(buffer).subspan(filled));
			if(frames == 0)
				break;
			filled += frames * decoder.channels();
		}
		SDL_PutAudioStreamData(stream, buffer.data(), static_cast<int>(filled * sizeof(std::int16_t)));
	}
}
