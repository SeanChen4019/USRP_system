"""Exercise real H.264 1080p30 encoding, C++ transport and headless decoding."""
import pathlib
import subprocess
import time
import json

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXE = ROOT / 'build' / 'streamlink.exe'
FF = next((ROOT / 'tools' / 'ffmpeg').glob('*/bin/ffmpeg.exe'))
OUT = ROOT / 'out' / ('video-smoke-' + time.strftime('%Y%m%d-%H%M%S'))
OUT.mkdir(parents=True)
jobs, logs = [], []

def start(name, args):
    log = (OUT / (name + '.log')).open('w', encoding='utf-8')
    logs.append(log)
    p = subprocess.Popen(list(map(str, args)), stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
    jobs.append(p)
    return p

try:
    start('rx', [EXE, 'rx', '--listen', '127.0.0.1:19502', '--out', '127.0.0.1:19504', '--seconds', 20, '--csv', OUT / 'rx.csv'])
    start('channel', [EXE, 'channel', '--listen', '127.0.0.1:19501', '--to', '127.0.0.1:19502', '--seconds', 20, '--mbps', 20])
    start('ingress', [EXE, 'ingress', '--listen', '127.0.0.1:19503', '--to', '127.0.0.1:19501', '--seconds', 20])
    decoder = start('decoder', [FF, '-hide_banner', '-loglevel', 'info', '-probesize', 32768, '-analyzeduration', 100000,
                    '-i', 'udp://127.0.0.1:19504?fifo_size=4096&overrun_nonfatal=1', '-an', '-frames:v', 180,
                    '-progress', OUT / 'decode-progress.txt', '-f', 'null', '-'])
    time.sleep(.5)
    encoder = start('encoder', [FF, '-hide_banner', '-re', '-f', 'lavfi', '-i', 'testsrc2=size=1920x1080:rate=30',
                    '-t', 10, '-an', '-c:v', 'libx264', '-preset', 'ultrafast', '-tune', 'zerolatency', '-b:v', '6M',
                    '-maxrate', '6M', '-bufsize', '600k', '-g', 30, '-bf', 0, '-pix_fmt', 'yuv420p', '-f', 'mpegts',
                    'udp://127.0.0.1:19503?pkt_size=1316'])
    assert encoder.wait(timeout=30) == 0, 'Encoder failed'
    assert decoder.wait(timeout=10) == 0, 'Decoder failed'
    progress = (OUT / 'decode-progress.txt').read_text()
    assert 'frame=180' in progress and 'progress=end' in progress, progress
    info = (OUT / 'decoder.log').read_text()
    assert '1920x1080' in info, info
    result = {'backend': 'UDP simulation, NOT RF', 'video': '1920x1080 H.264 30fps', 'target_encoder_mbps': 6,
              'decoded_frames': 180, 'glass_to_glass_latency': 'not measured; no camera/display used', 'passed': True}
    (OUT / 'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result))
    print(OUT)
finally:
    for p in jobs:
        if p.poll() is None:
            p.terminate()
            p.wait(timeout=5)
    for log in logs:
        log.close()
