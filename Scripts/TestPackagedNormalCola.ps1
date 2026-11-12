param(
    [Parameter(Mandatory=$true)][string]$ArchiveRoot,
    [string]$EvidenceRoot = 'Saved/PackagedNormalColaTests',
    [int]$Port = 17882,
    [ValidateRange(60,600)][int]$TestSeconds = 240,
    [switch]$TestLateJoin,
    [ValidateRange(0,250)][int]$PacketLagMs = 0,
    [ValidateRange(0,10)][int]$PacketLossPercent = 0
)
$ErrorActionPreference = 'Stop'
$prototypeRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not [IO.Path]::IsPathRooted($ArchiveRoot)) { $ArchiveRoot = Join-Path $prototypeRoot $ArchiveRoot }
if (-not [IO.Path]::IsPathRooted($EvidenceRoot)) { $EvidenceRoot = Join-Path $prototypeRoot $EvidenceRoot }
$EvidenceRoot = [IO.Path]::GetFullPath($EvidenceRoot)
$prototypeSaved = [IO.Path]::GetFullPath((Join-Path $prototypeRoot 'Saved/'))
if (-not $EvidenceRoot.StartsWith($prototypeSaved,[StringComparison]::OrdinalIgnoreCase)) { throw 'Evidence must stay inside project Saved.' }
if ($Port -lt 1024 -or $Port -gt 65535) { throw 'Invalid test port.' }
$prototypeExe = Join-Path $ArchiveRoot 'Windows/MarryByMidnight.exe'
if (-not (Test-Path -LiteralPath $prototypeExe)) { throw 'Packaged bootstrap missing.' }
New-Item -ItemType Directory -Force -Path $EvidenceRoot | Out-Null
$prototypeRun = Join-Path $EvidenceRoot (Get-Date -Format 'yyyyMMdd_HHmmss')
New-Item -ItemType Directory -Path $prototypeRun | Out-Null
$prototypeHostLog = Join-Path $prototypeRun 'Host.log'
$prototypeClientLog = Join-Path $prototypeRun 'Client.log'
$prototypeCommon = @('-Windowed','-ResX=800','-ResY=450','-NoSplash','-NoSound','-Unattended',
    '-MULTIHOME=127.0.0.1','-NormalColaPressureTest','-ExecCmds="t.MaxFPS 30"',('-PktLag='+$PacketLagMs),('-PktLoss='+$PacketLossPercent))
$prototypeHost = Start-Process -FilePath $prototypeExe -WindowStyle Normal -PassThru -ArgumentList (@(
    '/Game/NormalColaPrototype/Lvl_NormalColaPrototype_Validation?listen',('-port='+$Port),('-NormalColaTestExitAfter='+$TestSeconds),'-WinX=0','-WinY=0',
    ('-UserDir="'+(Join-Path $prototypeRun 'HostUser')+'"'),('-abslog="'+$prototypeHostLog+'"'))+$prototypeCommon)
$prototypeReadyDeadline = (Get-Date).AddSeconds(60)
do {
    Start-Sleep -Seconds 2
    $prototypeHostText = if (Test-Path -LiteralPath $prototypeHostLog) { Get-Content -LiteralPath $prototypeHostLog -Raw } else { '' }
    if ($prototypeHost.HasExited) { throw ('Host exited before listening. See '+$prototypeHostLog) }
    if ((Get-Date) -gt $prototypeReadyDeadline) { throw ('Host did not listen within 60 seconds. Its bounded test timer will request normal exit. See '+$prototypeHostLog) }
} until ($prototypeHostText -match 'GameNetDriver.*listening|IpNetDriver.*listening')
$prototypeClient = Start-Process -FilePath $prototypeExe -WindowStyle Normal -PassThru -ArgumentList (@(
    ('127.0.0.1:'+$Port),('-NormalColaTestExitAfter='+[math]::Max(30,$TestSeconds-30)),'-WinX=810','-WinY=0',
    ('-UserDir="'+(Join-Path $prototypeRun 'ClientUser')+'"'),('-abslog="'+$prototypeClientLog+'"'))+$prototypeCommon)
