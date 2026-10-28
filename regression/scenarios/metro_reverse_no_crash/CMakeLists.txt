include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 510123
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 95000
    SAVE_ARTIFACT metro_reverse_no_crash_final.sav
    FILES
        info.nut
        main.nut
)
