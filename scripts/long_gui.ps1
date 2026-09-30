Set-Location $PSScriptRoot\..
Remove-Item build\phase.txt,build\long_done.txt -ErrorAction SilentlyContinue
$sys = Get-ChildItem -Recurse stdlib\System -Filter *.zan | Where-Object { $_.FullName -notmatch 'Windows\\Forms' } | ForEach-Object { $_.FullName }
$plat = Get-ChildItem -Recurse stdlib\Platform -Filter *.zan | ForEach-Object { $_.FullName }
$gui = @(Get-ChildItem -Recurse packages\Zan.Gui\src\Gui -Filter *.zan | ForEach-Object { $_.FullName })
$f=$sys+$plat+$gui
$sw=[Diagnostics.Stopwatch]::StartNew()
& .\build\zanc1dbg.exe build\long.ll @f 2>build\long_e.txt | Out-Null
$sw.Stop()
"rc=$LASTEXITCODE secs=$([int]$sw.Elapsed.TotalSeconds)" | Out-File build\long_done.txt -Encoding utf8
