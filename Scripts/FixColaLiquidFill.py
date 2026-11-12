"""Patch the designer's reorganized liquid graph without rebuilding its appearance.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>, editor closed.
The tracked HLSL is embedded in existing custom nodes for cooked builds.
"""
import json
from pathlib import Path
import unreal
from editor_toolset.toolsets.material import MaterialTools
from editor_toolset.toolsets.object import ObjectTools

project = Path(unreal.Paths.project_dir())
evidence = project / 'Saved/LiquidFillFix_20261112'
evidence.mkdir(parents=True, exist_ok=True)
material = unreal.load_asset('/Game/Assest/Materials/ColaMaterials/Liquied/M_BaseLiquid')
if not isinstance(material, unreal.Material):
    raise RuntimeError('Existing M_BaseLiquid is missing')
expressions = list(MaterialTools.get_expressions(material))
nodes = {e.get_name(): e for e in expressions}
report = {'material': material.get_path_name(), 'before_expressions': len(expressions)}


def props(node, names):
    return json.loads(ObjectTools.get_properties(node, names))


def update(node, values):
    if not ObjectTools.set_properties(node, json.dumps(values)):
        raise RuntimeError('Failed property update: ' + node.get_path_name())


def node_with_desc(cls, desc, x, y):
    node = next((e for e in expressions if isinstance(e, cls)
                 and str(e.get_editor_property('desc')) == desc), None)
    if node is None:
        node = MaterialTools.add_expression(material, cls.static_class(), x, y)
        update(node, {'Desc': desc})
        expressions.append(node)
    return node


def scalar(name, default, x, y):
    node = next((e for e in expressions if isinstance(e, unreal.MaterialExpressionScalarParameter)
                 and str(e.get_editor_property('parameter_name')) == name), None)
    if node is None:
        node = MaterialTools.add_expression(material, unreal.MaterialExpressionScalarParameter.static_class(), x, y)
        update(node, {'ParameterName': name, 'DefaultValue': default, 'Group': 'Native Liquid Motion'})
        expressions.append(node)
    return node


def bind(node, bindings, code=None):
    inputs = props(node, ['Inputs'])['Inputs']
    additions = [{'inputName': name, 'input': {'expression': {'refPath': source.get_path_name()}, 'outputIndex': index}}
                 for name, (source, index) in bindings.items() if not any(i['inputName'] == name for i in inputs)]
    if additions:
        update(node, {'Inputs': inputs + additions})
        inputs = props(node, ['Inputs'])['Inputs']
    for name, (source, index) in bindings.items():
        old = next(i for i, item in enumerate(inputs) if item['inputName'] == name)
        inputs[old] = {'inputName': name, 'input': {'expression': {'refPath': source.get_path_name()}, 'outputIndex': index}}
    values = {'Inputs': inputs}
    if code is not None:
        values['Code'] = code
    update(node, values)


# The bottle body (not the separate cap) bounds are measured from SKM_ColaBottel_N.
# Preserve mesh transforms, player/customer scale, tint and designer graph layout.
dimensions = {'Liquid Container Height': 186.5885625563, 'Liquid Container Radius': 37.65829277}
for expression in expressions:
    if isinstance(expression, unreal.MaterialExpressionScalarParameter):
        name = str(expression.get_editor_property('parameter_name'))
        if name in dimensions:
            update(expression, {'DefaultValue': dimensions[name]})

mode = scalar('Use Instance Liquid Data', 0, -5600, 2200)
stamp = scalar('Liquid Cached Fill', -1, -5600, 2300)
level = nodes['MaterialExpressionScalarParameter_2']
if str(level.get_editor_property('parameter_name')) != 'Liquid Level':
    raise RuntimeError('Designer fill parameter changed; inspect graph before editing')
MaterialTools.connect_expressions(level, '', nodes['MaterialExpressionNamedRerouteDeclaration_10'], 'None')

# Explicit instance space in the vertex stage works for ISM and ordinary ASM.
# Pixel-stage generic Local transforms are deliberately not used for bottle axes.
for name in ['MaterialExpressionTransform_1', 'MaterialExpressionTransform_2']:
    update(nodes[name], {'TransformSourceType': 'TRANSFORMSOURCE_Instance', 'TransformType': 'TRANSFORM_World'})

