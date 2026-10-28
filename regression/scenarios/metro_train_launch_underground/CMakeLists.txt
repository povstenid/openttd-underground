include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 510005
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 180000
    FILES
        info.nut
        main.nut
)
