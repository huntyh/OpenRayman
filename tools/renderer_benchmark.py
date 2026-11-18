# OpenRayman is free software: GNU GPL v3. See LICENSE_openrayman.
"""Card 55: frozen release frames, per-pass submissions and exact screenshot comparisons.

Requires Pillow. Outputs (including game screenshots) must be outside the repository.
Run reference before modifying the renderer, baseline with its diagnostic fallback,
then after. Each child exits naturally; this tool never terminates a game process.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import statistics
import subprocess
import tempfile

from PIL import Image, ImageChops

VIEWS = [
    ('start', 'Learn_10', '17,344.4,13.5,137.8,5.2', 12, [], {}),
    ('water', 'Learn_10', '-93.7,223,2.3,700,0.1', 12, [], {}),
    ('lums', 'Learn_10', '-100,280.4,-12.6,169,-2.1', 12, [], {}),
    ('cave', 'Learn_10', '-54.2,178.3,1.5,758.6,-12.4', 12, [], {}),
    ('jail10', 'Jail_10', None, 8, [], {}),
    ('jail20', 'Jail_20', None, 12, [], {}),
    ('learn30', 'Learn_30', None, 12, [], {}),
    ('bast20', 'Bast_20', None, 12, [], {}),
    ('menu', 'Menu', None, 2, [], {}),
    ('gameplay', 'Learn_10', None, 12, [], {}),
    ('all-sectors', 'Learn_10', '-93.7,223,2.3,700,0.1', 12, [], {'OPENRAYMAN_ALL_SECTORS': '1'}),
    ('unlit', 'Learn_10', '-93.7,223,2.3,700,0.1', 12, ['--lighting', 'off'], {}),
    ('luminosity', 'Learn_10', '-93.7,223,2.3,700,0.1', 12, ['--luminosity', '0.7'], {}),
    ('flash', 'Jail_10', None, 6.2, [], {}),
    ('menu-main', 'Menu', None, 6, ['--keys', 'Enter:240-246'], {}),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--phase', choices=['reference', 'baseline', 'after'], required=True)
    parser.add_argument('--frames', type=int, default=1500)
    parser.add_argument('--view', action='append', help='Only these view names (repeatable)')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.out.resolve()
    if out == root or root in out.parents:
        parser.error('Game screenshots must be outside the repository')
    out.mkdir(parents=True, exist_ok=True)
    results_path = out / f'{args.phase}-results.json'
    results = json.loads(results_path.read_text(encoding='utf-8')) if args.view and results_path.exists() else []
    for name, level, camera, seconds, extra, switches in VIEWS:
        if args.view and name not in args.view:
            continue
        env = os.environ.copy()
        # Isolated user settings, menus and saves, including when launched from an interactive shell.
        session = Path(tempfile.mkdtemp(prefix=f'{args.phase}-{name}-', dir=out))
        env['APPDATA'] = str(session / 'appdata')
        env['OPENRAYMAN_SAVE_DIR'] = str(session / 'saves')
        env['OPENRAYMAN_DRAW_BASELINE'] = '1' if args.phase == 'baseline' else '0'
        env['OPENRAYMAN_BENCHMARK_FRAMES'] = '1' if args.phase == 'reference' else str(args.frames)
        for key in ['OPENRAYMAN_ALL_SECTORS', 'OPENRAYMAN_NO_CULL', 'OPENRAYMAN_HIDE_TEXTURES']:
            env.pop(key, None)
        env.update(switches)
        prefix = out / f'{args.phase}-{name}'
        env['OPENRAYMAN_SUBMISSION_LOG'] = str(prefix) + '-submission.csv'
        env['OPENRAYMAN_PROFILE'] = str(prefix) + '-profile.csv'
        command = [str(args.exe.resolve()), '--game', str(args.game.resolve()), '--level', level,
                   '--time', str(seconds), '--vsync', 'off', '--fps-limit', '0', '--window-size', '1280x720',
                   '--start', 'new', '--screenshot', str(prefix) + '.bmp', *extra]
        if camera:
            command += ['--camera', camera]
        result = subprocess.run(command, env=env, capture_output=True, text=True)
        prefix.with_suffix('.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        result.check_returncode()
        entry = dict(view=name, command=command, switches=switches)
        if args.phase != 'reference':
            reference = Image.open(out / f'reference-{name}.bmp').convert('RGBA')
            current = Image.open(prefix.with_suffix('.bmp')).convert('RGBA')
            if reference.size != current.size:
                raise RuntimeError(f'{name}: screenshot dimensions changed')
            difference = ImageChops.difference(reference, current)
            entry['max_channel_difference'] = [v[1] for v in difference.getextrema()]
            entry['changed_pixels'] = sum(any(pixel) for pixel in difference.getdata())
            rows = list(csv.DictReader(Path(str(prefix) + '-submission.csv').open()))
            # One level and one HUD scope per frozen frame. Discard the first 100 frames.
            rows = [r for r in rows if int(r['frame']) >= 200]
            frames = sum(r['pass'] == 'setup' and int(r['program']) > 0 for r in rows)
            if not frames:
                raise RuntimeError('No submission data: rebuild with card 55 instrumentation')
            entry['passes'] = {}
            for stage in dict.fromkeys(r['pass'] for r in rows):
                subset = [r for r in rows if r['pass'] == stage]
                entry['passes'][stage] = {k: sum(int(r[k]) for r in subset) / frames
                                          for k in ['draws', 'program', 'texture', 'blend', 'uniform', 'buffer', 'vao', 'alpha_draws', 'additive_draws']}
            # cpu_ms is the whole scope repeated on each pass row; do not sum it across passes.
            times = [float(r['cpu_ms']) for r in rows if r['pass'] == 'setup' and int(r['program']) > 0]
            entry['submission_cpu_ms'] = statistics.mean(times)
            profile = list(csv.DictReader(Path(str(prefix) + '-profile.csv').open()))
            # Exclude the report containing startup work when another full report is available.
            profile = profile[1:] or profile
            entry['profile'] = {key: sum(float(r[key]) * int(r['frames']) for r in profile) /
                               sum(int(r['frames']) for r in profile)
                               for key in ['draw ms', 'gpu ms']} if profile else {}
        results = [r for r in results if r['view'] != name]
        results.append(entry)
        print(args.phase, name, {k: v for k, v in entry.items() if k not in ['command', 'switches', 'passes']}, flush=True)
        results_path.write_text(json.dumps(results, indent=2), encoding='utf-8')

    if any(r.get('changed_pixels', 0) for r in results):
        raise SystemExit('Pixel comparison failed; inspect results.json and the reference pictures')


if __name__ == '__main__':
    main()
