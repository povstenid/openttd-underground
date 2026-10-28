include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 510011
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 320000
    SAVE_ARTIFACT metro_industry_cargo_via_underground_final.sav
    FILES
        info.nut
        main.nut
)
