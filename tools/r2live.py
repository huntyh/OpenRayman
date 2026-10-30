# r2live: watches the real Rayman2.exe while it runs, to compare it with OpenRayman.
#
# It only reads the game's memory (ReadProcessMemory), never writes it. Every game frame it prints
# what changed of Rayman's: behaviour and reflex index, state, script variables, the dynamics base
# block (flags, script speeds, speed, limits, inertia), position and axes. Windows only.
#
#   python tools/r2live.py                     watch until Ctrl+C
#   python tools/r2live.py --vars 22,31,87     also these script variables (indexes as in ortool brain)
#   python tools/r2live.py --log run.txt       write to a file as well
#
# Where things are (Rayman2.exe, GOG):
#   0x500578  Rayman's super object (made with the level's other world pointers, 0x403730)
#   0x5003EC  the game clock (ms), one value per engine frame
#   super object +4 actor, +0x24 global transform (+4 position, +0x10 X, Y, Z axes)
#   actor +0 3D data (+4 current state), +8 dynamics (+0 base block), +0xC brain (+0 mind)
#   mind +0 model (+0 -> behaviours list, +4 -> reflexes list; a list is array, u8 count), +4 intelligence, +8 reflex intelligence,
#   +0xC variable memory (+0 -> declaration: +4 info (0x10 each: offset, type), +0xC u8 count;
#   +8 current buffer); intelligence +8 current behaviour. See docs/formats/scripts.md, dynamics.md.

import argparse
import ctypes
import ctypes.wintypes as wt
import struct
import sys
import time

MAIN_ACTOR = 0x500578
CLOCK = 0x5003EC

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
PROCESS_VM_READ = 0x10
PROCESS_QUERY_INFORMATION = 0x400
TH32CS_SNAPPROCESS = 0x2


class PROCESSENTRY32(ctypes.Structure):
	_fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ProcessID", wt.DWORD),
		("th32DefaultHeapID", ctypes.c_size_t), ("th32ModuleID", wt.DWORD), ("cntThreads", wt.DWORD),
		("th32ParentProcessID", wt.DWORD), ("pcPriClassBase", ctypes.c_long), ("dwFlags", wt.DWORD),
		("szExeFile", ctypes.c_char * 260)]


def find_process(name):
	snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
	entry = PROCESSENTRY32()
	entry.dwSize = ctypes.sizeof(entry)
	found = None
	ok = kernel32.Process32First(snap, ctypes.byref(entry))
	while ok:
		if entry.szExeFile.decode(errors="replace").lower() == name.lower():
			found = entry.th32ProcessID
			break
		ok = kernel32.Process32Next(snap, ctypes.byref(entry))
	kernel32.CloseHandle(snap)
	return found


class game_memory:
	def __init__(self, pid):
		self.handle = kernel32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid)
		if not self.handle:
			raise OSError(f"can't open process {pid} (error {ctypes.get_last_error()})")

	def read(self, address, size):
		buffer = ctypes.create_string_buffer(size)
		done = ctypes.c_size_t()
		if not address or not kernel32.ReadProcessMemory(self.handle, ctypes.c_void_p(address), buffer, size, ctypes.byref(done)):
			return None
		return buffer.raw[:done.value]

	def u32(self, address):
		b = self.read(address, 4)
		return struct.unpack("<I", b)[0] if b and len(b) == 4 else 0

	def u8(self, address):
		b = self.read(address, 1)
		return b[0] if b else 0

	def f32s(self, address, count):
		b = self.read(address, 4 * count)
		return struct.unpack(f"<{count}f", b) if b and len(b) == 4 * count else (float("nan"),) * count


def fmt(values):
	return " ".join(f"{v:.3f}" for v in values)


def list_index(m, list_at, pointer, size):
	# A script list: array pointer, u8 count. Returns the entry's index, or -1.
	array = m.u32(list_at)
	if not array or not pointer or pointer < array:
		return -1
	index, rest = divmod(pointer - array, size)
	return index if rest == 0 and index < m.u8(list_at + 4) else -1


def variable(m, mind, index):
	memory = m.u32(mind + 0xC)
	declaration = m.u32(m.u32(memory))
	if not declaration or index >= m.u8(declaration + 0xC):
		return "-"
	info = m.u32(declaration + 4) + index * 0x10
	at = m.u32(memory + 8) + m.u32(info)
	kind = m.u32(info + 4)
	if kind in (0, 1):
		return str(struct.unpack("<b", m.read(at, 1) or b"\0")[0])
	if kind == 2:
		return str(m.u8(at))
	if kind == 5:
		return str(struct.unpack("<i", m.read(at, 4) or b"\0\0\0\0")[0])
	if kind == 7:
		return f"{m.f32s(at, 1)[0]:.3f}"
	if kind == 11:
		return fmt(m.f32s(at, 3))
	return f"0x{m.u32(at):X} (type {kind})"


def collide_flags(m, material):
	# A game material's collide flags: +0xC collide material, its u16 at +2 (0x42DB30, 0x42DDD0).
	collide = m.u32(material + 0xC) if material and material != 0xCDCDCDCD else 0
	b = m.read(collide + 2, 2) if collide else None
	return struct.unpack("<H", b)[0] if b and len(b) == 2 else 0


