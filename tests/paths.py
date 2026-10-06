"""Repository paths for the Python tests (run: python3 -m unittest discover -s tests)."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
if str(ROOT / 'scripts') not in sys.path:
    sys.path.insert(0, str(ROOT / 'scripts'))
# Executables carry .exe on Windows.
EXE_SUFFIX = '.exe' if sys.platform == 'win32' else ''