Write-Output ('Running real packaged listen server and client. Evidence: '+$prototypeRun)
$prototypeLateClient = $null
$prototypeLateLog = Join-Path $prototypeRun 'LateClient.log'
$prototypeDeadline = (Get-Date).AddSeconds($TestSeconds+120)
do {
    Start-Sleep -Seconds 2
    if ($TestLateJoin -and -not $prototypeLateClient -and -not $prototypeHost.HasExited) {
        $prototypeCurrentHost = Get-Content -LiteralPath $prototypeHostLog -Raw
        if ($prototypeCurrentHost -match 'NORMAL_COLA_PEER_ACCEPTANCE authority=1 stage=400') {
            $prototypeLateClient = Start-Process -FilePath $prototypeExe -WindowStyle Normal -PassThru -ArgumentList (@(
                ('127.0.0.1:'+$Port),'-NormalColaTestExitAfter=45','-WinX=810','-WinY=460',
                ('-UserDir="'+(Join-Path $prototypeRun 'LateClientUser')+'"'),('-abslog="'+$prototypeLateLog+'"'))+$prototypeCommon)
            Write-Output 'Joining a third packaged peer after settlement.'
        }
    }
    if ((Get-Date) -gt $prototypeDeadline) {
        @{Failure='Graceful exit deadline exceeded';HostPID=$prototypeHost.Id;ClientPID=$prototypeClient.Id;Run=$prototypeRun} |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $prototypeRun 'Result.json') -Encoding utf8
        throw 'Bounded prototype processes have not exited; inspect logs and close normally. No force termination was attempted.'
    }
} until ($prototypeHost.HasExited -and $prototypeClient.HasExited -and (-not $prototypeLateClient -or $prototypeLateClient.HasExited))
$prototypeHostText = Get-Content -LiteralPath $prototypeHostLog -Raw
$prototypeClientText = Get-Content -LiteralPath $prototypeClientLog -Raw
$prototypeSummary = [ordered]@{ Run=$prototypeRun; HostExit=$prototypeHost.ExitCode; ClientExit=$prototypeClient.ExitCode; Checks=[ordered]@{} }
$prototypeSummary['PacketLagMs']=$PacketLagMs
$prototypeSummary['PacketLossPercent']=$PacketLossPercent
$prototypeSummary.Checks['NoFatalError'] = ($prototypeHostText+$prototypeClientText) -notmatch 'Fatal error:|Assertion failed:|NORMAL_COLA_PLAYTEST_FAILED'
$prototypePattern = 'NORMAL_COLA_PEER_ACCEPTANCE authority=(?<authority>\d) stage=(?<stage>\d+) bank=(?<bank>-?\d+) cash=(?<cash>-?\d+) receipts=(?<receipts>\d+)'
foreach ($prototypePeer in @(@('Host',$prototypeHostText,'1'),@('Client',$prototypeClientText,'0'))) {
    foreach ($prototypeStage in @(200,400)) {
        $prototypeMatches = [regex]::Matches($prototypePeer[1],$prototypePattern) | Where-Object { $_.Groups['stage'].Value -eq [string]$prototypeStage -and $_.Groups['authority'].Value -eq $prototypePeer[2] }
        $prototypeSummary.Checks[$prototypePeer[0]+'Stage'+$prototypeStage] = @($prototypeMatches).Count -gt 0
    }
}
$prototypeHostFinal = [regex]::Matches($prototypeHostText,$prototypePattern) | Where-Object {$_.Groups['stage'].Value -eq '400'} | Select-Object -Last 1
$prototypeClientFinal = [regex]::Matches($prototypeClientText,$prototypePattern) | Where-Object {$_.Groups['stage'].Value -eq '400'} | Select-Object -Last 1
$prototypeSummary.Checks['FinancialAgreement'] = $prototypeHostFinal -and $prototypeClientFinal -and
    $prototypeHostFinal.Groups['bank'].Value -eq $prototypeClientFinal.Groups['bank'].Value -and
    $prototypeHostFinal.Groups['cash'].Value -eq '0' -and $prototypeClientFinal.Groups['cash'].Value -eq '0' -and
    $prototypeHostFinal.Groups['receipts'].Value -eq $prototypeClientFinal.Groups['receipts'].Value
