# r2record: records a whole playthrough of the real Rayman2.exe into one folder, a file per topic.
#
# Like r2live it only reads the game's memory, never writes it. Each file logs changes only, so an
# hour of play stays small. Start it before the game (or any time), play, Ctrl+C to stop:
#
#   python tools/r2record.py H:\r2export\live\run1
#
# Files (every line starts with the seconds since recording began, which never reset, then the
# game clock in ms, which restarts each level and stops while paused, and the level number):
#   session.txt    levels as they load (Rayman's super object changes), and what was recorded
#   story.txt      story bits turning on or off, with Rayman's position
#   rayman.txt     Rayman's behaviour, reflex and state changes, with position
#   actors.txt     every actor of the level: listed at load (index, super object, position), then
#                  behaviour/state changes, hiding/showing (super object flag 0x2, +0x30) and custom
#                  bit 14 (0x2000, the animation freeze), custom bit changes in general
#   camera.txt     the camera: position, where it looks, mode and projection, ten times a second
#                  while it moves and whenever its mode or behaviour changes
#   generated.txt  generated actors (shots, text actors) appearing and going
#   text.txt       text boxes (function 158's text actors, 30 slots at 0x4FF8C0): made by whom,
#                  the events their text returns (/E<n>:, Murfy's page end is 9), how far the reveal
#                  got, and when they go
#   texts.txt      once: the current language's string table (id: text), to read text ids by
#   sound.txt      voices as they start and end (0x4B7470 list): event id (bank << 16 | index,
#                  as `ortool sound` takes it), channel, the actor it sounds from
#   music.txt      the music theme playing, its segment index and the one prepared next
#                  (0x4B95B8..), segment resource ids (bank:index)
#
# Select topics with --only story,camera (default: all). The super object order of the dynamic
# world matches the level file, so actor indexes are the `node` numbers of `ortool brain` minus the
# nodes before the dynamic world; positions identify them either way.

import argparse
import math
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import r2live as live  # noqa: E402

TOPICS = ("session", "story", "rayman", "actors", "camera", "generated", "text", "sound", "music")

LEVEL_NAME = 0x500381     # the level being played or loaded (0x404DA0), a character array
PENDING_LEVEL = 0x50039F  # the next level asked for (0x4054D0), empty when none
TEXT_ACTORS = 0x4FF8C0    # 30 text actors' super objects (0x409A20); 0 = none
TEXT_STATES = 0x4FFDC0    # 0x14 each: +4 timing, +8 last update, +0xC last event, +0x10 i16 reveal cursor
STRINGS = 0x50026C        # -> per language 8-byte records, first word -> string pointer array (0x494C70)
LANGUAGE = 0x500FA8       # low byte: the language index
VOICES = 0x4B7470         # first sound record (0x451B60); +0x34 next, +0x20 active, +8 emitter,
                          # +0xC channel, +0x10 event, +0x1C resource, +0x28 request serial
BANKS = 0x514D50          # loaded banks: +0x3C next, +4 number, +0x24 tables (+4 events, +8 count,
                          # +0xC resources, +0x10 count; 0x20 and 0x5C bytes each)
THEME = 0x4B95B8          # active; +0x14 theme, +0x20 repeat state, +0x2C index, +0x38 prepared index,
                          # +0x40 asked theme (0x459760, 0x4595D0)


def c_string(m, at, limit=64):
	raw = m.read(at, limit) if at else None
	return raw.split(b"\0")[0].decode("latin-1") if raw else ""


def bank_map(m):
	"""Event and resource addresses of every loaded bank -> ids (events as (bank << 16) | index,
	resources as "bank:index")."""
	events, resources = {}, {}
	bank = m.u32(BANKS)
	for _ in range(512):
		if not bank:
			break
		number = m.u32(bank + 4)
		tables = m.u32(bank + 0x24)
		if tables:
			base, count = m.u32(tables + 4), m.u32(tables + 8)
			for i in range(min(count, 4096)):
				events[base + 0x20 * i] = (number << 16) | i
			base, count = m.u32(tables + 0xC), m.u32(tables + 0x10)
			for i in range(min(count, 4096)):
				resources[base + 0x5C * i] = f"{number}:{i}"
		bank = m.u32(bank + 0x3C)
	return events, resources


