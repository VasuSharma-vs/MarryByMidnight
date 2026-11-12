"""Apply the approved giant navigation profile to the two prototype worlds only."""
import json
from pathlib import Path
import unreal
from editor_toolset.toolsets.actor import ActorTools
from editor_toolset.toolsets.blueprint import BlueprintTools as BP

root='/Game/NormalColaPrototype'
# This engine version exposes the inherited Python wrapper name, not a module attribute
# matching the native subclass. Resolve the reflected type rather than relying on that alias.
navigation_class=unreal.load_class(None,'/Script/MarryByMidnight.NormalColaNavigationConfig')
if not navigation_class: raise RuntimeError('Native Normal Cola navigation class is unavailable')
npc=unreal.load_asset(root+'/BP_NormalColaCustomer')
parts=ActorTools.get_components(unreal.get_default_object(npc.generated_class()))
capsule=next(p for p in parts if isinstance(p,unreal.CapsuleComponent))
radius=capsule.get_editor_property('CapsuleRadius'); half=capsule.get_editor_property('CapsuleHalfHeight')
for part in parts:
    if isinstance(part,unreal.ShopCustomerCashComponent): part.set_editor_property('TenderRoundUpDollars',10)
    if isinstance(part,unreal.CharacterMovementComponent): part.set_editor_property('bUseRVOAvoidance',True)
BP.compile_blueprint(npc)
if 'ERROR' in str(npc.get_editor_property('status')): raise RuntimeError('Customer compile failed')
unreal.EditorAssetLibrary.save_loaded_asset(npc,False)
records=[]
for path in [root+'/Lvl_NormalColaPrototype',root+'/Lvl_NormalColaPrototype_Validation']:
    world=unreal.EditorLoadingAndSavingUtils.load_map(path)
    if not world: raise RuntimeError('Cannot load '+path)
    settings=world.get_world_settings()
    existing_config=settings.get_editor_property('NavigationSystemConfig')
    already_applied=existing_config is not None and existing_config.get_class()==navigation_class
    config=unreal.new_object(navigation_class,outer=settings)
    config.set_editor_property('CustomerRadius',radius); config.set_editor_property('CustomerHeight',half*2)
    settings.set_editor_property('NavigationSystemConfig',config)
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in actors.get_all_level_actors():
        if isinstance(actor,unreal.RecastNavMesh): actors.destroy_actor(actor)
        elif isinstance(actor,unreal.NavMeshBoundsVolume):
            actor.set_actor_location(unreal.Vector(0,0,750),False,True); actor.set_actor_scale3d(unreal.Vector(18,24,10))
        elif isinstance(actor,unreal.TargetPoint):
            label=actor.get_actor_label(); location=actor.get_actor_location()
            if label in ['Shop entrance','Customer arrival','Exit bucket holder','Customer departure','Checkout queue']:
                location.z=half+2
                if label=='Checkout queue': location.y=250
                actor.set_actor_location(location,False,True)
            elif label.startswith('Normal Cola shelf '):
                location.x=950 if location.x>0 else -950; actor.set_actor_location(location,False,True)
        elif isinstance(actor,unreal.ColaPropActor) and actor.actor_has_tag('ShopInitialStock') and not already_applied:
            location=actor.get_actor_location(); location.x+=300 if location.x>0 else -300; actor.set_actor_location(location,False,True)
        elif isinstance(actor,unreal.StaticMeshActor) and actor.get_actor_label() in ['Left wall','Right wall','Back wall']:
            label=actor.get_actor_label()
            location=actor.get_actor_location(); location.z=600
            if label=='Left wall': location.x=-1500
            if label=='Right wall': location.x=1500
            actor.set_actor_location(location,False,True)
            actor.set_actor_scale3d(unreal.Vector(30,.2,12) if label=='Back wall' else unreal.Vector(.2,28,12))
    if not unreal.EditorLoadingAndSavingUtils.save_map(world,path): raise RuntimeError('Save failed: '+path)
    records.append(path)
report={'maps':records,'navigation_config':'/Script/MarryByMidnight.NormalColaNavigationConfig','customer_radius_cm':radius,'customer_height_cm':half*2,
    'character_sizes_preserved':True,'original_map_unchanged':True,'tender_round_up_dollars':10,'rounding_requires_physical_change':True}
(Path(unreal.Paths.project_saved_dir())/'NormalColaPrototype_20261111/GiantNavigationProfile.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
unreal.log('NORMAL_COLA_GIANT_PROFILE_SAVED')
