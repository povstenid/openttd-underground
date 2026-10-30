include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 510071
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 20000
    SAVE_ARTIFACT metro_hillside_portal_final.sav
    FILES
        info.nut
        main.nut
)
