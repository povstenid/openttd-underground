include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 610202
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 70000
    SAVE_ARTIFACT metro_level_crossing_above_underground_final.sav
    FILES
        info.nut
        main.nut
)
