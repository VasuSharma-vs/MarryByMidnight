"""Author an isolated Normal Cola map. Never edits the original world or existing save-game files."""
import json
from pathlib import Path
import unreal
from editor_toolset.toolsets.actor import ActorTools
from editor_toolset.toolsets.blueprint import BlueprintTools as BP

ROOT='/Game/NormalColaPrototype'
MAP=ROOT+'/Lvl_NormalColaPrototype'
if unreal.EditorAssetLibrary.does_asset_exist(MAP):
    raise RuntimeError('Prototype map already exists. Inspect it rather than overwriting it.')
evidence=Path(unreal.Paths.project_saved_dir())/'NormalColaPrototype_20261111'
catalog=unreal.load_asset(ROOT+'/DA_NormalColaSupplyCatalog')
if not catalog: raise RuntimeError('Configure the Normal Cola catalog first')

def fields(obj,values):
    for name,value in values.items(): obj.set_editor_property(name,value)
def components(bp): return list(ActorTools.get_components(unreal.get_default_object(bp.generated_class())))
def add(owner,cls,name):
    parts=components(owner) if isinstance(owner,unreal.Blueprint) else owner.get_components_by_class(unreal.ActorComponent)
    existing=next((c for c in parts if c.get_name().removesuffix('_GEN_VARIABLE')==name),None)
    result=existing or ActorTools.add_component(owner,cls.static_class(),name)
    if not result: raise RuntimeError('Cannot add component '+name)
    return result
def copy(source,name):
    return unreal.load_asset(ROOT+'/'+name) if unreal.EditorAssetLibrary.does_asset_exist(ROOT+'/'+name) else unreal.EditorAssetLibrary.duplicate_asset(source,ROOT+'/'+name)
def compile_save(bp):
    BP.compile_blueprint(bp)
    if 'ERROR' in str(bp.get_editor_property('status')): raise RuntimeError('Blueprint error: '+bp.get_path_name())
    if not unreal.EditorAssetLibrary.save_loaded_asset(bp,False): raise RuntimeError('Cannot save '+bp.get_path_name())
def box(owner,name,tag,location,extent):
    c=add(owner,unreal.BoxComponent,name)
    fields(c,{'ComponentTags':[tag],'RelativeLocation':unreal.Vector(*location),'BoxExtent':unreal.Vector(*extent)})
    c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    return c
def label(owner,name,text,location,size=15):
    c=add(owner,unreal.TextRenderComponent,name); c.set_text(text); c.set_world_size(size)
    c.set_editor_property('relative_location',unreal.Vector(*location)); return c

# Keep the actual player class required by existing Blueprint interface parameter types.
# The native input component reads the world profile tag to suppress unfinished passive punch overlaps.
player=unreal.load_asset('/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter')
npc=copy('/Game/AI/BasicNPC/BP_Basic_NPC','BP_NormalColaCustomer')
npc_cdo=unreal.get_default_object(npc.generated_class())
fields(npc_cdo,{'AIControllerClass':unreal.AIController.static_class(),'AutoPossessAI':unreal.AutoPossessAI.PLACED_IN_WORLD_OR_SPAWNED,'bReplicates':True,'bReplicateMovement':True})
cash_parts=[c for c in components(npc) if isinstance(c,unreal.ShopCustomerCashComponent)]
if not cash_parts: add(npc,unreal.ShopCustomerCashComponent,'CustomerCash')
for extra in cash_parts[1:]: ActorTools.remove_component(extra)
add(npc,unreal.ShopCustomerVisitComponent,'ShopVisit')
status=label(npc,'VisitStatus','Shopping',(0,0,130),15)
status.set_editor_property('component_tags',['ShopVisitStatus'])
for part in components(npc):
    if isinstance(part,unreal.CharacterMovementComponent): part.set_editor_property('max_walk_speed',190.)
