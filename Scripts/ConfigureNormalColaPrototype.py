"""Author Normal Cola assets and thin Blueprint adapters; all gameplay services remain native."""
import json
import shutil
from pathlib import Path
import unreal
from editor_toolset.toolsets.blueprint import BlueprintTools as BP
from editor_toolset.toolsets.actor import ActorTools

ROOT='/Game/NormalColaPrototype'
tools=unreal.AssetToolsHelpers.get_asset_tools()
project=Path(unreal.Paths.project_dir())
evidence=project/'Saved/NormalColaPrototype_20261111'
before=evidence/'BeforePrototypeAdapters'
before.mkdir(parents=True,exist_ok=True)
saved=[]

def backup(path):
    disk=project/'Content'/(path.removeprefix('/Game/')+'.uasset')
    target=before/(path.removeprefix('/Game/')+'.uasset')
    if disk.exists() and not target.exists():
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(disk,target)

def duplicate(source,name):
    path=ROOT+'/'+name
    if unreal.EditorAssetLibrary.does_asset_exist(path): return unreal.load_asset(path)
    obj=unreal.EditorAssetLibrary.duplicate_asset(source,path)
    if not obj: raise RuntimeError('Duplicate failed: '+path)
    return obj

def set_fields(obj,fields):
    for key,value in fields.items(): obj.set_editor_property(key,value)

def info(node): return BP.get_node_infos([node])[0]
def norm(value): return ''.join(str(value).split()).lower()
def pin(node,name,output=True):
    matches=[p for p in (info(node).output_pins if output else info(node).input_pins) if norm(p.name)==norm(name)]
    if len(matches)!=1: raise RuntimeError(node.get_name()+': pin '+name+' not unique')
    return matches[0].pin_id
def link(a,out,b,inp): BP.connect_pins(pin(a,out),pin(b,inp,False))
def node(graph,kind,x=400,y=0):
    result=BP.create_node(graph,kind,unreal.IntPoint(x,y))
    if not result: raise RuntimeError('Node creation failed: '+kind)
    return result
def compile_ok(bp):
    BP.compile_blueprint(bp)
    if 'ERROR' in str(bp.get_editor_property('Status')): raise RuntimeError('Blueprint compile failed: '+bp.get_path_name())
def components(bp): return list(ActorTools.get_components(unreal.get_default_object(bp.generated_class())))
def add(bp,cls,name):
    existing=next((c for c in components(bp) if c.get_name()==name or c.get_name()==name+'_GEN_VARIABLE'),None)
    result=existing or ActorTools.add_component(bp,cls.static_class(),name)
    if not result: raise RuntimeError('Component creation failed: '+name)
    return result

# Generic managed props already implement the legacy interfaces, but their attach stub returned false.
generic=unreal.load_asset('/Game/Props/BP_ASM_Actor')
backup('/Game/Props/BP_ASM_Actor')
for name,native,args in [
    ('Request Attach to Component','Interaction|Carry|AttachforInteraction',[('Component','Parent'),('Socket Name','SocketName')]),
    ('Request Detach from Component','Interaction|Carry|DetachforInteraction',[])]:
    graph=BP.get_graph(generic,name)
    ns=list(BP.find_nodes(graph,''))
    entry=next(n for n in ns if n.get_class().get_name()=='K2Node_FunctionEntry')
    result=next(n for n in ns if n.get_class().get_name()=='K2Node_FunctionResult')
    call=next((n for n in ns if norm(info(n).type_id.split('|')[-1])==norm(native.split('|')[-1])),None) or node(graph,native)
    for source,target in args: link(entry,source,call,target)
    link(entry,'then',call,'execute'); link(call,'then',result,'execute')
    success=next(p for p in info(result).input_pins if p.type_id=='Boolean')
    BP.connect_pins(pin(call,'ReturnValue'),success.pin_id)
    for output in info(result).input_pins:
        if 'Object Reference' in output.type_id:
            self_node=node(graph,'Variables|GetaReferencetoSelf',200,200)
            BP.connect_pins(pin(self_node,'self'),output.pin_id)
graph=BP.get_graph(generic,'Can grab')
result=next(n for n in BP.find_nodes(graph,'') if n.get_class().get_name()=='K2Node_FunctionResult')
types=[t for t in BP.find_node_types(graph,'GetIsGrabable',[]) if norm(t.split('|')[-1])=='getisgrabable']
if len(types)!=1: raise RuntimeError('Native grabbable getter missing: '+str(types))
getter=node(graph,types[0],0,250)
value=next(p for p in info(getter).output_pins if p.type_id=='Boolean')
target=next(p for p in info(result).input_pins if p.type_id=='Boolean')
BP.connect_pins(value.pin_id,target.pin_id)
event=BP.add_event(generic,'Move Actor To Location',unreal.IntPoint(2000,0))
move=node(event.get_outer(),'Interaction|Carry|MoveHeldtoLocation',2400,0)
link(event,'then',move,'execute'); link(event,'Location',move,'Location')
compile_ok(generic)

