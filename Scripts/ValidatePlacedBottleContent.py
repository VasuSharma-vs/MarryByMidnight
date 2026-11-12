"""Exercise EXISTING Lvl_FirstPerson bottles in a fresh, unsaved SIE session.

The first original BP_Cola gets a reproducible half fill; the other originals
sample the new debug range. No bottles or replacement level are created.
Commands in Saved/PlacedBottleFill_20261112/Control.json: capture, promote,
demote, rotate, stop. Each command needs a distinct nonce. No assets are saved.
"""
import builtins
import json
import time
from pathlib import Path
import unreal

out=Path(unreal.Paths.project_dir())/'Saved/PlacedBottleFill_20261112'
out.mkdir(parents=True,exist_ok=True)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world=editor.get_editor_world() or editor.get_game_world()
if world is None or not world.get_path_name().split('.')[0].endswith('Lvl_FirstPerson'):
    raise RuntimeError('Requires the actual Lvl_FirstPerson map')


def prop(obj,name):
    return obj.get_editor_property(name)


class Session:
    def __init__(self):
        self.game=None
        self.manager=None
        self.start=time.perf_counter()
        self.last_sample=0
        self.nonce=None
        self.phase='waiting'
        self.records=[]
        self.original=[]
        self.restored=False
        sources=sorted([a for a in actors.get_all_level_actors()
                        if a.get_class().get_path_name()=='/Game/Props/Colas/BP_Cola.BP_Cola_C'],key=lambda a:a.get_name())
        if not sources:
            raise RuntimeError('No original BP_Cola actors found')
        for i,actor in enumerate(sources):
            names=['bDebugRandomizeContentLevelOnISMRegistration','DebugMinInitialContentLevel','DebugMaxInitialContentLevel']
            self.original.append((actor,{n:prop(actor,n) for n in names}))
            actor.set_editor_property(names[0],True)
            actor.set_editor_property(names[1],.5 if i==0 else .15)
            actor.set_editor_property(names[2],.5 if i==0 else .85)
            self.records.append({'source':actor.get_name(),'position':[actor.get_actor_location().x,actor.get_actor_location().y,actor.get_actor_location().z],
                                 'fixed_half':i==0,'isid':None})
        for actor in actors.get_all_level_actors():
            if isinstance(actor,unreal.PropManager):
                names=['InitialSaveSlot','bEnableAutosave','AutosaveSlot']
                self.original.append((actor,{n:prop(actor,n) for n in names}))
                actor.set_editor_property('InitialSaveSlot','')
                actor.set_editor_property('bEnableAutosave',False)
                actor.set_editor_property('AutosaveSlot','Automation_PlacedContent')
        (out/'Control.json').write_text(json.dumps({'action':'none','nonce':0}))

    def restore_settings(self):
        if not self.restored:
            for obj,values in self.original:
                for name,value in values.items():
                    obj.set_editor_property(name,value)
            self.restored=True

    def write(self):
        (out/'Runtime.json').write_text(json.dumps({'phase':self.phase,'records':self.records},indent=2,default=str))

    def sample(self):
        registry=prop(self.manager,'ISIDData')
        for row in self.records:
            if row['isid'] is None:
                expected=unreal.Vector(*row['position'])
                for identity,record in registry.items():
                    pose=prop(record,'ActiveTransform')
                    if (pose.translation-expected).length()<1:
                        row['isid']=identity
                        row['original_transform']=pose
                        break
            identity=row['isid']
            if identity is None:
                continue
            scan=self.manager.get_product_scan_data(identity)
            if isinstance(scan,tuple):
                scan=next(item for item in scan if isinstance(item,unreal.ShopProductScanData))
            if not isinstance(scan,unreal.ShopProductScanData):
                raise RuntimeError('Scanner could not resolve original bottle '+str(identity))
            row['content_level']=prop(scan,'ContentLevel')
            component=self.manager.get_component_from_isid(identity)
            index=prop(component,'ISIDToISMI')[identity]
            stride=prop(component,'NumCustomDataFloats')
            gpu=prop(component,'PerInstanceSMCustomData')
            row['ism_level']=gpu[index*stride]
            row['ism_plane']=gpu[index*stride+4]
            row['ism_component']=component.get_path_name()
            active=self.manager.get_active_actor_from_isid(identity)
            row['active']=active is not None
            mid=prop(active,'DynamicMaterialInstance') if active else component.get_material(1)
            if mid and isinstance(mid,unreal.MaterialInstanceDynamic):
                row['material_level']=mid.get_scalar_parameter_value('Liquid Level')
                row['instance_mode']=mid.get_scalar_parameter_value('Use Instance Liquid Data')
            row['matches']=abs(row['content_level']-row['ism_level'])<.0001

    def command(self,cmd):
        row=next(r for r in self.records if r['fixed_half'])
        identity=row['isid']
        if identity is None:
            raise RuntimeError('Original half-fill bottle not registered yet')
        action=cmd['action']
        if action=='promote':
            if not self.manager.set_instance_to_unsettled(identity,unreal.Vector(),unreal.Vector(),600):
                raise RuntimeError('Original bottle promotion failed')
            active=self.manager.get_active_actor_from_isid(identity)
            mesh=prop(active,'PropMesh')
            mesh.set_enable_gravity(False)
            mesh.set_simulate_physics(False)
            self.phase='ASM'
        elif action=='demote':
            if not self.manager.set_instance_to_rest(identity):
                raise RuntimeError('Original bottle demotion failed')
            self.phase='ISM_returned'
        elif action=='rotate':
            pose=row['original_transform']
            center=pose.translation+unreal.Vector(0,0,93.29428)
            rot=unreal.Rotator(pitch=cmd.get('pitch',0),yaw=0,roll=0)
            up=unreal.MathLibrary.get_up_vector(rot)
            position=center-up*93.29428
            transform=unreal.Transform(location=position,rotation=rot,scale=pose.scale3d)
            active=self.manager.get_active_actor_from_isid(identity)
            if active:
                active.set_actor_transform(transform,False,True)
            else:
                self.manager.get_component_from_isid(identity).set_instance_transform_by_isid(identity,transform,True,True,True)
            self.phase=('ASM' if active else 'ISM')+'_pitch_'+str(cmd.get('pitch',0))
        elif action=='capture':
            position=unreal.Vector(*row['position'])
            offset=cmd.get('offset',[240,-420,190])
            camera=position+unreal.Vector(*offset)
            rotation=unreal.MathLibrary.find_look_at_rotation(camera,position+unreal.Vector(0,0,93.29428))
            unreal.EditorLevelLibrary.set_level_viewport_camera_info(camera,rotation)
            filename=str(out/(cmd.get('name',self.phase)+'.png'))
            self.capture=unreal.AutomationLibrary.take_high_res_screenshot(1400,1000,filename,delay=1.0)
        elif action=='stop':
            self.restore_settings()
            self.phase='stopping'
            self.write()
            level.editor_request_end_play()

    def tick(self,dt):
        try:
            current=editor.get_game_world()
            if current is None:
                if self.game is not None:
                    self.restore_settings()
                    self.phase='stopped'
                    self.write()
                    unreal.unregister_slate_post_tick_callback(self.handle)
                    # Discard only this fixture's editor-memory changes and reload
                    # the saved original. No designer asset is written.
                    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
                    level.load_level('/Game/FirstPerson/Lvl_FirstPerson')
                return
            self.game=current
            if self.manager is None:
                self.manager=unreal.GameplayStatics.get_actor_of_class(current,unreal.PropManager)
                if self.manager is None:
                    return
                self.manager.set_editor_property('bEnableAutosave',False)
                self.restore_settings()
            if time.perf_counter()-self.last_sample>1:
                self.last_sample=time.perf_counter()
                self.sample()
                if self.phase=='waiting' and all(r['isid'] for r in self.records):
                    self.phase='ISM'
                    unreal.log('PLACED_CONTENT_READY')
                cmd=json.loads((out/'Control.json').read_text())
                if cmd.get('nonce')!=self.nonce:
                    self.nonce=cmd.get('nonce')
                    if cmd.get('action') not in ['none',None]:
                        self.command(cmd)
                self.write()
        except Exception as error:
            self.restore_settings()
            (out/'Error.txt').write_text(str(error))
            unreal.log_error('Placed bottle content validation: '+str(error))
            unreal.unregister_slate_post_tick_callback(self.handle)


builtins.placed_content_session=Session()
builtins.placed_content_session.handle=unreal.register_slate_post_tick_callback(builtins.placed_content_session.tick)
level.editor_play_simulate()
