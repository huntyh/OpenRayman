// This file is part of libopenrayman, licensed under the GNU Lesser General Public License v3.
// See LICENSE_libopenrayman in the repository root.

#include <openrayman/save_file.h>
#include <openrayman/binary_reader.h>
#include <openrayman/error.h>
#include <openrayman/file.h>

#include <algorithm>
#include <array>
#include <set>
#include <fstream>
#include <limits>

namespace openrayman
{
	save_file::save_file(const std::filesystem::path& path)
	{
		*this = decode(read_file(path));
	}

	std::vector<std::byte> save_file::encode(std::span<const std::byte> plain)
	{
		if(plain.size() > std::numeric_limits<std::uint32_t>::max()) throw format_error("save too large");
		std::vector<std::uint8_t> stream;
		for(std::size_t at = 0; at < plain.size(); at += 8)
		{
			stream.push_back(0); stream.push_back(255);
			for(unsigned i = 0; i < 8; ++i)
				stream.push_back(at + i < plain.size() ? std::to_integer<std::uint8_t>(plain[at + i]) : 0);
		}
		if(plain.size() % 8) stream.push_back(std::uint8_t(plain.size() % 8));
		std::uint8_t sum = 0;
		for(auto v : stream) sum = std::uint8_t(sum + v);
		std::vector<std::byte> result{std::byte(sum ^ 0x53)};
		auto length = std::uint32_t(plain.size()) ^ 0x54555657u;
		for(unsigned i = 0; i < 4; ++i) result.push_back(std::byte((length >> (8 * i)) & 255));
		std::uint8_t counter = 0x57;
		for(auto v : stream)
		{
			unsigned reversed = 0;
			for(unsigned b = 0; b < 8; ++b) reversed |= ((counter >> b) & 1u) << (7 - b);
			result.push_back(std::byte(v ^ 0xb9u ^ reversed)); ++counter;
		}
		return result;
	}

	void save_file::write(const std::filesystem::path& path, std::span<const std::byte> plain)
	{
		auto encoded = encode(plain);
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if(!out || !out.write(reinterpret_cast<const char*>(encoded.data()), std::streamsize(encoded.size())))
			throw format_error("cannot write save file: " + path.string());
	}

	save_file save_file::decode(std::span<const std::byte> encoded)
	{
		binary_reader r(encoded);
		std::uint8_t checksum = r.u8() ^ 0x53;
		const auto length = r.u32() ^ 0x54555657u;
		std::vector<std::uint8_t> stream;
		std::uint8_t counter = 0x57;
		for(std::size_t i = 5; i < encoded.size(); ++i, ++counter)
		{
			unsigned reversed = 0;
			for(unsigned b = 0; b < 8; ++b) reversed |= ((counter >> b) & 1u) << (7 - b);
			auto v = std::uint8_t(std::to_integer<unsigned>(encoded[i]) ^ 0xB9u ^ reversed);
			stream.push_back(v);
			checksum = std::uint8_t(checksum - v);
		}
		if(checksum) throw format_error("save checksum mismatch");
		// Each token produces at most eight bytes. Check before allocating from an untrusted length.
		if(std::uint64_t(length) > std::uint64_t(stream.size() / 2) * 8)
			throw format_error("save expanded length exceeds its token stream");
		save_file result;
		result.bytes.reserve(length);
		std::array<std::array<std::uint8_t, 8>, 256> dictionary;
		for(unsigned i = 0; i < 256; ++i) dictionary[i].fill(std::uint8_t(i));
		std::array<std::uint8_t, 8> pending{};
		bool have_pending = false;
		auto emit = [&](std::size_t count) {
			if(count > length - result.bytes.size()) throw format_error("save has excess expanded bytes");
			for(std::size_t i = 0; i < count; ++i) result.bytes.push_back(std::byte(pending[i]));
		};
		std::size_t at = 0;
		while(at < stream.size())
		{
			unsigned index = stream[at++];
			if(at == stream.size())
			{
				if(!have_pending || index == 0 || index > 7) throw format_error("invalid save final block length");
				emit(index);
				have_pending = false;
				break;
			}
			if(have_pending) emit(8);
			unsigned mask = stream[at++];
			for(unsigned b = 0; b < 8; ++b)
				if(mask & (1u << b))
				{
					if(at == stream.size()) throw format_error("truncated save dictionary patch");
					dictionary[index][b] = stream[at++];
				}
			pending = dictionary[index];
			have_pending = true;
		}
		if(have_pending) emit(8);
		if(result.bytes.size() != length) throw format_error("save expanded length mismatch");
		return result;
	}

	namespace
	{
		std::size_t scalar_size(std::uint32_t type)
		{
			if(type <= 2) return 1;
			if(type <= 4) return 2;
			if(type == 11) return 12;
			if(type <= 24 && type != 10 && !(type >= 18 && type <= 23)) return 4;
			throw format_error("unsupported registered save variable type " + std::to_string(type));
		}
	}

