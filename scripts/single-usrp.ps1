param(
 [Parameter(Mandatory=$true)][double]$FrequencyHz,
 [string]$DeviceArgs='addr=192.168.10.2',
 [string]$TxAntenna='TX/RX',[string]$RxAntenna='RX2',
 [double]$Rate=1000000,[double]$Mbps=0.5,
 [double]$TxGain=0,[double]$RxGain=0,[int]$Seconds=30,
 [string]$UhdRoot=''
)
$ErrorActionPreference='Stop'
if($Seconds -le 0 -or $Rate -le 0 -or $Mbps -le 0 -or $FrequencyHz -le 0){throw 'Parameters must be positive'}
$root=Split-Path $PSScriptRoot -Parent
$exe="$root\build\streamlink.exe";$radio="$root\build\streamlink_usrp.exe"
if(!(Test-Path $exe) -or !(Test-Path $radio)){throw 'Run scripts/build-windows.ps1 -WithUhd first'}
if(!$UhdRoot -and $env:UHD_ROOT){$UhdRoot=$env:UHD_ROOT}
if(!$UhdRoot -and (Test-Path "$env:ProgramFiles\UHD")){$UhdRoot="$env:ProgramFiles\UHD"}
if($UhdRoot){$env:PATH="$UhdRoot\bin;$env:PATH"}
$run=Join-Path "$root\out" ('single-usrp-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Force -Path $run | Out-Null
$jobs=@()
try{
 # Single owner: do not start separate tx/rx processes with the same device address.
 $hw=Start-Process $radio -ArgumentList @('duplex','--args',"`"$DeviceArgs`"",'--freq',$FrequencyHz,'--rate',$Rate,'--tx-antenna',$TxAntenna,'--rx-antenna',$RxAntenna,'--tx-gain',$TxGain,'--rx-gain',$RxGain,'--seconds',($Seconds+8)) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$run\radio.log" -RedirectStandardError "$run\radio.err"
 $jobs+=$hw
 $deadline=[DateTime]::UtcNow.AddSeconds(30)
 $ready=$false
 while([DateTime]::UtcNow -lt $deadline){
  if($hw.HasExited){throw "Radio startup failed; inspect $run\radio.err"}
  if(Test-Path "$run\radio.log"){$ready=[bool](Select-String -LiteralPath "$run\radio.log" -SimpleMatch 'RX_READY' -Quiet)}
  if($ready){break}
  Start-Sleep -Milliseconds 100
 }
 if(!$ready){throw "Radio initialization timed out; inspect $run\radio.err"}
 $rx=Start-Process $exe -ArgumentList @('rx','--listen','127.0.0.1:9002','--seconds',($Seconds+3),'--csv',"`"$run\rx.csv`"") -WindowStyle Hidden -PassThru -RedirectStandardOutput "$run\rx.log" -RedirectStandardError "$run\rx.err"
 $jobs+=$rx
 Start-Sleep -Milliseconds 300
 & $exe tx --to 127.0.0.1:9001 --seconds $Seconds --mbps $Mbps --payload 1200 --csv "$run\tx.csv" > "$run\tx.log"
 if($LASTEXITCODE -ne 0){throw 'Traffic source failed'}
 foreach($job in $jobs){$job.WaitForExit();if($job.ExitCode -ne 0){throw "Worker failed; inspect $run"}}
 $r=@(Import-Csv "$run\rx.csv")[-1];$t=@(Import-Csv "$run\tx.csv")[-1]
 $ratio=[double]$r.unique_bytes/[Math]::Max(1,[double]$t.unique_bytes)
 [ordered]@{backend='single X310 duplex; actual radio';sent_bytes=$t.unique_bytes;received_bytes=$r.unique_bytes;delivery_ratio=$ratio;rate=$Rate;frequency=$FrequencyHz;log_directory=$run} | ConvertTo-Json | Tee-Object -FilePath "$run\summary.json"
 if([double]$r.unique_bytes -eq 0){throw 'No valid radio packets received; inspect tuning, antenna, cable/attenuation and overflow logs'}
}finally{foreach($job in $jobs){if(!$job.HasExited){Stop-Process -Id $job.Id}}}
