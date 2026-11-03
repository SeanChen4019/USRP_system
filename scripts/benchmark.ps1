param([double]$Mbps=12,[double]$ChannelMbps=20,[double]$Loss=0,[double]$Duplicate=0,[int]$Seconds=10,[int]$BasePort=19001)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$exe="$root\build\streamlink.exe"
if(!(Test-Path $exe)){throw 'Run scripts/build.ps1 first'}
$run=Join-Path "$root\out" (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
New-Item -ItemType Directory -Force -Path $run | Out-Null
$jobs=@()
try {
 $rxPort=$BasePort+1
 $jobs+=Start-Process -FilePath $exe -ArgumentList @('rx','--listen',"127.0.0.1:$rxPort",'--seconds',($Seconds+4),'--csv',"`"$run\rx.csv`"") -WindowStyle Hidden -PassThru -RedirectStandardOutput "$run\rx.log" -RedirectStandardError "$run\rx.err"
 $jobs+=Start-Process -FilePath $exe -ArgumentList @('channel','--listen',"127.0.0.1:$BasePort",'--to',"127.0.0.1:$rxPort",'--seconds',($Seconds+3),'--mbps',$ChannelMbps,'--loss',$Loss,'--duplicate',$Duplicate) -WindowStyle Hidden -PassThru -RedirectStandardOutput "$run\channel.log" -RedirectStandardError "$run\channel.err"
 Start-Sleep -Milliseconds 500
 & $exe tx --to "127.0.0.1:$BasePort" --mbps $Mbps --seconds $Seconds --csv "$run\tx.csv" > "$run\tx.log"
 if($LASTEXITCODE -ne 0){throw 'Transmitter failed'}
 foreach($job in $jobs){$job.WaitForExit();if($job.ExitCode -ne 0){throw "Child failed: see $run"}}
 $rx=@(Import-Csv "$run\rx.csv");$tx=@(Import-Csv "$run\tx.csv")
 $received=[double]$rx[-1].unique_bytes;$sent=[double]$tx[-1].unique_bytes
 $summary=[ordered]@{backend='UDP loopback; NOT radio';requested_mbps=$Mbps;channel_mbps=$ChannelMbps;random_loss=$Loss;duplicate_probability=$Duplicate;sent_bytes=$sent;received_unique_bytes=$received;delivery_ratio=($received/[Math]::Max(1,$sent));invalid=[long]$rx[-1].invalid;duplicates=[long]$rx[-1].duplicates;output=$run}
 $summary | ConvertTo-Json | Set-Content -Encoding utf8 "$run\summary.json"
 $summary | Format-List
 if($Loss -eq 0 -and $ChannelMbps -gt $Mbps*1.1 -and $received/$sent -lt 0.99){throw 'Clean channel delivered less than 99%'}
 if([long]$rx[-1].invalid -ne 0){throw 'Invalid packet detected'}
} finally {foreach($job in $jobs){if(!$job.HasExited){Stop-Process -Id $job.Id}}}
