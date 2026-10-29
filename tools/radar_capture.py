# radar_capture.py - Record the master's radar trace (and optionally the slave's log) and print a summary.
#
# Needs APP_RADAR_TRACE 1 in main/app_config.h. Does not reset the boards.
#   py tools/radar_capture.py                      45 s from COM6
#   py tools/radar_capture.py --seconds 60 --slave COM7 --csv walk.csv
#
# Summary: frame rate and gaps, each second's nearest target (x, y, bearing), the widest bearing seen on each
# side, how much a still target jitters, frozen "held" repeats, and (with --slave) how far apart the two eyes'
# blinks start. Radar x is in radar coordinates: which sign is the viewer's left has to be checked by walking.
import argparse
import math
import statistics
import threading
import time

import serial


def readPort(port, seconds, lines, stop):
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.2
    ser.dtr = False                                     # Set before open so the board isn't reset
    ser.rts = False
    ser.open()
    end = time.time() + seconds
    while time.time() < end and not stop.is_set():
        raw = ser.readline()
        if raw:
            lines.append((time.time(), raw.decode('utf-8', 'replace').strip()))
    ser.close()


def parseTrace(lines):
    frames = []                                         # (pc time, board ms, [(x, y, speed, res) or None] * 3)
    for pcT, text in lines:
        parts = text.split()
        if len(parts) != 5 or parts[0] != 'RT':
            continue                                    # Not a trace line, or two log lines ran together
        try:
            slots = [None if p == '-' else tuple(int(v) for v in p.split(',')) for p in parts[2:]]
            frames.append((pcT, int(parts[1]), slots))
        except ValueError:
            continue
    return frames


def bearing(x, y):
    return math.degrees(math.atan2(x, y))


def summarize(frames, gazeLines):
    if len(frames) < 2:
        print('No radar trace lines. Is APP_RADAR_TRACE 1 and the master on this port?')
        return
    gaps = [b[1] - a[1] for a, b in zip(frames, frames[1:])]
    seconds = (frames[-1][1] - frames[0][1]) / 1000
    print(f'Frames: {len(frames)} in {seconds:.1f} s = {len(frames) / seconds:.1f} per s, '
          f'gap median {statistics.median(gaps)} ms, max {max(gaps)} ms')

    print('\nNearest target each second (radar coordinates, + bearing = radar +x side):')
    t0 = frames[0][1]
    shown = -1
    for pcT, ms, slots in frames:
        sec = (ms - t0) // 1000
        if sec == shown:
            continue
        shown = sec
        live = [s for s in slots if s]
        if not live:
            print(f'  {sec:3d}s  nobody')
            continue
        x, y, speed, res = min(live, key=lambda s: math.hypot(s[0], s[1]))
        print(f'  {sec:3d}s  x {x / 1000:+.2f} m  y {y / 1000:.2f} m  bearing {bearing(x, y):+5.1f} deg  '
              f'speed {speed:+d} cm/s  ({len(live)} target(s))')

    allPts = [s for _, _, slots in frames for s in slots if s]
    if allPts:
        bearings = [bearing(s[0], s[1]) for s in allPts]
        print(f'\nWidest bearing seen: {min(bearings):+.1f} deg (-x side) to {max(bearings):+.1f} deg (+x side), '
              f'range {min(math.hypot(s[0], s[1]) for s in allPts) / 1000:.2f} to '
              f'{max(math.hypot(s[0], s[1]) for s in allPts) / 1000:.2f} m')

    # Still periods per slot: 3 s or more within 30 cm. Their spread is the radar's jitter on a still person.
    for slot in range(3):
        run = []
        for _, ms, slots in frames + [(0, 0, [None] * 3)]:
            s = slots[slot]
            if s and run and math.hypot(s[0] - run[0][1][0], s[1] - run[0][1][1]) < 300:
                run.append((ms, s))
                continue
            if len(run) >= 2 and run[-1][0] - run[0][0] >= 3000:
                xs = [p[1][0] for p in run]
                ys = [p[1][1] for p in run]
                print(f'Still target in slot {slot + 1} for {(run[-1][0] - run[0][0]) / 1000:.1f} s at '
                      f'x {statistics.mean(xs) / 1000:+.2f} y {statistics.mean(ys) / 1000:.2f} m: jitter '
                      f'x {statistics.pstdev(xs):.0f} mm, y {statistics.pstdev(ys):.0f} mm')
            run = [(ms, s)] if s else []

    # Held repeats: the exact same reading 10+ frames in a row (the radar repeating a lost target)
    for slot in range(3):
        same = 0
        for a, b in zip(frames, frames[1:]):
            if a[2][slot] and a[2][slot] == b[2][slot]:
                same += 1
                continue
            if same >= 10:
                print(f'Held: slot {slot + 1} repeated {a[2][slot][:2]} for {same + 1} frames')
            same = 0

    if gazeLines:
        print(f'\nGaze lines: {len(gazeLines)} (RG = where each frame aimed the eyes, -1 left .. +1 right)')


def blinkSync(masterLines, slaveLines):
    masterT = [t for t, line in masterLines if 'Blink start' in line]
    slaveT = [t for t, line in slaveLines if 'Blink start' in line]
    offsets = []
    for t in masterT:                                   # Pair each master blink with the slave blink nearest to it
        near = min(slaveT, key=lambda s: abs(s - t), default=None)
        if near is not None and abs(near - t) < 0.5:
            offsets.append((near - t) * 1000)
    if not offsets:
        print('\nBlink sync: no matching "Blink start" lines on both ports')
        return
    print(f'\nBlink sync over {len(offsets)} blinks: slave starts {statistics.mean(offsets):+.0f} ms after the master '
          f'(min {min(offsets):+.0f}, max {max(offsets):+.0f}). Add the mean to APP_BLINK_SYNC_LEAD_MS.')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', default='COM6', help='master (radar) port')
    ap.add_argument('--slave', help='slave port, to check blink sync')
    ap.add_argument('--seconds', type=float, default=45)
    ap.add_argument('--csv', help='save every frame to this file')
    args = ap.parse_args()

    stop = threading.Event()
    masterLines, slaveLines = [], []
    threads = [threading.Thread(target=readPort, args=(args.port, args.seconds, masterLines, stop))]
    if args.slave:
        threads.append(threading.Thread(target=readPort, args=(args.slave, args.seconds, slaveLines, stop)))
    print(f'Recording {args.seconds:.0f} s...')
    for t in threads:
        t.start()
    try:
        for t in threads:
            t.join()
    except KeyboardInterrupt:
        stop.set()
        for t in threads:
            t.join()

    frames = parseTrace(masterLines)
    if args.csv:
        with open(args.csv, 'w') as f:
            f.write('pc_time,board_ms,slot,x_mm,y_mm,speed_cms,res_mm\n')
            for pcT, ms, slots in frames:
                for i, s in enumerate(slots):
                    if s:
                        f.write(f'{pcT:.3f},{ms},{i + 1},{s[0]},{s[1]},{s[2]},{s[3]}\n')
    summarize(frames, [l for l in masterLines if l[1].startswith('RG ')])
    if args.slave:
        blinkSync(masterLines, slaveLines)


if __name__ == '__main__':
    main()
