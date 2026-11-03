param([switch]$WithUhd,[string]$UhdRoot='',[string]$CMake='cmake',[ValidateSet('all','transmitter','receiver')][string]$Endpoint='all')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!(Get-Command $CMake -ErrorAction SilentlyContinue)){throw 'CMake missing. Use a Visual Studio x64 developer PowerShell with CMake installed.'}
$build=Join-Path $root $(if($WithUhd){'build/windows-uhd'}else{'build/windows'})
$options=@('-S',$root,'-B',$build,'-DCMAKE_BUILD_TYPE=Release')
if($WithUhd){
 $options+='-DSTREAMLINK_UHD=ON'
 if(!$UhdRoot -and $env:UHD_ROOT){$UhdRoot=$env:UHD_ROOT}
 if(!$UhdRoot -and (Test-Path "$env:ProgramFiles\UHD")){$UhdRoot="$env:ProgramFiles\UHD"}
 if($UhdRoot){$options+="-DCMAKE_PREFIX_PATH=$UhdRoot"}
}else{$options+='-DSTREAMLINK_UHD=OFF'}
& $CMake @options
if($LASTEXITCODE -ne 0){throw 'CMake configure failed. UHD requires its C++ SDK and compatible x64 MSVC toolchain.'}
$targets=@('streamlink')
$apps=if($Endpoint -eq 'all'){@('video_transmitter','video_receiver')}else{@("video_$Endpoint")}
$targets+=$apps
if($WithUhd -and $Endpoint -eq 'all'){$targets+='streamlink_usrp'}
& $CMake --build $build --config Release --parallel --target @targets
if($LASTEXITCODE -ne 0){throw 'Compilation failed'}
$core=Get-ChildItem -LiteralPath $build -Filter streamlink.exe -Recurse | Where-Object {$_.FullName -notmatch 'CMakeFiles'} | Select-Object -First 1
if(!$core){throw 'Core executable missing'}
& $core.FullName selftest
if($LASTEXITCODE -ne 0){throw 'Selftest failed'}
$bin=Join-Path $root 'build'
Copy-Item -LiteralPath $core.FullName -Destination "$bin\streamlink.exe" -Force
foreach($name in $apps){
 $app=Get-ChildItem -LiteralPath $build -Filter "$name.exe" -Recurse | Where-Object {$_.FullName -notmatch 'CMakeFiles'} | Select-Object -First 1
 if(!$app){throw "Missing executable: $name"}
 Copy-Item -LiteralPath $app.FullName -Destination "$bin/$name.exe" -Force
}
if($WithUhd -and $Endpoint -eq 'all'){
 $radio=Get-ChildItem -LiteralPath $build -Filter streamlink_usrp.exe -Recurse | Where-Object {$_.FullName -notmatch 'CMakeFiles'} | Select-Object -First 1
 if(!$radio){throw 'UHD executable missing'}
 Copy-Item -LiteralPath $radio.FullName -Destination "$bin\streamlink_usrp.exe" -Force
 if($UhdRoot -and (Test-Path "$UhdRoot\bin")){$env:PATH="$UhdRoot\bin;$env:PATH"}
 & "$bin\streamlink_usrp.exe" help
 if($LASTEXITCODE -ne 0){throw 'UHD executable cannot load. Check UHD bin PATH, DLL dependencies and MSVC runtime.'}
}
if($UhdRoot -and (Test-Path "$UhdRoot\bin")){$env:PATH="$UhdRoot\bin;$env:PATH"}
foreach($name in $apps){
 & "$bin/$name.exe"
 if($LASTEXITCODE -ne 0){throw "Cannot load $name; check UHD runtime DLLs and MSVC runtime"}
}
Write-Host "Built into $bin. No radio connection or transmission was attempted."
