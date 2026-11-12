"""Compatibility entry point for the current liquid material repair.

The historical appearance upgrade predates the designer's reorganized graph.
Delegate to the scoped fill repair so rerunning this command preserves that work.
"""
import runpy
from pathlib import Path

runpy.run_path(str(Path(__file__).with_name('FixColaLiquidFill.py')), run_name='__main__')
