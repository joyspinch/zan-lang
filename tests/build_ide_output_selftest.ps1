# Exercises the official script's AST without drivers, generators or zanc.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$scriptPath = Join-Path $root 'scripts\build_ide.ps1'
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
$statements = @($ast.EndBlock.Statements)
$pathStatements = @($statements | Where-Object {
    $_.Extent.StartOffset -lt ($statements | Where-Object {
        $_.Extent.Text -eq '$script:embedArgs = @()'
    }).Extent.StartOffset
})
$pathPlan = [scriptblock]::Create($ast.ParamBlock.Extent.Text + "`n" + '$PSScriptRoot = $buildScriptDirectory' + "`n" + ($pathStatements.Extent.Text -join "`n"))
$argumentPlan = [scriptblock]::Create((@($statements | Where-Object {
    $_.Extent.Text -like '$zanArgs += @("-o",*' -or
    $_.Extent.Text -eq '$zanArgs += @("--publish")'
}).Extent.Text -join "`n"))
$parkingPlan = [scriptblock]::Create((@($statements | Where-Object {
    $_.Extent.Text -like 'if (Test-Path -LiteralPath $exeOld)*' -or
    $_.Extent.Text -like 'if (Test-Path -LiteralPath $exeOut)*'
}).Extent.Text -join "`n"))
if ($pathStatements.Count -lt 5 -or -not $argumentPlan.ToString() -or -not $parkingPlan.ToString()) {
    throw 'Required official build statements were not found.'
}
$versionPlan = [scriptblock]::Create((@($statements | Where-Object {
    $_.Extent.Text -like '$versionTemplate =*' -or
    $_.Extent.Text -like '$versionSource =*' -or
    $_.Extent.Text -like 'Write-IdeVersionSource -TemplatePath*' -or
    $_.Extent.Text -like '$files = @($files | Where-Object*' -or
    $_.Extent.Text -eq '$files += $versionSource'
}).Extent.Text -join "`n"))
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Test-Plan([string]$OutputPath = 'build\ZanIDE.exe', [switch]$RealParking, [string]$Version, [switch]$GenerateMetadata) {
    $buildScriptDirectory = Join-Path $root 'scripts'
    $pathParameters = @{ OutputPath = $OutputPath }
    if ($PSBoundParameters.ContainsKey('Version')) { $pathParameters.Version = $Version }
    . $pathPlan @pathParameters
    $expected = [IO.Path]::GetFullPath($(if ([IO.Path]::IsPathRooted($OutputPath)) { $OutputPath } else { Join-Path $root $OutputPath }))
    Assert ($exeOut -eq $expected) 'Wrong -o target'
    Assert ($exeOld -eq [IO.Path]::ChangeExtension($expected, '.prev.exe')) 'Wrong parked target'
    Assert ($depsOut -eq [IO.Path]::ChangeExtension($expected, '.deps.txt')) 'Wrong dependency target'
    $zanArgs = @()
    . $argumentPlan
    Assert ($zanArgs[0] -eq '-o' -and $zanArgs[1] -eq $expected) 'Official compiler arguments lost output target'
    Assert ($zanArgs -contains '--publish') 'Official release plan lost --publish'
    if ($GenerateMetadata) {
        $template = Join-Path $root 'src\ide_zan\src\services\IdeVersion.zan'
        $entry = Join-Path $root 'src\ide_zan\src\IdeForm.zan'
        $otherSource = Join-Path $root 'src\ide_zan\src\services\IdeUpdate.zan'
        $files = @($template, $entry, $otherSource)
        . $versionPlan
        Assert ($files.Count -eq 2 -and $files -contains $otherSource -and $files -contains $versionSource) 'Wrong version source list'
        Assert ($files -notcontains $template -and $files -notcontains $entry) 'Placeholder or duplicate entry was retained'
        $generated = [IO.File]::ReadAllText($versionSource)
        $expectedVersion = if ($PSBoundParameters.ContainsKey('Version')) { $Version } else { [IO.File]::ReadAllText((Join-Path $root 'VERSION')).Trim() }
        Assert ($generated.Contains('static string Number() { return "' + $expectedVersion + '"; }')) 'Generated Number() is wrong'
        Assert ($generated -match 'static string Date\(\) \{ return "[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:]{8}Z"; \}') 'Missing UTC build date'
        Assert ($generated -match 'static string Commit\(\) \{ return "[0-9a-f]{7,40}"; \}') 'Missing build commit'
        $normalize = { param($Text) [regex]::Replace($Text, '(static string (?:Number|Date|Commit)\(\) \{ return ")[^"]*("; \})', '$1$2') }
        Assert ((& $normalize $generated) -eq (& $normalize ([IO.File]::ReadAllText($template)))) 'Generation changed methods beyond metadata'
        $brokenTemplate = Join-Path $outputDir 'broken-version.zan'
        [IO.File]::WriteAllText($brokenTemplate, 'public class IdeVersion {}')
        $rejected = $false
        try { Write-IdeVersionSource $brokenTemplate $versionSource $buildVersion } catch { $rejected = $true }
        Assert $rejected 'Missing version method anchor was accepted'
        [IO.File]::WriteAllText($brokenTemplate, ([IO.File]::ReadAllText($template) + "`n" + [IO.File]::ReadAllText($template)))
        $rejected = $false
        try { Write-IdeVersionSource $brokenTemplate $versionSource $buildVersion } catch { $rejected = $true }
        Assert $rejected 'Duplicate version method anchors were accepted'
    }
    $operations = [Collections.Generic.List[string]]::new()
    function Test-Path { param($LiteralPath) return $true }
    function Remove-Item {
        param($LiteralPath, [switch]$Force, $ErrorAction)
        $operations.Add("remove $LiteralPath")
        if ($RealParking) { Microsoft.PowerShell.Management\Remove-Item -LiteralPath $LiteralPath -Force }
    }
    function Move-Item {
        param($LiteralPath, $Destination, [switch]$Force, $ErrorAction)
        $operations.Add("move $LiteralPath -> $Destination")
        if ($RealParking) { Microsoft.PowerShell.Management\Move-Item -LiteralPath $LiteralPath -Destination $Destination -Force }
    }
    . $parkingPlan
    Assert ($operations.Count -eq 2) 'Expected exactly one removal and one move'
    Assert ($operations[0] -eq "remove $exeOld") 'Removal escaped selected output'
    Assert ($operations[1] -eq "move $exeOut -> $exeOld") 'Move escaped selected output'
    if ($RealParking) {
        Assert ([IO.File]::ReadAllText($exeOld) -eq 'current isolated') 'Actual parking did not replace isolated .prev'
        Assert (-not [IO.File]::Exists($exeOut)) 'Isolated original was not moved'
    }
    Write-Output ("PLAN_OK -o $exeOut --publish; " + ($operations -join '; '))
}
$fixture = Join-Path $root ('build\test-out\build-ide-output-' + [guid]::NewGuid().ToString('N'))
$originalLocation = Get-Location
$trackedInputs = @((Join-Path $root 'VERSION'), (Join-Path $root 'src\ide_zan\src\services\IdeVersion.zan'))
$beforeHashes = @($trackedInputs | ForEach-Object { (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash })
try {
    Test-Plan
    [IO.Directory]::CreateDirectory($fixture) | Out-Null
    $custom = Join-Path $fixture 'with spaces\Acceptance.exe'
    # First pass verifies missing parents are created, with parking mocked.
    Test-Plan -OutputPath $custom -GenerateMetadata
    Test-Plan -OutputPath $custom -Version '0.2.22.1001' -GenerateMetadata
    [IO.File]::WriteAllText($custom, 'current isolated')
    [IO.File]::WriteAllText([IO.Path]::ChangeExtension($custom, '.prev.exe'), 'previous isolated')
    Test-Plan -OutputPath $custom -RealParking
    foreach ($bad in @(' ', 'src\invalid.exe', 'build\..\invalid.exe', 'build-other\invalid.exe', 'build\bad.txt', 'build\')) {
        $rejected = $false
        try { Test-Plan -OutputPath $bad } catch { $rejected = $true }
        Assert $rejected "Invalid output accepted: $bad"
    }
    $directoryExe = Join-Path $fixture 'directory.exe'
    [IO.Directory]::CreateDirectory($directoryExe) | Out-Null
    $rejected = $false
    try { Test-Plan -OutputPath $directoryExe } catch { $rejected = $true }
    Assert $rejected 'Directory masquerading as executable accepted'
    foreach ($badVersion in @('', '1.2', '1.2.3.4.5', '-1.2.3', '1.2.65536', '1.2.999999999999', '1.2.3"', "1.2.3`n", "1.2.3`r`n")) {
        $rejected = $false
        try { Test-Plan -OutputPath $custom -Version $badVersion } catch { $rejected = $true }
        Assert $rejected "Invalid version accepted: $badVersion"
    }
    $linkTarget = Join-Path $fixture 'link-target'
    $junction = Join-Path $fixture 'junction'
    [IO.Directory]::CreateDirectory($linkTarget) | Out-Null
    New-Item -ItemType Junction -Path $junction -Target $linkTarget | Out-Null
    $rejected = $false
    try { Test-Plan -OutputPath (Join-Path $junction 'escape.exe') } catch { $rejected = $_.Exception.Message -like '*reparse point*' }
    Assert $rejected 'Junction ancestor accepted'
    [IO.Directory]::Delete($junction)
    $linkOutput = Join-Path $fixture 'linked.exe'
    $linkVictim = Join-Path $fixture 'victim.txt'
    [IO.File]::WriteAllText($linkVictim, 'preserve target')
    $linksAvailable = $true
    foreach ($linkPath in @($linkOutput, [IO.Path]::ChangeExtension($linkOutput, '.prev.exe'), [IO.Path]::ChangeExtension($linkOutput, '.deps.txt'), [IO.Path]::ChangeExtension($linkOutput, '.IdeVersion.zan'), (Join-Path $fixture 'ide.css'))) {
        try { New-Item -ItemType SymbolicLink -Path $linkPath -Target $linkVictim -ErrorAction Stop | Out-Null } catch {
            $linksAvailable = $false
            Write-Output "FILE_LINK_TEST_SKIP: $($_.Exception.Message)"
            break
        }
        try {
            $rejected = $false
            try { Test-Plan -OutputPath $linkOutput } catch { $rejected = $_.Exception.Message -like '*reparse point*' }
            Assert $rejected "File link accepted: $linkPath"
        } finally { [IO.File]::Delete($linkPath) }
    }
    Assert ([IO.File]::ReadAllText($linkVictim) -eq 'preserve target') 'Link target was modified'
    # Exercise the actual guard branch even on hosts without file-link privilege.
    $guard = $statements | Where-Object { $_.Extent.Text -like 'function Assert-IdeOutputPath*' }
    $mockGuard = $guard.Extent.Text.Replace('[IO.File]::GetAttributes($cursor)', '[IO.FileAttributes]::ReparsePoint')
    . ([scriptblock]::Create($mockGuard))
    $buildRoot = (Join-Path $root 'build') + [IO.Path]::DirectorySeparatorChar
    $rejected = $false
    try { Assert-IdeOutputPath $linkOutput } catch { $rejected = $_.Exception.Message -like '*reparse point*' }
    Assert $rejected 'Reparse attribute branch failed'
    $designPlan = [scriptblock]::Create(($statements | Where-Object { $_.Extent.Text -like 'if (-not $SkipDesignGeneration)*' }).Extent.Text)
    function python { throw 'Design generator must not run for acceptance build' }
    $SkipDesignGeneration = $true
    . $designPlan
    $afterHashes = @($trackedInputs | ForEach-Object { (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash })
    Assert (($beforeHashes -join ',') -eq ($afterHashes -join ',')) 'Tracked VERSION/IdeVersion changed'
    Write-Output 'BUILD_IDE_OUTPUT_SELFTEST_OK (no compilation or design generation)'
} finally {
    Set-Location $originalLocation
    if ([IO.Directory]::Exists($fixture)) { Remove-Item -LiteralPath $fixture -Recurse -Force }
}
