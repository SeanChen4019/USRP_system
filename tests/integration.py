"""Stdlib-only PC integration tests. Runs C++ binaries, never opens an RF device."""
import csv
import json
import pathlib
import socket
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXE = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build' / 'streamlink.exe'
OUT = ROOT / 'out' / ('integration-' + time.strftime('%Y%m%d-%H%M%S'))
OUT.mkdir(parents=True)

def port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]

def last(path):
    with path.open(encoding='utf-8-sig') as handle:
        return list(csv.DictReader(handle))[-1]

def scenario(name, rate, command, expected_min, expected_max=1.001):
    folder = OUT / name
    folder.mkdir()
    p1, p2 = port(), port()
    while p2 == p1:
        p2 = port()
    processes, handles = [], []
    def start(label, args):
        log = (folder / (label + '.log')).open('w', encoding='utf-8')
        handles.append(log)
        proc = subprocess.Popen([str(EXE), *map(str, args)], stdout=log, stderr=subprocess.STDOUT)
        processes.append(proc)
        return proc
    try:
        rx = start('rx', ['rx', '--listen', f'127.0.0.1:{p2}', '--seconds', 8, '--csv', folder / 'rx.csv'])
        ch = start('channel', [*command, '--listen', f'127.0.0.1:{p1}', '--to', f'127.0.0.1:{p2}', '--seconds', 7])
        time.sleep(.4)
        tx = start('tx', ['tx', '--to', f'127.0.0.1:{p1}', '--mbps', rate, '--seconds', 5, '--csv', folder / 'tx.csv'])
        for proc in (tx, ch, rx):
            assert proc.wait(timeout=15) == 0, f'{name}: process failed; see logs'
        r, t = last(folder / 'rx.csv'), last(folder / 'tx.csv')
        ratio = int(r['unique_bytes']) / int(t['unique_bytes'])
        result = dict(name=name, sent=int(t['unique_bytes']), received=int(r['unique_bytes']),
                      delivery_ratio=ratio, duplicates=int(r['duplicates']), invalid=int(r['invalid']),
                      source_mbps=float(t['average_mbps']), backend='simulation, not RF')
        assert expected_min <= ratio <= expected_max, result
        assert result['invalid'] == 0, result
        if name == 'duplicates':
            assert result['duplicates'] > 0, result
        if name == 'clean':
            assert result['source_mbps'] > 18, result
        print(json.dumps(result), flush=True)
        return result
    finally:
        for proc in processes:
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=5)
        for handle in handles:
            handle.close()

subprocess.run([str(EXE), 'selftest'], check=True)
results = [
    scenario('clean', 20, ['channel', '--mbps', 30], .99),
    scenario('duplicates', 12, ['channel', '--mbps', 20, '--duplicate', .2], .99),
    scenario('loss', 12, ['channel', '--mbps', 20, '--loss', .1], .87, .93),
    scenario('overload', 20, ['channel', '--mbps', 8, '--queue-ms', 50], .2, .6),
    scenario('iq_cfo_awgn', 5, ['iq-channel', '--rate', 10e6, '--cfo', 3000, '--snr', 30], .95),
]
(OUT / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
print('PASS. Results:', OUT)
