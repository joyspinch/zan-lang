$ErrorActionPreference="Continue"
Set-Location $PSScriptRoot\..
$sys = Get-ChildItem -Recurse stdlib\System -Filter *.zan | Where-Object { $_.FullName -notmatch 'Windows\\Forms' } | ForEach-Object { $_.FullName }
$sdl = Get-ChildItem -Recurse stdlib\SDL3,stdlib\Platform -Filter *.zan | ForEach-Object { $_.FullName }
$gui = Get-ChildItem -Recurse stdlib\Gui -Filter *.zan | ForEach-Object { $_.FullName }
$fg=@($sys+$sdl+$gui)
$sw=[Diagnostics.Stopwatch]::StartNew()
& .\build\zanc1.exe build\sw_gui.ll @fg 2>build\sw_gui_e.txt | Out-Null
$rc=$LASTEXITCODE; $sw.Stop()
"gui gen1=$rc files=$($fg.Count) secs=$([int]$sw.Elapsed.TotalSeconds)" | Out-File build\sw_status.txt -Encoding utf8
"DONE" | Out-File build\sw_status.txt -Append -Encoding utf8
