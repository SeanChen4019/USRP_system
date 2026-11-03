"""1080p30 smoke test of NEW endpoints and the real transmitter launcher.
Explicit UDP_TEST + test pattern: does not claim camera or RF validation.
"""
import pathlib
import shutil
import subprocess
import time
import json
from endpoints import ROOT, free_port, write_config, ready

out=ROOT/'out'/('new-video-'+time.strftime('%Y%m%d-%H%M%S'))
out.mkdir(parents=True)
ff=next((ROOT/'tools/ffmpeg').glob('*/bin/ffmpeg.exe'),None)
if ff is None:
    ff=shutil.which('ffmpeg')
assert ff, 'FFmpeg required'
shell=shutil.which('pwsh') or shutil.which('powershell')
camera,link,player=free_port(),free_port(),free_port()
txc,rxc=out/'tx.ini',out/'rx.ini'
write_config(txc,'transmitter',video_port=camera,test_link_address=f'127.0.0.1:{link}',ffmpeg=ff,duration_seconds=12,width=1920,height=1080,bitrate_mbps=6)
write_config(rxc,'receiver',video_port=player,test_link_address=f'127.0.0.1:{link}',duration_seconds=20)
jobs=[];logs=[]
def start(name,args):
    path=out/(name+'.log');log=path.open('w');logs.append(log)
    p=subprocess.Popen(list(map(str,args)),stdout=log,stderr=subprocess.STDOUT);jobs.append(p);return p,path
try:
    rx,path=start('receiver',[ROOT/'build/video_receiver.exe',rxc,'--stop-file',out/'stop'])
    ready(rx,path,'RX_READY')
    decoder,_=start('decoder',[ff,'-hide_banner','-probesize','32768','-analyzeduration','100000','-i',f'udp://127.0.0.1:{player}?fifo_size=4096&overrun_nonfatal=1','-an','-frames:v','180','-progress',out/'progress.txt','-f','null','-'])
    time.sleep(.3)
    launcher,_=start('launcher',[shell,'-NoProfile','-ExecutionPolicy','Bypass','-File',ROOT/'transmitter/start.ps1','-ConfigPath',txc,'-TestPattern'])
    assert decoder.wait(timeout=25)==0
    assert launcher.wait(timeout=10)==0
    assert 'frame=180' in (out/'progress.txt').read_text()
    assert '1920x1080' in (out/'decoder.log').read_text(errors='replace')
    result={'passed':True,'frames':180,'resolution':'1920x1080','fps':30,'backend':'UDP_TEST','source':'explicit test pattern; not camera','target_mbps':6}
    (out/'result.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result))
finally:
    (out/'stop').write_text('stop')
    for job in jobs:
        if job.poll() is None:
            try:job.wait(timeout=3)
            except subprocess.TimeoutExpired:job.kill();job.wait()
    for log in logs:log.close()
