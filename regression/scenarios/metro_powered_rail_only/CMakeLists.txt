include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 510933
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 120000
    SAVE_ARTIFACT metro_powered_rail_only_final.sav
    FILES
        info.nut
        main.nut
)
