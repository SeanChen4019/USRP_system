param([string]$UhdRoot='',[string]$CMake='cmake',[switch]$SoftwareOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
& "$root/scripts/build-windows.ps1" -Endpoint transmitter -WithUhd:(!$SoftwareOnly) -UhdRoot $UhdRoot -CMake $CMake
