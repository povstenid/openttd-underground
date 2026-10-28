include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 510010
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 260000
    SAVE_ARTIFACT metro_passenger_acceptance_via_exit_final.sav
    FILES
        info.nut
        main.nut
)
