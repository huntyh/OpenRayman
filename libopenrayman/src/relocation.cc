// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/relocation.h>
#include <openrayman/binary_reader.h>
#include <openrayman/error.h>
#include <openrayman/mask.h>

#include <cmath>
#include <format>

namespace openrayman
{
	std::size_t relocation_table::pointer_count() const
	{
		std::size_t count = 0;
		for(const auto& [id, list] : blocks)
			count += list.size();
		return count;
	}

	// Table layout: u8 block count, u32 unknown, then per block: u8 module, u8 id, u32 count,
	// and count entries of (u32 address, u8 target module, u8 target id, 2 padding bytes).
	// Works on any reader with u8() and u32(), so tables can be parsed while being unmasked.
	template<typename Reader>
	static relocation_table parse_table(Reader& r)
	{
		relocation_table table;
		std::uint8_t block_count = r.u8();
		r.u32();
		for(std::uint8_t b = 0; b < block_count; b++)
		{
			block_id id{ r.u8(), r.u8() };
			std::uint32_t count = r.u32();
			if(count > 10'000'000)
				throw format_error("relocation table block " + id.to_string() + " has an implausible pointer count");

			std::vector<relocation>& list = table.blocks[id];
			list.reserve(count);
			for(std::uint32_t n = 0; n < count; n++)
			{
				relocation reloc;
				reloc.address = r.u32();
				reloc.target = { r.u8(), r.u8() };
				r.u8();
				r.u8();
				list.push_back(reloc);
			}
		}
		return table;
	}

	relocation_table parse_relocation_table(std::span<const std::byte> data)
	{
		binary_reader r(data);
		relocation_table table = parse_table(r);
		if(!r.at_end())
			throw format_error(std::format("{} unexpected bytes after the relocation table", r.remaining()));
		return table;
	}

	// The k-th u32 after a 16 byte key (a, b, c, d) is stored as (value ^ (b + d*k)) + (a + c*k).
	struct dat_key
	{
		std::uint32_t a, b, c, d;

		std::uint32_t decode(std::uint32_t raw, std::uint32_t k) const
		{
			return (raw - (a + c * k)) ^ (b + d * k);
		}
	};

	// Reads masked bytes from a file through a small buffer.
	class masked_file_reader
	{
	public:
		masked_file_reader(const std::filesystem::path& path, std::uint64_t offset, std::uint32_t mask_value)
			: m_stream(path, std::ios::binary), m_mask(mask_value)
		{
			m_stream.seekg(static_cast<std::streamoff>(offset));
		}

		std::uint8_t u8()
		{
			if(m_position == m_buffer.size())
			{
				m_buffer.resize(64 * 1024);
				m_stream.read(reinterpret_cast<char*>(m_buffer.data()), static_cast<std::streamsize>(m_buffer.size()));
				m_buffer.resize(static_cast<std::size_t>(m_stream.gcount()));
				m_position = 0;
				if(m_buffer.empty())
					throw format_error("unexpected end of LEVELS0.DAT");
			}
			return m_mask.decode(m_buffer[m_position++]);
		}

		std::uint32_t u32()
		{
			std::uint32_t v = u8();
			v |= std::uint32_t(u8()) << 8;
			v |= std::uint32_t(u8()) << 16;
			v |= std::uint32_t(u8()) << 24;
			return v;
		}

	private:
		std::ifstream m_stream;
		openrayman::mask m_mask;
		std::vector<std::uint8_t> m_buffer;
		std::size_t m_position = 0;
	};

	static std::vector<std::byte> read_brain_at(const std::filesystem::path& path, std::uint64_t offset, std::size_t size)
	{
		std::ifstream stream(path, std::ios::binary);
		stream.seekg(static_cast<std::streamoff>(offset));
		std::vector<std::byte> data(size);
		if(!stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size)))
			throw format_error(std::format("could not read LEVELS0.DAT at 0x{:X}", offset));
		return data;
	}

	static dat_key read_key(binary_reader& r)
	{
		return { r.u32(), r.u32(), r.u32(), r.u32() };
	}

	levels_dat::levels_dat(const std::filesystem::path& path) : m_path(path)
	{
		m_file_size = std::filesystem::file_size(path);
		std::vector<std::byte> header = read_brain_at(path, 0, 16 + 8 * 4);
		binary_reader r(header);
		dat_key key = read_key(r);

		std::uint32_t values[8];
		for(std::uint32_t k = 0; k < 8; k++)
			values[k] = key.decode(r.u32(), k);

		m_sectors = values[1];
		m_level_count = values[2] / 4;
		m_copies = values[4];
		m_base = values[6];
		m_sector_size = values[7];

		if(m_sector_size != 2048 || m_copies == 0 || m_copies > 64 || m_level_count == 0 || m_level_count > 1000
			|| std::uint64_t(m_sectors) * m_sector_size + m_base > m_file_size)
			throw format_error("LEVELS0.DAT header doesn't decode to sensible values");
	}

	std::uint32_t levels_dat::table_offset(std::uint32_t index, std::uint32_t copy) const
	{
		// Each copy of the index lives at a sector picked by this formula from Rayman2.exe.
		double v = 1.06913;
		for(std::uint32_t i = 0; i < copy; i++)
			v = v + 0.69314 * std::abs(std::sin(double(i) * double(i) * 1.69314)) + 0.52658;

		double whole;
		double fraction = std::modf(v, &whole);
		double x = std::floor(fraction * 1e6) * 1e-6;
		auto sector = static_cast<std::uint32_t>(x * m_sectors);
		std::uint64_t index_offset = std::uint64_t(sector) * m_sector_size + m_base;

		std::vector<std::byte> data = read_brain_at(m_path, index_offset, 16 + (std::size_t(index) + 1) * 4);
		binary_reader r(data);
		dat_key key = read_key(r);
		r.seek(16 + std::size_t(index) * 4);
		return key.decode(r.u32(), index);
	}

	relocation_table levels_dat::read_copy(std::uint32_t level, relocation_type type, std::uint32_t copy) const
	{
		auto t = static_cast<std::uint32_t>(type);
		std::uint32_t offset = table_offset(level * 4 + t, copy);
		if(offset >= m_file_size)
			throw format_error("LEVELS0.DAT table offset out of range");

		std::uint32_t seed = level | t << 8 | copy << 16 | ((~copy) & 0xFF) << 24;
		masked_file_reader r(m_path, offset, mask::step(seed));
		r.u32(); // differs per copy, meaning unknown
		return parse_table(r);
	}

	relocation_table levels_dat::read(std::uint32_t level, relocation_type type) const
	{
		if(level >= m_level_count)
			throw format_error(std::format("level index {} out of range, LEVELS0.DAT has {}", level, m_level_count));

		// All copies hold the same table. Copy 0 needs no trigonometry, so it's exact; the others
		// are only a fallback in case floating point rounding picks the wrong sector somewhere.
		std::string first_error;
		for(std::uint32_t copy = 0; copy < m_copies; copy++)
		{
			try
			{
				return read_copy(level, type, copy);
			}
			catch(const format_error& e)
			{
				if(first_error.empty())
					first_error = e.what();
			}
		}
		throw format_error(std::format("no readable copy of table {} for level {}: {}", static_cast<int>(type), level, first_error));
	}
}