	save_registry read_save_registry(const memory_image& m, const level_header& h, const scene& tree)
	{
		save_registry result;
		std::vector<save_entry> ordinary, bits;
		std::set<std::uint32_t> visited;
		auto actor = [&](const memory_location& a, std::optional<std::size_t> fix, std::optional<std::size_t> node) {
			if(!visited.insert(a.address()).second) return;
			auto standard = m.follow(a + 4);
			if(standard && m.read_u32(*standard + 0x14) == 5)
			{
				save_entry e;
				e.actor = a; e.fix_actor = fix; e.node = node; e.address = (*standard + 0x29).address();
				e.flags = 0x200; e.bit = 0;
				bits.push_back(e);
			}
			if(standard && (m.read_u32(*standard + 0x24) & 0x80000000u))
				for(std::uint32_t offset : { 0x21u, 0x22u, 0x23u })
				{
					save_entry e;
					e.actor = a; e.fix_actor = fix; e.node = node; e.standard_offset = offset;
					e.address = (*standard + offset).address(); e.type = 2; e.size = 1; e.flags = 0x80;
					ordinary.push_back(e);
				}
			auto brain = m.follow(a + 0x0c);
			auto mind = brain ? m.follow(*brain) : std::nullopt;
			auto model = mind ? m.follow(*mind) : std::nullopt;
			auto decl = model ? m.follow(*model + 8) : std::nullopt;
			auto vars = mind ? m.follow(*mind + 0x0c) : std::nullopt;
			auto buffer = vars ? m.follow(*vars + 8) : std::nullopt;
			if(!decl || !buffer) return;
			auto info = m.follow(*decl + 4);
			auto count = m.read_u32(*decl + 0x0c) & 0xff;
			if(count && !info) throw format_error("save variable declaration has no info array");
			for(std::uint32_t v = 0; v < count; ++v)
			{
				auto d = *info + v * 16;
				auto flags = std::uint16_t(m.read_u32(d + 8));
				if(!(flags & 0x3c0)) continue;
				auto type = m.read_u32(d + 4);
				auto value = *buffer + m.read_u32(d);
				save_entry e;
				e.actor = a; e.fix_actor = fix; e.node = node; e.variable = v; e.flags = flags;
				auto add = [&](memory_location at, std::uint32_t t) {
					// Pointer registrations use a module/offset pair in the file. No such entries
					// occur in the installed general/level registries; refuse to misapply them.
					if(t == 8 || t == 9 || t == 12 || t == 13 || t == 15)
						throw format_error("pointer save registration is not supported");
					e.type = t; e.address = at.address(); e.size = scalar_size(t);
					ordinary.push_back(e);
				};
				if(type >= 18 && type <= 23)
				{
					auto element_type = m.read_u32(value);
					auto elements = m.read_u32(value + 4) & 0xff;
					for(std::uint32_t k = 0; k < elements; ++k)
					{
						e.element = k;
						add(value + 8 + k * (element_type == 11 ? 12u : 4u), element_type);
					}
				}
				else add(value, type);
			}
		};
		// 0x4062F0 initializes the dynamic subtree (0x406620, children first), then
		// the always list (0x40BC30). Fix header order is not registration order.
		auto register_actor = [&](const memory_location& a) {
			std::optional<std::size_t> fix, node;
			for(std::size_t i = 0; i < h.fix_actors.size(); ++i)
				if(h.fix_actors[i] == a) { fix = i; break; }
			for(std::size_t n = 0; n < tree.nodes().size(); ++n)
				if(tree.node(n).type == super_object_type::actor && tree.node(n).data == a) { node = n; break; }
			actor(a, fix, node);
		};
		auto walk = [&](auto&& self, std::size_t n) -> void {
			for(auto child : tree.node(n).children) self(self, child);
			if(tree.node(n).type == super_object_type::actor && tree.node(n).data)
				register_actor(*tree.node(n).data);
		};
		auto dynamic = tree.find(h.dynamic_world);
		if(!dynamic) throw format_error("save registry has no dynamic world");
		walk(walk, *dynamic);
		for(const auto& a : h.always_actors) register_actor(a);
		std::reverse(ordinary.begin(), ordinary.end());
		for(auto e : ordinary)
		{
			if(e.flags & 0xc0) { e.offset = result.general_size; result.general_size += e.size; result.general.push_back(e); }
			if(e.flags & 0x300) { e.offset = result.level_size; result.level_size += e.size; result.level.push_back(e); }
		}
		std::size_t packed = 0;
		for(auto e : bits)
		{
			e.offset = result.level_size + packed / 8;
			e.bit = unsigned(packed++ % 8);
			result.level.push_back(e);
		}
		result.level_size += (packed + 7) / 8;
		return result;
	}

	std::filesystem::path save_slot_folder(const std::filesystem::path& saves, unsigned slot)
	{
		if(slot < 1 || slot > 3) throw format_error("save slot must be 1, 2 or 3");
		auto path = find_case_insensitive(saves, "Slot" + std::to_string(slot));
		if(!path) throw format_error("save slot folder is missing");
		return *path;
	}
}
