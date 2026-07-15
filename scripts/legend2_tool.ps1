param(
    [Parameter(Position = 0)]
    [ValidateSet("init", "validate", "generate", "component")]
    [string]$Command = "validate",

    [Parameter(Position = 1)]
    [string]$Project = ".",

    [string]$Output = "",
    [string]$Namespace = "Game.Generated",
    [ValidateSet("map", "actor", "item", "skill", "buff", "window", "growth", "prefab")]
    [string]$Kind = "",
    [string]$Id = "",
    [string]$Factory = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$script:Kinds = @("map", "actor", "item", "skill", "buff", "window", "growth", "prefab")

function Resolve-Legend2Root {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

function Get-Legend2ManifestPath {
    param([string]$Root)
    return Join-Path $Root "legend2.project.json"
}

function Escape-ZanString {
    param([AllowEmptyString()][string]$Text)
    if ($null -eq $Text) {
        return ""
    }
    return $Text.Replace("\", "\\").Replace('"', '\"').Replace("`r", "\r").Replace("`n", "\n")
}

function ConvertTo-ZanIdentifier {
    param([string]$Text)
    $parts = @($Text -split '[^A-Za-z0-9]+' | Where-Object { $_ })
    $builder = [System.Text.StringBuilder]::new()
    foreach ($part in $parts) {
        if ($part.Length -gt 0) {
            [void]$builder.Append($part.Substring(0, 1).ToUpperInvariant())
            if ($part.Length -gt 1) {
                [void]$builder.Append($part.Substring(1))
            }
        }
    }
    $identifier = $builder.ToString()
    if ([string]::IsNullOrWhiteSpace($identifier)) {
        throw "Component id must contain at least one letter or digit."
    }
    if ($identifier[0] -match '[0-9]') {
        $identifier = "Component$identifier"
    }
    return $identifier
}

function Get-PropertyValue {
    param(
        [object]$Object,
        [string]$Name,
        [object]$Default
    )
    if ($null -eq $Object) {
        return $Default
    }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        return $Default
    }
    return $property.Value
}

function Test-SafeRelativePath {
    param(
        [string]$Root,
        [string]$RelativePath
    )
    if ([string]::IsNullOrWhiteSpace($RelativePath)) {
        return $false
    }
    $full = [System.IO.Path]::GetFullPath((Join-Path $Root $RelativePath))
    $prefix = $Root.TrimEnd("\", "/") + [System.IO.Path]::DirectorySeparatorChar
    return $full.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)
}

function Read-Legend2Manifest {
    param([string]$Root)
    $path = Get-Legend2ManifestPath $Root
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Legend2 manifest not found: $path"
    }
    try {
        return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    } catch {
        throw "Invalid JSON in ${path}: $($_.Exception.Message)"
    }
}

function Test-Legend2Manifest {
    param(
        [string]$Root,
        [object]$Manifest,
        [switch]$CheckFiles
    )

    $errors = [System.Collections.Generic.List[string]]::new()
    $warnings = [System.Collections.Generic.List[string]]::new()

    $schemaVersion = [int](Get-PropertyValue $Manifest "schemaVersion" 0)
    if ($schemaVersion -ne 1) {
        $errors.Add("schemaVersion must be 1.")
    }

    $namespaceName = [string](Get-PropertyValue $Manifest "namespace" "")
    if ([string]::IsNullOrWhiteSpace($namespaceName)) {
        $errors.Add("namespace is required.")
    } elseif ($namespaceName -notmatch '^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*$') {
        $errors.Add("namespace is not a valid Zan namespace.")
    }

    $app = Get-PropertyValue $Manifest "app" $null
    if ($null -eq $app) {
        $errors.Add("app is required.")
    } else {
        $title = [string](Get-PropertyValue $app "title" "")
        $width = [int](Get-PropertyValue $app "width" 0)
        $height = [int](Get-PropertyValue $app "height" 0)
        $frameRate = [int](Get-PropertyValue $app "frameRate" 0)
        $screenMode = [int](Get-PropertyValue $app "screenMode" -1)
        if ([string]::IsNullOrWhiteSpace($title)) { $errors.Add("app.title is required.") }
        if ($width -le 0) { $errors.Add("app.width must be greater than zero.") }
        if ($height -le 0) { $errors.Add("app.height must be greater than zero.") }
        if ($frameRate -le 0) { $errors.Add("app.frameRate must be greater than zero.") }
        if ($screenMode -lt 0 -or $screenMode -gt 3) {
            $errors.Add("app.screenMode must be between 0 and 3.")
        }

        $background = @(Get-PropertyValue $app "background" @())
        if ($background.Count -ne 4) {
            $errors.Add("app.background must contain four RGBA integers.")
        } else {
            foreach ($channel in $background) {
                if ([int]$channel -lt 0 -or [int]$channel -gt 255) {
                    $errors.Add("app.background channels must be between 0 and 255.")
                    break
                }
            }
        }
    }

    $resourceIds = @{}
    foreach ($resource in @(Get-PropertyValue $Manifest "resources" @())) {
        $id = [string](Get-PropertyValue $resource "id" "")
        $file = [string](Get-PropertyValue $resource "file" "")
        if ([string]::IsNullOrWhiteSpace($id)) {
            $errors.Add("Every resource needs an id.")
            continue
        }
        if ($resourceIds.ContainsKey($id)) {
            $errors.Add("Duplicate resource id: $id")
        } else {
            $resourceIds[$id] = $true
        }
        if (!(Test-SafeRelativePath $Root $file)) {
            $errors.Add("Resource '$id' has an invalid project-relative path.")
        } elseif ($CheckFiles -and !(Test-Path -LiteralPath (Join-Path $Root $file) -PathType Leaf)) {
            $errors.Add("Resource file not found for '$id': $file")
        }
    }

    $componentIds = @{}
    foreach ($component in @(Get-PropertyValue $Manifest "components" @())) {
        $kind = [string](Get-PropertyValue $component "kind" "")
        $id = [string](Get-PropertyValue $component "id" "")
        $file = [string](Get-PropertyValue $component "file" "")
        $factory = [string](Get-PropertyValue $component "factory" "")
        if ($script:Kinds -notcontains $kind) {
            $errors.Add("Unknown component kind '$kind'.")
        }
        if ([string]::IsNullOrWhiteSpace($id)) {
            $errors.Add("Every component needs an id.")
            continue
        }
        $key = "$kind/$id"
        if ($componentIds.ContainsKey($key)) {
            $errors.Add("Duplicate component id within kind: $key")
        } else {
            $componentIds[$key] = $true
        }
        if (!(Test-SafeRelativePath $Root $file)) {
            $errors.Add("Component '$key' has an invalid project-relative path.")
        } elseif ($CheckFiles -and !(Test-Path -LiteralPath (Join-Path $Root $file) -PathType Leaf)) {
            $errors.Add("Component file not found for '$key': $file")
        }
        if ($factory -and $factory -notmatch '^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*$') {
            $errors.Add("Component '$key' has an invalid factory type name.")
        }
    }

    $scriptPaths = @{}
    foreach ($scriptFile in @(Get-PropertyValue $Manifest "scripts" @())) {
        $file = [string]$scriptFile
        if ($scriptPaths.ContainsKey($file)) {
            $errors.Add("Duplicate script path: $file")
        } else {
            $scriptPaths[$file] = $true
        }
        if (!(Test-SafeRelativePath $Root $file)) {
            $errors.Add("Script has an invalid project-relative path: $file")
        } elseif ($CheckFiles -and !(Test-Path -LiteralPath (Join-Path $Root $file) -PathType Leaf)) {
            $errors.Add("Script file not found: $file")
        }
    }

    if ($resourceIds.Count -eq 0) {
        $warnings.Add("Project does not define any resources.")
    }
    if ($componentIds.Count -eq 0) {
        $warnings.Add("Project does not define any component files.")
    }

    return [pscustomobject]@{
        Errors = $errors
        Warnings = $warnings
        IsValid = $errors.Count -eq 0
    }
}

function Write-Legend2Validation {
    param([object]$Result)
    foreach ($warning in $Result.Warnings) {
        Write-Warning $warning
    }
    foreach ($problem in $Result.Errors) {
        Write-Output "ERROR: $problem"
    }
    if ($Result.IsValid) {
        Write-Output "Legend2 project is valid."
    } else {
        Write-Output "Legend2 project has $($Result.Errors.Count) error(s)."
    }
}

function New-Legend2Project {
    param(
        [string]$Root,
        [string]$NamespaceName,
        [switch]$Overwrite
    )
    $rootPath = [System.IO.Path]::GetFullPath($Root)
    $manifestPath = Get-Legend2ManifestPath $rootPath
    if ((Test-Path -LiteralPath $manifestPath) -and !$Overwrite) {
        throw "Manifest already exists. Use -Force to replace it: $manifestPath"
    }

    $directories = @(
        $rootPath,
        (Join-Path $rootPath "Assets"),
        (Join-Path $rootPath "Components"),
        (Join-Path $rootPath "Components\Maps"),
        (Join-Path $rootPath "Components\Actors"),
        (Join-Path $rootPath "Components\Items"),
        (Join-Path $rootPath "Components\Skills"),
        (Join-Path $rootPath "Components\Buffs"),
        (Join-Path $rootPath "Components\Windows"),
        (Join-Path $rootPath "Components\Growth"),
        (Join-Path $rootPath "Components\Prefabs"),
        (Join-Path $rootPath "Scripts"),
        (Join-Path $rootPath "Generated")
    )
    foreach ($directory in $directories) {
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
    }

    $manifest = [ordered]@{
        schemaVersion = 1
        namespace = $NamespaceName
        app = [ordered]@{
            title = "Zan Legend2"
            width = 1280
            height = 720
            frameRate = 60
            verticalSync = $false
            screenMode = 1
            repeatKeys = $false
            defaultFont = ""
            background = @(0, 0, 0, 255)
        }
        resources = @()
        components = @()
        scripts = @()
    }
    $json = $manifest | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText(
        $manifestPath,
        $json + [Environment]::NewLine,
        [System.Text.UTF8Encoding]::new($false))

    Write-Output "Created Legend2 project: $rootPath"
    Write-Output "Manifest: $manifestPath"
}

function New-Legend2AppSource {
    param(
        [string]$Root,
        [string]$NamespaceName
    )
    $appPath = Join-Path $Root "App.zan"
    if (Test-Path -LiteralPath $appPath) {
        return
    }

    $source = @"
using System;
using Game.Legend2;

namespace $NamespaceName;

class Program {
    static void Main() {
        Legend2Engine engine = GeneratedLegend2Project.CreateEngine();
        Legend2Diagnostics diagnostics = engine.Validate();
        if (diagnostics.HasErrors()) {
            int i = 0;
            while (i < diagnostics.Count()) {
                Legend2Diagnostic diagnostic = diagnostics.At(i);
                Console.WriteLine(
                    diagnostic.Code() + " " + diagnostic.Path()
                    + ": " + diagnostic.Message());
                i = i + 1;
            }
            return;
        }
        engine.Run("");
    }
}
"@
    [System.IO.File]::WriteAllText(
        $appPath,
        $source,
        [System.Text.UTF8Encoding]::new($false))
    Write-Output "Created app source: $appPath"
}

function New-Legend2GeneratedSource {
    param(
        [string]$Root,
        [object]$Manifest,
        [string]$Destination
    )

    $namespaceName = [string](Get-PropertyValue $Manifest "namespace" "Game.Generated")
    $app = Get-PropertyValue $Manifest "app" $null
    $title = Escape-ZanString ([string](Get-PropertyValue $app "title" "Zan Legend2"))
    $width = [int](Get-PropertyValue $app "width" 1280)
    $height = [int](Get-PropertyValue $app "height" 720)
    $frameRate = [int](Get-PropertyValue $app "frameRate" 60)
    $verticalSync = [bool](Get-PropertyValue $app "verticalSync" $false)
    $screenMode = [int](Get-PropertyValue $app "screenMode" 1)
    $repeatKeys = [bool](Get-PropertyValue $app "repeatKeys" $false)
    $defaultFont = Escape-ZanString ([string](Get-PropertyValue $app "defaultFont" ""))
    $background = @(Get-PropertyValue $app "background" @(0, 0, 0, 255))

    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.AppendLine("// Generated by scripts/legend2_tool.ps1. Do not edit.")
    [void]$builder.AppendLine("using System;")
    [void]$builder.AppendLine("using Game.Legend2;")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("namespace $namespaceName;")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("class GeneratedLegend2Project {")
    [void]$builder.AppendLine("    static Legend2Config CreateConfig() {")
    [void]$builder.AppendLine("        Legend2Config config = Legend2Config.Create();")
    [void]$builder.AppendLine("        config.SetTitle(`"$title`");")
    [void]$builder.AppendLine("        config.SetSize($width, $height);")
    [void]$builder.AppendLine("        config.SetFrameRate($frameRate);")
    [void]$builder.AppendLine("        config.SetVerticalSync($($verticalSync.ToString().ToLowerInvariant()));")
    [void]$builder.AppendLine("        config.SetScreenMode($screenMode);")
    [void]$builder.AppendLine("        config.SetRepeatKeys($($repeatKeys.ToString().ToLowerInvariant()));")
    [void]$builder.AppendLine("        config.SetDefaultFont(`"$defaultFont`");")
    [void]$builder.AppendLine(
        "        config.SetBackground($([int]$background[0]), $([int]$background[1]), $([int]$background[2]), $([int]$background[3]));")
    [void]$builder.AppendLine("        return config;")
    [void]$builder.AppendLine("    }")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("    static Legend2Project CreateProject() {")
    [void]$builder.AppendLine("        Legend2Project project = Legend2Project.Create();")

    foreach ($resource in @(Get-PropertyValue $Manifest "resources" @())) {
        $id = Escape-ZanString ([string](Get-PropertyValue $resource "id" ""))
        $file = Escape-ZanString ([string](Get-PropertyValue $resource "file" ""))
        [void]$builder.AppendLine(
            "        project.Registry().AddResource(`"$id`", `"$file`");")
    }
    foreach ($component in @(Get-PropertyValue $Manifest "components" @())) {
        $kind = Escape-ZanString ([string](Get-PropertyValue $component "kind" ""))
        $id = Escape-ZanString ([string](Get-PropertyValue $component "id" ""))
        $file = Escape-ZanString ([string](Get-PropertyValue $component "file" ""))
        $factory = [string](Get-PropertyValue $component "factory" "")
        [void]$builder.AppendLine(
            "        project.Registry().AddComponentFile(`"$kind`", `"$id`", `"$file`");")
        if ($factory) {
            [void]$builder.AppendLine("        $factory.Register(project);")
        }
    }
    foreach ($scriptFile in @(Get-PropertyValue $Manifest "scripts" @())) {
        $file = Escape-ZanString ([string]$scriptFile)
        [void]$builder.AppendLine("        project.Registry().AddScript(`"$file`");")
    }

    [void]$builder.AppendLine("        return project;")
    [void]$builder.AppendLine("    }")
    [void]$builder.AppendLine()
    [void]$builder.AppendLine("    static Legend2Engine CreateEngine() {")
    [void]$builder.AppendLine("        Legend2Engine engine = Legend2Engine.Create(CreateConfig());")
    [void]$builder.AppendLine("        engine.LoadProject(CreateProject());")
    [void]$builder.AppendLine("        return engine;")
    [void]$builder.AppendLine("    }")
    [void]$builder.AppendLine("}")

    $destinationPath = if ([System.IO.Path]::IsPathRooted($Destination)) {
        [System.IO.Path]::GetFullPath($Destination)
    } else {
        [System.IO.Path]::GetFullPath((Join-Path $Root $Destination))
    }
    $parent = Split-Path -Parent $destinationPath
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    [System.IO.File]::WriteAllText(
        $destinationPath,
        $builder.ToString(),
        [System.Text.UTF8Encoding]::new($false))
    Write-Output "Generated Zan source: $destinationPath"

    $sourceLines = [System.Collections.Generic.List[string]]::new()
    if (Test-Path -LiteralPath (Join-Path $Root "App.zan") -PathType Leaf) {
        $sourceLines.Add("App.zan")
    }
    $relativeGenerated = [System.IO.Path]::GetRelativePath($Root, $destinationPath)
    $sourceLines.Add($relativeGenerated.Replace("\", "/"))
    foreach ($component in @(Get-PropertyValue $Manifest "components" @())) {
        $sourceLines.Add(
            ([string](Get-PropertyValue $component "file" "")).Replace("\", "/"))
    }
    foreach ($scriptFile in @(Get-PropertyValue $Manifest "scripts" @())) {
        $sourceLines.Add(([string]$scriptFile).Replace("\", "/"))
    }
    $sourceListPath = Join-Path $parent "legend2.sources.txt"
    [System.IO.File]::WriteAllLines(
        $sourceListPath,
        $sourceLines,
        [System.Text.UTF8Encoding]::new($false))
    Write-Output "Generated source list: $sourceListPath"
}

function New-Legend2Component {
    param(
        [string]$Root,
        [object]$Manifest,
        [string]$ComponentKind,
        [string]$ComponentId,
        [string]$FactoryName,
        [switch]$Overwrite
    )

    if ($script:Kinds -notcontains $ComponentKind) {
        throw "A valid -Kind is required for the component command."
    }
    if ([string]::IsNullOrWhiteSpace($ComponentId)) {
        throw "-Id is required for the component command."
    }
    if ($ComponentId -notmatch '^[A-Za-z0-9][A-Za-z0-9_.-]*$') {
        throw "Component id may contain letters, digits, dots, underscores and hyphens."
    }

    $metadata = @{
        map = @{ Directory = "Maps"; Suffix = "Map"; Type = "Legend2MapDefinition"; Add = "AddMap"; Create = 'Legend2MapDefinition.Create("{0}", 32, 32)' }
        actor = @{ Directory = "Actors"; Suffix = "Actor"; Type = "Legend2ActorDefinition"; Add = "AddActor"; Create = 'Legend2ActorDefinition.Create("{0}", "{0}")' }
        item = @{ Directory = "Items"; Suffix = "Item"; Type = "Legend2ItemDefinition"; Add = "AddItem"; Create = 'Legend2ItemDefinition.Create("{0}", "{0}")' }
        skill = @{ Directory = "Skills"; Suffix = "Skill"; Type = "Legend2SkillDefinition"; Add = "AddSkill"; Create = 'Legend2SkillDefinition.Create("{0}", "{0}")' }
        buff = @{ Directory = "Buffs"; Suffix = "Buff"; Type = "Legend2BuffDefinition"; Add = "AddBuff"; Create = 'Legend2BuffDefinition.Create("{0}", "{0}")' }
        window = @{ Directory = "Windows"; Suffix = "Window"; Type = "Legend2WindowDefinition"; Add = "AddWindow"; Create = 'Legend2WindowDefinition.Create("{0}", 640, 480)' }
        growth = @{ Directory = "Growth"; Suffix = "Growth"; Type = "Legend2GrowthDefinition"; Add = "AddGrowth"; Create = 'Legend2GrowthDefinition.Create("{0}")' }
        prefab = @{ Directory = "Prefabs"; Suffix = "Prefab"; Type = "Legend2PrefabDefinition"; Add = "AddPrefab"; Create = 'Legend2PrefabDefinition.Create("{0}", 32, 32)' }
    }[$ComponentKind]

    $baseName = ConvertTo-ZanIdentifier $ComponentId
    $className = "$baseName$($metadata.Suffix)"
    $manifestNamespace = [string](Get-PropertyValue $Manifest "namespace" "Game.Generated")
    $componentNamespace = "$manifestNamespace.Components"
    $resolvedFactory = "$componentNamespace.$className"

    if (![string]::IsNullOrWhiteSpace($FactoryName)) {
        if ($FactoryName -notmatch '^[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*$') {
            throw "-Factory is not a valid Zan type name."
        }
        $resolvedFactory = $FactoryName
        $lastDot = $FactoryName.LastIndexOf(".")
        if ($lastDot -ge 0) {
            $componentNamespace = $FactoryName.Substring(0, $lastDot)
            $className = $FactoryName.Substring($lastDot + 1)
        } else {
            $componentNamespace = $manifestNamespace
            $className = $FactoryName
        }
    }

    $relativePath = "Components/$($metadata.Directory)/$className.zan"
    $componentPath = [System.IO.Path]::GetFullPath(
        (Join-Path $Root $relativePath))
    if (!(Test-SafeRelativePath $Root $relativePath)) {
        throw "Generated component path is outside the project root."
    }

    $existing = @(
        Get-PropertyValue $Manifest "components" @() |
            Where-Object {
                ([string](Get-PropertyValue $_ "kind" "")) -eq $ComponentKind -and
                ([string](Get-PropertyValue $_ "id" "")) -eq $ComponentId
            }
    )
    if ($existing.Count -gt 0 -and !$Overwrite) {
        throw "Component already exists: $ComponentKind/$ComponentId. Use -Force to replace it."
    }
    if ((Test-Path -LiteralPath $componentPath) -and !$Overwrite) {
        throw "Component source already exists. Use -Force to replace it: $componentPath"
    }

    $escapedId = Escape-ZanString $ComponentId
    $createExpression = [string]::Format(
        [string]$metadata.Create, $escapedId)
    $source = @"
using System;
using Game.Legend2;

namespace $componentNamespace;

class $className {
    static void Register(Legend2Project project) {
        $($metadata.Type) definition =
            $createExpression;
        project.$($metadata.Add)(definition);
    }
}
"@

    $parent = Split-Path -Parent $componentPath
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    [System.IO.File]::WriteAllText(
        $componentPath,
        $source,
        [System.Text.UTF8Encoding]::new($false))

    $components = @(
        Get-PropertyValue $Manifest "components" @() |
            Where-Object {
                !(
                    ([string](Get-PropertyValue $_ "kind" "")) -eq $ComponentKind -and
                    ([string](Get-PropertyValue $_ "id" "")) -eq $ComponentId
                )
            }
    )
    $components += [ordered]@{
        kind = $ComponentKind
        id = $ComponentId
        file = $relativePath
        factory = $resolvedFactory
    }
    $Manifest.components = @($components)

    $manifestPath = Get-Legend2ManifestPath $Root
    $json = $Manifest | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText(
        $manifestPath,
        $json + [Environment]::NewLine,
        [System.Text.UTF8Encoding]::new($false))

    Write-Output "Created component source: $componentPath"
    Write-Output "Registered component: $ComponentKind/$ComponentId"
}

$root = Resolve-Legend2Root $Project

switch ($Command) {
    "init" {
        New-Legend2Project -Root $root -NamespaceName $Namespace -Overwrite:$Force
        $manifest = Read-Legend2Manifest $root
        $result = Test-Legend2Manifest -Root $root -Manifest $manifest
        Write-Legend2Validation $result
        if (!$result.IsValid) { exit 1 }
        New-Legend2AppSource -Root $root -NamespaceName $Namespace
        $destination = if ($Output) { $Output } else { "Generated\Legend2Project.g.zan" }
        New-Legend2GeneratedSource -Root $root -Manifest $manifest -Destination $destination
    }
    "validate" {
        $manifest = Read-Legend2Manifest $root
        $result = Test-Legend2Manifest -Root $root -Manifest $manifest -CheckFiles
        Write-Legend2Validation $result
        if (!$result.IsValid) { exit 1 }
    }
    "generate" {
        $manifest = Read-Legend2Manifest $root
        $result = Test-Legend2Manifest -Root $root -Manifest $manifest -CheckFiles
        Write-Legend2Validation $result
        if (!$result.IsValid) { exit 1 }
        $destination = if ($Output) { $Output } else { "Generated\Legend2Project.g.zan" }
        New-Legend2GeneratedSource -Root $root -Manifest $manifest -Destination $destination
    }
    "component" {
        $manifest = Read-Legend2Manifest $root
        New-Legend2Component `
            -Root $root `
            -Manifest $manifest `
            -ComponentKind $Kind `
            -ComponentId $Id `
            -FactoryName $Factory `
            -Overwrite:$Force
        $manifest = Read-Legend2Manifest $root
        $result = Test-Legend2Manifest -Root $root -Manifest $manifest -CheckFiles
        Write-Legend2Validation $result
        if (!$result.IsValid) { exit 1 }
        $destination = if ($Output) { $Output } else { "Generated\Legend2Project.g.zan" }
        New-Legend2GeneratedSource -Root $root -Manifest $manifest -Destination $destination
    }
}
