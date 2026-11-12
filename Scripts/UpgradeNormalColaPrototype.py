"""Scoped, repeatable prototype upgrade. Never saves reference-world or shared character assets."""
import json
import shutil
from pathlib import Path
import unreal
from editor_toolset.toolsets.actor import ActorTools
from editor_toolset.toolsets.blueprint import BlueprintTools as BP

ROOT = '/Game/NormalColaPrototype'
project = Path(unreal.Paths.project_dir())
evidence = project / 'Saved/PrototypeResume_20261112'
backup = evidence / 'BeforePrototypeUpgrade'
if not backup.exists():
    shutil.copytree(project / 'Content/NormalColaPrototype', backup)
report = {'saved': [], 'maps': [], 'reference_assets_saved': False}

def load(name):
    obj = unreal.load_asset(ROOT + '/' + name)
    if not obj: raise RuntimeError('Missing prototype asset ' + name)
    return obj

def parts(owner):
    if isinstance(owner, unreal.Blueprint): owner = unreal.get_default_object(owner.generated_class())
    return list(ActorTools.get_components(owner))

def named(owner, name):
    return next(p for p in parts(owner) if p.get_name().removesuffix('_GEN_VARIABLE') == name)

def fields(obj, values):
    for key, value in values.items(): obj.set_editor_property(key, value)

def vector(obj, name, value): obj.set_editor_property(name, unreal.Vector(*value))

def save(obj):
    if not obj.get_path_name().startswith(ROOT + '/'): raise RuntimeError('Out-of-scope save')
    if isinstance(obj, unreal.Blueprint):
        BP.compile_blueprint(obj)
        if 'ERROR' in str(obj.get_editor_property('status')): raise RuntimeError('Compile failed: ' + obj.get_path_name())
    if not unreal.EditorAssetLibrary.save_loaded_asset(obj, False): raise RuntimeError('Save failed ' + obj.get_path_name())
    report['saved'].append(obj.get_path_name())

def controls(bp, location):
    control = next((p for p in parts(bp) if p.component_has_tag('ShopInteractionPoint')), None)
    if not control: control = ActorTools.add_component(bp, unreal.SceneComponent.static_class(), 'PlayerControls')
    if not control: raise RuntimeError('Cannot create player controls')
    fields(control, {'ComponentTags': ['ShopInteractionPoint'], 'RelativeLocation': unreal.Vector(*location)})

flavors = [('Lemonade', 2.5), ('Mint', 3.), ('Sweet', 3.25), ('Spicy', 4.5), ('ZeroCalorie', 4.5)]
products = []
for name, price in [('EmptyBottle', 0)] + flavors:
    obj = load('DA_Normal' + name)
    label = 'Empty plastic bottle' if name == 'EmptyBottle' else ('Zero Calories' if name == 'ZeroCalorie' else name) + ' Cola'
    fields(obj, {'BottleMaterialType': unreal.BottleMaterialType.PLASTIC, 'FragmentAssembly': None, 'FractureFragments': [],
                 'bCanInflate': True, 'bCanBend': True, 'bCanDent': True, 'bCanCrack': False,
                 'DefaultCapType': unreal.BottleCapType.TWIST, 'SupportedCapMask': 1, 'ItemDisplayName': unreal.Text(label),
                 'BrokenProductID': 0, 'BurstBrokenProductID': 0, 'GameplayMassKg': 80.,
                 'CapAttachmentTransform': unreal.Transform(location=unreal.Vector(0, 0, 170))})
    if name != 'EmptyBottle':
        fields(obj, {'Price': price, 'DefaultContentLevel': 1., 'DefaultCapSealed': 1.})
        products.append(obj)
    save(obj)
catalog = load('DA_NormalColaSupplyCatalog')
offers = list(catalog.get_editor_property('Offers'))
for offer in offers:
    if str(offer.get_editor_property('OfferID')) == 'EmptyBottle': offer.set_editor_property('DisplayName', unreal.Text('Empty plastic bottle'))
    for flavor, _ in flavors:
        if str(offer.get_editor_property('OfferID')) == 'Cylinder' + flavor:
            offer.set_editor_property('DisplayName', unreal.Text(('Zero Calories' if flavor == 'ZeroCalorie' else flavor) + ' Normal Bulk Cola Container'))
