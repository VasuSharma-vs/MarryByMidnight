"""Create the isolated multiplayer fragment validation map with a fresh native build."""
import unreal

path = '/Game/ColaShopTests/FragmentReplication'
if unreal.EditorAssetLibrary.does_asset_exist(path):
    raise RuntimeError('Test map already exists; inspect it instead of overwriting it.')
world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
if not world:
    raise RuntimeError('Could not create the isolated test world')
settings = world.get_world_settings()
settings.set_editor_property('default_game_mode', unreal.GameModeBase.static_class())
floor = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -5))
floor.set_actor_label('FragmentTestGround')
floor.static_mesh_component.set_static_mesh(unreal.EditorAssetLibrary.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(20, 20, .1))
manager = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.PropManager, unreal.Vector(0, 0, 0))
manager.set_editor_property('bEnableAutosave', False)
fixture = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.FragmentNetworkTestActor, unreal.Vector(0, 0, 0))
fixture.set_actor_label('FragmentNetworkFixture')
unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(500, 0, 150))
if not unreal.EditorLoadingAndSavingUtils.save_map(world, path):
    raise RuntimeError('Failed to save isolated fragment map')
unreal.log('FRAGMENT_TEST_MAP_CREATED ' + path + ' fixture=' + fixture.get_name())
