"""Validate the two new C++ applications without opening a radio or camera."""
import pathlib
import socket
import subprocess
import time
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

def free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]

def write_config(path, role, **updates):
    text = (ROOT / role / 'config.ini').read_text(encoding='utf-8')
    values = {}
    for line in text.splitlines():
        if line and not line.startswith('#'):
            k, v = line.split('=', 1)
            values[k] = v
    values.update(backend='udp_test', duration_seconds='10')
    values.update({k: str(v) for k, v in updates.items()})
    path.write_text('\n'.join(f'{k}={v}' for k, v in values.items()), encoding='utf-8-sig')

def ready(proc, path, marker):
    until = time.monotonic() + 5
    while time.monotonic() < until:
        if proc.poll() is not None:
            raise AssertionError(path.read_text(errors='replace'))
        if marker in path.read_text(errors='replace'):
            return
        time.sleep(.02)
    raise AssertionError('readiness timeout')

def main():
    (ROOT/'out').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='双端 测试 ', dir=ROOT/'out') as folder:
        d = pathlib.Path(folder)
        camera, link, player = free_port(), free_port(), free_port()
        assert len({camera,link,player}) == 3
        txc, rxc = d/'发射.ini', d/'接收.ini'
        write_config(txc, 'transmitter', video_port=camera, test_link_address=f'127.0.0.1:{link}')
        write_config(rxc, 'receiver', video_port=player, test_link_address=f'127.0.0.1:{link}')
        txexe, rxexe = ROOT/'build/video_transmitter.exe', ROOT/'build/video_receiver.exe'
        for exe,cfg in ((txexe,txc),(rxexe,rxc)):
            subprocess.run([str(exe),str(cfg),'--check'],check=True)
        # Unknown settings and production placeholders must fail before opening hardware.
        bad=d/'bad.ini';bad.write_text(txc.read_text(encoding='utf-8-sig')+'\nunknown_setting=1')
        assert subprocess.run([str(txexe),str(bad),'--check'],capture_output=True).returncode != 0
        assert subprocess.run([str(txexe),str(ROOT/'transmitter/config.ini'),'--check'],capture_output=True).returncode != 0
        logs=[];jobs=[]
        try:
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sink, socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as source:
                sink.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,1024*1024)
                sink.bind(('127.0.0.1',player));sink.settimeout(3)
                for exe,cfg,marker,name in ((rxexe,rxc,'RX_READY','rx'),(txexe,txc,'TX_READY','tx')):
                    path=d/(name+'.log');log=path.open('w');logs.append(log)
                    job=subprocess.Popen([str(exe),str(cfg),'--stop-file',str(d/(name+'.stop'))],stdout=log,stderr=subprocess.STDOUT)
                    jobs.append(job);ready(job,path,marker)
                # Oversize input is rejected, not truncated or allowed to kill the sender.
                source.sendto(b'x'*1500,('127.0.0.1',camera))
                expected=[i.to_bytes(4,'big')+bytes([i])*1312 for i in range(40)]
                for packet in expected:
                    source.sendto(packet,('127.0.0.1',camera));time.sleep(.002)
                actual=[sink.recvfrom(2048)[0] for _ in expected]
                assert actual == expected
                for name in ('rx','tx'):(d/(name+'.stop')).write_text('stop')
                for job in jobs:assert job.wait(timeout=3)==0
        finally:
            for job in jobs:
                if job.poll() is None:job.kill();job.wait()
            for log in logs:log.close()
    print('PASS: config validation, UTF-8/Chinese paths, 40 video datagrams, oversize rejection, graceful stop')

if __name__=='__main__':main()
