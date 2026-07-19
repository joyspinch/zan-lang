Set-Location $PSScriptRoot\..
$gui = @(Get-ChildItem -Recurse stdlib\Gui -Filter *.zan | ForEach-Object { $_.FullName })
"COUNT=$($gui.Count)" | Out-File build\pf_status.txt -Encoding utf8
foreach($g in $gui){
  $p = Start-Process -FilePath .\build\zanc1.exe -ArgumentList @("build\pf.ll",$g) -RedirectStandardError build\pf_e.txt -PassThru -WindowStyle Hidden
  if($p.WaitForExit(4000)){
    # only note quick ones that are NOT simple error exits (rc0 unexpected) - we care about HANG
  } else {
    $p.Kill()
    "HANG: $($g -replace [regex]::Escape((Get-Location).Path + '\'),'')" | Out-File build\pf_status.txt -Append -Encoding utf8
  }
}
"DONE" | Out-File build\pf_status.txt -Append -Encoding utf8
