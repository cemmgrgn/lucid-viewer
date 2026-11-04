"""Draws the startup chart of the README from a results file of startup_bench.py (light and dark version)."""
import json, sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch

results = [json.loads(l) for l in open(sys.argv[1], encoding='utf-8')]
out = sys.argv[2]  # path prefix, -light.png / -dark.png are appended
apps = ['Lucid Viewer 1.0.0', 'JPEGView 1.3.46', 'Windows Photos', 'ImageGlass']
labels = ['Lucid Viewer', 'JPEGView 1.3.46', 'Windows Photos', 'ImageGlass 10']
tests = [('bench2.jpg', '2 MP JPEG'), ('bench24.jpg', '24 MP JPEG'), ('bench24.png', '24 MP PNG')]
val = {(r['name'], r['image']): r['median_ms'] for r in results}

THEMES = {
    'light': dict(surface='#ffffff', text='#1f2328', muted='#59636e', grid='#e5e7eb', accent='#2563eb', other='#9ca3af'),
    'dark': dict(surface='#0d1117', text='#e6edf3', muted='#9198a1', grid='#262c36', accent='#60a5fa', other='#6b7280'),
}
for name, t in THEMES.items():
    fig, axes = plt.subplots(1, 3, figsize=(12, 3.0), dpi=150, sharey=True)
    fig.patch.set_facecolor(t['surface'])
    vmax = max(val.values()) * 1.18
    for ax, (img, title) in zip(axes, tests):
        ax.set_facecolor(t['surface'])
        for i, app in enumerate(apps):
            v = val[(app, img)]
            y = len(apps) - 1 - i
            color = t['accent'] if i == 0 else t['other']
            ax.add_patch(FancyBboxPatch((0, y - 0.22), v, 0.44, boxstyle='round,pad=0,rounding_size=0.08',
                                        mutation_aspect=vmax / 12, linewidth=0, facecolor=color))
            ax.text(v + vmax * 0.02, y, f'{v} ms', va='center', ha='left', fontsize=9.5,
                    color=t['text'] if i == 0 else t['muted'], fontweight='bold' if i == 0 else 'normal')
        ax.set_xlim(0, vmax)
        ax.set_ylim(-0.6, len(apps) - 0.4)
        ax.set_title(title, color=t['text'], fontsize=11, loc='left', pad=8)
        ax.set_yticks(range(len(apps)))
        ax.set_yticklabels(list(reversed(labels)), color=t['text'], fontsize=10)
        ax.tick_params(axis='x', colors=t['muted'], labelsize=8, length=0)
        ax.tick_params(axis='y', length=0)
        ax.xaxis.grid(True, color=t['grid'], linewidth=0.8)
        ax.set_axisbelow(True)
        for s in ax.spines.values():
            s.set_visible(False)
    fig.text(0.995, 0.01, 'Time until the photo is on screen, lower is better. Median of 6-10 runs.',
             ha='right', va='bottom', fontsize=8, color=t['muted'])
    fig.tight_layout(rect=(0, 0.04, 1, 1))
    fig.savefig(f'{out}-{name}.png', facecolor=t['surface'])
    print('wrote', f'{out}-{name}.png')
