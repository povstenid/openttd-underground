include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 510211
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 40000
    SAVE_ARTIFACT metro_industry_foundations_final.sav
    FILES
        info.nut
        main.nut
)
