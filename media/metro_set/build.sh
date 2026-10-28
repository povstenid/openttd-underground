#!/bin/sh
# Builds metro_set.grf: needs python3 with Pillow and numpy, and nmlc (pip install nml).
set -e
cd "$(dirname "$0")"
python3 generate.py
nmlc -c --grf=metro_set.grf metro_set.nml
python3 generate.py --fix-grf metro_set.grf
