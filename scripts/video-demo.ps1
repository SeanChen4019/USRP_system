param([string]$FFmpeg='ffmpeg',[string]$FFplay='ffplay',[int]$Seconds=60,[string]$InputFile='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$exe="$root\build\streamlink.exe"
$bundled=Get-ChildItem "$root\tools\ffmpeg\*\bin\ffmpeg.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
if($FFmpeg -eq 'ffmpeg' -and $bundled){$FFmpeg=$bundled.FullName}
if($FFplay -eq 'ffplay' -and $bundled){$FFplay=Join-Path $bundled.DirectoryName 'ffplay.exe'}
foreach($command in @($FFmpeg,$FFplay)){if(!(Get-Command $command -ErrorAction SilentlyContinue)){throw "Missing $command; install FFmpeg/FFplay or provide full paths"}}
$run=Join-Path "$root\out" ('video-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $run | Out-Null
$jobs=@()
try {
 foreach($spec in @(
  @('rx','--listen','127.0.0.1:9002','--out','127.0.0.1:5601','--seconds',($Seconds+8),'--csv',"`"$run\rx.csv`""),
  @('channel','--listen','127.0.0.1:9001','--to','127.0.0.1:9002','--seconds',($Seconds+8),'--mbps','20'),
  @('ingress','--listen','127.0.0.1:5600','--to','127.0.0.1:9001','--seconds',($Seconds+8)))) {
   $name=$spec[0];$jobs+=Start-Process $exe -ArgumentList $spec -WindowStyle Hidden -PassThru -RedirectStandardOutput "$run\$name.log" -RedirectStandardError "$run\$name.err"
 }
 # The playback window is intentionally visible: it is the requested interactive demo.
 $jobs+=Start-Process $FFplay -ArgumentList @('-fflags','nobuffer','-flags','low_delay','-framedrop','-probesize','32768','-analyzeduration','100000','-i','udp://127.0.0.1:5601?fifo_size=4096&overrun_nonfatal=1') -PassThru -RedirectStandardError "$run\player.log"
 Start-Sleep -Milliseconds 500
 $source=@('-re','-f','lavfi','-i','testsrc2=size=1920x1080:rate=30')
 if($InputFile){$source=@('-re','-stream_loop','-1','-i',$InputFile)}
 & $FFmpeg @source -t $Seconds -an -vf 'scale=1920:1080,fps=30' -c:v libx264 -preset ultrafast -tune zerolatency -b:v 6M -maxrate 6M -bufsize 600k -g 30 -bf 0 -pix_fmt yuv420p -f mpegts 'udp://127.0.0.1:5600?pkt_size=1316'
 if($LASTEXITCODE -ne 0){throw 'Encoder failed'}
}finally{foreach($job in $jobs){if(!$job.HasExited){Stop-Process -Id $job.Id}}}
