include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE AI
    SEED 424242
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    FILES
        info.nut
        main.nut
)
