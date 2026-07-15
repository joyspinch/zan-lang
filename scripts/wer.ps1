Get-WinEvent -FilterHashtable @{LogName='Application'; Level=2} -MaxEvents 8 |
  ForEach-Object {
    $m = $_.Message
    if ($m.Length -gt 500) { $m = $m.Substring(0,500) }
    Write-Output ('==== ' + $_.TimeCreated + ' id=' + $_.Id + ' ' + $_.ProviderName)
    Write-Output $m
  }