catalog.set_editor_property('Offers', offers); save(catalog)
cap = load('DA_NormalTwistCap'); cap.set_editor_property('GameplayMassKg', .2); cap.set_editor_property('MaterialOverrides', []); save(cap)
for name, _ in flavors:
    bulk = load('DA_Cylinder' + name)
    bulk.set_editor_property('GameplayMassKg', 20.)
    bulk.set_editor_property('MaterialOverrides', [])
    bulk.set_editor_property('ItemDisplayName', unreal.Text(('Zero Calories' if name == 'ZeroCalorie' else name) + ' Normal Bulk Cola Container'))
    save(bulk)
npc = load('BP_NormalColaCustomer')
for part in parts(npc):
    if isinstance(part, unreal.CharacterMovementComponent): part.set_editor_property('Mass', 50.)
save(npc)
coins = {}
for dollars in [1, 2, 5, 10, 20, 50, 100]:
    path = ROOT + '/DA_NormalCoin_' + str(dollars)
    coin = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else unreal.EditorAssetLibrary.duplicate_asset('/Game/Props/Currency/DA_Coin_' + str(dollars), path)
    if not coin: raise RuntimeError('Cannot create prototype currency definition')
    coin.set_editor_property('GameplayMassKg', .25); save(coin); coins[dollars] = coin

bench = load('BP_NormalColaWorkbench')
housing = next(p for p in parts(bench) if isinstance(p, unreal.StaticMeshComponent))
vector(housing, 'RelativeScale3D', (2.5, 2, 1))
station = next(p for p in parts(bench) if isinstance(p, unreal.ShopProductionStationComponent))
station.set_editor_property('bRequiresCooperativeCap', True)
for name, extent in [('BottleInput', (45, 45, 105)), ('CylinderInput', (25, 25, 45)), ('CapInput', (22, 22, 25))]:
    vector(named(bench, name), 'BoxExtent', extent)
vector(named(bench, 'SupplyOutput'), 'RelativeLocation', (-500, 0, 110))
vector(named(bench, 'SupplyOutput'), 'RelativeScale3D', (1, 1, 1))
controls(bench, (0, -100, 95)); save(bench)

checkout = load('BP_NormalColaCheckout')
housing = next(p for p in parts(checkout) if isinstance(p, unreal.StaticMeshComponent))
vector(housing, 'RelativeLocation', (0, 0, -230)); vector(housing, 'RelativeScale3D', (17, 12.4, 4.6))
for name, loc, extent in [('ScanVolume', (450, 220, 100), (280, 200, 120)),
                          ('TenderVolume', (0, -320, 20), (210, 180, 35)),
                          ('ChangeVolume', (450, -320, 20), (180, 180, 35))]:
    vector(named(checkout, name), 'RelativeLocation', loc); vector(named(checkout, name), 'BoxExtent', extent)
vector(named(checkout, 'BucketDock'), 'RelativeLocation', (-450, -220, 0))
for name, loc in [('ScanLabel', (450, 220, 230)), ('TenderLabel', (0, -320, 80)), ('ChangeLabel', (450, -320, 80))]:
    vector(named(checkout, name), 'RelativeLocation', loc)
controls(checkout, (350, -610, 20))
customer_control = next((p for p in parts(checkout) if p.component_has_tag('ShopCustomerInteractionPoint')), None)
if not customer_control: customer_control = ActorTools.add_component(checkout, unreal.SceneComponent.static_class(), 'CustomerHandover')
fields(customer_control, {'ComponentTags': ['ShopCustomerInteractionPoint'], 'RelativeLocation': unreal.Vector(-450, -610, 0)})
save(checkout)