compile_save(npc)
bucket=copy('/Game/DirtyActors/BP_Bucket','BP_NormalShoppingBucket')
bucket_cdo=unreal.get_default_object(bucket.generated_class())
fields(bucket_cdo,{'bReplicates':True,'bReplicateMovement':True})
bucket_mesh=next(c for c in components(bucket) if isinstance(c,unreal.StaticMeshComponent))
bucket_mesh.set_simulate_physics(False)
bucket_mesh.set_collision_profile_name('IgnoreOnlyPawn')
bucket_mesh.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
bucket_bounds=bucket_mesh.get_editor_property('static_mesh').get_bounds()
bucket_scale=90./max(1.,2.*max(bucket_bounds.box_extent.x,bucket_bounds.box_extent.y))
pockets=sorted([c for c in components(bucket) if isinstance(c,unreal.SphereComponent)],key=lambda c:c.get_name())
for i,part in enumerate(pockets):
    part.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    fields(part,{'ComponentTags':['ShopBucketPocket'],'RelativeLocation':unreal.Vector(((i%3)-1)*22./bucket_scale,((i//3)-1)*22./bucket_scale,8./bucket_scale)})
compile_save(bucket)

# The existing ATM interaction adapter opens the shared native terminal for all station types.
checkout=copy('/Game/DirtyActors/BP_ATM','BP_NormalColaCheckout')
for part in components(checkout):
    if isinstance(part,unreal.ShopATMComponent) or isinstance(part,unreal.BoxComponent): ActorTools.remove_component(part)
housing=next(c for c in components(checkout) if isinstance(c,unreal.StaticMeshComponent))
housing.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
fields(housing,{'RelativeLocation':unreal.Vector(0,0,50),'RelativeScale3D':unreal.Vector(4,2,1)})
add(checkout,unreal.ShopScannerComponent,'ShopScanner'); add(checkout,unreal.ShopCashRegisterComponent,'CashRegister')
box(checkout,'ScanVolume','ShopScannerVolume',(130,30,135),(35,40,35))
box(checkout,'TenderVolume','ShopTender',(0,-50,110),(40,35,12))
box(checkout,'ChangeVolume','ShopChange',(130,-60,110),(35,25,12))
dock=add(checkout,unreal.SceneComponent,'BucketDock')
fields(dock,{'ComponentTags':['ShopBucketDock'],'RelativeLocation':unreal.Vector(-110,30,105)})
for name,text,loc in [('ScanLabel','SCAN',(130,30,180)),('TenderLabel','PAYMENT',(0,-50,145)),('ChangeLabel','CHANGE',(130,-60,145))]: label(checkout,name,text,loc,12)
compile_save(checkout)
mode=copy('/Game/FirstPerson/Blueprints/BP_FirstPersonGameMode','BP_NormalColaGameMode')
fields(unreal.get_default_object(mode.generated_class()),{'DefaultPawnClass':player.generated_class()})
compile_save(mode)

world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
if not world: raise RuntimeError('Cannot create prototype world')
world.get_world_settings().set_editor_property('default_game_mode',mode.generated_class())
world.get_world_settings().set_editor_property('Tags',['NormalColaPrototype'])
def spawn(cls,location,label_text,rotation=None):
    actor=unreal.EditorLevelLibrary.spawn_actor_from_class(cls,unreal.Vector(*location),rotation or unreal.Rotator())
    if not actor: raise RuntimeError('Cannot spawn '+label_text)
    actor.set_actor_label(label_text); return actor
def marker(name,location,yaw=0): return spawn(unreal.TargetPoint,location,name,unreal.Rotator(0,yaw,0))
cube=unreal.load_asset('/Engine/BasicShapes/Cube')
def slab(owner,name,location,scale):
    part=add(owner,unreal.StaticMeshComponent,name); part.set_static_mesh(cube)
    fields(part,{'RelativeLocation':unreal.Vector(*location),'RelativeScale3D':unreal.Vector(*scale)})
    part.set_collision_profile_name('BlockAll'); return part
floor=spawn(unreal.StaticMeshActor,(0,0,-10),'Prototype ground')
floor.static_mesh_component.set_static_mesh(cube); floor.set_actor_scale3d(unreal.Vector(40,50,.2))
manager=spawn(unreal.load_asset('/Game/Props/BP_PropManager').generated_class(),(0,0,0),'Normal Cola Prop Manager')
fields(manager,{'InitialSaveSlot':'NormalColaPrototypeAutosave','AutosaveSlot':'NormalColaPrototypeAutosave','AutosaveIntervalSeconds':60.,'bEnableAutosave':True})
manager.set_editor_property('DefaultProductDataAssets',[unreal.load_asset(ROOT+'/DA_NormalEmptyBottle')]+[unreal.load_asset(ROOT+'/DA_Normal'+n) for n in ['Lemonade','Mint','Sweet','Spicy','ZeroCalorie']])
counter=spawn(checkout.generated_class(),(0,700,0),'Shared checkout - interact to bill')
spawn(unreal.load_asset(ROOT+'/BP_NormalColaWorkbench').generated_class(),(400,-1000,0),'Supplies and Normal Cola workbench')
spawn(unreal.load_asset('/Game/DirtyActors/BP_ATM').generated_class(),(950,-1050,0),'Shop bank ATM')
entry=marker('Shop entrance',(0,-1300,88))
arrival=marker('Customer arrival',(-800,-2000,88),90)
queue=marker('Checkout queue',(0,450,88),90)
returns=marker('Exit bucket holder',(-400,-1100,88))
departure=marker('Customer departure',(900,-2100,88),-90)
spawn(unreal.PlayerStart,(100,-950,100),'Prototype player start',unreal.Rotator(0,90,0))
buckets=[]
for i in range(3):
    actor=spawn(bucket.generated_class(),(-500+i*110,-1100,10),'Shopping bucket '+str(i+1))
    actor.set_actor_scale3d(unreal.Vector(bucket_scale,bucket_scale,bucket_scale)); buckets.append(actor)
flow=spawn(unreal.ShopCustomerFlow,(0,0,0),'Normal Cola customer coordinator')
fields(flow,{'CustomerClass':npc.generated_class(),'CheckoutStation':counter,'SpawnPoint':arrival,'EntrancePoint':entry,'QueuePoint':queue,
             'BucketReturnPoint':returns,'DeparturePoint':departure,'Buckets':buckets,'bRecycleDepartedCustomers':True})

inspection=json.loads((evidence/'PrototypeIntegrationInspection.json').read_text(encoding='utf-8'))
starter_count=sum(1 for a in inspection['actors'] if a['class'] in ['BP_Cola_C','BP_SpicyCola_C','BP_SugerCrushCola_C'])
products=[unreal.load_asset(ROOT+'/DA_Normal'+name) for name in ['Lemonade','Mint','Sweet','Spicy','ZeroCalorie']]
placed=0
for shelf_index in range(10):
    sx=-650 if shelf_index%2==0 else 650; sy=-700+(shelf_index//2)*400
    shelf=marker('Normal Cola shelf '+str(shelf_index+1),(sx,sy,0))
    for layer in range(3): slab(shelf,'Shelf'+str(layer),(0,0,50+layer*70),(3,1.4,.1))
    volume=box(shelf,'StockVolume','ShopShelfStock',(0,0,125),(155,75,125))
    stock=add(shelf,unreal.ShopShelfStockComponent,'ShopShelfStock'); stock.set_editor_property('StockVolume',volume)
    for layer in range(3):
        for row in range(3):
            for col in range(4):
                if placed>=starter_count: break
                product=products[placed%5]
                actor=spawn(product.get_editor_property('CustomActiveActorClass'),(sx-75+col*50,sy-45+row*45,56+layer*70),'Starter Normal Cola '+str(placed+1))
                fields(actor,{'PropDataAsset':product,'ISID':0,'ProductID':0,'State':unreal.PropState.AT_REST,'bAutoConvertToISMIfAtRest':True,
                              'NumOfPortions':4,'RemainingPortions':4,'CurrentTemperature':product.get_editor_property('ConsumableTemperature'),'Tags':['ShopInitialStock']})
                actor.set_actor_scale3d(unreal.Vector(.15,.15,.15))
                placed+=1

for name,location,scale in [('Left wall',(-1250,0,150),(.2,28,3)),('Right wall',(1250,0,150),(.2,28,3)),('Back wall',(0,1400,150),(25,.2,3))]:
    actor=spawn(unreal.StaticMeshActor,location,name); actor.static_mesh_component.set_static_mesh(cube); actor.set_actor_scale3d(unreal.Vector(*scale))
light=spawn(unreal.DirectionalLight,(0,0,1000),'Shop daylight',unreal.Rotator(-45,-30,0))
light.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(5.)
spawn(unreal.SkyLight,(0,0,1000),'Shop sky light')
nav=spawn(unreal.NavMeshBoundsVolume,(0,0,250),'Prototype navigation bounds')
nav.set_actor_scale3d(unreal.Vector(16,24,4))
unreal.SystemLibrary.execute_console_command(world,'RebuildNavigation')
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if isinstance(actor,unreal.RecastNavMesh): actor.set_editor_property('RuntimeGeneration',unreal.RuntimeGenerationType.DYNAMIC)
guide=marker('Prototype controls',(-200,-850,150))
label(guide,'Controls','Normal Cola prototype\nHold G: pick up / place | GG: drop\nInteract with workbench, checkout or ATM\nScan each customer bottle, lock bill, complete payment\nDeposit sale coins at ATM to buy supplies',(0,0,0),14)
if placed!=starter_count: raise RuntimeError('Starter-stock count changed')
if not unreal.EditorLoadingAndSavingUtils.save_map(world,MAP): raise RuntimeError('Cannot save prototype map')
(evidence/'NormalPrototypeMap.json').write_text(json.dumps({'map':MAP,'starter_bottles':placed,'source_count_preserved':True,
    'starting_bank_cents':0,'autosave_slot':'NormalColaPrototypeAutosave','customer_limit':3,'prototype_assets':[player.get_path_name(),npc.get_path_name(),bucket.get_path_name(),checkout.get_path_name(),mode.get_path_name()]},indent=2),encoding='utf-8')
unreal.log('NORMAL_COLA_PROTOTYPE_MAP_SAVED '+MAP)
