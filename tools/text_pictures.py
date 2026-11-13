# Picture check for screen text and HUD placement (card 109).
#
# Renders a fixed list of text screens at a wide and a 4:3 window size and saves BMPs outside the
# repo, then lays two runs side by side on sheets to look at.
#
#   python tools/text_pictures.py render after            # -> H:\r2export\text109\after\*.bmp
#   python tools/text_pictures.py render before --exe H:\r2export\text109\bin_before\openrayman.exe
#   python tools/text_pictures.py sheet before after      # -> H:\r2export\text109\sheets\*.png
#   python tools/text_pictures.py render after --only menu,jail10_look
#
# The sheets are half-size PNGs with guide lines: yellow = the centred 4:3 area's edges, and on
# the wide pictures a red line 43/640 into that area, where the story texts start.
# Needs Pillow. Uses its own save folders, never the game's saves: H:\r2export\text109\saves is made
# once with ortool mksave, and every picture runs on a fresh copy of it (the menu remembers the
# chosen language in the save folder, which would change the next run's menu pictures).
# Nothing is written into the repo.
import argparse, os, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, 'build', 'windows-debug', 'bin')
GAME = os.environ.get('OPENRAYMAN_GAME_DIR', r'F:\Games\Rayman 2')
OUT = os.environ.get('OPENRAYMAN_PICTURES_DIR', r'H:\r2export\text109')  # outside the repo
SIZES = ['2000x1125', '1600x1200']

# name: the engine's arguments (the time is in seconds of script run before the picture).
SCENES = {
    'jail10_presents': ['--level', 'Jail_10', '--time', '3'],
    'jail10_look': ['--level', 'Jail_10', '--time', '8.7'],
    'jail10_planet': ['--level', 'Jail_10', '--time', '20'],
    'jail10_dark': ['--level', 'Jail_10', '--time', '25'],
    'jail10_lums': ['--level', 'Jail_10', '--time', '55'],
    'jail10_title': ['--level', 'Jail_10', '--time', '72'],
    'jail10_ship': ['--level', 'Jail_10', '--time', '84'],
    'jail10_slaves': ['--level', 'Jail_10', '--time', '88'],
    'menu_language': ['--level', 'Menu', '--time', '3'],
    'menu_main': ['--level', 'Menu', '--keys', 'Enter:240-246', '--time', '6'],
    'menu_name': ['--level', 'Menu', '--keys', 'Enter:240-246,Enter:420-426', '--time', '9.5'],
    'learn10_hud_new': ['--level', 'Learn_10', '--start', 'new', '--time', '1'],
    'learn10_hud_save': ['--level', 'Learn_10', '--save', '1', '--time', '1'],
    'learn10_murfy': ['--level', 'Learn_10', '--start', 'new', '--place', '7.4,311.9,-6.9,0@2', '--time', '16'],
    'jail20_world_text': ['--level', 'Jail_20', '--start', 'new', '--time', '15'],
    'jail20_globox': ['--level', 'Jail_20', '--start', 'new', '--time', '50'],
}


def render(folder, exe, only):
    out = os.path.join(OUT, folder)
    os.makedirs(out, exist_ok=True)
    saves = os.path.join(OUT, 'saves')
    env = dict(os.environ, OPENRAYMAN_SAVE_DIR=saves, OPENRAYMAN_GAME_DIR=GAME, OPENRAYMAN_OVERLAY_LOG='1')
    if not os.path.isdir(os.path.join(saves, 'Slot1')):
        subprocess.run([os.path.join(os.path.dirname(exe), 'ortool.exe'), 'mksave', 'Learn_10', '1'], env=env, check=True)

    def one(job):
        name, size = job
        picture = os.path.join(out, f'{name}_{size}.bmp')
        if os.path.exists(picture):
            os.remove(picture)
        own = os.path.join(OUT, 'saves_run', f'{folder}_{name}_{size}')
        shutil.rmtree(own, ignore_errors=True)
        shutil.copytree(saves, own)
        run = subprocess.run([exe, '--game', GAME, *SCENES[name], '--window-size', size, '--screenshot', picture],
            env=dict(env, OPENRAYMAN_SAVE_DIR=own), capture_output=True, text=True, errors='replace', timeout=1800)
        shutil.rmtree(own, ignore_errors=True)
        # What was on the screen (pieces and their class), when the build logs it.
        lines = [line for line in (run.stdout + run.stderr).splitlines() if 'OVERLAY' in line]
        if lines:
            with open(os.path.join(out, f'{name}_{size}.txt'), 'w', encoding='utf-8') as log:
                log.write('\n'.join(lines) + '\n')
        print(('ok   ' if os.path.exists(picture) else 'FAIL ') + picture, flush=True)

    jobs = [(name, size) for name in SCENES if not only or name in only for size in SIZES]
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(one, jobs))


def sheet(first, second, only):
    from PIL import Image, ImageDraw
    out = os.path.join(OUT, 'sheets')
    os.makedirs(out, exist_ok=True)
    for name in SCENES:
        if only and name not in only:
            continue
        for size in SIZES:
            panes = []
            for folder in (first, second):
                path = os.path.join(OUT, folder, f'{name}_{size}.bmp')
                if not os.path.exists(path):
                    continue
                image = Image.open(path).convert('RGB')
                w, h = image.size
                draw = ImageDraw.Draw(image)
                area = min(w, h * 4 // 3)
                left = (w - area) // 2
                if area < w:
                    for x in (left, left + area):
                        draw.line([(x, 0), (x, h)], fill=(255, 255, 0), width=2)
                    x = left + area * 43 // 640
                    draw.line([(x, 0), (x, h)], fill=(255, 0, 0), width=2)
                image = image.resize((w // 2, h // 2), Image.LANCZOS)
                ImageDraw.Draw(image).text((8, 8), f'{folder}  {name}  {size}', fill=(255, 255, 0))
                panes.append(image)
            if not panes:
                continue
            # At 4:3 nothing may move: say so when two runs differ there.
            if len(panes) == 2 and size == '1600x1200' and first != second:
                from PIL import ImageChops
                box = ImageChops.difference(*[Image.open(os.path.join(OUT, f, f'{name}_{size}.bmp')).convert('RGB') for f in (first, second)]).getbbox()
                print(f'4:3 {name}: ' + ('identical' if box is None else f'DIFFERS in {box}'), flush=True)
            # Wide pictures one above the other, 4:3 ones side by side.
            wide = panes[0].width > panes[0].height * 3 // 2
            if wide:
                joined = Image.new('RGB', (panes[0].width, sum(p.height for p in panes)))
                y = 0
                for p in panes:
                    joined.paste(p, (0, y)); y += p.height
            else:
                joined = Image.new('RGB', (sum(p.width for p in panes), panes[0].height))
                x = 0
                for p in panes:
                    joined.paste(p, (x, 0)); x += p.width
            target = os.path.join(out, f'{name}_{size}.png')
            joined.save(target)
            print(target, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('what', choices=['render', 'sheet'])
    parser.add_argument('folders', nargs='+')
    parser.add_argument('--exe', default=os.path.join(BIN, 'openrayman.exe'))
    parser.add_argument('--only', default='')
    args = parser.parse_args()
    only = set(filter(None, args.only.split(',')))
    if args.what == 'render':
        render(args.folders[0], args.exe, only)
    else:
        sheet(args.folders[0], args.folders[1] if len(args.folders) > 1 else args.folders[0], only)
