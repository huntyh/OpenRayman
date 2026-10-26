#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace openrayman
{
	// The XOR obfuscation used by DSB and SNA files and the relocation tables in LEVELS0.DAT:
	// every byte is XORed with bits 8..15 of a mask, which then advances like a Park-Miller
	// random number generator.
	class mask
	{
	public:
		explicit mask(std::uint32_t value) : m_value(value) {}

		std::uint32_t value() const { return m_value; }

		std::uint8_t decode(std::uint8_t b)
		{
			b ^= static_cast<std::uint8_t>(m_value >> 8);
			m_value = step(m_value);
			return b;
		}

		void decode(std::span<std::byte> data)
		{
			for(std::byte& b : data)
				b = static_cast<std::byte>(decode(static_cast<std::uint8_t>(b)));
		}

		static constexpr std::uint32_t step(std::uint32_t m)
		{
			std::uint32_t x = m ^ 0x75BD924u;
			return 16807 * x - 0x7FFFFFFFu * (x / 0x1F31Du);
		}

	private:
		std::uint32_t m_value;
	};
}
