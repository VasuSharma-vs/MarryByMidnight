param([Parameter(Mandatory)][string]$Session)
$ErrorActionPreference = 'Stop'
$headers = @{Accept='application/json, text/event-stream'; 'Mcp-Session-Id'=$Session; 'MCP-Protocol-Version'='2025-11-25'}
$evidence = [ordered]@{passed=$false; phases=@(); cleanup=@{}; error=$null}
function Call-UE([string]$Toolset, [string]$Tool, [hashtable]$Arguments) {
    $body = @{jsonrpc='2.0'; id=10; method='tools/call'; params=@{name='call_tool'; arguments=@{toolset_name=$Toolset;tool_name=$Tool;arguments=$Arguments}}} | ConvertTo-Json -Depth 35 -Compress
    $reply = (Invoke-WebRequest -Uri 'http://127.0.0.1:8000/mcp' -Method Post -ContentType application/json -Headers $headers -Body $body -TimeoutSec 45).Content | ConvertFrom-Json
    if ($reply.error) { throw ($reply.error | ConvertTo-Json -Compress) }
    $message = ($reply.result.content | Where-Object type -eq text | ForEach-Object text) -join "`n"
    if ($reply.result.isError) { throw $message }
    $envelope = $message | ConvertFrom-Json
    return $envelope.returnValue
}
$objectTools = 'editor_toolset.toolsets.object.ObjectTools'
$fixtureReply = Get-Content -LiteralPath 'Saved/FragmentFixtureActor.json' -Raw | ConvertFrom-Json
$fixtureRef = ($fixtureReply.result.content[0].text | ConvertFrom-Json).returnValue[0].refPath
$paths = @(0..3 | ForEach-Object { $fixtureRef.Replace('/FragmentReplication.', ('/UEDPIE_' + $_ + '_FragmentReplication.')) })
$properties = @('Stage','FragmentISIDs','LocalRecordCount','LocalOriginalCount','LocalISMCount','LocalASMCount','LocalVisibleOriginalCount','LocalSimulatingOriginalCount','LocalVisibleISMCount','LocalDedicatedActorCount','LocalFragmentPositions','LocalPreviousRestCount','LocalSlotChanges','LocalReleasedLiquidCount','LocalReleasedPortions','bLocalSourcePresent','LocalPlayerCount','LastFailure')
function Read-Worlds {
    $observations = @()
    foreach ($path in $paths) {
        $json = Call-UE $objectTools get_properties @{instance=@{refPath=$path}; properties=$properties}
        $state = $json | ConvertFrom-Json
        $state | Add-Member -NotePropertyName WorldObject -NotePropertyValue $path
        $observations += $state
    }
    return $observations
}
function Wait-Phase([string]$Name, [scriptblock]$Condition) {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $observations = @(Read-Worlds)
        foreach ($state in $observations) { if ($state.LastFailure) { throw $state.LastFailure } }
        if (& $Condition $observations) {
            $evidence.phases += @{name=$Name; observations=$observations}
            Write-Output ('Passed phase: ' + $Name) | Out-Host
            return
        }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    $evidence.phases += @{name=$Name; observations=$observations; timedOut=$true}
    throw ('Timed out waiting for ' + $Name)
}
function Set-Command([int]$Command) {
    $changed = Call-UE $objectTools set_properties @{instance=@{refPath=$paths[0]}; values=(@{Command=$Command}|ConvertTo-Json -Compress)}
    if (!$changed) { throw 'Failed to set fixture command' }
}
try {
    Wait-Phase 'Four connected players' {param($states) @($states | Where-Object LocalPlayerCount -eq 4).Count -eq 4}
    Set-Command 1
    Wait-Phase 'Eleven independent originals in every world' {param($states) @($states | Where-Object { $_.LocalRecordCount -eq 11 -and $_.LocalOriginalCount -eq 11 -and $_.LocalVisibleOriginalCount -eq 11 -and !$_.bLocalSourcePresent -and $_.LocalReleasedLiquidCount -eq 1 }).Count -eq 4 -and $states[0].LocalSimulatingOriginalCount -eq 11 -and @($states | Select-Object -Skip 1 | Where-Object LocalSimulatingOriginalCount -eq 0).Count -eq 3}
    Wait-Phase 'Eleven settled ISMs in every world' {param($states) @($states | Where-Object { $_.LocalISMCount -eq 11 -and $_.LocalVisibleISMCount -eq 11 -and $_.LocalOriginalCount -eq 0 -and $_.LocalSlotChanges -eq 0 }).Count -eq 4}
    Set-Command 3
    Wait-Phase 'One dedicated active fragment in every world' {param($states) @($states | Where-Object { $_.LocalASMCount -eq 1 -and $_.LocalDedicatedActorCount -eq 1 -and $_.LocalISMCount -eq 10 -and $_.LocalVisibleISMCount -eq 10 -and $_.LocalOriginalCount -eq 0 -and $_.LocalSlotChanges -eq 0 }).Count -eq 4}
    Set-Command 4
    Wait-Phase 'Dedicated fragment returned to its ISM' {param($states) @($states | Where-Object { $_.LocalISMCount -eq 11 -and $_.LocalASMCount -eq 0 -and $_.LocalSlotChanges -eq 0 }).Count -eq 4}
    Set-Command 1
    Wait-Phase 'Reused carrier preserved earlier fragments' {param($states) @($states | Where-Object { $_.LocalOriginalCount -eq 11 -and $_.LocalPreviousRestCount -eq 11 -and $_.LocalReleasedLiquidCount -eq 2 }).Count -eq 4}
    Wait-Phase 'Both destruction groups settled without slot changes' {param($states) @($states | Where-Object { $_.LocalISMCount -eq 11 -and $_.LocalPreviousRestCount -eq 11 -and $_.LocalSlotChanges -eq 0 -and !$_.bLocalSourcePresent }).Count -eq 4}
    $final = @(Read-Worlds)
    if (@($final | Where-Object { [Math]::Abs($_.LocalReleasedPortions - $final[0].LocalReleasedPortions) -lt .00001 -and $_.LocalReleasedPortions -gt 0 }).Count -ne 4) { throw 'Liquid accounting differs between worlds' }
    foreach ($client in ($final | Select-Object -Skip 1)) {
        for ($i=0; $i -lt 11; $i++) {
            $serverPosition = $final[0].LocalFragmentPositions[$i]; $clientPosition = $client.LocalFragmentPositions[$i]
            $distance = [Math]::Sqrt([Math]::Pow($serverPosition.X-$clientPosition.X,2)+[Math]::Pow($serverPosition.Y-$clientPosition.Y,2)+[Math]::Pow($serverPosition.Z-$clientPosition.Z,2))
            if ($distance -gt .5) { throw ('Settled client fragment position differs by ' + $distance + ' cm') }
        }
    }
    $evidence.passed = $true
} catch {
    $evidence.error = $_.Exception.Message
    Write-Output ('Fragment multiplayer validation failed: ' + $evidence.error)
} finally {
    try {
        $null = Call-UE 'EditorToolset.EditorAppToolset' StopPIE @{}
        $evidence.cleanup.pieStopped = $true
        $settingsReply = Get-Content -LiteralPath 'Saved/FragmentOriginalPlaySettings.json' -Raw | ConvertFrom-Json
        $settings = ($settingsReply.result.content[0].text | ConvertFrom-Json).returnValue
        $evidence.cleanup.settingsRestored = Call-UE $objectTools set_properties @{instance=@{refPath='/Script/UnrealEd.Default__LevelEditorPlaySettings'};values=$settings}
        $mapReply = Get-Content -LiteralPath 'Saved/FragmentOriginalEditorLevel.json' -Raw | ConvertFrom-Json
        $originalMap = ($mapReply.result.content[0].text | ConvertFrom-Json).returnValue
        $null = Call-UE 'editor_toolset.toolsets.scene.SceneTools' load_level @{level_path=$originalMap}
        $evidence.cleanup.mapRestored = $originalMap
    } catch { $evidence.cleanup.error = $_.Exception.Message }
    $evidence | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath 'Saved/Automation/FragmentFourPlayer.json'
}
if (!$evidence.passed) { exit 1 }
if ($evidence.cleanup.error) { throw $evidence.cleanup.error }
Write-Output 'FOUR_PLAYER_FRAGMENT_TEST_PASSED'
