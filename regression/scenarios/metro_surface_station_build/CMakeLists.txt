include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 510001
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 40000
    FILES
        info.nut
        main.nut
)
