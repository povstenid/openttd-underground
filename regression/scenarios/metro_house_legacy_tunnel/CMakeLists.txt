include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    FIXTURE ${CMAKE_CURRENT_SOURCE_DIR}/fixture.sav
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 20000
    FILES
        info.nut
        main.nut
)