$prototypeSummary.Checks['GracefulProcessExit'] = $prototypeHost.ExitCode -eq 0 -and $prototypeClient.ExitCode -eq 0
$prototypeStock = [regex]::Matches($prototypeHostText,'NORMAL_COLA_TEST_PROGRESS.*?bottles=(?<count>\d+) sealed=\d+ registered=\d+ available=(?<available>\d+)') | Select-Object -Last 1
$prototypeSummary.Checks['SustainedCatalogAndStock'] = $prototypeStock -and [int]$prototypeStock.Groups['count'].Value -gt 0 -and [int]$prototypeStock.Groups['available'].Value -gt 0
$prototypeSummary.Checks['FullSealedManufacture'] = $prototypeHostText -match 'op=before-restock.*level=1\.0000 sealed=1\.000' -and $prototypeClientText -match 'stage=200.*level=1\.000 sealed=1\.000 cap=[1-9]'
$prototypeClientPressure = [regex]::Matches($prototypeClientText,'NORMAL_COLA_PEER_ACCEPTANCE authority=0 stage=400.*peak=(?<peak>[\d.]+)') | Select-Object -Last 1
$prototypeSummary.Checks['ClientObservedPressureAscent'] = $prototypeClientPressure -and [double]$prototypeClientPressure.Groups['peak'].Value -gt 50
if ($PacketLagMs -gt 0 -or $PacketLossPercent -gt 0) {
    $prototypeSummary.Checks['AppliedNetworkProfile'] = $prototypeHostText -match ('NORMAL_COLA_NET_PROFILE authority=1 lag_ms='+$PacketLagMs+' loss_pct='+$PacketLossPercent) -and
        $prototypeClientText -match ('NORMAL_COLA_NET_PROFILE authority=0 lag_ms='+$PacketLagMs+' loss_pct='+$PacketLossPercent)
}
if ($TestLateJoin) {
    $prototypeLateText = if (Test-Path -LiteralPath $prototypeLateLog) { Get-Content -LiteralPath $prototypeLateLog -Raw } else { '' }
    $prototypeLateFinal = [regex]::Matches($prototypeLateText,$prototypePattern) | Where-Object {$_.Groups['stage'].Value -eq '400'} | Select-Object -Last 1
    $prototypeSummary.Checks['LateJoinFinancialAgreement'] = $prototypeLateFinal -and $prototypeHostFinal -and
        $prototypeLateFinal.Groups['bank'].Value -eq $prototypeHostFinal.Groups['bank'].Value -and
        $prototypeLateFinal.Groups['cash'].Value -eq '0' -and $prototypeLateFinal.Groups['receipts'].Value -eq $prototypeHostFinal.Groups['receipts'].Value
    $prototypeSummary.Checks['LateJoinSettledPressureState'] = $prototypeLateText -match 'stage=400.*pressure=\[ISID=\d+ powered=0 physics=\d state=6 .*level=0\.\d+ seal=0\.000 cap=0'
    $prototypePositionPattern = 'stage=400.*?pressure=\[ISID=(?<id>\d+) powered=0 physics=\d state=6 pos=V\(X=(?<x>-?[\d.]+), Y=(?<y>-?[\d.]+), Z=(?<z>-?[\d.]+)\)'
    $prototypeServerPosition = [regex]::Matches($prototypeHostText,$prototypePositionPattern) | Select-Object -Last 1
    $prototypeLatePosition = [regex]::Matches($prototypeLateText,$prototypePositionPattern) | Select-Object -Last 1
    $prototypePositionDistance = [double]::PositiveInfinity
    if ($prototypeServerPosition -and $prototypeLatePosition -and $prototypeServerPosition.Groups['id'].Value -eq $prototypeLatePosition.Groups['id'].Value) {
        $prototypeSquaredDistance = 0.0
        foreach ($prototypeAxis in @('x','y','z')) {
            $prototypeDelta = [double]$prototypeServerPosition.Groups[$prototypeAxis].Value-[double]$prototypeLatePosition.Groups[$prototypeAxis].Value
            $prototypeSquaredDistance += $prototypeDelta*$prototypeDelta
        }
        $prototypePositionDistance = [math]::Sqrt($prototypeSquaredDistance)
    }
    $prototypeSummary['LateJoinPositionErrorCm'] = if ([double]::IsInfinity($prototypePositionDistance)) { $null } else { $prototypePositionDistance }
    $prototypeSummary.Checks['LateJoinPositionAgreement'] = $prototypePositionDistance -le 5
    $prototypeSummary.Checks['LateJoinGracefulExit'] = $prototypeLateClient -and $prototypeLateClient.HasExited -and $prototypeLateClient.ExitCode -eq 0
}
$prototypeSummary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $prototypeRun 'Result.json') -Encoding utf8
$prototypeSummary | ConvertTo-Json -Depth 5 | Write-Output
if ($prototypeSummary.Checks.Values -contains $false) { throw 'Packaged prototype acceptance failed. Inspect Result.json and both logs.' }
