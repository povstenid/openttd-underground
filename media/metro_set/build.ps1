# Builds metro_set.grf: needs Python with Pillow and numpy, and nmlc (pip install nml).
# Pass -Python / -Nmlc to use a virtual environment, e.g. -Python venv\Scripts\python.exe
param([string]$Python = "python", [string]$Nmlc = "nmlc")
$ErrorActionPreference = "Stop"
Push-Location $PSScriptRoot
try {
	& $Python generate.py
	if ($LASTEXITCODE -ne 0) { throw "generate.py failed" }
	& $Nmlc -c --grf=metro_set.grf metro_set.nml
	if ($LASTEXITCODE -ne 0) { throw "nmlc failed" }
	& $Python generate.py --fix-grf metro_set.grf
	if ($LASTEXITCODE -ne 0) { throw "generate.py --fix-grf failed" }
} finally {
	Pop-Location
}
