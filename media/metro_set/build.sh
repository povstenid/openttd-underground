#!/bin/sh
# Builds metro_set.grf: needs python3 with Pillow and numpy, and nmlc (pip install nml).
set -e
cd "$(dirname "$0")"
python3 generate.py
nmlc --grf=metro_set.grf metro_set.nml
