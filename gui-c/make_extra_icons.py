#!/usr/bin/env python3
"""Draws the five extra sequence-button pictures (tilde, plus, comma, Del, START) into icons/ (24 px high, transparent).
Usage: python make_extra_icons.py   (needs "pip install pillow"), then python gen_icons.py"""
import os
from PIL import Image, ImageDraw

SS = 8                      # supersampling
here = os.path.dirname(os.path.abspath(__file__))
OUT = (20, 20, 20, 255)     # dark outline, so the picture shows on light and dark buttons


def canvas(w, h=24):
    return Image.new('RGBA', (w * SS, h * SS), (0, 0, 0, 0))


def save(im, name, w, h=24):
    im.resize((w, h), Image.LANCZOS).save(os.path.join(here, 'icons', name + '.png'))


def poly(d, pts, fill, outline=OUT, width=1.6):
    pts = [(x * SS, y * SS) for x, y in pts]
    d.polygon(pts, fill=outline)
    # inner fill: shrink by drawing the outline as a thick line first and the fill on top
    d.line(pts + [pts[0]], fill=outline, width=int(width * SS), joint='curve')
    d.polygon(pts, fill=fill)
    d.line(pts + [pts[0]], fill=outline, width=int(width * SS), joint='curve')


# ~  a wave: pause
im = canvas(24); d = ImageDraw.Draw(im)
import math
pts = [((3 + i * 18 / 40) * SS, (12 - 4.2 * math.sin(i / 40 * 2 * math.pi)) * SS) for i in range(41)]
d.line(pts, fill=OUT, width=int(6.2 * SS), joint='curve')
d.line(pts, fill=(255, 190, 40, 255), width=int(3.4 * SS), joint='curve')
save(im, 'tilde', 24)

# +  together
im = canvas(24); d = ImageDraw.Draw(im)
def rect(x0, y0, x1, y1, fill):
    d.rectangle([x0 * SS, y0 * SS, x1 * SS, y1 * SS], fill=fill)
rect(2.5, 9, 21.5, 15, OUT); rect(9, 2.5, 15, 21.5, OUT)
rect(4, 10.4, 20, 13.6, (60, 200, 90, 255)); rect(10.4, 4, 13.6, 20, (60, 200, 90, 255))
save(im, 'plus', 24)

# ,  next step
im = canvas(24); d = ImageDraw.Draw(im)
d.ellipse([7 * SS, 5 * SS, 17 * SS, 15 * SS], fill=OUT)
d.polygon([(9 * SS, 13 * SS), (16 * SS, 12 * SS), (11 * SS, 21.5 * SS), (8 * SS, 21 * SS)], fill=OUT)
d.ellipse([8.4 * SS, 6.4 * SS, 15.6 * SS, 13.6 * SS], fill=(70, 150, 255, 255))
d.polygon([(10 * SS, 12.4 * SS), (14.4 * SS, 11.8 * SS), (10.6 * SS, 19.6 * SS), (9.4 * SS, 19.3 * SS)], fill=(70, 150, 255, 255))
save(im, 'comma', 24)

# Del  a backspace key with an X
im = canvas(28); d = ImageDraw.Draw(im)
body = [(2, 12), (9, 4), (26, 4), (26, 20), (9, 20)]
poly(d, body, (230, 60, 60, 255))
d.line([(13 * SS, 9 * SS), (21 * SS, 15 * SS)], fill=(255, 255, 255, 255), width=int(2.6 * SS))
d.line([(21 * SS, 9 * SS), (13 * SS, 15 * SS)], fill=(255, 255, 255, 255), width=int(2.6 * SS))
save(im, 'Del', 28)

# HOLD and START: a red badge with bold white letters and a dark shadow (the same look, drawn at the width the word needs)
from PIL import ImageFont


def badge(word, W, name):
    im = canvas(W); d = ImageDraw.Draw(im)
    d.ellipse([0.6 * SS, 0.6 * SS, (W - 0.6) * SS, 23.4 * SS], fill=(178, 0, 32, 255))        # darker rim
    d.ellipse([1.4 * SS, 1.0 * SS, (W - 1.4) * SS, 22.2 * SS], fill=(222, 0, 41, 255))        # the red body
    fontfile = next(f for f in ('/usr/share/fonts/truetype/freefont/FreeSansBold.ttf', '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf') if os.path.exists(f))
    size = 14 * SS
    while True:
        font = ImageFont.truetype(fontfile, size)
        box = d.textbbox((0, 0), word, font=font)
        if box[2] - box[0] <= (W - 7) * SS or size < 6 * SS:
            break
        size -= SS // 4
    tw, th = box[2] - box[0], box[3] - box[1]
    tx, ty = (W * SS - tw) / 2 - box[0], (24 * SS - th) / 2 - box[1] - 0.3 * SS
    for dx, dy in ((0, 1.6), (1.0, 1.0), (-1.0, 1.0), (0, 1.0)):                                # the dark shadow under the letters
        d.text((tx + dx * SS, ty + dy * SS), word, font=font, fill=(30, 30, 30, 255))
    d.text((tx, ty), word, font=font, fill=(255, 255, 255, 255))
    save(im, name, W)


badge('HOLD', 32, 'Hold')
badge('START', 36, 'START')
print('ok')