# Refresh the target at the actual pickup decision, rather than using the press-start trace.
player=unreal.load_asset('/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter')
backup('/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter')
graph=BP.get_graph(player,'Grab and Intract')
ns=list(BP.find_nodes(graph,'')); by_name={n.get_name():n for n in ns}
switch=next(n for n in ns if norm(info(n).type_id.split('|')[-1])=='switchonepropgrabintent')
started=next(n for n in ns if norm(info(n).type_id.split('|')[-1])=='processgrabinput'
             and any(p.name=='Phase' and p.value=='Started' for p in info(n).input_pins))
link(by_name['K2Node_EnhancedInputAction_0'],'Started',started,'execute')
fresh=by_name['K2Node_VariableSet_20']
link(switch,'Grab',fresh,'execute'); link(fresh,'then',by_name['K2Node_Knot_8'],'InputPin')
if len(next(p for p in info(by_name['K2Node_EnhancedInputAction_0']).output_pins if p.name=='Started').connected_pins)!=1:
    raise RuntimeError('Started execution must have exactly one native gesture path')
compile_ok(player)

empty=duplicate('/Game/Assest/DataAssets/DA_EmptyBottel','DA_NormalEmptyBottle')
cap_mesh=unreal.load_asset('/Game/Assest/meshs/SKM/SM_BottelCap')
plastic={'BottleMaterialType':unreal.BottleMaterialType.PLASTIC,'FragmentAssembly':None,'FractureFragments':[],
         'bCanInflate':True,'bCanBend':True,'bCanDent':True,'bCanCrack':False,'DefaultCapType':unreal.BottleCapType.TWIST}
set_fields(empty,dict(plastic,**{'ItemDisplayName':unreal.Text('Empty plastic bottle'),'NumOfPortions':4,'DefaultContentID':0,'DefaultContentLevel':0.,
                  'DefaultCapSealed':0.,'DefaultCapMesh':cap_mesh,'SupportedCapMask':1,'bEnableTemperatureUpdate':True,'bEnableContinuousUpdateData':True}))
flavors=[('Lemonade','DA_BasicCola',2.5),('Mint','DA_GoodCola',3.),('Sweet','DA_SweetCola',3.25),('Spicy','DA_SpicyCola',4.5),('ZeroCalorie','DA_NormalCola',4.5)]
finished=[]; cylinders=[]
for index,(name,source,rate) in enumerate(flavors):
    product=duplicate('/Game/Assest/DataAssets/'+source,'DA_Normal'+name)
    display='Zero Calories Cola' if name=='ZeroCalorie' else name+' Cola'
    set_fields(product,dict(plastic,**{'ItemDisplayName':unreal.Text(display),'NumOfPortions':4,'Price':rate,'DefaultContentID':901+index,
                        'DefaultContentLevel':1.,'DefaultCapSealed':1.,'DefaultCapMesh':cap_mesh,'SupportedCapMask':1}))
    finished.append(product)
    cylinder=duplicate('/Game/Assest/DataAssets/'+source,'DA_Cylinder'+name)
    set_fields(cylinder,{'ItemDisplayName':unreal.Text(display+' cylinder'),'StaticMesh':unreal.load_asset('/Game/Assest/meshs/SM_GasSylender'),
                         'SkeletalMesh':None,'CustomActiveActorClass':generic.generated_class(),'NumOfPortions':40,'DefaultContentID':901+index,
                         'DefaultContentLevel':1.,'DefaultCapSealed':0.,'DefaultCapType':unreal.BottleCapType.NONE,'SupportedCapMask':0,
                         'FragmentAssembly':None,'FractureFragments':[],'bCanConsume':False,'bCanCrack':False,'BottleMaterialType':unreal.BottleMaterialType.HEAVY_METAL})
    cylinders.append(cylinder)
cap=duplicate('/Game/Assest/DataAssets/DA_EmptyBottel','DA_NormalTwistCap')
set_fields(cap,{'ItemDisplayName':unreal.Text('Compatible twist cap'),'StaticMesh':cap_mesh,'SkeletalMesh':None,'CustomActiveActorClass':generic.generated_class(),
                'NumOfPortions':0,'DefaultContentID':0,'DefaultContentLevel':0.,'DefaultCapSealed':0.,'DefaultCapType':unreal.BottleCapType.TWIST,
                'bIsBottleCap':True,'FragmentAssembly':None,'FractureFragments':[],'bCanConsume':False,'bCanCrack':False,'BottleMaterialType':unreal.BottleMaterialType.HEAVY_METAL})