def mind_of(m, actor):
	brain = m.u32(actor + 0xC) if actor else 0
	return m.u32(brain) if brain else 0


def behaviour_of(m, actor):
	mind = mind_of(m, actor)
	if not mind:
		return "-", "-"
	model = m.u32(mind)
	normal, reflex = m.u32(mind + 4), m.u32(mind + 8)
	b = live.list_index(m, m.u32(model), m.u32(normal + 8), 0xC) if normal else -1
	r = live.list_index(m, m.u32(model + 4), m.u32(reflex + 8), 0xC) if reflex else -1
	return str(b), str(r)


def position(m, so):
	t = m.u32(so + 0x24) if so else 0
	return live.fmt(m.f32s(t + 4, 3)) if t else "-"


def dynamic_world(m):
	# Rayman's parent is the dynamic world; its children are the level's actors in file order.
	so = m.u32(live.MAIN_ACTOR)
	return m.u32(so + 0x1C) if so else 0


def actors_of(m, world):
	out, child = [], m.u32(world + 0x08) if world else 0
	while child and len(out) < 1024:
		out.append(child)
		child = m.u32(child + 0x14)
	return out


class recorder:
	def __init__(self, m, folder, topics):
		self.m = m
		self.files = {t: open(os.path.join(folder, f"{t}.txt"), "a", encoding="utf-8") for t in topics}
		self.level = 0
		self.started = time.monotonic()  # wall-clock seconds since recording began: never resets
		self.clock = 0
		self.strings_dumped = False
		self.reset()

	def reset(self):
		self.rayman = None
		self.story_at = None
		self.story = None
		self.story_missing = False
		self.rayman_last = None
		self.actors = {}
		self.camera_last = None
		self.camera_sampled = -1e9
		self.generated = {}
		self.texts = {}
		self.voices = {}
		self.music_last = None
		self.banks = None
		self.banks_built = -1e9

	def say(self, topic, line):
		f = self.files.get(topic)
		if f:
			f.write(f"{time.monotonic() - self.started:9.3f}s t {self.clock:>9} L{self.level}: {line}\n")
			f.flush()

	def close(self):
		for f in self.files.values():
			f.close()

	def frame(self, clock):
		m = self.m
		self.clock = clock
		so = m.u32(live.MAIN_ACTOR)
		if so != self.rayman:
			if so:
				self.level += 1
			self.reset()
			self.rayman = so
			pending = c_string(m, PENDING_LEVEL, 32)
			self.say("session", f"level {'loaded' if so else 'unloaded'}: {c_string(m, LEVEL_NAME, 32) or '?'}"
				+ (f" (next: {pending})" if pending else "") + f" (Rayman's super object 0x{so:08X})")
			if so:
				self.list_actors()
				if "text" in self.files and not self.strings_dumped:
					self.dump_strings()
		if not so:
			return
		if "story" in self.files:
			self.poll_story(so)
		if "rayman" in self.files:
			self.poll_rayman(so)
		if "actors" in self.files:
			self.poll_actors()
		if "camera" in self.files:
			self.poll_camera(so)
		if "generated" in self.files:
			self.poll_generated()
		if "text" in self.files:
			self.poll_text()
		if "sound" in self.files:
			self.poll_sound()
		if "music" in self.files:
			self.poll_music()

	def node_name(self, so):
		if so == self.rayman:
			return "Rayman"
		if so in self.actors:
			return f"actor {self.actors[so][0]}"
		return "none" if so in (0, 0xFFFFFFFF) else f"0x{so:08X}"

	def bank_ids(self, force=False):
		# Rebuilt at most once a second when something isn't found (banks load with levels).
		if self.banks is None or (force and self.clock - self.banks_built > 1000):
			self.banks = bank_map(self.m)
			self.banks_built = self.clock
		return self.banks

	def creator_of(self, so):
		m = self.m
		count, objects, creators = m.u32(live.POOL_COUNT), m.u32(live.POOL_SUPER_OBJECTS), m.u32(live.POOL_CREATORS)
		if objects and creators and objects <= so < objects + 0x48 * count and (so - objects) % 0x48 == 0:
			return self.node_name(m.u32(creators + 4 * ((so - objects) // 0x48)))
		return "?"

	def dump_strings(self):
		# The string table has no count: read ids until 64 in a row point nowhere.
		m = self.m
		records = m.u32(STRINGS)
		array = m.u32(records + (m.u8(LANGUAGE) & 0xFF) * 8) if records else 0
		if not array:
			return
		self.strings_dumped = True
		with open(os.path.join(os.path.dirname(self.files["text"].name), "texts.txt"), "w", encoding="utf-8") as f:
			f.write(f"language {m.u8(LANGUAGE)}, table 0x{array:08X}\n")
			misses = 0
			for i in range(20000):
				text = c_string(m, m.u32(array + 4 * i), 1024)
				if not text:
					misses += 1
					if misses >= 64:
						break
					continue
				misses = 0
				f.write(f"{i}: {text!r}\n")

	def poll_text(self):
		m = self.m
		raw = m.read(TEXT_ACTORS, 30 * 4)
		if not raw or len(raw) != 120:
			return
		now = {}
		for i, so in enumerate(struct.unpack("<30I", raw)):
			if so:
				state = TEXT_STATES + 0x14 * i
				cursor = struct.unpack("<h", m.read(state + 0x10, 2) or b"\0\0")[0]
				now[i] = (so, m.u32(state + 0xC), cursor, m.u32(state + 4))
		for i, (so, event, cursor, timing) in now.items():
			last = self.texts.get(i)
			if last is None or last[0] != so:
				self.say("text", f"slot {i} made by {self.creator_of(so)} at {position(m, so)} timing {timing}")
			elif event != last[1]:
				self.say("text", f"slot {i} event {event} (reveal at {cursor})")
		for i in self.texts.keys() - now.keys():
			self.say("text", f"slot {i} gone (reveal reached {self.texts[i][2]}, last event {self.texts[i][1]})")
		self.texts = now

	def poll_sound(self):
		m = self.m
		now = {}
		record = m.u32(VOICES)
		for _ in range(128):
			if not record:
				break
			if m.u32(record + 0x20):
				now[(record, m.u32(record + 0x28))] = (m.u32(record + 0x10), m.u32(record + 8), m.u32(record + 0xC), m.u32(record + 0x1C))
			record = m.u32(record + 0x34)
		events, resources = self.bank_ids()
		for key, (event, emitter, channel, resource) in now.items():
			if key in self.voices:
				continue
			if event not in events:
				events, resources = self.bank_ids(force=True)
			eid = events.get(event)
			name = f"0x{eid:08X}" if eid is not None else f"event@0x{event:08X}"
			self.say("sound", f"play {name} resource {resources.get(resource, f'@0x{resource:08X}')} channel {channel} from {self.node_name(emitter)}")
		for key in self.voices.keys() - now.keys():
			event = self.voices[key][0]
			eid = self.bank_ids()[0].get(event)
			self.say("sound", "end " + (f"0x{eid:08X}" if eid is not None else f"event@0x{event:08X}"))
		self.voices = now

	def poll_music(self):
		m = self.m
		raw = m.read(THEME, 0x44)
		if not raw or len(raw) < 0x44:
			return
		w = struct.unpack("<17I", raw)
		active, theme, repeat, index, prepared_index, asked = w[0], w[5], w[8], w[11], w[14], w[16]
		now = (active, theme, index, prepared_index, repeat, asked)
		if now == self.music_last:
			return
		resources = self.bank_ids()[1]
		if theme and theme not in resources:
			resources = self.bank_ids(force=True)[1]

		def rid(p):
			return resources.get(p, f"@0x{p:08X}") if p else "-"
		line = f"{'playing' if active else 'stopped'} theme {rid(theme)} segment {index} next {prepared_index} repeat {repeat}"
		if asked:
			line += f" asked {rid(asked)}"
		if theme and (self.music_last is None or self.music_last[1] != theme):
			count, entries = m.u32(theme + 0x1C), m.u32(theme + 0x2C)
			segments = [rid(m.u32(entries + 0x18 * k)) for k in range(min(count, 64))] if entries else []
			tail = m.u32(theme + 0x20)
			line += f" | segments {' '.join(segments)}" + (f" tail {rid(m.u32(tail))}" if tail else "")
		self.say("music", line)
		self.music_last = now

	def poll_story(self, so):
		m = self.m
		if self.story_at is None:
			self.story_at = live.story_address(m)
			if self.story_at is None:
				if not self.story_missing:
					self.say("story", "story bits not found yet")
					self.story_missing = True
				return
		bits = live.read_story(m, self.story_at)
		if bits is None:
			self.story_at = None
			return
		if bits == self.story:
			return
		where = position(m, so)
		if self.story is None:
			self.say("story", f"on ({len(bits)}), Rayman at {where}: {' '.join(map(str, sorted(bits)))}")
		else:
			added, removed = sorted(bits - self.story), sorted(self.story - bits)
			self.say("story", (f"+{' +'.join(map(str, added))} " if added else "") + (f"-{' -'.join(map(str, removed))} " if removed else "") + f"| Rayman at {where}")
		self.story = bits

	def poll_rayman(self, so):
		m = self.m
		actor = m.u32(so + 4)
		data = m.u32(actor) if actor else 0
		behaviour, reflex = behaviour_of(m, actor)
		now = (behaviour, reflex, m.u32(data + 4) if data else 0, m.u32(m.u32(actor + 4) + 0x24) if actor else 0)
		if now != self.rayman_last:
			self.say("rayman", f"behaviour {now[0]} reflex {now[1]} state 0x{now[2]:X} custom 0x{now[3]:08X} at {position(m, so)}")
			self.rayman_last = now

	def actor_state(self, so):
		m = self.m
		actor = m.u32(so + 4) if m.u32(so) == 2 else 0
		if not actor:
			return None
		data = m.u32(actor)
		behaviour, reflex = behaviour_of(m, actor)
		return (behaviour, reflex, m.u32(data + 4) if data else 0, m.u32(so + 0x30), m.u32(m.u32(actor + 4) + 0x24))

	def list_actors(self):
		m = self.m
		if "actors" not in self.files:
			return
		for i, so in enumerate(actors_of(m, dynamic_world(m))):
			state = self.actor_state(so)
			if state is None:
				continue
			self.actors[so] = (i, state)
			self.say("actors", f"actor {i} so 0x{so:08X} at {position(m, so)}: behaviour {state[0]} state 0x{state[2]:X} flags 0x{state[3]:X} custom 0x{state[4]:08X}")

	def poll_actors(self):
		m = self.m
		for so, (i, last) in list(self.actors.items()):
			now = self.actor_state(so)
			if now is None or now == last:
				continue
			parts = []
			if now[0] != last[0] or now[1] != last[1]:
				parts.append(f"behaviour {last[0]}->{now[0]} reflex {now[1]}")
			if now[2] != last[2]:
				parts.append(f"state 0x{now[2]:X}")
			if (now[3] ^ last[3]) & 0x2:
				parts.append("hidden" if now[3] & 0x2 else "shown")
			if now[3] != last[3] and not (now[3] ^ last[3]) & 0x2:
				parts.append(f"flags 0x{now[3]:X}")
			if (now[4] ^ last[4]) & 0x2000:
				parts.append("frozen (bit 14)" if now[4] & 0x2000 else "unfrozen (bit 14)")
			if now[4] != last[4]:
				parts.append(f"custom 0x{last[4]:08X}->0x{now[4]:08X}")
			self.say("actors", f"actor {i} at {position(m, so)}: " + ", ".join(parts))
			self.actors[so] = (i, now)

	def poll_camera(self, so):
		m = self.m
		cam_actor = m.u32(live.FIX_CAMERA)
		cam_standard = m.u32(cam_actor + 4) if cam_actor else 0
		cam_super = m.u32(cam_standard + 0xC) if cam_standard else 0
		t = m.u32(cam_super + 0x24) if cam_super else 0
		if not t:
			return
		at = m.f32s(t + 4, 3)
		# All three axes (X, Y, Z), so where it looks can be worked out afterwards.
		axes = m.f32s(t + 0x10, 9)
		cam_object = m.u32(cam_actor + 0x10)
		mode = m.u32(cam_object + 0x60) if cam_object else 0
		behaviour, reflex = behaviour_of(m, cam_actor)
		projection = m.u32(live.PROJECTION)
		angles = m.f32s(projection + 0x64, 2) if projection else (0.0, 0.0)
		key = (mode, behaviour, reflex)
		moved = self.camera_last is None or math.dist(at, self.camera_last[1]) > 0.01
		if self.camera_last is None or key != self.camera_last[0] or (moved and self.clock - self.camera_sampled >= 100):
			self.say("camera", f"at {live.fmt(at)} axes {live.fmt(axes)} mode {mode} behaviour {behaviour} reflex {reflex} "
				f"fov {live.fmt(angles)} Rayman at {position(m, so)}")
			self.camera_last = (key, at)
			self.camera_sampled = self.clock

	def poll_generated(self):
		m = self.m
		count = m.u32(live.POOL_COUNT)
		objects = m.u32(live.POOL_SUPER_OBJECTS)
		creators = m.u32(live.POOL_CREATORS)
		if not count or not objects or not creators:
			return
		seen = {}
		for i in range(min(count, 256)):
			creator = m.u32(creators + 4 * i)
			so = objects + 0x48 * i
			actor = m.u32(so + 4)
			if creator and actor and m.u32(actor):
				seen[i] = (creator, m.u32(m.u32(actor) + 4))
		for i, (creator, state) in seen.items():
			if self.generated.get(i) != (creator, state):
				what = "made" if i not in self.generated else "state"
				self.say("generated", f"slot {i} {what} by 0x{creator:08X} state 0x{state:X} at {position(m, objects + 0x48 * i)}")
		for i in self.generated.keys() - seen.keys():
			self.say("generated", f"slot {i} gone")
		self.generated = seen


def main():
	parser = argparse.ArgumentParser(description="Record a playthrough of the running Rayman2.exe (read-only), a file per topic.")
	parser.add_argument("folder", help="where the files go (created if missing; appended to)")
	parser.add_argument("--only", default=",".join(TOPICS), help="topics, comma separated: " + ", ".join(TOPICS))
	parser.add_argument("--process", default="Rayman2.exe")
	args = parser.parse_args()
	topics = [t for t in args.only.split(",") if t in TOPICS]
	if "session" not in topics:
		topics.insert(0, "session")
	os.makedirs(args.folder, exist_ok=True)
	print(f"waiting for {args.process}...", flush=True)
	pid = None
	while not pid:
		pid = live.find_process(args.process)
		if not pid:
			time.sleep(1)
	m = live.game_memory(pid)
	r = recorder(m, args.folder, topics)
	r.say("session", f"recording {', '.join(topics)} from {args.process} (pid {pid})")
	print(f"recording into {args.folder}; Ctrl+C stops", flush=True)
	last_clock = None
	try:
		while True:
			clock = m.u32(live.CLOCK)
			if clock == last_clock:
				time.sleep(0.002)
				continue
			last_clock = clock
			r.frame(clock)
	except KeyboardInterrupt:
		pass
	finally:
		r.say("session", "stopped")
		r.close()


if __name__ == "__main__":
	main()
