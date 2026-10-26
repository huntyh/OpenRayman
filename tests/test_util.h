#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

// Little-endian byte buffer builder for synthetic test files.
struct bytes
{
	std::vector<std::byte> data;

	bytes& u8(std::uint8_t v) { data.push_back(static_cast<std::byte>(v)); return *this; }
	bytes& u16(std::uint16_t v) { return u8(v & 0xFF).u8(v >> 8); }
	bytes& u32(std::uint32_t v) { return u16(v & 0xFFFF).u16(v >> 16); }
	bytes& f32(float v) { std::uint32_t u; std::memcpy(&u, &v, 4); return u32(u); }
	bytes& str(const std::string& s) { for(char c : s) u8(static_cast<std::uint8_t>(c)); return *this; }
	bytes& pad_to(std::size_t size) { while(data.size() < size) u8(0); return *this; }
};

inline std::filesystem::path write_temp(const std::string& name, const std::vector<std::byte>& data)
{
	auto path = std::filesystem::temp_directory_path() / ("openrayman_test_" + name);
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
	return path;
}

// The Rayman 2 install to test against, from the OPENRAYMAN_GAME_DIR environment variable.
inline std::optional<std::filesystem::path> real_game_dir()
{
#ifdef _MSC_VER
	char* value = nullptr;
	std::size_t length = 0;
	if(_dupenv_s(&value, &length, "OPENRAYMAN_GAME_DIR") != 0 || !value)
		return std::nullopt;
	std::string result(value);
	std::free(value);
#else
	const char* value = std::getenv("OPENRAYMAN_GAME_DIR");
	if(!value)
		return std::nullopt;
	std::string result(value);
#endif
	if(result.empty())
		return std::nullopt;
	return std::filesystem::path(result);
}
