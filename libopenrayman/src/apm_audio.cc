#include <openrayman/apm_audio.h>
#include <openrayman/binary_reader.h>
#include <openrayman/error.h>

#include <algorithm>
#include <cstring>

// Format notes come from the Ray2Get project's APM description and the old OpenRayman decoder.
//
// 0x00  WAVEFORMATEX, format tag 0x2000, 4 bits per sample
// 0x14  "vs12"
// 0x2C  right channel: i32 initial predictor, i32 initial step index
// 0x38  left channel:  i32 initial predictor, i32 initial step index
// 0x60  "DATA", followed by the nibbles. Each byte pair holds (left, right),
//       high nibble first, so one byte pair covers two stereo frames.
//
// Pairing the first byte with the 0x2C state (as the old decoder did) starts that channel
// from the wrong predictor, which shows up as heavy clipping on one side of some tracks.

namespace openrayman
{
	constexpr std::uint16_t apm_format_tag = 0x2000;
	constexpr std::size_t apm_data_offset = 0x64;

	static constexpr int index_table[16] = {
		-1, -1, -1, -1, 2, 4, 6, 8,
		-1, -1, -1, -1, 2, 4, 6, 8,
	};

	static constexpr int step_table[89] = {
		7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
		19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
		50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
		130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
		337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
		876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
		2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
		5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
		15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
	};

	apm_decoder::apm_decoder(std::vector<std::byte> data) : m_data(std::move(data))
	{
		binary_reader reader(m_data);
		if(reader.u16() != apm_format_tag)
			throw format_error("not an APM file (bad format tag)");
		m_channels = reader.u16();
		m_sample_rate = reader.u32();
		if(m_channels != 1 && m_channels != 2)
			throw format_error("APM files must be mono or stereo");

		reader.seek(0x14);
		if(reader.string(4) != "vs12")
			throw format_error("not an APM file (missing vs12)");

		// Mono files (only outro1.apm) keep their single channel in the "right" slot.
		reader.seek(0x2C);
		channel_state& first = m_channels == 2 ? m_initial[1] : m_initial[0];
		first.predictor = reader.i32();
		first.step_index = reader.i32();
		if(m_channels == 2)
		{
			reader.seek(0x38);
			m_initial[0].predictor = reader.i32();
			m_initial[0].step_index = reader.i32();
		}
		for(auto& s : m_initial)
		{
			if(s.step_index < 0 || s.step_index > 88)
				throw format_error("APM initial step index out of range");
		}

		reader.seek(0x60);
		if(reader.string(4) != "DATA")
			throw format_error("APM DATA chunk not found");

		m_audio_offset = apm_data_offset;
		// One nibble per sample, so a byte holds one stereo frame or two mono frames.
		m_frames = (m_data.size() - m_audio_offset) * (m_channels == 2 ? 1 : 2);
		reset();
	}

	std::int16_t apm_decoder::decode_nibble(std::uint8_t nibble, channel_state& state)
	{
		int step = step_table[state.step_index];
		int diff = step >> 3;
		if(nibble & 1)
			diff += step >> 2;
		if(nibble & 2)
			diff += step >> 1;
		if(nibble & 4)
			diff += step;
		if(nibble & 8)
			diff = -diff;

		state.predictor = std::clamp(state.predictor + diff, -32768, 32767);
		state.step_index = std::clamp(state.step_index + index_table[nibble], 0, 88);
		return static_cast<std::int16_t>(state.predictor);
	}

	std::size_t apm_decoder::decode(std::span<std::int16_t> out)
	{
		std::size_t count = std::min(out.size() / m_channels, frames_left());
		if(m_channels == 1)
		{
			for(std::size_t n = 0; n < count; n++, m_position++)
			{
				auto byte = static_cast<std::uint8_t>(m_data[m_audio_offset + m_position / 2]);
				out[n] = decode_nibble((m_position & 1) ? (byte & 0xF) : (byte >> 4), m_state[0]);
			}
			return count;
		}

		for(std::size_t n = 0; n < count; n++, m_position++)
		{
			// Frames 2k and 2k+1 share the byte pair at 2k.
			std::size_t pair = m_audio_offset + (m_position & ~std::size_t(1));
			auto left_byte = static_cast<std::uint8_t>(m_data[pair]);
			auto right_byte = pair + 1 < m_data.size() ? static_cast<std::uint8_t>(m_data[pair + 1]) : std::uint8_t(0);
			int shift = (m_position & 1) ? 0 : 4;

			out[n * 2 + 0] = decode_nibble((left_byte >> shift) & 0xF, m_state[0]);
			out[n * 2 + 1] = decode_nibble((right_byte >> shift) & 0xF, m_state[1]);
		}
		return count;
	}

	std::vector<std::int16_t> apm_decoder::decode_all()
	{
		reset();
		std::vector<std::int16_t> pcm(m_frames * m_channels);
		decode(pcm);
		return pcm;
	}

	void apm_decoder::reset()
	{
		m_state[0] = m_initial[0];
		m_state[1] = m_initial[1];
		m_position = 0;
	}
}
