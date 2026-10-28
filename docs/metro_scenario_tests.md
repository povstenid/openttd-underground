# Metro Scenario Tests

GS-only metro autotests live in `regression/scenarios/metro_*` and report their result via `GSTest.Report(...)`.

## What is covered

- Surface rail/station/depot/signal construction
- Underground rail and underground station construction
- Surface to underground portal linking
- Launching a train on a surface line
- Launching a train through an underground station and back to surface
- Regression check: removing an underground station preserves the surface exit complex
- Regression check: underground signal place/remove preserves track
- Regression check: portal connectivity persists through signal changes

## Main files

- API: `src/script/api/script_metro.hpp`
- GS helper library: `bin/game/library/metrotest/`
- Metro scenarios: `regression/scenarios/metro_*`
- Scenario runner: `cmake/CreateScenario.cmake`

## Run the metro suite

```bash
cmake -S . -B build -DOPTION_DEDICATED=ON
cmake --build build --target openttd scenario_files gs_library_files -j4
ctest --test-dir build -R scenario_metro_ --output-on-failure
```

Each test writes a normalized JSON result:

```text
build/scenario_metro_<suite>_result.json
```

Examples:

- `scenario_metro_train_launch_underground_result.json`
- `scenario_metro_regression_signal_roundtrip_result.json`

## Result format

Every scenario must emit exactly one line:

```text
OTTD_TEST_RESULT <json>
```

The JSON contains:

- `suite`
- `fixture`
- `seed`
- `tick`
- `status`
- `message`
- `checks`
- `metrics`

`status != "pass"` fails the CTest.

## Environment note

Scenario tests require a usable base graphics set.

The runner first looks in `build/baseset/`. If OpenGFX is not there, it also checks:

- `OPENTTD_BASESET_DIR`
- `~/.local/share/openttd/baseset`
- `~/Documents/OpenTTD/baseset`

If you already have OpenGFX somewhere else, the simplest local setup is:

```bash
export OPENTTD_BASESET_DIR=/path/to/opengfx/baseset
```
