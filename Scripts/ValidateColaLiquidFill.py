"""Unsaved Entry-map visual fixture; never modifies the shop or its saves.

Top row: native ASMs. Bottom row: five transforms in ONE managed ISM.
Both rows: half fill, upright/45/90/135/180 degrees. Side row: other fills.
Launch editor on /Engine/Maps/Entry, execute this script, then capture the viewport.
"""
import builtins
import json
import time
from pathlib import Path
import unreal

output = Path(unreal.Paths.project_dir()) / 'Saved/LiquidFillFix_20261112/Visual'
output.mkdir(parents=True, exist_ok=True)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = editor.get_editor_world()
if not world.get_path_name().startswith('/Engine/Maps/Entry'):
    raise RuntimeError('Use only an isolated Entry-map session')
world.get_world_settings().set_editor_property('default_game_mode', unreal.GameModeBase)
# Entry can contain old preview fixtures. Clear only this unsaved test world.
for actor in actors.get_all_level_actors():
    if not isinstance(actor, (unreal.WorldSettings, unreal.LevelScriptActor)):
        actors.destroy_actor(actor)
sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0,-500,1000), unreal.Rotator(pitch=-35,yaw=-40,roll=0))
sun.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(6)
sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0,0,1000))
light = sky.get_component_by_class(unreal.SkyLightComponent)
light.set_editor_property('source_type', unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
light.set_editor_property('cubemap', unreal.load_asset('/Engine/MapTemplates/Sky/DaylightAmbientCubemap'))
light.set_intensity(1)
manager = actors.spawn_actor_from_class(unreal.PropManager, unreal.Vector())
manager.set_editor_property('bEnableAutosave', False)
manager.set_editor_property('InitialSaveSlot', '')
manager.set_editor_property('AutosaveSlot', 'Automation_LiquidFillVisual')
unreal.EditorLevelLibrary.set_level_viewport_camera_info(unreal.Vector(0,-1350,450), unreal.Rotator(pitch=0,yaw=90,roll=0))


class FillSession:
    def __init__(self):
        self.game_world = None
        self.manager = None
        self.records = []
        self.started = None
        self.last_report = 0

    def write(self):
        (output/'Runtime.json').write_text(json.dumps({'records': self.records}, indent=2))

    def tick(self, delta):
        try:
            current = editor.get_game_world()
            if current is None:
                if self.game_world is not None:
                    self.write()
                    unreal.unregister_slate_post_tick_callback(self.handle)
                return
            if self.game_world is None:
                self.game_world = current
                self.manager = unreal.GameplayStatics.get_actor_of_class(current, unreal.PropManager)
                if self.manager is None:
                    self.game_world = None
                    return
                product = unreal.load_asset('/Game/NormalColaPrototype/DA_NormalMint')
                pid = self.manager.register_product_from_data_asset(product)
                specs = []
                for row, height in [('ASM',750),('ISM',450)]:
                    specs.extend((row,.5,pitch,-480+240*i,height) for i,pitch in enumerate([0,45,90,135,180]))
                specs.extend(('ISM',fill,0,-480+240*i,150) for i,fill in enumerate([0,.25,.5,.75,1]))
                for row,fill,pitch,x,z in specs:
                    rotation = unreal.Rotator(pitch=pitch,yaw=0,roll=0)
                    up = unreal.MathLibrary.get_up_vector(rotation)
                    position = unreal.Vector(x-up.x*93.29428,-up.y*93.29428,z-up.z*93.29428)
                    transform = unreal.Transform(location=position,rotation=rotation)
                    identity = self.manager.generate_product_instance(pid,transform)
                    event = unreal.PropEventSyncDelta()
                    event.set_editor_property('ISID',identity)
                    event.set_editor_property('DirtyMask',1)
                    event.set_editor_property('ContentLevel',fill)
                    if not self.manager.submit_instance_event_update(identity,event):
                        raise RuntimeError('Could not set test contents')
                    component = self.manager.get_component_from_isid(identity)
                    record = {'representation':row,'fill':fill,'pitch':pitch,'isid':identity,
                              'component':component.get_path_name(),'transform':str(transform)}
                    if row == 'ASM':
                        if not self.manager.set_instance_to_unsettled(identity,unreal.Vector(),unreal.Vector(),600):
                            raise RuntimeError('Could not promote visual ASM')
                        bottle = self.manager.get_active_actor_from_isid(identity)
                        mesh = bottle.get_editor_property('PropMesh')
                        mesh.set_enable_gravity(False)
                        mesh.set_simulate_physics(False)
                        record['actor'] = bottle.get_path_name()
                    self.records.append(record)
                self.started = time.perf_counter()
                self.write()
                unreal.log('LIQUID_FILL_VISUAL_READY')
            if time.perf_counter()-self.last_report > 2:
                self.last_report = time.perf_counter()
                for record in self.records:
                    bottle = self.manager.get_active_actor_from_isid(record['isid'])
                    record['currently_active'] = bottle is not None
                    if bottle:
                        mid = bottle.get_editor_property('DynamicMaterialInstance')
                        if mid:
                            record['material_fill'] = mid.get_scalar_parameter_value('Liquid Level')
                            record['instance_mode'] = mid.get_scalar_parameter_value('Use Instance Liquid Data')
                            record['plane'] = mid.get_scalar_parameter_value('Liquid Plane Offset')
                self.write()
        except Exception as error:
            (output/'Error.txt').write_text(str(error))
            unreal.log_error('Liquid fill visual fixture: ' + str(error))
            unreal.unregister_slate_post_tick_callback(self.handle)


builtins.cola_fill_session = FillSession()
builtins.cola_fill_session.handle = unreal.register_slate_post_tick_callback(builtins.cola_fill_session.tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_play_simulate()
