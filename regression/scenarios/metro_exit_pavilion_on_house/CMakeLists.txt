include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 510098
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 30000
    FILES
        info.nut
        main.nut
)