resolver = node_with_desc(unreal.MaterialExpressionCustom, 'ColaLiquidFillResolver', -5100, 2250)
update(resolver, {'OutputType': 'CMOT_Float2'})
bind(resolver, {
    'InstanceUpWS': (nodes['MaterialExpressionTransform_1'], 0),
    'InstanceRightWS': (nodes['MaterialExpressionTransform_2'], 0),
    'VelocityWS': (nodes['MaterialExpressionVectorParameter_8'], 0),
    'SloshStrength': (nodes['MaterialExpressionScalarParameter_10'], 0),
    'NativeMotion': (nodes['MaterialExpressionScalarParameter_0'], 0),
    'NativeSurfaceNormalWS': (nodes['MaterialExpressionVectorParameter_3'], 0),
    'UseInstanceLiquidData': (mode, 0),
    'InstanceVolumeValid': (nodes['MaterialExpressionPerInstanceCustomData_3'], 0),
    'InstanceFill': (nodes['MaterialExpressionPerInstanceCustomData_1'], 0),
    'LiquidLevel': (level, 0),
    'ContainerHeight': (nodes['MaterialExpressionScalarParameter_7'], 0),
    'ContainerRadius': (nodes['MaterialExpressionScalarParameter_8'], 0),
    'InstancePlaneOffset': (nodes['MaterialExpressionPerInstanceCustomData_0'], 0),
    'NativeCachedFill': (stamp, 0),
    'NativePlaneOffset': (nodes['MaterialExpressionScalarParameter_23'], 0),
}, (project / 'Shaders/ColaLiquidFill.hlsl').read_text())
interpolator = node_with_desc(unreal.MaterialExpressionVertexInterpolator, 'ColaLiquidResolvedFill', -4600, 2250)
MaterialTools.connect_expressions(resolver, '', interpolator, 'VS')
resolved_level = node_with_desc(unreal.MaterialExpressionComponentMask, 'ColaLiquidResolvedLevel', -4350, 2220)
resolved_plane = node_with_desc(unreal.MaterialExpressionComponentMask, 'ColaLiquidResolvedPlane', -4350, 2350)
for mask, red, green in [(resolved_level, True, False), (resolved_plane, False, True)]:
    update(mask, {'R': red, 'G': green, 'B': False, 'A': False})
    MaterialTools.connect_expressions(interpolator, '', mask, 'None')

bind(nodes['MaterialExpressionCustom_3'], {
    'LiquidLevel': (resolved_level, 0), 'NativePlaneOffset': (resolved_plane, 0),
}, (project / 'Shaders/ColaLiquidSurface.hlsl').read_text())
bind(nodes['MaterialExpressionCustom_7'], {
    'LocalPos': (nodes['MaterialExpressionVertexInterpolator_5'], 0),
    'CameraVectorWS': (nodes['MaterialExpressionCameraVectorWS_0'], 0),
    'InstanceUpWS': (nodes['MaterialExpressionVertexInterpolator_6'], 0),
    'InstanceRightWS': (nodes['MaterialExpressionVertexInterpolator_7'], 0),
    'LiquidLevel': (resolved_level, 0),
}, (project / 'Shaders/ColaLiquidSurfaceUV.hlsl').read_text())

# Keep removed/unused experiments and appearance branches intact.
MaterialTools.recompile(material)
report.update({'dimensions_cm': dimensions, 'after_expressions': len(MaterialTools.get_expressions(material)),
               'resolver': resolver.get_path_name(), 'manual_fill': level.get_editor_property('default_value'),
               'nodes': {node.get_name(): props(node, ['Inputs', 'Code']) for node in
                         [resolver, nodes['MaterialExpressionCustom_3'], nodes['MaterialExpressionCustom_7']]}})
stats = unreal.MaterialEditingLibrary.get_statistics(material)
report['statistics'] = {name: getattr(stats, name) for name in
                        ['num_pixel_shader_instructions', 'num_vertex_shader_instructions', 'num_samplers',
                         'num_pixel_texture_samples', 'num_vertex_texture_samples',
                         'num_uv_scalars', 'num_interpolator_scalars'] if hasattr(stats, name)}
if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
    raise RuntimeError('Could not save M_BaseLiquid')
(evidence / 'MaterialFix.json').write_text(json.dumps(report, indent=2))
unreal.log('LIQUID_FILL_FIX_SAVED ' + json.dumps({k: v for k, v in report.items() if k != 'nodes'}))
