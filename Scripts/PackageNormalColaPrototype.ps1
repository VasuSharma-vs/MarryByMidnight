param(
    [string]$EngineRoot = 'C:/Program Files/Epic Games/UE_5.8',
    [string]$OutputRoot = 'Saved/Builds/NormalColaPrototype_Win64',
    [switch]$SkipNativeBuild
)
$ErrorActionPreference = 'Stop'
$prototypeProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$prototypeProjectFile = Join-Path $prototypeProjectRoot 'MarryByMidnight.uproject'
$prototypeSavedRoot = [IO.Path]::GetFullPath((Join-Path $prototypeProjectRoot 'Saved/'))
if (-not [IO.Path]::IsPathRooted($OutputRoot)) { $OutputRoot = Join-Path $prototypeProjectRoot $OutputRoot }
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
if (-not $OutputRoot.StartsWith($prototypeSavedRoot,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Package output must stay inside this project Saved directory.'
}
if (Get-Process -Name UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue) {
    throw 'Save and close Unreal before packaging.'
}
$prototypeBuild = Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat'
$prototypeUAT = Join-Path $EngineRoot 'Engine/Build/BatchFiles/RunUAT.bat'
if (-not (Test-Path -LiteralPath $prototypeBuild) -or -not (Test-Path -LiteralPath $prototypeUAT)) {
    throw 'Unreal build tools are missing from EngineRoot.'
}
$prototypeLogRoot = Join-Path $prototypeSavedRoot 'Logs'
New-Item -ItemType Directory -Path $prototypeLogRoot -Force | Out-Null
$prototypeStamp = Get-Date -Format 'yyyyMMdd_HHmmss'
Push-Location -LiteralPath $prototypeProjectRoot
try {
    if (-not $SkipNativeBuild) {
        foreach ($prototypeTarget in @('MarryByMidnightEditor','MarryByMidnight')) {
            $prototypeNativeLog = Join-Path $prototypeLogRoot ('PrototypePackageBuild_'+$prototypeTarget+'_'+$prototypeStamp+'.log')
            & $prototypeBuild $prototypeTarget Win64 Development ('-Project='+$prototypeProjectFile) -WaitMutex -NoHotReloadFromIDE -MaxParallelActions=2 *> $prototypeNativeLog
            if ($LASTEXITCODE -ne 0) { throw ('Native build failed. See '+$prototypeNativeLog) }
        }
    }
    $prototypeMap = '/Game/NormalColaPrototype/Lvl_NormalColaPrototype.Lvl_NormalColaPrototype'
    $prototypeSection = '[/Script/EngineSettings.GameMapsSettings]:'
    # Multiple overrides for the same ini belong in one comma-separated argument.
    $prototypeCookOptions = '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False -ini:Engine:'+
        $prototypeSection+'GameDefaultMap='+$prototypeMap+','+$prototypeSection+'EditorStartupMap='+$prototypeMap+','+$prototypeSection+'ServerDefaultMap='+$prototypeMap
    $prototypePackageArgs = @('BuildCookRun',('-project='+$prototypeProjectFile),'-platform=Win64','-clientconfig=Development',
        '-skipbuild','-nocompileeditor','-cook','-map=/Game/NormalColaPrototype/Lvl_NormalColaPrototype+/Game/NormalColaPrototype/Lvl_NormalColaPrototype_Validation',
        '-cookcultures=en','-stage','-pak','-compressed','-archive',('-stagingdirectory='+(Join-Path $OutputRoot 'PackageStage')),
        ('-archivedirectory='+(Join-Path $OutputRoot 'PackageArchive')),('-AdditionalCookerOptions='+$prototypeCookOptions),
        '-nodebuginfo','-nop4','-unattended','-utf8output')
    $prototypePackageLog = Join-Path $prototypeLogRoot ('PrototypePackage_'+$prototypeStamp+'.log')
    & $prototypeUAT @prototypePackageArgs *> $prototypePackageLog
    if ($LASTEXITCODE -ne 0) { throw ('Prototype cook/package failed. See '+$prototypePackageLog) }
    Write-Output ('Prototype archive: '+(Join-Path $OutputRoot 'PackageArchive'))
    Write-Output ('Package log: '+$prototypePackageLog)
}
finally { Pop-Location }
