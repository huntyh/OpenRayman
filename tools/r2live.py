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
import math
import struct
import sys
import time

MAIN_ACTOR = 0x500578
CLOCK = 0x5003EC
FIX_CAMERA = 0x4B7308  # second Fix entry: camera actor (0x403730)
PROJECTION = 0x4A723C  # current renderer projection record (0x4228D0)

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


# The pool of generated actors (function 66, 0x40BCC0): slot count, super objects (0x48 each), and
# per slot the super object of whoever made it (0 = free). See docs/formats/generated.md. "draw" is the
# super object's draw mask (+0x2C): ANDed with the material flags (0x42A410), its bit 0x40000000 says
# whether the shot is depth-tested.
POOL_COUNT = 0x4A6B18
POOL_SUPER_OBJECTS = 0x4A6B28
POOL_CREATORS = 0x4A6B30


def generated_actors(m):
	s = {}
	count = m.u32(POOL_COUNT)
	objects = m.u32(POOL_SUPER_OBJECTS)
	creators = m.u32(POOL_CREATORS)
	if not count or not objects or not creators:
		return s
	s["pool"] = str(count)
	live = 0
	for i in range(min(count, 256)):
		creator = m.u32(creators + 4 * i)
		so = objects + 0x48 * i
		actor = m.u32(so + 4)
		if not creator or not actor or not m.u32(actor):
			continue
		live += 1
		t = m.u32(so + 0x24)
		mind = m.u32(m.u32(actor + 0xC)) if m.u32(actor + 0xC) else 0
		line = f"by 0x{creator:X} at " + (fmt(m.f32s(t + 4, 3)) if t else "-") + f" state 0x{m.u32(m.u32(actor) + 4):X} draw 0x{m.u32(so + 0x2C):X}"
		# Its part as drawn: 3D data +0x90 lists the parts by channel id (0x14 each, +0 the part's
		# super object; the shot's one channel is id 22). The part's local transform (+0x20) carries
		# the animation's scale in its matrix (+0x34), its +0x38 the opacity (255 opaque).
		parts = m.u32(m.u32(actor) + 0x90)
		part = m.u32(parts + 22 * 0x14) if parts else 0
		pt = m.u32(part + 0x20) if part else 0
		if pt:
			column = m.f32s(pt + 0x34, 3)
			line += f" part scale {math.sqrt(sum(v * v for v in column)):.3f} opacity {m.f32s(part + 0x38, 1)[0]:.0f} frame {m.u32(m.u32(actor) + 0x74) & 0xFFFF}"
		if mind:
			# The shot's timers: var1 its rank among Rayman's shots, var3 bounces left, var16 when it fades.
			line += " var1 " + variable(m, mind, 1) + " var3 " + variable(m, mind, 3) + " var16 " + variable(m, mind, 16)
		s[f"gen{i}"] = line
	s["generated"] = str(live)
	return s


def snapshot(m, variables, contacts=False, zones=False, camera=False, generated=False):
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
	if camera:
		cam_actor = m.u32(FIX_CAMERA)
		cam_standard = m.u32(cam_actor + 4) if cam_actor else 0
		cam_super = m.u32(cam_standard + 0xC) if cam_standard else 0
		cam_transform = m.u32(cam_super + 0x24) if cam_super else 0
		if cam_transform and t:
			cam_pos = m.f32s(cam_transform + 4, 3)
			player_pos = m.f32s(t + 4, 3)
			s["camera_at"] = " ".join(f"{v:.6f}" for v in cam_pos)
			s["camera_distance"] = f"{math.dist(cam_pos, player_pos):.3f}"
			cam_object = m.u32(cam_actor + 0x10) if cam_actor else 0
			if cam_object:
				s["camera_mode"] = str(m.u32(cam_object + 0x60))
		projection = m.u32(PROJECTION)
		if projection:
			# 0x422AB0 computes these angles and the focal factor used by 0x42C8A0.
			s["projection"] = f"kind {m.u32(projection + 8)} angles " + fmt(m.f32s(projection + 0x64, 2)) + " focal " + fmt(m.f32s(projection + 0x74, 1))
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
	if generated:
		s.update(generated_actors(m))
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
	parser.add_argument("--camera", action="store_true", help="also camera position, distance, mode and renderer projection")
	parser.add_argument("--generated", action="store_true", help="also the generated actors (Rayman's shots): slot, maker, position, state, timers")
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
			now = snapshot(m, variables, args.contacts, args.zones, args.camera, args.generated)
			# Average world-space movement between observed game frames, in units per game second.
			# If frames are missed this is their net displacement, not the exact path travelled.
			if args.camera and last_clock is not None and clock > last_clock and "camera_at" in now and "camera_at" in last:
				position = tuple(map(float, now["camera_at"].split()))
				previous = tuple(map(float, last["camera_at"].split()))
				now["camera_speed"] = f"{math.dist(position, previous) * 1000 / (clock - last_clock):.3f}"
			last_clock = clock
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
