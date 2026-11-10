"""Compile saved gameplay Blueprints without saving or changing their packages."""
import hashlib
import json
from pathlib import Path
import unreal

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
folders = ['/Game/Props', '/Game/DirtyActors', '/Game/FirstPerson/Blueprints',
           '/Game/AI/BasicNPC', '/Game/AI/AI_IntractAssets', '/Game/Assest/Blueprints',
           '/Game/Assest/meshs/Bottle_Fragments_Mesh', '/Game/ColaShopTests']
paths = set()
for folder in folders:
    for data in registry.get_assets_by_path(folder, recursive=True):
        if str(data.asset_class_path.asset_name) in ('Blueprint', 'AnimBlueprint'):
            paths.add(str(data.package_name))
content = Path(unreal.Paths.project_content_dir())
results = []
for path in sorted(paths):
    entry = {'asset': path}
    package_file = content / (path.removeprefix('/Game/') + '.uasset')
    before = hashlib.sha256(package_file.read_bytes()).hexdigest()
    try:
        blueprint = unreal.EditorAssetLibrary.load_asset(path)
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        entry['status'] = str(blueprint.get_editor_property('status'))
    except Exception as exc:
        entry['error'] = str(exc)
    entry['package_unchanged'] = before == hashlib.sha256(package_file.read_bytes()).hexdigest()
    results.append(entry)
    unreal.log('FRAGMENT_BLUEPRINT_COMPATIBILITY ' + json.dumps(entry))
report = Path(unreal.Paths.project_saved_dir()) / 'Automation' / 'FragmentGameplayBlueprintCompatibility.json'
report.parent.mkdir(parents=True, exist_ok=True)
report.write_text(json.dumps({'assets': results}, indent=2), encoding='utf-8')
failures = [entry for entry in results if entry.get('error') or 'BS_ERROR' in entry.get('status', '').upper() or not entry['package_unchanged']]
unreal.log('FRAGMENT_BLUEPRINT_SUMMARY checked=' + str(len(results)) + ' failures=' + str(len(failures)))
if failures:
    raise RuntimeError('Gameplay Blueprint compatibility failed; see ' + str(report))
