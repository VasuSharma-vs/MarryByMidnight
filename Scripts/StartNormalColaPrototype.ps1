[CmdletBinding(SupportsShouldProcess=$true)]
param(
    [Parameter(Mandatory=$true)][string]$ArchiveRoot,
    [ValidateSet('LocalPair','Host','Client')][string]$Mode = 'LocalPair',
    [ValidatePattern('^[A-Za-z0-9.:-]+$')][string]$ServerAddress = '127.0.0.1',
    [ValidateRange(1024,65535)][int]$Port = 17777,
    [string]$SaveRoot = 'Saved/PrototypePlay'
)
$ErrorActionPreference = 'Stop'
$prototypeRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not [IO.Path]::IsPathRooted($ArchiveRoot)) { $ArchiveRoot = Join-Path $prototypeRoot $ArchiveRoot }
if (-not [IO.Path]::IsPathRooted($SaveRoot)) { $SaveRoot = Join-Path $prototypeRoot $SaveRoot }
$SaveRoot = [IO.Path]::GetFullPath($SaveRoot)
$prototypeSaved = [IO.Path]::GetFullPath((Join-Path $prototypeRoot 'Saved/'))
if (-not $SaveRoot.StartsWith($prototypeSaved,[StringComparison]::OrdinalIgnoreCase)) { throw 'Prototype play saves must stay under this project Saved directory.' }
$prototypeExe = Join-Path $ArchiveRoot 'Windows/ColossalFizzCo.exe'
if (-not (Test-Path -LiteralPath $prototypeExe)) { throw 'Packaged game missing; provide the PackageArchive directory.' }
$prototypePlans = @()
if ($Mode -in @('LocalPair','Host')) {
    $prototypePlans += [pscustomobject]@{Name='Host';URL='/Game/NormalColaPrototype/Lvl_NormalColaPrototype?listen';X=0}
}
if ($Mode -in @('LocalPair','Client')) {
    $prototypePlans += [pscustomobject]@{Name='Client';URL=($ServerAddress+':'+$Port);X=810}
}
# Preflight all save locations before starting either half of a local pair.
$prototypeProcesses = Get-CimInstance Win32_Process -Filter "Name='ColossalFizzCo.exe'"
foreach ($prototypePlan in $prototypePlans) {
    $prototypeUserDir = Join-Path $SaveRoot $prototypePlan.Name
    if ($prototypeProcesses | Where-Object { $_.CommandLine -and $_.CommandLine.Contains($prototypeUserDir) }) {
        throw ('A game using this '+$prototypePlan.Name+' save directory is already running.')
    }
}
foreach ($prototypePlan in $prototypePlans) {
    $prototypeUserDir = Join-Path $SaveRoot $prototypePlan.Name
    if ($PSCmdlet.ShouldProcess($prototypePlan.Name+' at '+$prototypePlan.URL,'Start playable Normal Cola prototype')) {
        New-Item -ItemType Directory -Path $prototypeUserDir -Force | Out-Null
        $prototypeArgs = @($prototypePlan.URL,('-port='+$Port),'-Windowed','-ResX=800','-ResY=450',('-WinX='+$prototypePlan.X),'-WinY=0',
            '-NoSplash','-ExecCmds="t.MaxFPS 60"',('-UserDir="'+$prototypeUserDir+'"'),('-abslog="'+(Join-Path $prototypeUserDir 'PrototypePlay.log')+'"'))
        $prototypeLaunchTime=Get-Date
        Start-Process -FilePath $prototypeExe -WindowStyle Normal -ArgumentList $prototypeArgs | Out-Null
        if ($Mode -eq 'LocalPair' -and $prototypePlan.Name -eq 'Host') {
            $prototypeHostLog=Join-Path $prototypeUserDir 'PrototypePlay.log'
            $prototypeDeadline=(Get-Date).AddSeconds(90)
            $prototypeListening=$false
            do {
                Start-Sleep -Seconds 2
                if ((Test-Path -LiteralPath $prototypeHostLog) -and (Get-Item -LiteralPath $prototypeHostLog).LastWriteTime -ge $prototypeLaunchTime) {
                    $prototypeHostText=Get-Content -LiteralPath $prototypeHostLog -Raw
                    $prototypeListening=$prototypeHostText -match ('listening on port '+$Port+'\b')
                }
            } until ($prototypeListening -or (Get-Date) -ge $prototypeDeadline)
            if (-not $prototypeListening) { throw ('Host did not become ready. Inspect '+$prototypeHostLog+' before starting a client.') }
        }
        Write-Output ($prototypePlan.Name+' started. Persistent prototype saves: '+$prototypeUserDir)
    }
}
