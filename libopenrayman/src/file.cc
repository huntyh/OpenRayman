#include <openrayman/file.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>

namespace openrayman
{
	std::vector<std::byte> read_file(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::binary | std::ios::ate);
		if(!stream)
			throw std::runtime_error("could not open " + path.string());

		std::vector<std::byte> data(static_cast<std::size_t>(stream.tellg()));
		stream.seekg(0);
		if(!stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size())))
			throw std::runtime_error("could not read " + path.string());
		return data;
	}

	static bool equals_ignore_case(const std::string& a, const std::string& b)
	{
		return std::ranges::equal(a, b, [](unsigned char x, unsigned char y) {
			return std::tolower(x) == std::tolower(y);
		});
	}

	std::optional<std::filesystem::path> find_case_insensitive(
		const std::filesystem::path& base, const std::filesystem::path& relative)
	{
		// Game paths use backslashes, which aren't separators outside of Windows.
		std::string normalized = relative.generic_string();
		std::ranges::replace(normalized, '\\', '/');

		std::filesystem::path current = base;
		for(const auto& component : std::filesystem::path(normalized))
		{
			if(component.empty() || component == "/")
				continue;

			std::filesystem::path exact = current / component;
			std::error_code ec;
			if(std::filesystem::exists(exact, ec))
			{
				current = exact;
				continue;
			}

			bool found = false;
			for(const auto& entry : std::filesystem::directory_iterator(current, ec))
			{
				if(equals_ignore_case(entry.path().filename().string(), component.string()))
				{
					current = entry.path();
					found = true;
					break;
				}
			}
			if(!found)
				return std::nullopt;
		}
		return current;
	}
}
