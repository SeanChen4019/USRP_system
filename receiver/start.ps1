param([string]$ConfigPath=(Join-Path $PSScriptRoot 'config.ini'),[switch]$CheckOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. "$root/shared/launch.ps1"
$ConfigPath=(Resolve-Path -LiteralPath $ConfigPath).Path
$config=Read-LinkConfig $ConfigPath
$ffplay=Find-MediaTool 'ffplay' $config.ffplay $root
$argsList=@('-hide_banner','-window_title','USRP live video receiver','-fflags','nobuffer','-flags','low_delay','-framedrop','-probesize','32768','-analyzeduration','100000','-i',"udp://127.0.0.1:$($config.video_port)?fifo_size=4096&overrun_nonfatal=1")
Invoke-VideoEndpoint -Role receiver -ConfigPath $ConfigPath -Root $root -MediaExe $ffplay -MediaArguments $argsList -CheckOnly:$CheckOnly
