// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#pragma once

#include <openrayman/error.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <type_traits>

namespace openrayman
{
	// Bounds checked little-endian reader over a block of memory.
	// All Rayman 2 PC data is little-endian, so this is the only byte order supported.
	class binary_reader
	{
	public:
		explicit binary_reader(std::span<const std::byte> data) : m_data(data) {}

		std::size_t size() const { return m_data.size(); }
		std::size_t position() const { return m_position; }
		std::size_t remaining() const { return m_data.size() - m_position; }
		bool at_end() const { return m_position >= m_data.size(); }

		void seek(std::size_t position)
		{
			if(position > m_data.size())
				throw format_error("seek past end of data");
			m_position = position;
		}

		void skip(std::size_t count) { seek(m_position + require(count)); }

		template<typename T>
			requires std::is_arithmetic_v<T>
		T read()
		{
			static_assert(std::endian::native == std::endian::little, "big-endian hosts are not supported yet");
			T value;
			std::memcpy(&value, m_data.data() + m_position, require(sizeof(T)));
			m_position += sizeof(T);
			return value;
		}

		std::uint8_t u8() { return read<std::uint8_t>(); }
		std::uint16_t u16() { return read<std::uint16_t>(); }
		std::uint32_t u32() { return read<std::uint32_t>(); }
		std::int16_t i16() { return read<std::int16_t>(); }
		std::int32_t i32() { return read<std::int32_t>(); }
		float f32() { return read<float>(); }

		std::span<const std::byte> bytes(std::size_t count)
		{
			auto result = m_data.subspan(m_position, require(count));
			m_position += count;
			return result;
		}

		// Reads a fixed size string, cutting it off at the first NUL.
		std::string string(std::size_t count)
		{
			auto raw = bytes(count);
			std::string result(reinterpret_cast<const char*>(raw.data()), raw.size());
			if(auto nul = result.find('\0'); nul != std::string::npos)
				result.resize(nul);
			return result;
		}

	private:
		std::size_t require(std::size_t count) const
		{
			if(count > remaining())
				throw format_error("unexpected end of data");
			return count;
		}

		std::span<const std::byte> m_data;
		std::size_t m_position = 0;
	};
}
