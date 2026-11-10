"""Approved legacy food-container migration. Preview unless -ApplyFoodMigration is supplied.

IN PROGRESS AT USER-REQUESTED PAUSE: preview failed; do not apply until the
graph-clearing, parent-construction and getter resolution errors are repaired.

Keeps the original Blueprint identity and its Food amount member, so external
actor GUIDs, transforms and serialized per-instance amounts remain intact.
"""
import hashlib
import json
from datetime import datetime
from pathlib import Path
import shutil
import unreal
from editor_toolset.toolsets.blueprint import BlueprintTools

SOURCE = '/Game/Assest/Blueprints/PSO_FoodContainer'
PARENT = '/Game/Assest/Blueprints/BP_IPA_FoodContainer'
REVIEW = '/Game/ColaShopTests/LegacyFoodContainerModernPreview'
content = Path(unreal.Paths.project_content_dir()).resolve()
saved = Path(unreal.Paths.project_saved_dir()).resolve()
apply = '-ApplyFoodMigration' in unreal.SystemLibrary.get_command_line()
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
references = sorted(str(name) for name in registry.get_referencers(SOURCE, unreal.AssetRegistryDependencyOptions()))
source_file = content / (SOURCE.removeprefix('/Game/') + '.uasset')
reference_files = [content / (path.removeprefix('/Game/') + '.uasset') for path in references]
before = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in [source_file] + reference_files}
if len(reference_files) != 12:
    raise RuntimeError('Expected twelve inspected placed references; review changed references before applying.')

parent = unreal.EditorAssetLibrary.load_asset(PARENT)
unreal.BlueprintEditorLibrary.compile_blueprint(parent)
if not parent.generated_class():
    raise RuntimeError('Modern food-container parent did not compile')
source = unreal.EditorAssetLibrary.load_asset(SOURCE)
if not apply:
    if unreal.EditorAssetLibrary.does_asset_exist(REVIEW):
        raise RuntimeError('Preserving an existing review asset; use a new preview path after inspection')
    target = unreal.EditorAssetLibrary.duplicate_asset(SOURCE, REVIEW)
else:
    backup = saved / 'FoodContainerMigrationBackup' / datetime.now().strftime('%Y%m%d_%H%M%S')
    for path in [source_file] + reference_files:
        destination = backup / path.relative_to(content)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)
    (backup / 'manifest.json').write_text(json.dumps({'before': before, 'references': references}, indent=2), encoding='utf-8')
    target = source

# The old UI/delegate contract is superseded by the explicitly approved modern behavior.
unreal.BlueprintEditorLibrary.reparent_blueprint(target, parent.generated_class())
BlueprintTools.set_variable_replication(target, 'Food amount', unreal.BlueprintVariableReplication.NONE)
BlueprintTools.set_variable_instance_editable(target, 'Food amount', True)
for graph in list(BlueprintTools.list_graphs(target)):
    if graph.get_name().startswith('OnRep_'):
        BlueprintTools.remove_function_graph(target, graph.get_name())
    elif graph.get_name() == 'EventGraph':
        BlueprintTools.write_graph_dsl(graph, '')
    elif graph.get_name() == 'UserConstructionScript':
        BlueprintTools.write_graph_dsl(graph, '(fn ConstructionScript () (|Parent:ConstructionScript))')
override = BlueprintTools.add_function_graph(target, 'GetInitialContentLevel')
getters = [name for name in BlueprintTools.find_node_types(override, 'Food')
           if name.rsplit('|', 1)[-1].replace(' ', '').lower() == 'getfoodamount']
if len(getters) != 1:
    raise RuntimeError('Could not resolve the preserved Food amount getter: ' + str(getters))
BlueprintTools.write_graph_dsl(override, '(fn GetInitialContentLevel (DefinitionDefault) (return (' + getters[0] + ')))')
unreal.BlueprintEditorLibrary.compile_blueprint(target)
status = str(target.get_editor_property('status'))
if 'BS_ERROR' in status.upper() or not target.generated_class():
    raise RuntimeError('Modern food-container migration did not compile: ' + status)
actor = unreal.get_default_object(target.generated_class())
default_amount = actor.get_editor_property('Food amount')
samples = []
for amount in [0.0, 0.37, 0.83, 1.0]:
    actor.set_editor_property('Food amount', amount)
    result = actor.get_initial_content_level(1.0)
    if abs(result - amount) > .00001:
        raise RuntimeError('Initial canonical quantity does not preserve the legacy fraction')
    samples.append({'legacy_fraction': amount, 'canonical_fraction': result})
actor.set_editor_property('Food amount', default_amount)

report = {'applied': apply, 'source': SOURCE, 'parent': PARENT, 'status': status,
          'default_amount': default_amount, 'quantity_samples': samples,
          'references': references, 'before': before}
if apply:
    rice = actor.get_editor_property('PropDataAsset')
    if not rice:
        raise RuntimeError('Modern container has no product definition')
    rice_file = content / (rice.get_path_name().split('.')[0].removeprefix('/Game/') + '.uasset')
    rice_backup = backup / rice_file.relative_to(content)
    rice_backup.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(rice_file, rice_backup)
    rice.set_editor_property('bEnableInstanceUniqueData', True)
    if not unreal.EditorAssetLibrary.save_loaded_asset(rice) or not unreal.EditorAssetLibrary.save_loaded_asset(target):
        raise RuntimeError('Migration packages did not save successfully')
    report['backup'] = str(backup)
for path in reference_files:
    if hashlib.sha256(path.read_bytes()).hexdigest() != before[str(path)]:
        raise RuntimeError('A placed actor package changed unexpectedly: ' + str(path))
report['all_twelve_placed_packages_unchanged'] = True
if not apply and hashlib.sha256(source_file.read_bytes()).hexdigest() != before[str(source_file)]:
    raise RuntimeError('Preview changed the original source')
output = saved / 'ResumeBlueprintInspection' / ('FoodContainerMigrationApplied.json' if apply else 'FoodContainerMigrationPreview.json')
output.write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('FOOD_CONTAINER_MIGRATION ' + json.dumps(report))