bucket = load('BP_NormalShoppingBucket')
bucket_body = next(p for p in parts(bucket) if isinstance(p, unreal.StaticMeshComponent))
bucket_body.set_mass_override_in_kg('None', 2., True)
reference = unreal.load_asset('/Game/DirtyActors/BP_Bucket')
original = {p.get_name().removesuffix('_GEN_VARIABLE'): p for p in parts(reference)}
for part in parts(bucket):
    if isinstance(part, unreal.SphereComponent):
        source = original.get(part.get_name().removesuffix('_GEN_VARIABLE'))
        if not source: raise RuntimeError('Bucket pocket has no reference counterpart')
        part.set_editor_property('RelativeLocation', source.get_editor_property('RelativeLocation'))
        part.set_editor_property('ComponentTags', ['ShopBucketPocket'])
save(bucket)

atm_path = ROOT + '/BP_NormalColaATM'
atm = unreal.load_asset(atm_path) if unreal.EditorAssetLibrary.does_asset_exist(atm_path) else unreal.EditorAssetLibrary.duplicate_asset('/Game/DirtyActors/BP_ATM', atm_path)
for part in parts(atm):
    if part.component_has_tag('ATMDeposit'):
        vector(part, 'RelativeLocation', (-250, -180, 180)); vector(part, 'BoxExtent', (200, 180, 40))
    if part.component_has_tag('ATMCoinOutput'): vector(part, 'RelativeLocation', (350, -180, 200))
controls(atm, (0, -200, 120)); save(atm)

