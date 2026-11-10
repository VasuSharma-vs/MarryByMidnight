"""Export project asset/Blueprint evidence without saving any Unreal package.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>.
This is an audit helper, not an asset migration or gameplay implementation.
"""
import collections
import hashlib
import json
import re
from pathlib import Path

import unreal
from editor_toolset.toolsets.actor import ActorTools
from editor_toolset.toolsets.blueprint import BlueprintTools

root = Path(unreal.Paths.project_dir()).resolve()
out = root / 'Saved' / 'ProjectAudit_2026-11-10'
out.mkdir(parents=True, exist_ok=True)
graphs_dir = out / 'BlueprintGraphs'
graphs_dir.mkdir(exist_ok=True)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
assets = sorted(registry.get_assets_by_path('/Game', recursive=True), key=lambda a: str(a.package_name))
package_files = list((root / 'Content').rglob('*.uasset')) + list((root / 'Content').rglob('*.umap'))
before = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in package_files}
report = {'base_commit': 'e99fdb3', 'mode': 'read-only; compile in memory; never save',
          'assets': [], 'blueprints': [], 'products': [], 'content_definitions': []}

def safe_property(obj, name):
    try:
        value = obj.get_editor_property(name)
        if value is None or isinstance(value, (str, bool, int, float)):
            return value
        if isinstance(value, unreal.Object):
            return value.get_path_name()
        return str(value)
    except Exception:
        return None

for data in assets:
    path = str(data.package_name)
    cls = str(data.asset_class_path.asset_name)
    entry = {'path': path, 'class': cls}
    report['assets'].append(entry)
    if cls in ('Blueprint', 'AnimBlueprint', 'WidgetBlueprint'):
        bp_entry = {'path': path, 'asset_class': cls, 'graphs': [], 'components': []}
        report['blueprints'].append(bp_entry)
        for tag in ('ParentClass', 'NativeParentClass', 'GeneratedClass'):
            bp_entry[tag] = data.get_tag_value(tag)
        try:
            bp = data.get_asset()
            if not bp:
                raise RuntimeError('Asset could not be loaded')
            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
            bp_entry['status'] = str(bp.get_editor_property('status'))
            bp_entry['variables'] = [str(v) for v in BlueprintTools.list_variables(bp)]
            # Orphaned classes cannot safely expose reflection-backed node details.
            for graph in BlueprintTools.list_graphs(bp) if bp.generated_class() else []:
                graph_entry = {'name': graph.get_name()}
                bp_entry['graphs'].append(graph_entry)
                try:
                    dsl = BlueprintTools.read_graph_dsl(graph)
                    graph_file = re.sub(r'[^a-zA-Z0-9_]', '_', path.rsplit('/', 1)[-1])[:50] + '__' + hashlib.sha256((path + ':' + graph.get_name()).encode()).hexdigest()[:16] + '.dsl'
                    (graphs_dir / graph_file).write_text(dsl, encoding='utf-8')
                    graph_entry.update({'file': graph_file, 'lines': len(dsl.splitlines())})
                except Exception as exc:
                    graph_entry['error'] = str(exc)
            generated = bp.generated_class()
            if generated:
                cdo = unreal.get_default_object(generated)
                bp_entry['defaults'] = {name: safe_property(cdo, name) for name in (
                    'bReplicates', 'bAlwaysRelevant', 'PropDataAsset', 'NPCguid', 'AIControllerClass',
                    'DefaultPawnClass', 'GameStateClass', 'PlayerControllerClass', 'AutoSaveIntervalSeconds')}
                if isinstance(cdo, unreal.Actor):
                    for comp in ActorTools.get_components(cdo):
                        bp_entry['components'].append({'name': comp.get_name(), 'class': comp.get_class().get_path_name()})
        except Exception as exc:
            bp_entry['error'] = str(exc)
        unreal.log('PROJECT_AUDIT_BLUEPRINT ' + path + ' ' + bp_entry.get('status', bp_entry.get('error', 'unknown')))
    if cls in ('InteractablePropDataAsset', 'FragmentAssemblyDataAsset'):
        try:
            obj = data.get_asset()
            fields = ('ItemDisplayName', 'Price', 'NumOfPortions', 'DefaultContentID', 'DefaultContentLevel', 'bEnableTemperatureUpdate',
                      'bEnableInstanceUniqueData', 'bEnableContinuousUpdateData', 'StaticMesh', 'CustomActiveActorClass',
                      'BottleMaterialType', 'FragmentAssembly', 'ConsumableType', 'bHasExpiry', 'ExpiryLifetimeMinutes',
                      'ConsumableTemperature', 'PressureDamageThreshold', 'PressureFailureThreshold', 'MaxOverfillLevel')
            report['products'].append({'path': path, 'class': cls, 'properties': {f: safe_property(obj, f) for f in fields}})
        except Exception as exc:
            report['products'].append({'path': path, 'error': str(exc)})

after = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in package_files}
report['asset_class_counts'] = dict(collections.Counter(a['class'] for a in report['assets']))
report['package_count'] = len(before)
report['package_changes'] = [p for p in before if before[p] != after[p]]
report['package_hashes'] = before
report['blueprint_counts'] = dict(collections.Counter(b.get('status', 'exception') for b in report['blueprints']))
(out / 'AssetAudit.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('PROJECT_AUDIT_COMPLETE ' + json.dumps({'asset_classes': report['asset_class_counts'], 'blueprint_counts': report['blueprint_counts'], 'package_changes': report['package_changes']}))
if report['package_changes']:
    raise RuntimeError('Unexpected package mutation during audit')
if any('BS_ERROR' in entry.get('status', '') for entry in report['blueprints']):
    raise RuntimeError('Blueprint compile failures; inventory was exported without saving packages')
