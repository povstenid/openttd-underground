include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 610203
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 40000
    SAVE_ARTIFACT metro_build_over_running_underground_final.sav
    FILES
        info.nut
        main.nut
)
