param([string]$ConfigPath=(Join-Path $PSScriptRoot 'config.ini'),[switch]$Formats)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. "$root/shared/launch.ps1"
$config=Read-LinkConfig $ConfigPath
$ffmpeg=Find-MediaTool 'ffmpeg' $config.ffmpeg $root
if($Formats){
 if(!$config.camera_name){throw 'Set camera_name first'}
 & $ffmpeg -hide_banner -list_options true -f dshow -i "video=$($config.camera_name)"
}else{& $ffmpeg -hide_banner -list_devices true -f dshow -i dummy}
# DirectShow listing normally exits nonzero because it does not capture a stream.
Write-Host 'Copy the video device name into camera_name. No recording was started.'