catalog=unreal.load_asset(ROOT+'/DA_NormalColaSupplyCatalog') if unreal.EditorAssetLibrary.does_asset_exist(ROOT+'/DA_NormalColaSupplyCatalog') else None
if not catalog:
    factory=unreal.DataAssetFactory(); factory.set_editor_property('data_asset_class',unreal.ShopSupplyCatalog)
    catalog=tools.create_asset('DA_NormalColaSupplyCatalog',ROOT,unreal.ShopSupplyCatalog,factory)
if not catalog: raise RuntimeError('Supply catalog creation failed')
offers=[]
for name,label,kind,product,cost,flavor in [
    ('EmptyBottle','Empty plastic bottle',unreal.ShopSupplyKind.EMPTY_BOTTLE,empty,400,unreal.NormalColaFlavor.LEMONADE),
    ('TwistCap','Compatible twist cap',unreal.ShopSupplyKind.CAP,cap,50,unreal.NormalColaFlavor.LEMONADE)]+[
    ('Cylinder'+name,('Zero-Calorie' if name=='ZeroCalorie' else name)+' Cola cylinder',unreal.ShopSupplyKind.COLA_CYLINDER,cylinders[i],2000,
     [unreal.NormalColaFlavor.LEMONADE,unreal.NormalColaFlavor.MINT,unreal.NormalColaFlavor.SWEET,unreal.NormalColaFlavor.SPICY,unreal.NormalColaFlavor.ZERO_CALORIE][i])
    for i,(name,_,_) in enumerate(flavors)]:
    offer=unreal.ShopSupplyOffer(); set_fields(offer,{'OfferID':name,'DisplayName':unreal.Text(label),'Kind':kind,'Product':product,'UnitCostCents':cost,'Flavor':flavor}); offers.append(offer)
set_fields(catalog,{'Offers':offers,'EmptyBottleProduct':empty,'FinishedBottleProducts':finished})

# Reuse the existing interaction-only ATM adapter; keep the original ATM asset unchanged.
bench=duplicate('/Game/DirtyActors/BP_ATM','BP_NormalColaWorkbench')
for component in components(bench):
    if isinstance(component,unreal.ShopATMComponent):
        if not ActorTools.remove_component(component): raise RuntimeError('Could not remove ATM service from workbench copy')
mesh=next(c for c in components(bench) if isinstance(c,unreal.StaticMeshComponent))
mesh.set_static_mesh(unreal.load_asset('/Game/Assest/meshs/SM_HevyMetalDesk')); mesh.set_editor_property('relative_scale3d',unreal.Vector(2.5,2,1))
production=add(bench,unreal.ShopProductionStationComponent,'ProductionStation')
production.set_editor_property('bRequiresCooperativeCap',True)
filler=add(bench,unreal.BottleFillingComponent,'BottleFiller')
for name,tag,location in [('BottleInput','ShopBottleInput',(-55,0,95)),('CylinderInput','ShopCylinderInput',(55,0,95)),('CapInput','ShopCapInput',(-55,65,95))]:
    box=add(bench,unreal.BoxComponent,name)
    extent=(45,45,105) if name=='BottleInput' else (25,25,45) if name=='CylinderInput' else (22,22,25)
    set_fields(box,{'ComponentTags':[tag],'RelativeLocation':unreal.Vector(*location),'BoxExtent':unreal.Vector(*extent)})
    box.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    label=add(bench,unreal.TextRenderComponent,name+'Label'); label.set_text(name.removesuffix('Input'))
    label.set_editor_property('relative_location',unreal.Vector(location[0],location[1],145)); label.set_world_size(10.)
output=add(bench,unreal.SceneComponent,'SupplyOutput')
set_fields(output,{'ComponentTags':['ShopSupplyOutput'],'RelativeLocation':unreal.Vector(0,220,110),'RelativeScale3D':unreal.Vector(1,1,1)})
compile_ok(bench)
for obj in [generic,player,empty,cap,catalog,bench]+finished+cylinders:
    if not unreal.EditorAssetLibrary.save_loaded_asset(obj,False): raise RuntimeError('Save failed: '+obj.get_path_name())
    saved.append(obj.get_path_name())
(evidence/'NormalPrototypeAssets.json').write_text(json.dumps({'saved':saved,'flavors':[f[0] for f in flavors],
    'portion_count':4,'supply_cost_cents':{'bottle':400,'cap':50,'forty_portion_cylinder':2000},'existing_product_assets_unchanged':True},indent=2))
unreal.log('NORMAL_COLA_PROTOTYPE_ASSETS_SAVED')
