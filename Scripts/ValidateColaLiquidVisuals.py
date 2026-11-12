"""Temporary Editor/SIE visual fixture. Launch on /Engine/Maps/Entry; never save this level.
Creates isolated bottles, no shop customers or shop-save loading. Stop SIE to end the fixture.
"""
import builtins
import json
import time
from pathlib import Path
import unreal

output = Path(unreal.Paths.project_dir()) / 'Saved/LiquidPhysics_20261112/Visual'
output.mkdir(parents=True, exist_ok=True)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = editor.get_editor_world()
if not world.get_path_name().startswith('/Engine/Maps/Entry'):
    raise RuntimeError('Run this isolated fixture only on /Engine/Maps/Entry')
world.get_world_settings().set_editor_property('default_game_mode', unreal.GameModeBase)
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(100, 0, -10))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
floor.set_actor_scale3d(unreal.Vector(30, 20, .2))
sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, -200, 700), unreal.Rotator(-45, -30, 0))
sun.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(6)
sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 800))
sky_component = sky.get_component_by_class(unreal.SkyLightComponent)
sky_component.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
sky_component.set_editor_property('cubemap', unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
sky_component.set_intensity(1)
manager = actors.spawn_actor_from_class(unreal.PropManager, unreal.Vector(0, 0, 0))
manager.set_editor_property('bEnableAutosave', False)
manager.set_editor_property('InitialSaveSlot', '')
manager.set_editor_property('AutosaveSlot', 'Automation_LiquidVisual')
unreal.EditorLevelLibrary.set_level_viewport_camera_info(unreal.Vector(100, -1150, 400), unreal.Rotator(-10, 90, 0))

class Session:
    def __init__(self):
        self.game_world = None
        self.manager = None
        self.bottles = []
        self.started = 0
        self.cycle = -1
        self.last_sample = -1
        self.samples = []
        self.specs = [(.05, 0, -500, 0, 40), (.25, 65, -280, 0, 150),
                      (.5, 90, -40, 0, 150), (.9, 180, 200, 0, 240),
                      (.5, 0, 450, 0, 650), (.5, 0, 700, 0, 130)]

    def write(self):
        (output / 'Runtime.json').write_text(json.dumps({'samples': self.samples, 'actors': [
            {'isid': i, 'path': self.manager.get_active_actor_from_isid(i).get_path_name()
             if self.manager and self.manager.get_active_actor_from_isid(i) else None} for i in self.bottles]}, indent=2))

    def tick(self, delta):
        try:
            if self.game_world is None:
                self.game_world = editor.get_game_world()
                if self.game_world is None: return
                self.manager = unreal.GameplayStatics.get_actor_of_class(self.game_world, unreal.PropManager)
                if self.manager is None: return
                product = unreal.load_asset('/Game/NormalColaPrototype/DA_NormalMint')
                pid = self.manager.register_product_from_data_asset(product)
                for fill, pitch, x, y, z in self.specs:
                    transform = unreal.Transform(location=unreal.Vector(x,y,z), rotation=unreal.Rotator(pitch,0,0))
                    identity = self.manager.generate_product_instance(pid, transform)
                    event = unreal.PropEventSyncDelta()
                    event.set_editor_property('ISID', identity)
                    event.set_editor_property('DirtyMask', 1)
                    event.set_editor_property('ContentLevel', fill)
                    if not self.manager.submit_instance_event_update(identity, event): raise RuntimeError('Could not set test fill')
                    self.bottles.append(identity)
                self.started = time.perf_counter()
            if editor.get_game_world() is None:
                self.write()
                unreal.unregister_slate_post_tick_callback(self.handle)
                return
            elapsed = time.perf_counter()-self.started
            cycle = int(elapsed/16)
            phase = elapsed-cycle*16
            if cycle != self.cycle:
                self.cycle = cycle
                for index, (identity, spec) in enumerate(zip(self.bottles, self.specs)):
                    fill, pitch, x,y,z = spec
                    self.manager.set_instance_to_unsettled(identity, unreal.Vector(), unreal.Vector(), 60)
                    bottle = self.manager.get_active_actor_from_isid(identity)
                    bottle.set_actor_location(unreal.Vector(x,y,z), False, True)
                    bottle.set_actor_rotation(unreal.Rotator(pitch,0,0), True)
                    mesh = bottle.get_editor_property('PropMesh')
                    mesh.set_enable_gravity(index == 4)
                    mesh.set_physics_linear_velocity(unreal.Vector(80,0,-200) if index == 4 else unreal.Vector())
                    mesh.set_physics_angular_velocity_in_radians(unreal.Vector(.5,1.5,0) if index == 4 else (unreal.Vector(0,0,5) if index == 5 else unreal.Vector()))
                    if index == 5: mesh.set_angular_damping(0)
            if phase > 5:
                spinning = self.manager.get_active_actor_from_isid(self.bottles[5])
                if spinning: spinning.get_editor_property('PropMesh').set_physics_angular_velocity_in_radians(unreal.Vector())
            if elapsed-self.last_sample > .2:
                self.last_sample = elapsed
                for index in [4,5]:
                    bottle = self.manager.get_active_actor_from_isid(self.bottles[index])
                    if bottle and bottle.get_editor_property('DynamicMaterialInstance'):
                        mid = bottle.get_editor_property('DynamicMaterialInstance')
                        normal = mid.get_vector_parameter_value('Liquid Surface Normal WS')
                        motion = mid.get_vector_parameter_value('Liquid Motion State')
                        self.samples.append({'time': round(elapsed,3), 'case': index, 'phase': round(phase,3),
                                             'z': bottle.get_actor_location().z, 'normal': [normal.r,normal.g,normal.b],
                                             'energy': motion.r, 'spin': motion.g})
                self.write()
        except Exception as error:
            (output / 'Error.txt').write_text(str(error))
            unreal.log_error('Liquid visual fixture: ' + str(error))
            unreal.unregister_slate_post_tick_callback(self.handle)

builtins.cola_liquid_visual_session = Session()
builtins.cola_liquid_visual_session.handle = unreal.register_slate_post_tick_callback(builtins.cola_liquid_visual_session.tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
unreal.log('LIQUID_VISUAL_FIXTURE_STARTED')
