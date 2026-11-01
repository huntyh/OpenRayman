#include <audio/sound_player.h>

#include <SDL3/SDL_log.h>
#include <openrayman/apm_audio.h>
#include <openrayman/file.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <cctype>
#include <cstdlib>

namespace openrayman
{
	sound_player::sound_player(const game_data& game) : m_game(game)
	{
		m_music_log = std::getenv("OPENRAYMAN_MUSIC_LOG") && std::string(std::getenv("OPENRAYMAN_MUSIC_LOG")) == "1";
		SDL_AudioSpec spec{ SDL_AUDIO_S16, 2, 22050 };
		m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &sound_player::feed, this);
		if(m_stream)
			SDL_ResumeAudioStreamDevice(m_stream);
		else
			SDL_Log("No sound device: %s", SDL_GetError());
	}

	sound_player::~sound_player()
	{
		if(m_stream)
			SDL_DestroyAudioStream(m_stream);
	}

	const bnm_bank* sound_player::bank(std::uint32_t id)
	{
		auto found = m_banks.find(id);
		if(found != m_banks.end())
			return &found->second;
		try
		{
			auto path = m_game.sound_bank(id);
			if(path)
				return &m_banks.emplace(id, bnm_bank::load(*path)).first->second;
		}
		catch(const std::exception& e)
		{
			SDL_Log("Cannot load sound bank %u: %s", id, e.what());
		}
		return nullptr;
	}

	void sound_player::request(std::uint32_t id, std::uint32_t source, float gain, float pan, std::int32_t selector)
	{
		std::lock_guard lock(m_mutex);
		auto key = bnm_id::of(id);
		const auto* b = bank(key.bank);
		const auto* event = b ? b->event(key.index) : nullptr;
		if(!event)
			return;
		// 0x452D50 and 0x451E20: stop voices by their originating event.
		if(event->type == bnm_event_type::stop)
		{
			if(m_theme.event == event->target) { m_music_index = -1; m_pending_theme.reset(); }
			else if(m_pending_theme && m_pending_theme->event == event->target) m_pending_theme.reset();
			std::erase_if(m_voices, [event](const voice& v) { return v.event == event->target; });
			return;
		}
		if(!event->plays())
			return;
		// 0x4527D0: random 16.16 pitch variation per request.
		double variation = static_cast<double>(event->pitch) / 65536.0;
		std::uniform_real_distribution<double> random(-variation, variation);
		double pitch = std::max(0.001, 1.0 + random(m_random));
		resolve(event->target, id, source, std::clamp(gain, 0.0f, 1.0f),
			std::clamp(pan, -1.0f, 1.0f), 1.0f, pitch, selector, 0);
	}

	void sound_player::resolve(std::uint32_t id, std::uint32_t event, std::uint32_t source,
		float gain, float pan, float volume, double pitch, std::int32_t selector, unsigned depth)
	{
		if(depth >= 16)
			return;
		auto key = bnm_id::of(id);
		const auto* b = bank(key.bank);
		const auto* resource = b ? b->resource(key.index) : nullptr;
		if(!resource)
			return;
		volume *= static_cast<float>(std::min(resource->volume, 127u)) / 127.0f;
		if(resource->type == bnm_resource_type::theme)
		{
			request_theme(id, event, volume);
			return;
		}
		if(resource->type == bnm_resource_type::sample)
		{
			if(bnm_bank::is_external(*resource) || resource->data_format != bnm_data_format::pcm ||
				resource->bits() != 16 || resource->channels() < 1 || resource->channels() > 2 || resource->rate == 0)
				return;
			auto bytes = b->sample(*resource);
			if(bytes.size() < 2 * resource->channels())
				return;
			if(m_voices.size() >= 32)
			{
				auto oldest = std::find_if(m_voices.begin(), m_voices.end(), [](const voice& v) { return !v.loop; });
				if(oldest == m_voices.end())
					return;
				m_voices.erase(oldest);
			}
			voice v;
			v.samples.reserve(bytes.size() / 2);
			for(std::size_t i = 0; i + 1 < bytes.size(); i += 2)
			{
				auto value = std::to_integer<unsigned>(bytes[i]) | (std::to_integer<unsigned>(bytes[i + 1]) << 8);
				v.samples.push_back(static_cast<std::int16_t>(value));
			}
			v.event = event;
			v.source = source;
			v.channels = resource->channels();
			v.step = static_cast<double>(resource->rate) / 22050.0 * pitch;
			v.volume = volume;
			v.gain = gain;
			v.pan = pan;
			// Tentative: +0x30 correlates with ambient loops; the WAV DLL needs tracing.
			v.loop = resource->flag30 != 0;
			m_voices.push_back(std::move(v));
			return;
		}
		auto entries = b->entries(*resource);
		if(entries.empty())
			return;
		if(resource->type == bnm_resource_type::group8)
		{
			// 0x45A510: the entry whose second word is the selector. A set of kind 1 (bank +0x24) is
			// chosen by the ground's sound type (footsteps); otherwise, or without one, by the set's
			// default (+0x1C); nothing plays when neither matches. Entry 14 of Rayman's footstep sets
			// is the elastic launch: picking at random played it on plain ground.
			const std::int32_t fallback = std::int32_t(resource->gain);
			const std::int32_t wanted = resource->theme_count == 1 && selector >= 0 ? selector : fallback;
			auto match = [&](std::int32_t value) {
				return std::ranges::find_if(entries, [&](const auto& entry) { return std::int32_t(entry[1]) == value; });
			};
			auto found = match(wanted);
			if(found == entries.end() && wanted != fallback)
				found = match(fallback);
			if(found != entries.end())
				resolve((*found)[0], event, source, gain, pan, volume, pitch, selector, depth + 1);
		}
		else if(resource->type == bnm_resource_type::group10)
		{
			// 0x45A0A0: one entry by weight. Entries are (resource id, weight out of 0x10000, flag);
			// a number below 0x10000 walks down the weights, the first entry when none takes it.
			// The game also avoids repeating the last pick, which isn't kept here yet.
			// The set's +0x1C share of 0x10000 plays nothing: Rayman pants only now and then
			// (51760) and grunts on about half his jumps (29788).
			std::uniform_int_distribution<std::uint32_t> roll(0, 0xFFFF);
			std::int64_t left = std::int64_t(roll(m_random)) - std::int64_t(resource->gain);
			if(left < 0)
				return;
			std::uint32_t chosen = entries.front()[0];
			for(const auto& entry : entries)
			{
				if(left <= std::int64_t(entry[1]))
				{
					chosen = entry[0];
					break;
				}
				left -= entry[1];
			}
			resolve(chosen, event, source, gain, pan, volume, pitch, selector, depth + 1);
		}
		else if(resource->type == bnm_resource_type::group4)
		{
			// 0x45AD30 starts the first entry; subsequent sequence transitions are not implemented.
			resolve(entries.front()[3], event, source, gain, pan, volume, pitch, selector, depth + 1);
		}
	}


	// Segment data is decoded before playback, so the audio callback never waits on disk.
	sound_player::music_segment sound_player::load_music_segment(std::uint32_t id, unsigned depth)
	{
		music_segment result;
		if(depth >= 16) return result;
		auto key = bnm_id::of(id);
		const auto* b = bank(key.bank);
		const auto* resource = b ? b->resource(key.index) : nullptr;
		if(!resource) return result;
		if(resource->type == bnm_resource_type::group10)
		{
			auto entries = b->entries(*resource);
			if(!entries.empty()) return load_music_segment(entries.front()[0], depth + 1);
		}
		if(resource->type != bnm_resource_type::sample || !bnm_bank::is_external(*resource)) return result;
		try
		{
			auto lower = [](std::string value) { std::ranges::transform(value, value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return value; };
			for(const auto& path : m_game.music_files())
				if(lower(path.filename().string()) == lower(resource->name))
				{
					apm_decoder decoder(read_file(path));
					result.channels = decoder.channels();
					result.rate = decoder.sample_rate();
					result.samples = decoder.decode_all();
					result.name = resource->name;
					result.volume = static_cast<float>(std::min(resource->volume, 127u)) / 127.0f;
					break;
				}
		}
		catch(const std::exception& e) { SDL_Log("Cannot load music %s: %s", resource->name.c_str(), e.what()); }
		return result;
	}

	void sound_player::request_theme(std::uint32_t id, std::uint32_t event, float volume)
	{
		if(m_music_index >= 0 && m_theme.id == id && !m_pending_theme) return;
		auto key = bnm_id::of(id);
		const auto* b = bank(key.bank);
		const auto* resource = b ? b->resource(key.index) : nullptr;
		if(!resource) return;
		theme next;
		next.id = id; next.event = event; next.volume = volume;
		next.restart = resource->group10_count; next.cycles = resource->group8_count;
		next.entries = b->entries(*resource);
		if(next.entries.empty()) return;
		auto tail = b->theme_tail(*resource);
		for(const auto& entry : next.entries) next.segments.push_back(load_music_segment(entry[0]));
		if(tail) next.tail = load_music_segment(tail);
		if(m_music_index < 0) start_theme(std::move(next));
		else
		{
			// 0x4595D0: transition 2 uses the old theme's tail; 1 waits for the boundary.
			// Transition 0 asks the DLL for an immediate switch, approximated here as a cut.
			auto mode = m_music_tail ? 1u : m_theme.entries[m_music_index][4];
			m_pending_theme = std::make_unique<theme>(std::move(next));
			if(mode == 0) next_music_segment();
			else if(mode == 2 && !m_theme.tail.samples.empty()) m_music_tail = false;
		}
	}

	void sound_player::log_music(const music_segment& segment, int index)
	{
		if(m_music_log) SDL_Log("Music %u:%u segment %d %s %.3fs", m_theme.id >> 16, m_theme.id & 0xffff, index, segment.name.c_str(), static_cast<double>(m_music_frames) / 22050.0);
	}

	void sound_player::start_theme(theme next)
	{
		m_theme = std::move(next); m_music_index = 0; m_music_tail = false;
		m_music_position = 0; m_repeat_index = -1; m_repeat = 0xffffffff; m_cycles = 0;
		if(m_theme.entries[0][1]) { m_repeat_index = 0; m_repeat = m_theme.entries[0][3]; }
		log_music(m_theme.segments[0], 0);
	}

	void sound_player::next_music_segment()
	{
		m_music_position = 0;
		if(m_pending_theme)
		{
			if(!m_music_tail && m_music_index >= 0 && m_theme.entries[m_music_index][4] == 2 && !m_theme.tail.samples.empty())
			{
				m_music_tail = true; log_music(m_theme.tail, -1); return;
			}
			auto next = std::move(*m_pending_theme); m_pending_theme.reset(); start_theme(std::move(next)); return;
		}
		if(m_music_tail) { m_music_index = -1; return; }
		// 0x459460: a marked entry supplies the repeat count; a later +8 entry takes the jump.
		const auto& current = m_theme.entries[m_music_index];
		if(current[2] && m_repeat != 0)
		{
			--m_repeat; m_music_index = m_repeat_index < 0 ? 0 : m_repeat_index;
		}
		else
		{
			if(m_repeat == 0) m_repeat_index = -1;
			++m_music_index;
			if(static_cast<std::size_t>(m_music_index) >= m_theme.entries.size())
			{
				if(m_theme.cycles && ++m_cycles >= m_theme.cycles)
				{
					if(m_theme.tail.samples.empty()) { m_music_index = -1; return; }
					m_music_tail = true; log_music(m_theme.tail, -1); return;
				}
				m_music_index = static_cast<int>(std::min<std::size_t>(m_theme.restart, m_theme.entries.size() - 1));
				// On a restart, find the most recent repeat marker unless a jump boundary intervenes.
				for(int i = m_music_index; i >= 0; --i)
				{
					const auto& candidate = m_theme.entries[i];
					if(candidate[1]) { m_repeat_index = i; m_repeat = candidate[3]; break; }
					if(i == 0 || m_theme.entries[i - 1][2]) break;
				}
			}
		}
		const auto& entry = m_theme.entries[m_music_index];
		if(entry[1] && m_repeat_index < 0) { m_repeat_index = m_music_index; m_repeat = entry[3]; }
		log_music(m_theme.segments[m_music_index], m_music_index);
	}

	void sound_player::place(std::uint32_t source, float gain, float pan)
	{
		std::lock_guard lock(m_mutex);
		for(auto& v : m_voices)
		{
			if(v.source == source)
			{
				v.gain = std::clamp(gain, 0.0f, 1.0f);
				v.pan = std::clamp(pan, -1.0f, 1.0f);
			}
		}
	}

	void sound_player::stop_all()
	{
		std::lock_guard lock(m_mutex);
		m_voices.clear();
		m_music_index = -1;
		m_pending_theme.reset();
	}

	void sound_player::stop_source(std::uint32_t source)
	{
		std::lock_guard lock(m_mutex);
		std::erase_if(m_voices, [&](const voice& v) { return v.source == source; });
	}

	void sound_player::set_paused(bool paused)
	{
		if(m_stream)
		{
			if(paused)
				SDL_PauseAudioStreamDevice(m_stream);
			else
				SDL_ResumeAudioStreamDevice(m_stream);
		}
	}

	void SDLCALL sound_player::feed(void* self, SDL_AudioStream* stream, int additional, int)
	{
		if(additional <= 0)
			return;
		auto& player = *static_cast<sound_player*>(self);
		std::lock_guard lock(player.m_mutex);
		std::size_t frames = (static_cast<std::size_t>(additional) + 3) / 4;
		std::vector<std::int16_t> output(frames * 2);
		for(std::size_t frame = 0; frame < frames; ++frame)
		{
			float mix[2]{};
			if(player.m_music_index >= 0)
			{
				auto* segment = player.m_music_tail ? &player.m_theme.tail : &player.m_theme.segments[player.m_music_index];
				if(!segment->channels || player.m_music_position >= segment->samples.size() / segment->channels)
				{
					player.next_music_segment();
					segment = player.m_music_index < 0 ? nullptr : (player.m_music_tail ? &player.m_theme.tail : &player.m_theme.segments[player.m_music_index]);
				}
				if(segment && segment->channels && !segment->samples.empty())
				{
					std::size_t count = segment->samples.size() / segment->channels;
					std::size_t a = std::min(static_cast<std::size_t>(player.m_music_position), count - 1);
					std::size_t b = std::min(a + 1, count - 1);
					float fraction = static_cast<float>(player.m_music_position - a);
					for(unsigned c = 0; c < 2; ++c)
					{
						unsigned input = segment->channels == 1 ? 0 : c;
						mix[c] += (segment->samples[a * segment->channels + input] * (1 - fraction) + segment->samples[b * segment->channels + input] * fraction) * segment->volume * player.m_theme.volume;
					}
					player.m_music_position += static_cast<double>(segment->rate) / 22050.0;
				}
			}
			++player.m_music_frames;
			for(auto& v : player.m_voices)
			{
				std::size_t count = v.samples.size() / v.channels;
				if(v.position >= count)
				{
					if(!v.loop)
						continue;
					v.position = std::fmod(v.position, static_cast<double>(count));
				}
				std::size_t a = static_cast<std::size_t>(v.position);
				std::size_t b = a + 1 < count ? a + 1 : (v.loop ? 0 : a);
				float fraction = static_cast<float>(v.position - a);
				for(unsigned channel = 0; channel < 2; ++channel)
				{
					unsigned input = v.channels == 1 ? 0 : channel;
					float sample = v.samples[a * v.channels + input] * (1 - fraction) + v.samples[b * v.channels + input] * fraction;
					float balance = channel == 0 ? 1 - std::max(0.0f, v.pan) : 1 + std::min(0.0f, v.pan);
					mix[channel] += sample * v.volume * v.gain * balance;
				}
				v.position += v.step;
			}
			for(unsigned channel = 0; channel < 2; ++channel)
				output[frame * 2 + channel] = static_cast<std::int16_t>(std::clamp(mix[channel], -32768.0f, 32767.0f));
		}
		std::erase_if(player.m_voices, [](const voice& v) { return !v.loop && v.position >= v.samples.size() / v.channels; });
		SDL_PutAudioStreamData(stream, output.data(), static_cast<int>(output.size() * sizeof(std::int16_t)));
	}
}
