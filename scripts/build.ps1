param([string]$Compiler='clang++',[string]$Sysroot='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Force -Path "$root\build" | Out-Null
$extra=@()
if(!$Sysroot -and (Test-Path 'E:\sysroot\x86_64-w64-mingw32')){$Sysroot='E:/sysroot/x86_64-w64-mingw32'}
if($Sysroot){
  $compilerRoot=Split-Path (Split-Path (Get-Command $Compiler).Source -Parent) -Parent
  $extra=@("--sysroot=$Sysroot",'-stdlib=libc++','-isystem',"$compilerRoot/include/c++/v1",'-static')
  $gccRoot=Join-Path (Split-Path $Sysroot -Parent) 'lib/gcc/x86_64-w64-mingw32'
  if(Test-Path $gccRoot){$gcc=Get-ChildItem $gccRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1;$extra+=@('-L',$gcc.FullName)}
}
& $Compiler @extra -std=c++17 -O3 -Wall -Wextra -Wpedantic -I "$root\include" "$root\src\main.cpp" -o "$root\build\streamlink.exe" -lws2_32
if($LASTEXITCODE -ne 0){throw 'Build failed'}
& "$root\build\streamlink.exe" selftest
if($LASTEXITCODE -ne 0){throw 'Selftest failed'}
