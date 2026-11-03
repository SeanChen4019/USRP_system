param([string]$DeviceArgs='addr=192.168.10.2')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Force -Path "$root\out" | Out-Null
$report="$root\out\hardware-probe.txt"
"Device args: $DeviceArgs" | Set-Content $report
Get-NetAdapter | Select-Object Name,InterfaceDescription,Status,LinkSpeed | Format-Table | Out-String | Tee-Object -FilePath $report -Append
$probe=Get-Command uhd_usrp_probe -ErrorAction SilentlyContinue
if(!$probe){
 'UHD tools not on PATH. Install matching Ettus UHD release, then rerun. No device configuration was changed.' | Tee-Object -FilePath $report -Append
 exit 2
}
& $probe.Source --args $DeviceArgs 2>&1 | Tee-Object -FilePath $report -Append
if($LASTEXITCODE -ne 0){throw 'UHD probe failed; see out/hardware-probe.txt'}
