param([switch]$IncludeMedia)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$stage=Join-Path $root "release/StreamLink-$stamp"
New-Item -ItemType Directory -Path $stage -Force | Out-Null
foreach($name in @('src','include','scripts','tests','docs','CMakeLists.txt','README.md','THIRD_PARTY.md','.clang-format','.gitignore')){
 Copy-Item -LiteralPath (Join-Path $root $name) -Destination $stage -Recurse
}
New-Item -ItemType Directory -Path "$stage/build" | Out-Null
if(Test-Path "$root/build/streamlink.exe"){Copy-Item -LiteralPath "$root/build/streamlink.exe" -Destination "$stage/build"}
if($IncludeMedia -and (Test-Path "$root/tools/ffmpeg")){
 New-Item -ItemType Directory -Path "$stage/tools" | Out-Null
 Copy-Item -LiteralPath "$root/tools/ffmpeg" -Destination "$stage/tools" -Recurse
}
$manifest=Get-ChildItem -LiteralPath $stage -File -Recurse | ForEach-Object{
 [pscustomobject]@{path=$_.FullName.Substring($stage.Length+1).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
}
$manifest | ConvertTo-Json | Set-Content -Encoding utf8 "$stage/MANIFEST.json"
Compress-Archive -LiteralPath $stage -DestinationPath "$stage.zip"
Get-Item -LiteralPath "$stage.zip" | Select-Object FullName,Length
Get-FileHash -LiteralPath "$stage.zip" -Algorithm SHA256
