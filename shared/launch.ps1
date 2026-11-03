# Shared launcher plumbing. End-specific camera/player policy lives in its folder.
function Read-LinkConfig([string]$Path) {
 $result=@{}
 foreach($line in [IO.File]::ReadAllLines($Path,[Text.Encoding]::UTF8)){
  $line=$line.Trim();if(!$line -or $line.StartsWith('#')){continue}
  $parts=$line.Split(@('='),2)
  if($parts.Count -ne 2){throw "Invalid config line: $line"}
  $key=$parts[0].Trim();if($result.ContainsKey($key)){throw "Duplicate config key: $key"}
  $result[$key]=$parts[1].Trim()
 }
 return $result
}
function Find-MediaTool([string]$Name,[string]$Configured,[string]$Root) {
 if($Configured -and $Configured -ne $Name){
  $found=Get-Command $Configured -ErrorAction SilentlyContinue
  if(!$found){throw "Cannot find $Configured; edit $Name in config.ini"};return $found.Source
 }
 $found=Get-Command $Name -ErrorAction SilentlyContinue
 if($found){return $found.Source}
 $bundled=Get-ChildItem "$Root/tools/ffmpeg/*/bin/$Name.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
 if($bundled){return $bundled.FullName}
 throw "Install $Name and add its bin directory to PATH, or set its full path in config.ini"
}
function ConvertTo-WindowsArgument([string]$Value) {
 # Windows CRT quoting, including embedded quotes and trailing backslashes.
 $escaped=[regex]::Replace($Value,'(\\*)"','$1$1\"')
 $escaped=[regex]::Replace($escaped,'(\\+)$','$1$1')
 return '"'+$escaped+'"'
}
function Start-LinkChild([string]$Exe,[string[]]$Arguments,[string]$Log,[switch]$Visible) {
 $quoted=($Arguments | ForEach-Object {ConvertTo-WindowsArgument $_}) -join ' '
 $params=@{FilePath=$Exe;ArgumentList=$quoted;PassThru=$true;RedirectStandardOutput="$Log.out.log";RedirectStandardError="$Log.err.log"}
 if(!$Visible){$params.WindowStyle='Hidden'}
 return Start-Process @params
}
function Wait-LinkReady($Process,[string]$Log,[string]$Marker) {
 $deadline=[DateTime]::UtcNow.AddSeconds(45)
 while([DateTime]::UtcNow -lt $deadline){
  if($Process.HasExited){throw "Communication program exited. Read $Log.err.log"}
  if((Test-Path "$Log.out.log") -and (Select-String -LiteralPath "$Log.out.log" -SimpleMatch $Marker -Quiet)){return}
  Start-Sleep -Milliseconds 100
 }
 throw "USRP startup timed out. Read $Log.err.log"
}
function Invoke-VideoEndpoint([string]$Role,[string]$ConfigPath,[string]$Root,[string]$MediaExe,[string[]]$MediaArguments,[switch]$CheckOnly) {
 $config=Read-LinkConfig $ConfigPath
 if($config.uhd_bin){$env:PATH=$config.uhd_bin+';'+$env:PATH}
 $exe=Join-Path $Root "build/video_$Role.exe"
 if(!(Test-Path $exe)){throw "Missing $exe. Run scripts/build-windows.ps1 -WithUhd first."}
 & $exe $ConfigPath --check
 if($LASTEXITCODE -ne 0){throw 'Configuration/build check failed; see error above.'}
 if($CheckOnly){Write-Host 'Configuration and media executable checked. Camera and USRP were not opened.';return}
 $logDir=Join-Path $Root ('out/'+$Role+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
 New-Item -ItemType Directory -Force -Path $logDir | Out-Null
 $stop=Join-Path $logDir 'stop.signal';$radio=$null;$media=$null
 Write-Host "Role=$Role backend=$($config.backend); logs: $logDir"
 Write-Host 'Press Ctrl+C to stop this endpoint.'
 try{
  # Start player first so its UDP socket exists when the receiver emits video.
  if($Role -eq 'receiver'){$media=Start-LinkChild $MediaExe $MediaArguments "$logDir/player" -Visible}
  $radio=Start-LinkChild $exe @($ConfigPath,'--stop-file',$stop) "$logDir/link"
  $marker=if($Role -eq 'transmitter'){'TX_READY'}else{'RX_READY'}
  Wait-LinkReady $radio "$logDir/link" $marker
  if($Role -eq 'transmitter'){$media=Start-LinkChild $MediaExe $MediaArguments "$logDir/camera"}
  while(!$radio.HasExited -and !$media.HasExited){
   Start-Sleep -Seconds 1
   $latest=Get-Content -LiteralPath "$logDir/link.out.log" -Tail 1
   if($latest){Write-Host $latest}
  }
  if($radio.HasExited -and $radio.ExitCode -ne 0){throw "Communication failed. Read $logDir/link.err.log"}
  if($media.HasExited -and $media.ExitCode -ne 0){throw "Camera/player failed. Read media logs in $logDir"}
 }finally{
  # Request graceful UHD shutdown before a bounded forced-stop fallback.
  [IO.File]::WriteAllText($stop,'stop')
  if($radio -and !$radio.HasExited){if(!$radio.WaitForExit(3000)){$radio.Kill();$radio.WaitForExit()}}
  if($media -and !$media.HasExited){$media.Kill();$media.WaitForExit()}
 }
}
