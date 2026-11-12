"""Create a disposable twin of the prototype for real multiplayer/navigation acceptance."""
import unreal
path='/Game/NormalColaPrototype/Lvl_NormalColaPrototype_Validation'
if unreal.EditorAssetLibrary.does_asset_exist(path):
    raise RuntimeError('Validation world exists; inspect rather than overwrite it.')
if not unreal.EditorAssetLibrary.duplicate_asset('/Game/NormalColaPrototype/Lvl_NormalColaPrototype',path):
    raise RuntimeError('Cannot duplicate prototype world')
world=unreal.EditorLoadingAndSavingUtils.load_map(path)
if not world: raise RuntimeError('Cannot load validation world')
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if isinstance(actor,unreal.PropManager):
        actor.set_editor_property('InitialSaveSlot','')
        actor.set_editor_property('bEnableAutosave',False)
        actor.set_editor_property('AutosaveSlot','NormalColaPrototypeValidation_NoAutosave')
unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.NormalColaPrototypeTestActor,unreal.Vector())
if not unreal.EditorLoadingAndSavingUtils.save_map(world,path): raise RuntimeError('Cannot save validation world')
unreal.log('NORMAL_COLA_VALIDATION_MAP_SAVED '+path)
