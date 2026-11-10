"""Run with UE's PythonScript commandlet after building the native fragment types.

Creates product assets and an assembly from the supplied showcase, then assigns
the assembly to the existing Cola1 bottle products. Source meshes/showcase are
read-only. Existing product packages are backed up under Saved before saving.
"""
import hashlib
import json
from datetime import datetime
from pathlib import Path
import shutil
import unreal
from editor_toolset.toolsets.actor import ActorTools

SOURCE = '/Game/Assest/meshs/Bottle_Fragments_Mesh'
DESTINATION = '/Game/Assest/DataAssets/BottleFragments'
SHOWCASE = SOURCE + '/BP_bottel_Fragments_Visioul_Socase'
ASSEMBLY = DESTINATION + '/DA_GlassBottleFragmentAssembly'
BOTTLES = ['DA_BasicCola', 'DA_Cola', 'DA_EmptyBottel', 'DA_GoodCola',
           'DA_NormalCola', 'DA_SpicyCola', 'DA_SweetCola']
content = Path(unreal.Paths.project_content_dir())
backup = Path(unreal.Paths.project_saved_dir()) / 'BottleFragmentAssetBackup' / datetime.now().strftime('%Y%m%d_%H%M%S')
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def disk_path(package):
    return content / (package.removeprefix('/Game/') + '.uasset')


def fingerprint(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_or_create(name, asset_class):
    package = DESTINATION + '/' + name
    existing = unreal.EditorAssetLibrary.load_asset(package) if unreal.EditorAssetLibrary.does_asset_exist(package) else None
    if existing:
        if not isinstance(existing, asset_class):
            raise RuntimeError('Unexpected asset class: ' + package)
        return existing
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', asset_class)
    asset = asset_tools.create_asset(name, DESTINATION, asset_class, factory)
    if not asset:
        raise RuntimeError('Could not create ' + package)
    return asset


reference_hashes = {str(path): fingerprint(path) for path in disk_path(SHOWCASE).parent.glob('*.uasset')}
blueprint = unreal.EditorAssetLibrary.load_asset(SHOWCASE)
default_actor = unreal.get_default_object(blueprint.generated_class())
components = ActorTools.get_components(default_actor, unreal.StaticMeshComponent.static_class())
components.sort(key=lambda component: component.get_name())
if len(components) != 11:
    raise RuntimeError('Expected the supplied eleven-piece assembly; inspect changes before updating this recipe.')
targets = []
for name in BOTTLES:
    package = '/Game/Assest/DataAssets/' + name
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if not asset or asset.get_editor_property('StaticMesh').get_path_name() != '/Game/Assest/meshs/Cola1.Cola1':
        raise RuntimeError('Bottle mesh differs from the inspected product: ' + package)
    current = asset.get_editor_property('FragmentAssembly')
    if current and current.get_path_name().split('.')[0] != ASSEMBLY:
        raise RuntimeError('Preserving a different existing assembly on ' + package)
    targets.append((package, asset))

backup.mkdir(parents=True, exist_ok=True)
for package, _ in targets:
    relative = disk_path(package).relative_to(content)
    target = backup / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(disk_path(package), target)

pieces = []
products = []
for component in components:
    mesh = component.get_editor_property('StaticMesh')
    name = 'DA_GlassBottleFragment_' + mesh.get_name().removeprefix('SM_Fragments_')
    product = load_or_create(name, unreal.InteractablePropDataAsset)
    old_mesh = product.get_editor_property('StaticMesh')
    if old_mesh and old_mesh != mesh:
        raise RuntimeError('Preserving a different mesh on ' + product.get_path_name())
    values = {
        'StaticMesh': mesh, 'MaterialOverrides': component.get_editor_property('OverrideMaterials'),
        'CustomActiveActorClass': unreal.FragmentPropActor.static_class(),
        'ItemDisplayName': 'Glass Bottle Fragment ' + mesh.get_name().removeprefix('SM_Fragments_'),
        'Price': 0.0, 'NumOfPortions': 0, 'bCanConsume': False, 'bCanUse': False,
        'bHasExpiry': False, 'bEnableTemperatureUpdate': False,
        'bEnableInstanceUniqueData': False, 'bEnableContinuousUpdateData': False,
        'DefaultContentLevel': 0.0, 'DefaultContentID': 0, 'DefaultCapSealed': 0.0,
    }
    for key, value in values.items():
        product.set_editor_property(key, value)
    piece = unreal.FragmentAssemblyPiece()
    piece.set_editor_property('Product', product)
    piece.set_editor_property('LocalTransform', component.get_relative_transform())
    center = mesh.get_bounds().origin
    direction = unreal.Vector(center.x, center.y, center.z)
    if direction.length() > 0.001:
        direction.normalize()
    else:
        direction = unreal.Vector(0, 0, 1)
    piece.set_editor_property('ScatterDirection', direction)
    pieces.append(piece)
    products.append(product)

assembly = load_or_create('DA_GlassBottleFragmentAssembly', unreal.FragmentAssemblyDataAsset)
assembly.set_editor_property('Pieces', pieces)
for product in products:
    if not unreal.EditorAssetLibrary.save_loaded_asset(product):
        raise RuntimeError('Failed to save ' + product.get_path_name())
if not unreal.EditorAssetLibrary.save_loaded_asset(assembly):
    raise RuntimeError('Failed to save assembly')
for _, asset in targets:
    asset.set_editor_property('FragmentAssembly', assembly)
    asset.set_editor_property('bEnableInstanceUniqueData', True)
    if asset.get_name() == 'DA_EmptyBottel':
        # This is the same physical Cola1 container; zero content does not mean zero capacity.
        filled_reference = next(value for package, value in targets if package.endswith('/DA_Cola'))
        asset.set_editor_property('NumOfPortions', filled_reference.get_editor_property('NumOfPortions'))
        asset.set_editor_property('DefaultContentLevel', 0.0)
        asset.set_editor_property('DefaultContentID', 0)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset):
        raise RuntimeError('Failed to save ' + asset.get_path_name())
for path, before in reference_hashes.items():
    if fingerprint(Path(path)) != before:
        raise RuntimeError('Source reference changed unexpectedly: ' + path)
report = {'assembly': ASSEMBLY, 'products': [product.get_path_name() for product in products],
          'bottles': [package for package, _ in targets], 'backup': str(backup),
          'reference_hashes': reference_hashes}
(backup / 'manifest.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('BOTTLE_FRAGMENT_CONFIGURATION_COMPLETE ' + json.dumps(report))
