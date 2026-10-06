"""Windows: run.sh puts this directory on PYTHONPATH. The scripts' output is read by bash, which
expects '\\n' line ends; Python on Windows would write '\\r\\n'."""
import sys

for stream in (sys.stdout, sys.stderr):
    try:
        stream.reconfigure(newline='\n')
    except (AttributeError, ValueError):
        pass
