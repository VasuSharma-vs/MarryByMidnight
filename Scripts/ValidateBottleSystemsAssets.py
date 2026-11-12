"""Cold read-only native/Blueprint/material checks for the bottle systems update.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>, editor closed.
Compiles in memory and writes diagnostics only; never saves artist assets/maps.
"""
import json
from pathlib import Path
import unreal
from editor_toolset.toolsets.material import MaterialTools
from editor_toolset.toolsets.object import ObjectTools

output=Path(unreal.Paths.project_dir())/'Saved/BottleSystemsUpdate_20261112'
output.mkdir(parents=True,exist_ok=True)
report={'checks':[],'blueprints':[],'material':{},'scope':'Saved assets and GPU shader compilation; not gameplay acceptance'}

def check(label,passed,detail=None):
    report['checks'].append({'check':label,'passed':bool(passed),'detail':detail})

for name in ['EmptyBottle','Lemonade','Mint','Sweet','Spicy','ZeroCalorie']:
    product=unreal.load_asset('/Game/NormalColaPrototype/DA_Normal'+name)
    check(name+' product exists',product is not None)
    if product:
        values=json.loads(ObjectTools.get_properties(product,['BottleMass','PortionVolumeMl','LiquidDensityGPerMl','NumOfPortions']))
        check(name+' approved lightweight shell',abs(values['BottleMass']-.05)<1.e-6,values)
        check(name+' physical portion and density',values['PortionVolumeMl']>0 and values['LiquidDensityGPerMl']>0)

for path in ['/Game/Props/Colas/BP_Cola','/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter','/Game/DirtyActors/BP_CheckoutCounter','/Game/DirtyActors/BP_ProductSceaner']:
    bp=unreal.load_asset(path)
    if not bp:
        check(path+' exists',False)
        continue
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    state=str(bp.get_editor_property('status'))
    report['blueprints'].append({'path':path,'status':state})
    check(path+' compiles','BS_ERROR' not in state.upper(),state)

material=unreal.load_asset('/Game/Assest/Materials/ColaMaterials/Liquied/M_BaseLiquid')
nodes=list(MaterialTools.get_expressions(material))
custom={n.get_name():json.loads(ObjectTools.get_properties(n,['Code']))['Code'] for n in nodes if isinstance(n,unreal.MaterialExpressionCustom)}
shader=custom.get('MaterialExpressionCustom_3','')
check('Liquid shader explicitly masks empty content','if (fill <= 0.0001) mask = 0' in shader)
check('Existing liquid motion/waves retained','MotionState' in shader and 'vortexDepth' in shader)
report['material']['nodes']=len(nodes)
report['material']['custom_nodes']=custom
MaterialTools.recompile(material)
report['material']['recompile']='success'
task=unreal.AssetExportTask(); task.object=material; task.filename=str(output/'VerifiedLiquidMaterial.t3d'); task.automated=True; task.prompt=False
unreal.Exporter.run_asset_export_task(task)
report['failed']=[c['check'] for c in report['checks'] if not c['passed']]
(output/'AssetChecks.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
if report['failed']:
    raise RuntimeError('Bottle asset checks failed: '+str(report['failed']))
unreal.log('BOTTLE_SYSTEMS_ASSET_CHECKS_PASS '+str(len(report['checks'])))
