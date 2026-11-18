#!/usr/bin/env python3
"""The touch buttons' icons (platform/android/README.md), drawn as SVGs in
a cartoon style: brush strokes that swell and taper instead of even lines,
chunky arrowheads a little off true, wedges for speed and impact.

    make_icons.py [DIR]     writes jump.svg, spin.svg, slam.svg, turbo.svg,
                            pause.svg and counters.svg (white, 128x128)

A brush stroke is a curve (cubic Béziers) drawn as a filled outline whose
width follows a profile along it (start, middle, end), with round ends.
"""
import math
import os
import sys


def bezier(p0, p1, p2, p3, t):
    u = 1 - t
    return (u * u * u * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t * t * t * p3[0],
            u * u * u * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t * t * t * p3[1])


def sample(segments, n=40):
    """Points along a chain of cubic segments ((p0, p1, p2, p3), ...)."""
    pts = []
    for k, seg in enumerate(segments):
        for i in range(n + 1):
            if k and i == 0:
                continue
            pts.append(bezier(*seg, i / n))
    return pts


def width_at(profile, t):
    """A width along the stroke: profile is [(t, w), ...] from 0 to 1."""
    for (t0, w0), (t1, w1) in zip(profile, profile[1:]):
        if t0 <= t <= t1:
            f = (t - t0) / (t1 - t0) if t1 > t0 else 0
            f = f * f * (3 - 2 * f)  # eased
            return w0 + (w1 - w0) * f
    return profile[-1][1]


