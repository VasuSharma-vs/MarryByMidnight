"""Connect the player's existing E Started event to native shop view interaction.

Only two nodes are inserted. Successful shop opens stop this event chain; the
false branch preserves the original non-shop interaction route. Run with the
editor closed after building the native TryOpenShopTerminalFromView function.
"""
import json
from pathlib import Path
import unreal
from editor_toolset.toolsets.blueprint import BlueprintTools as BP

out=Path(unreal.Paths.project_dir())/'Saved/CheckoutInteraction_20261112'
out.mkdir(parents=True,exist_ok=True)
bp=unreal.load_asset('/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter')
graph=BP.get_graph(bp,'Grab and Intract')
before=BP.get_node_infos(BP.find_nodes(graph))
(out/'BeforeEGraph.txt').write_text('\n'.join(n.export_text() for n in before))


def info(node):
    return BP.get_node_infos([node])[0]


def pin(node,output=False,name=None,kind=None):
    matches=[p for p in (info(node).output_pins if output else info(node).input_pins)
             if (name is None or p.name.lower()==name.lower()) and (kind is None or p.type_id==kind)]
    if len(matches)!=1:
        raise RuntimeError('Ambiguous pin on '+node.get_path_name()+': '+str((name,kind,matches)))
    return matches[0]


event=next(n for n in BP.find_nodes(graph) if info(n).type_id.endswith('EnhancedInputActionIA_Intract'))
started=pin(event,True,'Started')
existing=[n for n in BP.find_nodes(graph) if info(n).type_id.lower().endswith('|tryopenshopterminalfromview')]
if existing:
    if len(existing)!=1 or not any(c.node==existing[0] for c in started.connected_pins):
        raise RuntimeError('Existing native shop input differs from expected wiring; inspect before editing')
    unreal.log('CHECKOUT_INPUT_ALREADY_CONNECTED')
else:
    if len(started.connected_pins)!=1:
        raise RuntimeError('Expected exactly one existing E Started route')
    old_target=started.connected_pins[0]
    self_nodes=BP.find_nodes(graph,node_class=unreal.load_class(None,'/Script/BlueprintGraph.K2Node_Self'))
    if not self_nodes:
        raise RuntimeError('Existing self reference not found')
    native_types=BP.find_node_types(graph,'TryOpenShopTerminalFromView')
    if len(native_types)!=1:
        raise RuntimeError('Native shop entry is missing or ambiguous: '+str(native_types))
    position=info(event).position
    native=BP.create_node(graph,native_types[0],unreal.IntPoint(position.x+380,position.y-160))
    branch=BP.create_node(graph,'Utilities|FlowControl|Branch',unreal.IntPoint(position.x+730,position.y-160))
    self_pin=next(p for p in info(self_nodes[0]).output_pins if p.type_id!='Exec')
    BP.connect_pins(self_pin.pin_id,pin(native,name='Player').pin_id)
    BP.connect_pins(pin(native,True,kind='Boolean').pin_id,pin(branch,name='Condition').pin_id)
    BP.connect_pins(pin(native,True,kind='Exec').pin_id,pin(branch,kind='Exec').pin_id)
    BP.break_pins(started.pin_id,old_target)
    BP.connect_pins(started.pin_id,pin(native,kind='Exec').pin_id)
    false_pin=next(p for p in info(branch).output_pins if p.name.lower() in ['else','false'])
    BP.connect_pins(false_pin.pin_id,old_target)
    BP.compile_blueprint(bp)
    if 'ERROR' in str(bp.get_editor_property('status')):
        raise RuntimeError('Player Blueprint did not compile')
    if not unreal.EditorAssetLibrary.save_loaded_asset(bp,False):
        raise RuntimeError('Could not save player Blueprint')
    (out/'AfterEGraph.txt').write_text('\n'.join(n.export_text() for n in BP.get_node_infos([event,native,branch])))
    unreal.log('CHECKOUT_E_INPUT_SAVED')
