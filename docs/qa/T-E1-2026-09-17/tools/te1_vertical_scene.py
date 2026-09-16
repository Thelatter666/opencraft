#!/usr/bin/env python3
"""T-E1 vertical scene, SATURATED aim: keep pushing the pitch down until the
client's own clamp (-89 deg) is reached, which is what guarantees the crosshair
is on the block directly under the feet. Then dig with frames inside the drop's
short visible window, then walk.
usage: vertical_run3.py <pid> <win> <tag>
"""
import subprocess, sys, time
from PIL import Image, ImageChops
PID, WIN, TAG = sys.argv[1], sys.argv[2], sys.argv[3]
OUT='/tmp/te1_evidence'; TOOL='/tmp/te1input2'; CENTER=('960','463')
def act():
    subprocess.run([TOOL,'front',PID],stdout=subprocess.DEVNULL)
    subprocess.run([TOOL,'activate',PID],stdout=subprocess.DEVNULL)
def shot(n):
    p=f'{OUT}/{TAG}_{n}.png'
    subprocess.run(['screencapture','-x','-o','-l',WIN,p],check=True)
    return Image.open(p).convert('RGB')
act(); time.sleep(0.6); shot('00_pristine')
# push the pitch past the clamp: 7 x 100 px = 1.75 rad > 1.5533 rad
for i in range(7):
    act(); time.sleep(0.3)
    b = shot('tmp_b')
    subprocess.run([TOOL,'move','0','100'],check=True)
    time.sleep(0.6)
    a = shot('tmp_a')
    moved = ImageChops.difference(b,a).getbbox() is not None
    print(f'  pitch step {i}: landed={moved}', flush=True)
shot('01_aimed_saturated')
print('aim saturated at the -89 deg clamp', flush=True)
act(); time.sleep(0.3)
subprocess.run([TOOL,'click','0','down',*CENTER],check=True)
t0=time.time()
for target,name in ((0.70,'02_crack'),(0.90,'03_drop_a'),(1.10,'04_drop_b'),(1.40,'05_drop_c')):
    while time.time()-t0 < target:
        time.sleep(0.05); subprocess.run([TOOL,'click','0','down',*CENTER],check=True)
    shot(name); print(f'{name} at {time.time()-t0:.2f}s', flush=True)
subprocess.run([TOOL,'click','0','up',*CENTER],check=True)
subprocess.run([TOOL,'key','13','down'],check=True)
dl=time.time()+1.0
while time.time()<dl:
    time.sleep(0.06); subprocess.run([TOOL,'key','13','down'],check=True)
subprocess.run([TOOL,'key','13','up'],check=True)
time.sleep(1.0); shot('06_after_walk')
time.sleep(1.5); shot('07_settled')