def brush(segments, profile, n=40):
    """A tapered stroke along the curve, as a filled path."""
    pts = sample(segments, n)
    total = sum(math.dist(a, b) for a, b in zip(pts, pts[1:]))
    left, right = [], []
    run = 0
    for i, p in enumerate(pts):
        if i:
            run += math.dist(pts[i - 1], p)
        a = pts[max(i - 1, 0)]
        b = pts[min(i + 1, len(pts) - 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        d = math.hypot(dx, dy) or 1
        nx, ny = -dy / d, dx / d
        w = width_at(profile, run / total) / 2
        left.append((p[0] + nx * w, p[1] + ny * w))
        right.append((p[0] - nx * w, p[1] - ny * w))
    outline = left + right[::-1]
    return ('<path d="M' + " L".join("%.1f %.1f" % q for q in outline) + ' Z" fill="#fff" '
            'stroke="#fff" stroke-width="2" stroke-linejoin="round"/>')


def line(a, b, profile):
    """A straight brush stroke."""
    return brush([(a, (a[0] + (b[0] - a[0]) / 3, a[1] + (b[1] - a[1]) / 3),
                   (a[0] + 2 * (b[0] - a[0]) / 3, a[1] + 2 * (b[1] - a[1]) / 3), b)], profile, 12)


def head(tip, direction, size, tilt=0.0):
    """A chunky arrowhead: a fat triangle with rounded corners and a notch,
    its tip at `tip`, pointing `direction` (degrees, 0 right, 90 down)."""
    a = math.radians(direction + tilt)
    ux, uy = math.cos(a), math.sin(a)
    px, py = -uy, ux
    back = (tip[0] - ux * size, tip[1] - uy * size)
    notch = (tip[0] - ux * size * 0.72, tip[1] - uy * size * 0.72)
    l = (back[0] + px * size * 0.78, back[1] + py * size * 0.78)
    r = (back[0] - px * size * 0.70, back[1] - py * size * 0.70)
    pts = [tip, l, notch, r]
    return ('<path d="M' + " L".join("%.1f %.1f" % q for q in pts) + ' Z" fill="#fff" stroke="#fff" '
            'stroke-width="7" stroke-linejoin="round"/>')


def wedge(base, tip, width):
    """A spike: wide at `base`, a point at `tip` (speed lines, impacts)."""
    dx, dy = tip[0] - base[0], tip[1] - base[1]
    d = math.hypot(dx, dy)
    px, py = -dy / d * width / 2, dx / d * width / 2
    pts = [(base[0] + px, base[1] + py), tip, (base[0] - px, base[1] - py)]
    return ('<path d="M' + " L".join("%.1f %.1f" % q for q in pts) + ' Z" fill="#fff" stroke="#fff" '
            'stroke-width="3" stroke-linejoin="round"/>')


def closed_brush(segments, width):
    """A closed outline drawn as a brush, its width a function of (x, y)."""
    pts = sample(segments, 24)
    if math.dist(pts[0], pts[-1]) < 0.5:
        pts = pts[:-1]
    outer, inner = [], []
    n = len(pts)
    for i, p in enumerate(pts):
        a = pts[i - 1]
        b = pts[(i + 1) % n]
        dx, dy = b[0] - a[0], b[1] - a[1]
        d = math.hypot(dx, dy) or 1
        nx, ny = -dy / d, dx / d
        w = width(*p) / 2
        outer.append((p[0] + nx * w, p[1] + ny * w))
        inner.append((p[0] - nx * w, p[1] - ny * w))
    return ('<path d="M' + " L".join("%.1f %.1f" % q for q in outer) + ' Z M' +
            " L".join("%.1f %.1f" % q for q in inner[::-1]) + ' Z" fill="#fff" fill-rule="evenodd" '
            'stroke="#fff" stroke-width="1.5" stroke-linejoin="round"/>')


def ring(cx, cy, rx, ry, thick, thin, rot=0.0):
    """An ellipse drawn as a brush: thick at the front (bottom), thin at
    the back, a little turned."""
    out = []
    n = 72
    a0 = math.radians(rot)
    outer, inner = [], []
    for i in range(n + 1):
        t = 2 * math.pi * i / n
        w = thin + (thick - thin) * (0.5 + 0.5 * math.sin(t))
        for r_, lst in ((w / 2, outer), (-w / 2, inner)):
            x = (rx + r_) * math.cos(t)
            y = (ry + r_) * math.sin(t)
            lst.append((cx + x * math.cos(a0) - y * math.sin(a0), cy + x * math.sin(a0) + y * math.cos(a0)))
    out.append('<path d="M' + " L".join("%.1f %.1f" % q for q in outer) + ' Z M' +
               " L".join("%.1f %.1f" % q for q in inner[::-1]) + ' Z" fill="#fff" fill-rule="evenodd"/>')
    return "".join(out)


def svg(comment, parts):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128">\n  <!-- %s -->\n  %s\n</svg>\n'
            % (comment, "\n  ".join(parts)))


def jump():
    parts = []
    # the take-off: dabs getting bigger as it lifts
    for (x, y), w in (((22, 116), 5), ((26, 101), 7), ((31, 86), 9)):
        parts.append(line((x - 1, y + 5), (x + 1, y - 5), [(0, w * 0.6), (0.5, w), (1, w * 0.7)]))
    # the leap: up, over once, and down, swelling at the top
    parts.append(brush([((34, 74), (40, 30), (78, 10), (94, 44)), ((94, 44), (98, 52), (99, 58), (99, 64))],
                       [(0, 7), (0.45, 15), (1, 11)]))
    parts.append(head((100, 98), 88, 34, tilt=-8))
    return svg("A: jump. Up off the ground, over once and down.", parts)


def spin():
    parts = []
    parts.append(ring(60, 30, 42, 12, 11, 4, rot=-6))
    parts.append(ring(58, 58, 31, 9, 9, 3, rot=5))
    parts.append(ring(66, 81, 21, 7, 8, 3, rot=-8))
    parts.append(ring(58, 99, 12, 4.5, 6, 2.5, rot=6))
    parts.append(brush([((60, 106), (66, 112), (56, 116), (62, 123))], [(0, 6), (1, 2)]))
    # the swirl's arrow off the top ring
    parts.append(head((122, 22), -18, 24, tilt=4))
    return svg("B: spin. A tornado, its top ring whipping round.", parts)


def slam():
    parts = []
    # over the top and straight down, fattening as it falls
    parts.append(brush([((106, 48), (108, 8), (60, 4), (62, 36)), ((62, 36), (63, 44), (63, 50), (63, 56))],
                       [(0, 6), (0.5, 12), (1, 18)]))
    parts.append(head((63, 88), 90, 34, tilt=3))
    # the impact: spikes out from where it lands
    for base, tip, w in (((40, 86), (12, 70), 10), ((36, 98), (6, 96), 9), ((86, 86), (116, 68), 10),
                         ((90, 98), (122, 98), 9)):
        parts.append(wedge(base, tip, w))
    parts.append(ring(63, 109, 44, 9, 9, 3, rot=-3))
    parts.append('<ellipse cx="63" cy="109" rx="18" ry="3.5" fill="#fff" transform="rotate(-3 63 109)"/>')
    return svg("R: crouch and body slam. Over the top and down hard.", parts)


def turbo():
    parts = []
    # the sneaker leaning into the run: its outline a brush, heavy along
    # the sole and the toe, light across the top
    def L(p, q):
        return (p, (p[0] + (q[0] - p[0]) / 3, p[1] + (q[1] - p[1]) / 3),
                (p[0] + 2 * (q[0] - p[0]) / 3, p[1] + 2 * (q[1] - p[1]) / 3), q)
    segs = [L((40, 46), (56, 42)), ((56, 42), (62, 54), (72, 58), (84, 60)), L((84, 60), (110, 68)),
            ((110, 68), (122, 72), (122, 86), (110, 88)), L((110, 88), (42, 88)),
            ((42, 88), (36, 88), (34, 82), (36, 76)), L((36, 76), (40, 46))]
    parts.append('<g transform="translate(81 64) rotate(-10) scale(0.86) translate(-78 -64)">%s</g>'
                 % closed_brush(segs, lambda x, y: 5 + 9 * max(0.0, min(1.0, (y - 44) / 44)) + (3 if x > 104 else 0)))
    # speed: wedges streaming back off it, each its own length
    for base, tip, w in (((24, 46), (2, 49), 10), ((20, 63), (0, 64), 12), ((24, 80), (6, 79), 9)):
        parts.append(wedge(base, tip, w))
    return svg("L: turbo run. A sneaker leaning into a sprint, speed streaming off it.", parts)


def pause():
    parts = []
    for x, rot in ((44, -5), (84, 4)):
        parts.append('<rect x="%d" y="24" width="22" height="80" rx="9" fill="#fff" transform="rotate(%d %d 64)"/>'
                     % (x - 11, rot, x))
    return svg("START: pause.", parts)


def counters():
    parts = []
    # a cartoon eye: a lid that swells at the top, a big pupil with a shine
    parts.append(brush([((8, 66), (30, 26), (98, 22), (120, 62))], [(0, 4), (0.5, 12), (1, 4)]))
    parts.append(brush([((10, 68), (34, 100), (94, 102), (118, 64))], [(0, 3), (0.5, 7), (1, 3)]))
    parts.append('<circle cx="66" cy="63" r="20" fill="#fff"/>')
    parts.append('<circle cx="73" cy="56" r="6" fill="#000" fill-opacity="0.55"/>')
    return svg("SELECT: show the HUD's counters. An eye.", parts)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
    for name, make in (("jump", jump), ("spin", spin), ("slam", slam), ("turbo", turbo), ("pause", pause),
                       ("counters", counters)):
        with open(os.path.join(out, name + ".svg"), "w") as f:
            f.write(make())


if __name__ == "__main__":
    main()
