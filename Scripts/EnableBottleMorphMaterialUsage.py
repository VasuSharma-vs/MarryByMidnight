"""Enable the existing bottle materials' required skeletal-morph shader permutation."""
import json
import shutil
from pathlib import Path
import unreal

project = Path(unreal.Paths.project_dir())
evidence = project / 'Saved/PrototypeResume_20261112'
materials = {}
roots = {}

def visit(material):
    if not material or material.get_path_name() in materials: return
    if not material.get_path_name().startswith('/Game/Assest/Materials/'): return
    materials[material.get_path_name()] = material
    if isinstance(material, unreal.MaterialInstance): visit(material.get_editor_property('Parent'))
    elif isinstance(material, unreal.Material): roots[material.get_path_name()] = material

for name in ['EmptyBottle', 'Lemonade', 'Mint', 'Sweet', 'Spicy', 'ZeroCalorie']:
    product = unreal.load_asset('/Game/NormalColaPrototype/DA_Normal' + name)
    if not product: raise RuntimeError('Missing prototype bottle ' + name)
    for material in product.get_editor_property('MaterialOverrides'): visit(material)
    skeletal = product.get_editor_property('SkeletalMesh')
    if skeletal:
        for slot in skeletal.get_editor_property('Materials'): visit(slot.get_editor_property('MaterialInterface'))

if not roots: raise RuntimeError('No bottle base materials found')
for path in materials:
    relative = path.split('.', 1)[0].removeprefix('/Game/') + '.uasset'
    source, backup = project / 'Content' / relative, evidence / 'BeforeMorphUsage' / relative
    if source.exists() and not backup.exists():
        backup.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(source, backup)
changed = []
for path, material in roots.items():
    if not material.get_editor_property('bUsedWithMorphTargets'):
        unreal.MaterialEditingLibrary.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_MORPH_TARGETS)
        unreal.MaterialEditingLibrary.recompile_material(material)
        changed.append(path)
    if not material.get_editor_property('bUsedWithMorphTargets'): raise RuntimeError('Morph usage remains disabled ' + path)
for material in materials.values():
    if isinstance(material, unreal.MaterialInstanceConstant): unreal.MaterialEditingLibrary.update_material_instance(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, False): raise RuntimeError('Cannot save ' + material.get_path_name())
(evidence / 'MorphMaterialUsage.json').write_text(json.dumps({'base_materials': list(roots), 'changed': changed, 'saved': list(materials),
    'change': 'Required morph shader usage only; material appearance, parameters and source meshes preserved'}, indent=2), encoding='utf-8')
unreal.log('BOTTLE_MORPH_MATERIAL_USAGE_SAVED')
