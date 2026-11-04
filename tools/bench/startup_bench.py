"""Measure how long an image viewer takes to show an image.

The test image has a pure magenta block in its center. The script starts the
viewer, waits for a new top-level window and samples the pixel in the middle
of that window until it turns magenta. That is the moment the user sees the
image, so the number is comparable between viewers whatever they do inside.

Usage:
  python startup_bench.py --make-image bench.jpg            # make a 24 MP test image
  python startup_bench.py --name "Lucid Viewer" --runs 10 -- "C:\\path\\LucidViewer.exe" "{image}"
  python startup_bench.py --name "Photos" -- explorer.exe "{image}"

The command after "--" is run with {image} replaced by the image path.
Results are appended as JSON lines to --out (default bench_results.jsonl).
"""
import argparse, ctypes, ctypes.wintypes as wt, json, os, statistics, subprocess, sys, time

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    user32.SetProcessDPIAware()

EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def top_windows():
    found = []

    def cb(hwnd, _):
        if user32.IsWindowVisible(hwnd) and not user32.GetWindow(hwnd, 4):  # GW_OWNER
            found.append(hwnd)
        return True

    user32.EnumWindows(EnumWindowsProc(cb), 0)
    return found


def window_center(hwnd):
    r = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    if r.right - r.left < 200 or r.bottom - r.top < 200:
        return None
    return (r.left + r.right) // 2, (r.top + r.bottom) // 2


def is_magenta(color):
    r, g, b = color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF
    return r > 200 and g < 60 and b > 200


def window_shows_marker(hwnd):
    """Render the window into memory (works when it is covered by other windows)
    and look at the pixel in its middle."""
    r = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top
    if w < 200 or h < 200:
        return False
    screen = user32.GetDC(0)
    mem = gdi32.CreateCompatibleDC(screen)
    bmp = gdi32.CreateCompatibleBitmap(screen, w, h)
    old = gdi32.SelectObject(mem, bmp)
    try:
        user32.PrintWindow(hwnd, mem, 2)  # PW_RENDERFULLCONTENT
        return is_magenta(gdi32.GetPixel(mem, w // 2, h // 2))
    finally:
        gdi32.SelectObject(mem, old)
        gdi32.DeleteObject(bmp)
        gdi32.DeleteDC(mem)
        user32.ReleaseDC(0, screen)


def window_pid(hwnd):
    pid = wt.DWORD()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
    return pid.value


def close_window(hwnd):
    pid = window_pid(hwnd)
    user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
    deadline = time.perf_counter() + 3
    while user32.IsWindow(hwnd) and time.perf_counter() < deadline:
        time.sleep(0.05)
    if user32.IsWindow(hwnd) and pid:
        subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)


def run_once(cmd, timeout):
    before = set(top_windows())
    t0 = time.perf_counter()
    subprocess.Popen(cmd)
    win = None
    try:
        while time.perf_counter() - t0 < timeout:
            if win is None:
                for h in top_windows():
                    if h not in before and window_center(h):
                        win = h
                        break
            if win is not None and window_shows_marker(win):
                return (time.perf_counter() - t0) * 1000.0, win
            time.sleep(0.002)
        return None, win
    finally:
        pass


def make_image(path):
    from PIL import Image, ImageDraw, ImageFilter
    import random
    random.seed(1)
    w, h = 6000, 4000
    im = Image.new("RGB", (w // 8, h // 8))
    d = ImageDraw.Draw(im)
    for _ in range(600):  # random shapes look more like a photo than noise
        x, y = random.randrange(w // 8), random.randrange(h // 8)
        s = random.randrange(5, 120)
        col = tuple(random.randrange(256) for _ in range(3))
        d.ellipse((x, y, x + s, y + s), fill=col)
    im = im.filter(ImageFilter.GaussianBlur(3)).resize((w, h), Image.BICUBIC)
    im = Image.blend(im, Image.effect_noise((w, h), 24).convert("RGB"), 0.15)
    im.paste((255, 0, 255), (w * 2 // 5, h * 2 // 5, w * 3 // 5, h * 3 // 5))
    if path.lower().endswith((".jpg", ".jpeg")):
        im.save(path, quality=92)
    else:
        im.save(path)
    print("wrote", path)


def main():
    argv = sys.argv[1:]
    cmd = []
    if "--" in argv:
        i = argv.index("--")
        argv, cmd = argv[:i], argv[i + 1:]
    p = argparse.ArgumentParser()
    p.add_argument("--make-image")
    p.add_argument("--image")
    p.add_argument("--name", default="viewer")
    p.add_argument("--runs", type=int, default=10)
    p.add_argument("--timeout", type=float, default=20)
    p.add_argument("--out", default="bench_results.jsonl")
    a = p.parse_args(argv)
    if a.make_image:
        make_image(a.make_image)
        return
    if not cmd or not a.image:
        p.error("need --image and a command after --")
    image = os.path.abspath(a.image)
    cmd = [c.replace("{image}", image) for c in cmd]
    times = []
    for i in range(a.runs):
        ms, win = run_once(cmd, a.timeout)
        if win:
            close_window(win)
        print(f"run {i + 1}: {'timeout' if ms is None else f'{ms:.0f} ms'}")
        if ms is not None:
            times.append(ms)
        time.sleep(1.0)
    if not times:
        print("no successful runs")
        return
    res = {
        "name": a.name, "image": os.path.basename(image), "runs": len(times),
        "first_ms": round(times[0]), "median_ms": round(statistics.median(times)),
        "min_ms": round(min(times)), "max_ms": round(max(times)),
    }
    print(json.dumps(res))
    with open(a.out, "a", encoding="utf-8") as f:
        f.write(json.dumps(res) + "\n")


if __name__ == "__main__":
    main()
