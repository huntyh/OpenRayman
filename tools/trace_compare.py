#!/usr/bin/env python3
# Compare two logs of Rayman in r2live's format: one from the real game (tools/r2live.py) and one
# from ours (OPENRAYMAN_TRACE=<file>). For every sample of ours, the nearest point of the real path,
# its speed, and how far ours is from it; then the largest gap. GPLv3.
#
#   python tools/trace_compare.py build\live_jump.txt build\our_jump.txt [--behaviour 38] [--every 30]
#
# --behaviour starts both logs at the first tick in that behaviour (38 is Rayman's slide);
# --every prints every n-th of our samples.
import argparse
import math


def read(path, behaviour):
	rows, state, started, last = [], {}, behaviour is None, None
	for line in open(path, encoding="utf-8", errors="replace"):
		if not line.startswith("t "):
			if line.startswith("level ") and rows:
				break  # a new level: the run ended
			continue
		head, rest = line.split(":", 1)
		t = int(head.split()[1])
		if last is not None and t < last and rows:
			break  # the clock started over (a new level in r2live)
		last = t
		for part in rest.split("|"):
			p = part.strip().split()
			if not p:
				continue
			if p[0] == "at":
				state["at"] = tuple(map(float, p[1:4]))
			elif p[0] == "speed":
				state["speed"] = tuple(map(float, p[1:4]))
			elif p[0] == "behaviour":
				state["behaviour"] = p[1]
			elif p[0] == "keys":
				state["keys"] = p[1]
		if not started and state.get("behaviour") == str(behaviour):
			started = True
		if started and "at" in state:
			rows.append((t, state["at"], state.get("speed", (0, 0, 0)), state.get("behaviour", "?"), state.get("keys", "")))
	return rows


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("real")
	ap.add_argument("ours")
	ap.add_argument("--behaviour", type=int)
	ap.add_argument("--every", type=int, default=30)
	args = ap.parse_args()
	real, ours = read(args.real, args.behaviour), read(args.ours, args.behaviour)
	if not real or not ours:
		print("no samples (check --behaviour)")
		return
	r0, o0 = real[0][0], ours[0][0]
	worst = (0.0, None)
	for i, (t, p, v, b, keys) in enumerate(ours):
		near = min(real, key=lambda r: sum((a - c) ** 2 for a, c in zip(r[1], p)))
		d = math.dist(near[1], p)
		if d > worst[0]:
			worst = (d, t)
		if i % args.every == 0:
			print(f"{(t - o0) / 1000:6.2f}s ours ({p[0]:7.1f},{p[1]:7.1f},{p[2]:7.1f}) |v| {math.hypot(*v):5.1f} b{b:>3} keys {keys:<12}"
				f" real@{(near[0] - r0) / 1000:6.2f}s |v| {math.hypot(*near[2]):5.1f} off {d:6.1f}")
	print(f"largest gap from the real path: {worst[0]:.1f} at our {(worst[1] - o0) / 1000:.2f}s")


if __name__ == "__main__":
	main()