cube = unreal.load_asset('/Engine/BasicShapes/Cube')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for map_name in ['Lvl_NormalColaPrototype', 'Lvl_NormalColaPrototype_Validation']:
    world = unreal.EditorLoadingAndSavingUtils.load_map(ROOT + '/' + map_name)
    if not world: raise RuntimeError('Cannot load ' + map_name)
    all_actors = actors.get_all_level_actors()
    by_label = {a.get_actor_label(): a for a in all_actors}
    stock = sorted([a for a in all_actors if isinstance(a, unreal.ColaPropActor) and a.actor_has_tag('ShopInitialStock')],
                   key=lambda a: int(a.get_actor_label().rsplit(' ', 1)[-1]))
    if len(stock) != 356: raise RuntimeError('Unexpected starter stock count: ' + str(len(stock)))
    for index, shelf in enumerate(sorted([a for a in all_actors if a.get_actor_label().startswith('Normal Cola shelf ')],
                                         key=lambda a: int(a.get_actor_label().rsplit(' ', 1)[-1]))):
        sx = -1450 if index % 2 == 0 else 1450
        sy = -1200 + (index // 2) * 650
        shelf.set_actor_location(unreal.Vector(sx, sy, 0), False, True)
        for layer in range(3):
            slab = named(shelf, 'Shelf' + str(layer))
            vector(slab, 'RelativeLocation', (0, 0, 50 + layer * 270)); vector(slab, 'RelativeScale3D', (2.8, 4.2, .12))
        volume = named(shelf, 'StockVolume')
        vector(volume, 'RelativeLocation', (0, 0, 400)); vector(volume, 'BoxExtent', (150, 220, 420))
        for slot in range(36):
            i = index * 36 + slot
            if i >= len(stock): break
            layer, row, col = slot // 12, (slot % 12) // 4, slot % 4
            bottle = stock[i]
            bottle.set_actor_location(unreal.Vector(sx - 90 + row * 90, sy - 150 + col * 100, 56 + layer * 270), False, True)
            bottle.set_actor_scale3d(unreal.Vector(1, 1, 1))
    for a in all_actors:
        label = a.get_actor_label()
        if label == 'Prototype ground': a.set_actor_scale3d(unreal.Vector(80, 90, .2))
        elif label == 'Shared checkout - interact to bill': a.set_actor_location(unreal.Vector(0, 700, 460), False, True)
        elif label == 'Shop bank ATM':
            replacement = actors.spawn_actor_from_class(atm.generated_class(), unreal.Vector(-2600, -1400, 0))
            replacement.set_actor_label(label); actors.destroy_actor(a)
        elif label == 'Supplies and Normal Cola workbench':
            a.set_actor_location(unreal.Vector(-2600, -2200, 0), False, True)
        elif label.startswith('Shopping bucket '):
            number = int(label.rsplit(' ', 1)[-1]) - 1
            a.set_actor_scale3d(unreal.Vector(6, 6, 6))
            a.set_actor_location(unreal.Vector(-950 + number * 750, -2000, 10), False, True)
        elif label in ['Left wall', 'Right wall', 'Back wall']:
            x = -3400 if label == 'Left wall' else 3400 if label == 'Right wall' else 0
            y = 2400 if label == 'Back wall' else 0
            a.set_actor_location(unreal.Vector(x, y, 600), False, True)
            a.set_actor_scale3d(unreal.Vector(68, .2, 12) if label == 'Back wall' else unreal.Vector(.2, 48, 12))
        elif isinstance(a, unreal.NavMeshBoundsVolume):
            a.set_actor_location(unreal.Vector(0, 0, 750), False, True); a.set_actor_scale3d(unreal.Vector(38, 43, 10))
        elif isinstance(a, unreal.RecastNavMesh): actors.destroy_actor(a)
        elif isinstance(a, unreal.PropManager):
            a.get_editor_property('ShopSupplies').set_editor_property('bUseDeliveryArea', True)
            recipes = list(a.get_editor_property('ShopCheckout').get_editor_property('CurrencyRecipes'))
            for recipe in recipes:
                recipe.set_editor_property('MeshScale', 1.)
                recipe.set_editor_property('Product', coins[recipe.get_editor_property('Dollars')])
            a.get_editor_property('ShopCheckout').set_editor_property('CurrencyRecipes', recipes)
    markers = {'Customer arrival': (-800, -3400, 530), 'Shop entrance': (0, -2700, 530),
               'Checkout queue': (-550, -250, 530), 'Exit bucket holder': (-900, -2400, 530), 'Customer departure': (1600, -3500, 530)}
    for label, loc in markers.items(): by_label[label].set_actor_location(unreal.Vector(*loc), False, True)
    # Player-sized access decks provide access to giant counter controls without resizing characters.
    def geometry(label, loc, scale, rotation=None):
        actor = by_label.get(label)
        if not actor:
            actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*loc))
            actor.set_actor_label(label)
        actor.static_mesh_component.set_static_mesh(cube)
        actor.static_mesh_component.set_collision_profile_name('BlockAll')
        actor.set_actor_location(unreal.Vector(*loc), False, True)
        actor.set_actor_scale3d(unreal.Vector(*scale))
        actor.set_actor_rotation(rotation or unreal.Rotator(), True)
    geometry('Player checkout access deck', (450, -120, 450), (13, 4, .2))
    # 25 degree incline, 1070 cm horizontal run: ordinary character walking remains practical.
    geometry('Player checkout access ramp', (450, -800, 225), (4, 10.7, .2), unreal.Rotator(0, 0, 25))
    geometry('Supply delivery platform', (-3100, -2200, 105), (5.2, 14, .1))
    geometry('Supply delivery access ramp', (-3100, -3075, 55), (5.2, 4.25, .1), unreal.Rotator(0, 0, 15))
    for side in [-1, 1]:
        geometry('Player shelf access deck ' + str(side), (side * 1900, 200, 530), (4, 34, .2))
        geometry('Player shelf access ramp ' + str(side), (side * 1900, -2080, 265), (4, 12.6, .2), unreal.Rotator(0, 0, 25))
    # Readability is required for physical interactions. The original reference world is untouched.
    for label, loc in [('Prototype front fill light', (0, -1600, 1300)), ('Prototype back fill light', (0, 1400, 1300))]:
        light = by_label.get(label) or actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(*loc))
        light.set_actor_label(label)
        component = light.get_component_by_class(unreal.PointLightComponent)
        component.set_mobility(unreal.ComponentMobility.MOVABLE)
        fields(component, {'bUseInverseSquaredFalloff': False, 'LightFalloffExponent': 2., 'AttenuationRadius': 7000., 'Intensity': 20., 'CastShadows': False})
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, ROOT + '/' + map_name): raise RuntimeError('Map save failed')
    report['maps'].append({'path': ROOT + '/' + map_name, 'starter_count': len(stock), 'bottle_scale': 1, 'bucket_scale': 6})
(evidence / 'PrototypeUpgrade.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.log('NORMAL_COLA_SCOPED_UPGRADE_COMPLETE')
