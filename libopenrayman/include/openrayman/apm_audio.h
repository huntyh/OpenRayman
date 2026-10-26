// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openrayman
{
	// APM files hold the game's music as Ubisoft's flavour of IMA ADPCM (4 bits per sample).
	// Decodes to interleaved signed 16-bit PCM.
	class apm_decoder
	{
	public:
		// Takes ownership of the file contents. Throws format_error if it isn't a valid APM file.
		explicit apm_decoder(std::vector<std::byte> data);

		std::uint16_t channels() const { return m_channels; }
		std::uint32_t sample_rate() const { return m_sample_rate; }

		// Total number of sample frames (one sample per channel).
		std::size_t frames() const { return m_frames; }
		std::size_t frames_left() const { return m_frames - m_position; }

		// Decodes up to out.size() / channels() frames into out. Returns the number of frames written.
		std::size_t decode(std::span<std::int16_t> out);

		// Decodes the whole file from the start.
		std::vector<std::int16_t> decode_all();

		// Rewinds to the first frame.
		void reset();

	private:
		struct channel_state
		{
			std::int32_t predictor = 0;
			std::int32_t step_index = 0;
		};

		static std::int16_t decode_nibble(std::uint8_t nibble, channel_state& state);

		std::vector<std::byte> m_data;
		std::uint16_t m_channels = 0;
		std::uint32_t m_sample_rate = 0;
		std::size_t m_audio_offset = 0;
		std::size_t m_frames = 0;
		std::size_t m_position = 0;

		channel_state m_initial[2];
		channel_state m_state[2];
	};
}
