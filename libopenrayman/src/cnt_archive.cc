#include <openrayman/cnt_archive.h>
#include <openrayman/binary_reader.h>
#include <openrayman/error.h>
#include <openrayman/file.h>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace openrayman
{
	static std::string normalize(std::string_view path)
	{
		std::string result(path);
		for(char& c : result)
			c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		while(!result.empty() && result.front() == '/')
			result.erase(result.begin());
		return result;
	}

	static std::string read_name(binary_reader& reader, bool xored, std::uint8_t key)
	{
		std::uint32_t length = reader.u32();
		if(length > 1024)
			throw format_error("CNT name length is implausible");
		std::string name = reader.string(length);
		if(xored)
		{
			for(char& c : name)
				c = static_cast<char>(c ^ key);
		}
		return name;
	}

	cnt_archive::cnt_archive(const std::filesystem::path& path) : m_path(path)
	{
		// The directory lives at the start of the archive, contents follow after it.
		// Read the whole file once: the archives are ~30 MB and this keeps the code simple.
		std::vector<std::byte> data = read_file(path);
		binary_reader reader(data);

		std::int32_t directory_count = reader.i32();
		std::int32_t file_count = reader.i32();
		if(directory_count < 0 || file_count < 0 || file_count > 100000)
			throw format_error("not a CNT archive (bad counts)");

		bool xored = reader.u8() != 0;
		bool checksummed = reader.u8() != 0;
		std::uint8_t name_key = reader.u8();

		std::vector<std::string> directories;
		directories.reserve(static_cast<std::size_t>(directory_count));
		for(std::int32_t n = 0; n < directory_count; n++)
			directories.push_back(read_name(reader, xored, name_key));

		// Checksum over the directory names. Not verified, the game doesn't either.
		if(checksummed)
			reader.u8();

		m_entries.reserve(static_cast<std::size_t>(file_count));
		for(std::int32_t n = 0; n < file_count; n++)
		{
			std::int32_t directory = reader.i32();
			std::string name = read_name(reader, xored, name_key);

			cnt_entry entry;
			for(auto& b : entry.xor_key)
				b = reader.u8();
			reader.u32(); // checksum
			entry.offset = reader.u32();
			entry.size = reader.u32();

			if(directory >= 0 && directory < directory_count)
			{
				entry.path = directories[static_cast<std::size_t>(directory)];
				while(!entry.path.empty() && (entry.path.back() == '\\' || entry.path.back() == '/'))
					entry.path.pop_back();
				entry.path += "/" + name;
			}
			else
				entry.path = name;
			std::ranges::replace(entry.path, '\\', '/');

			if(static_cast<std::uint64_t>(entry.offset) + entry.size > data.size())
				throw format_error("CNT entry " + entry.path + " points past the end of the archive");

			m_index.emplace(normalize(entry.path), m_entries.size());
			m_entries.push_back(std::move(entry));
		}
	}

	const cnt_entry* cnt_archive::find(std::string_view path) const
	{
		auto it = m_index.find(normalize(path));
		return it == m_index.end() ? nullptr : &m_entries[it->second];
	}

	std::vector<std::byte> cnt_archive::read(const cnt_entry& entry) const
	{
		std::ifstream stream(m_path, std::ios::binary);
		stream.seekg(entry.offset);
		std::vector<std::byte> data(entry.size);
		if(!stream.read(reinterpret_cast<char*>(data.data()), entry.size))
			throw format_error("could not read CNT entry " + entry.path);

		// The trailing (size % 4) bytes are stored unencoded.
		std::size_t encoded = entry.size - entry.size % 4;
		for(std::size_t n = 0; n < encoded; n++)
			data[n] ^= static_cast<std::byte>(entry.xor_key[n % 4]);
		return data;
	}
}
