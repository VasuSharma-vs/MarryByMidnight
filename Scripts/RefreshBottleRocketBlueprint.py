"""Cold native compile/resave of the bottle Blueprint; preserve authored component settings."""
import json
import shutil
from pathlib import Path
import unreal
from editor_toolset.toolsets.actor import ActorTools

project = Path(unreal.Paths.project_dir())
source = project / 'Content/Props/Colas/BP_Cola.uasset'
backup = project / 'Saved/PrototypeResume_20261112/BeforeRocketTemplateRefresh/BP_Cola.uasset'
backup.parent.mkdir(parents=True, exist_ok=True)
if not backup.exists():
    shutil.copy2(source, backup)
bp = unreal.load_asset('/Game/Props/Colas/BP_Cola')
if not bp:
    raise RuntimeError('Bottle Blueprint missing')

def snapshot():
    components = list(ActorTools.get_components(unreal.get_default_object(bp.generated_class())))
    rockets = [c for c in components if isinstance(c, unreal.RocketProjectileComponent)]
    if len(rockets) != 1:
        raise RuntimeError('Expected exactly one authored rocket component')
    rocket = rockets[0]
    properties = ['NozzleAxis', 'bTipOverToFly', 'BaseSpeed', 'MaxFuelDuration', 'MaxFuelDistance',
                  'bAlignRotationToVelocity', 'bEnableDebugLogs', 'bShowOnScreenDebug', 'bDrawDebugTrajectory']
    return {'path': rocket.get_path_name(), 'settings': {name: str(rocket.get_editor_property(name)) for name in properties}}

before = snapshot()
unreal.SystemLibrary.execute_console_command(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(),
                                            'Cola.RepairBottleRocketTemplate')
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
after = snapshot()
if before['settings'] != after['settings']:
    raise RuntimeError('Recompile unexpectedly changed authored rocket settings')
if not unreal.EditorAssetLibrary.save_loaded_asset(bp, False):
    raise RuntimeError('Cannot save refreshed bottle Blueprint')
(backup.parent / 'RefreshReport.json').write_text(json.dumps({'before': before, 'after': after}, indent=2), encoding='utf-8')
unreal.log('BOTTLE_ROCKET_BLUEPRINT_REFRESHED ' + json.dumps(after))
