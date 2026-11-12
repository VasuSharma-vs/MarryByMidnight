"""Run compatibility repair and the read-only prototype configuration audit in one editor process."""
from pathlib import Path
import runpy

scripts = Path(__file__).resolve().parent
runpy.run_path(str(scripts / 'EnableBottleMorphMaterialUsage.py'), run_name='__main__')
runpy.run_path(str(scripts / 'ValidateNormalColaPrototypeAssets.py'), run_name='__main__')
