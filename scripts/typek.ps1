param([string]$text="")
Add-Type -AssemblyName System.Windows.Forms
Start-Sleep -Milliseconds 150
if ($text -ne "") { [System.Windows.Forms.SendKeys]::SendWait($text) }
Write-Output ("TYPE " + $text)
