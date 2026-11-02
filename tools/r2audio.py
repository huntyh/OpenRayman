# r2audio: how loud the computer's sound output is, every 50 ms (Windows loopback capture of the
# default speakers). It measures whatever plays: the real Rayman2.exe, or OpenRayman, so the two can
# be compared like for like when mixing (music against effects against voices).
#
#   python tools/r2audio.py H:\r2export\live\levels.txt      # stand-alone, Ctrl+C to stop
#
# r2record uses it as its "audio" topic, on the same timeline as the other files. Each line gives
# the RMS and peak of the block in dBFS (0 = full scale) for the left and right channel together;
# silent stretches (below -70 dBFS) are logged once. Mute other programs while measuring.
#
# Needs `pip install soundcard`. Windows' master volume does not change the captured level (the
# capture is taken before it), but the game's own volume settings and the per-app mixer do.

import math
import sys
import threading
import time

BLOCK_SECONDS = 0.05
RATE = 48000
SILENT_DB = -70.0


def decibels(value):
	return 20.0 * math.log10(value) if value > 1e-7 else -140.0


class loudness_meter(threading.Thread):
	"""Calls report(rms_db, peak_db) for every block, from its own thread, until stop()."""

	def __init__(self, report):
		super().__init__(daemon=True)
		self.report = report
		self.stopping = False

	def stop(self):
		self.stopping = True

	def run(self):
		import numpy as np
		import soundcard as sc
		speaker = sc.default_speaker()
		capture = sc.get_microphone(id=str(speaker.name), include_loopback=True)
		frames = int(RATE * BLOCK_SECONDS)
		with capture.recorder(samplerate=RATE, blocksize=frames) as recorder:
			while not self.stopping:
				block = recorder.record(numframes=frames)
				rms = float(np.sqrt(np.mean(np.square(block)))) if block.size else 0.0
				peak = float(np.max(np.abs(block))) if block.size else 0.0
				self.report(decibels(rms), decibels(peak))


class loudness_log:
	"""Formats the meter's blocks, logging a silent stretch once."""

	def __init__(self, write):
		self.write = write
		self.silent = False

	def __call__(self, rms_db, peak_db):
		if peak_db < SILENT_DB:
			if not self.silent:
				self.write("silent")
			self.silent = True
			return
		self.silent = False
		self.write(f"rms {rms_db:6.1f} peak {peak_db:6.1f} dBFS")


def main():
	if len(sys.argv) != 2:
		print("usage: python tools/r2audio.py <output file>")
		return 1
	started = time.monotonic()
	with open(sys.argv[1], "a", encoding="utf-8") as out:
		def write(line):
			out.write(f"{time.monotonic() - started:9.3f}s {line}\n")
			out.flush()
		meter = loudness_meter(loudness_log(write))
		meter.start()
		print(f"measuring into {sys.argv[1]}; Ctrl+C stops", flush=True)
		try:
			while meter.is_alive():
				time.sleep(0.2)
		except KeyboardInterrupt:
			pass
		meter.stop()
	return 0


if __name__ == "__main__":
	sys.exit(main())
