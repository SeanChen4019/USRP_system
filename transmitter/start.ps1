param([string]$ConfigPath=(Join-Path $PSScriptRoot 'config.ini'),[switch]$CheckOnly,[switch]$TestPattern)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. "$root/shared/launch.ps1"
$ConfigPath=(Resolve-Path -LiteralPath $ConfigPath).Path
$config=Read-LinkConfig $ConfigPath
$ffmpeg=Find-MediaTool 'ffmpeg' $config.ffmpeg $root
if(!$TestPattern -and !$config.camera_name){throw 'Fill camera_name in transmitter/config.ini. Run transmitter/list-cameras.ps1 to list names.'}
# DirectShow captures the selected Windows camera; no -re for a live camera.
$source=@('-f','dshow','-video_size',"$($config.width)x$($config.height)",'-framerate',$config.fps,'-i',"video=$($config.camera_name)")
if($TestPattern){
 Write-Host 'TEST PATTERN selected explicitly; this run does NOT use the camera.'
 $source=@('-re','-f','lavfi','-i',"testsrc2=size=$($config.width)x$($config.height):rate=$($config.fps)")
}
$mbps=[double]::Parse($config.bitrate_mbps,[Globalization.CultureInfo]::InvariantCulture)
$bps=([long]($mbps*1000000)).ToString([Globalization.CultureInfo]::InvariantCulture)
$buffer=([long]($mbps*100000)).ToString([Globalization.CultureInfo]::InvariantCulture)
$argsList=@('-hide_banner','-nostdin')+$source+@('-an','-c:v','libx264','-preset','ultrafast','-tune','zerolatency','-b:v',$bps,'-maxrate',$bps,'-bufsize',$buffer,'-g',$config.fps,'-bf','0','-pix_fmt','yuv420p','-flush_packets','1','-f','mpegts',"udp://127.0.0.1:$($config.video_port)?pkt_size=1316")
Invoke-VideoEndpoint -Role transmitter -ConfigPath $ConfigPath -Root $root -MediaExe $ffmpeg -MediaArguments $argsList -CheckOnly:$CheckOnly
