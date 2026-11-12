"""Read-only asset/geometry audit. A pass does not accept the complete gameplay loop."""
import json
from pathlib import Path
import unreal
from editor_toolset.toolsets.actor import ActorTools

ROOT = '/Game/NormalColaPrototype'
report = {'checks': [], 'maps': [], 'failures': [], 'scope': 'Saved asset configuration and static geometry only'}

def check(label, passed, detail=''):
    report['checks'].append({'check': label, 'passed': bool(passed), 'detail': detail})
    if not passed: report['failures'].append(label)

def parts(bp): return list(ActorTools.get_components(unreal.get_default_object(bp.generated_class())))
def asset(name):
    result = unreal.load_asset(ROOT + '/' + name)
    if not result: raise RuntimeError('Missing asset ' + name)
    return result

names = ['Lemonade', 'Mint', 'Sweet', 'Spicy', 'ZeroCalorie']
products = [asset('DA_Normal' + name) for name in names]
for product in [asset('DA_NormalEmptyBottle')] + products:
    name = product.get_name()
    check(name + ' plastic only', product.get_editor_property('BottleMaterialType') == unreal.BottleMaterialType.PLASTIC)
    check(name + ' no fracture recipe', not product.get_editor_property('FragmentAssembly') and not product.get_editor_property('FractureFragments'))
    check(name + ' manual twist', product.get_editor_property('DefaultCapType') == unreal.BottleCapType.TWIST)
    check(name + ' light gameplay mass', 0 < product.get_editor_property('GameplayMassKg') < 100)
prices = [p.get_editor_property('Price') for p in products]
check('Approved Normal price order and upper tie', prices[0] < prices[1] < prices[2] < prices[3] == prices[4], str(prices))
station = next(p for p in parts(asset('BP_NormalColaWorkbench')) if isinstance(p, unreal.ShopProductionStationComponent))
check('Cooperative sealing saved', station.get_editor_property('bRequiresCooperativeCap'))
player = unreal.load_asset('/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter')
npc = asset('BP_NormalColaCustomer')
player_capsule = next(p for p in parts(player) if isinstance(p, unreal.CapsuleComponent))
npc_capsule = next(p for p in parts(npc) if isinstance(p, unreal.CapsuleComponent))
check('Player reference capsule preserved', abs(player_capsule.get_scaled_capsule_radius() - 34) < .01 and abs(player_capsule.get_scaled_capsule_half_height() - 96) < .01)
check('Giant reference capsule preserved', abs(npc_capsule.get_scaled_capsule_radius() - 204) < .01 and abs(npc_capsule.get_scaled_capsule_half_height() - 528) < .01)
player_mass = next(p for p in parts(player) if isinstance(p, unreal.CharacterMovementComponent)).get_editor_property('Mass')
npc_mass = next(p for p in parts(npc) if isinstance(p, unreal.CharacterMovementComponent)).get_editor_property('Mass')
check('NPC gameplay mass below unchanged player', 0 < npc_mass < player_mass, str({'player': player_mass, 'npc': npc_mass}))
check('Native giant nav default is dynamic after inherited config', unreal.get_default_object(unreal.NormalColaNavigationData).get_editor_property('RuntimeGeneration') == unreal.RuntimeGenerationType.DYNAMIC)

for map_name in ['Lvl_NormalColaPrototype', 'Lvl_NormalColaPrototype_Validation']:
    world = unreal.EditorLoadingAndSavingUtils.load_map(ROOT + '/' + map_name)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    navigation = [a for a in actors if isinstance(a, unreal.NormalColaNavigationData)]
    check(map_name + ' loaded giant navigation is dynamic', bool(navigation) and all(a.get_editor_property('RuntimeGeneration') == unreal.RuntimeGenerationType.DYNAMIC for a in navigation))
    stock = [a for a in actors if isinstance(a, unreal.ColaPropActor) and a.actor_has_tag('ShopInitialStock')]
    check(map_name + ' starter count', len(stock) == 356, str(len(stock)))
    boxes = []
    for bottle in stock:
        scale, loc = bottle.get_actor_scale3d(), bottle.get_actor_location()
        check(map_name + ' native scale ' + bottle.get_actor_label(), max(abs(scale.x - 1), abs(scale.y - 1), abs(scale.z - 1)) < .001)
        mesh = bottle.get_editor_property('PropDataAsset').get_editor_property('StaticMesh')
        bounds = mesh.get_bounds()
        boxes.append(([loc.x + bounds.origin.x - bounds.box_extent.x, loc.y + bounds.origin.y - bounds.box_extent.y, loc.z + bounds.origin.z - bounds.box_extent.z],
                      [loc.x + bounds.origin.x + bounds.box_extent.x, loc.y + bounds.origin.y + bounds.box_extent.y, loc.z + bounds.origin.z + bounds.box_extent.z], bottle.get_actor_label()))
    overlaps = []
    for i, first in enumerate(boxes):
        for second in boxes[i + 1:]:
            if all(min(first[1][axis], second[1][axis]) - max(first[0][axis], second[0][axis]) > .1 for axis in range(3)):
                overlaps.append([first[2], second[2]])
    check(map_name + ' separated full-size starter geometry', not overlaps, str(overlaps[:10]))
    buckets = [a for a in actors if a.get_actor_label().startswith('Shopping bucket ')]
    check(map_name + ' reference bucket scale', len(buckets) == 3 and all(abs(a.get_actor_scale3d().x - 6) < .001 for a in buckets))
    bench = next(a for a in actors if a.get_actor_label() == 'Supplies and Normal Cola workbench')
    check(map_name + ' production outside central customer traffic', bench.get_actor_location().x < -2000)
    check(map_name + ' delivery platform saved', any(a.get_actor_label() == 'Supply delivery platform' for a in actors))
    manager = next(a for a in actors if isinstance(a, unreal.PropManager))
    check(map_name + ' occupied delivery slots enabled', manager.get_editor_property('ShopSupplies').get_editor_property('bUseDeliveryArea'))
    report['maps'].append({'map': map_name, 'stock': len(stock), 'overlap_count': len(overlaps)})

output = Path(unreal.Paths.project_saved_dir()) / 'PrototypeResume_20261112/AssetValidation.json'
output.write_text(json.dumps(report, indent=2), encoding='utf-8')
if report['failures']: raise RuntimeError('Prototype asset validation failed: ' + '; '.join(report['failures'][:10]))
unreal.log('NORMAL_COLA_ASSET_CONFIGURATION_PASSED ' + str(len(report['checks'])) + ' checks')
