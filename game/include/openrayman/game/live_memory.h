#pragma once

#include <openrayman/memory_image.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace openrayman::game
{
	// The game's memory while it runs: every block of Fix and the level, writable, at the address the
	// block itself announced. Like the game's loader, every pointer is relocated on the way in, so a
	// pointer's value is simply the address of what it points at (the files' own views of some blocks
	// differ by a few bytes, see docs/formats/sna.md). Blocks the game only allocates at runtime are
	// zero-filled.
	class live_memory
	{
	public:
		explicit live_memory(const memory_image& image);

		// True if [address, address + size) lies inside one block.
		bool contains(std::uint32_t address, std::size_t size = 1) const;

		// The address of a place in the loaded image.
		static std::uint32_t address(const memory_location& at) { return at.address(); }

		template<typename T> T read(std::uint32_t address) const
		{
			static_assert(std::is_trivially_copyable_v<T>);
			T value;
			std::memcpy(&value, span(address, sizeof(T)).data(), sizeof(T));
			return value;
		}

		template<typename T> void write(std::uint32_t address, const T& value)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			std::memcpy(span(address, sizeof(T)).data(), &value, sizeof(T));
		}

		std::uint32_t u32(std::uint32_t address) const { return read<std::uint32_t>(address); }
		float f32(std::uint32_t address) const { return read<float>(address); }
		std::uint8_t u8(std::uint32_t address) const { return read<std::uint8_t>(address); }

		// The bytes at an address. Throws format_error outside every block.
		std::span<std::byte> span(std::uint32_t address, std::size_t size);
		std::span<const std::byte> span(std::uint32_t address, std::size_t size) const;

		// How many pointers were relocated when loading, and how many of those changed value.
		std::size_t relocated() const { return m_relocated; }
		std::size_t moved() const { return m_moved; }

	private:
		struct region
		{
			std::uint32_t base = 0;
			std::vector<std::byte> data;
		};
		const region* find(std::uint32_t address, std::size_t size) const;

		std::map<std::uint32_t, region> m_regions; // by base address
		std::size_t m_relocated = 0, m_moved = 0;
	};
}