def contact_report(m, base):
	# The dynamics contact report (base +0x150): +4 the word scripts read; six 0x28-byte slots from
	# +8: +4 normal, +0x10 point, +0x1C the actor's own zone material, +0x20 the touched surface's
	# (condition 34 reads slot 1's +0x1C, 35 slot 2's; 0x48A4F0).
	s = {}
	report = m.u32(base + 0x150)
	if not report:
		return s
	word = m.u32(report + 4)
	s["contact"] = f"0x{word:X}"
	for slot in range(6):
		entry = report + 8 + slot * 0x28
		own, touched = m.u32(entry + 0x1C), m.u32(entry + 0x20)
		if own or touched:
			s[f"slot{slot}"] = (f"n {fmt(m.f32s(entry + 4, 3))} p {fmt(m.f32s(entry + 0x10, 3))} "
				f"mine 0x{collide_flags(m, own):X} surface 0x{collide_flags(m, touched):X}")
	return s


def active_zones(m, actor):
	# Kind 1's zones (collide set +8, what the physics' sphere cast gathers: 0x494290) and which are
	# on: the set's group list for kind 1 (+0x28: nodes of next, u16 zone index; +8 count) and its
	# states (+0x3C, a byte per zone: 1 on, 0 only if the group names it; 0x40C700).
	s = {}
	collide = m.u32(actor + 0x14)
	if not collide:
		return s
	group = m.u32(collide + 0x28)
	names = []
	count = m.u32(group + 8) if group else 0
	node = m.u32(group) if group else 0
	while node and len(names) < min(count, 32):
		b = m.read(node + 4, 2)
		names.append(str(struct.unpack("<H", b)[0]) if b else "?")
		node = m.u32(node)
	states = m.u32(collide + 0x3C)
	raw = m.read(states, 12) if states else None
	s["zones"] = "group " + (",".join(names) or "-") + " | states " + (" ".join(str(x) for x in raw) if raw else "-")
	return s


def snapshot(m, variables, contacts=False, zones=False):
	s = {}
	super_object = m.u32(MAIN_ACTOR)
	actor = m.u32(super_object + 4) if super_object else 0
	if not actor:
		return s
	s["super"] = f"0x{super_object:X}"
	t = m.u32(super_object + 0x24)
	if t:
		s["at"] = fmt(m.f32s(t + 4, 3))
		s["axes"] = "X " + fmt(m.f32s(t + 0x10, 3)) + " Y " + fmt(m.f32s(t + 0x1C, 3)) + " Z " + fmt(m.f32s(t + 0x28, 3))
	data = m.u32(actor)
	if data:
		s["state"] = f"0x{m.u32(data + 4):X}"
	dynamics = m.u32(actor + 8)
	base = m.u32(dynamics) if dynamics else 0
	if base:
		s["flags"] = f"0x{m.u32(base + 8):X} end 0x{m.u32(base + 0xC):X}"
		s["imposed"] = fmt(m.f32s(base + 0x24, 3))
		s["proposed"] = fmt(m.f32s(base + 0x30, 3))
		s["speed"] = fmt(m.f32s(base + 0x3C, 3))
		s["inertia"] = fmt(m.f32s(base + 0x154, 3))
		s["limit"] = fmt(m.f32s(base + 0x178, 3))
		if contacts:
			s.update(contact_report(m, base))
	if zones:
		s.update(active_zones(m, actor))
	brain = m.u32(actor + 0xC)
	mind = m.u32(brain) if brain else 0
	if mind:
		model = m.u32(mind)
		normal = m.u32(mind + 4)
		reflex = m.u32(mind + 8)
		s["behaviour"] = str(list_index(m, m.u32(model), m.u32(normal + 8), 0xC)) if normal else "-"
		s["reflex"] = str(list_index(m, m.u32(model + 4), m.u32(reflex + 8), 0xC)) if reflex else "-"
		for v in variables:
			s[f"var{v}"] = variable(m, mind, v)
	return s


def main():
	parser = argparse.ArgumentParser(description="Watch Rayman in the running Rayman2.exe (read-only).")
	parser.add_argument("--vars", default="", help="script variable indexes, comma separated")
	parser.add_argument("--log", help="also write the output to this file")
	parser.add_argument("--process", default="Rayman2.exe")
	parser.add_argument("--quiet", default="at,axes,speed,slot0,slot1,slot2,slot3,slot4,slot5", help="fields printed only with another change")
	parser.add_argument("--contacts", action="store_true", help="also the contact report: word and slots with both materials' flags")
	parser.add_argument("--zones", action="store_true", help="also which of Rayman's kind-1 collision zones are switched on")
	args = parser.parse_args()
	variables = [int(v) for v in args.vars.split(",") if v.strip()]
	quiet = set(f for f in args.quiet.split(",") if f)

	pid = find_process(args.process)
	if not pid:
		sys.exit(f"{args.process} isn't running")
	m = game_memory(pid)
	log = open(args.log, "w", encoding="utf-8") if args.log else None

	def say(line):
		print(line, flush=True)
		if log:
			log.write(line + "\n")
			log.flush()

	say(f"watching {args.process} (pid {pid}); Ctrl+C stops")
	last_clock = None
	last = {}
	try:
		while True:
			clock = m.u32(CLOCK)
			if clock == last_clock:
				time.sleep(0.0005)
				continue
			last_clock = clock
			now = snapshot(m, variables, args.contacts, args.zones)
			changed = {k: v for k, v in now.items() if last.get(k) != v}
			if any(k not in quiet for k in changed):
				say(f"t {clock:>9}: " + " | ".join(f"{k} {now[k]}" for k in now if k in changed or k in quiet))
			last = now
	except KeyboardInterrupt:
		pass
	finally:
		if log:
			log.close()


if __name__ == "__main__":
	main()
