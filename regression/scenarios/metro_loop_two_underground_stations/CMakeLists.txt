include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 510888
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 110000
    SAVE_ARTIFACT metro_loop_two_underground_stations_final.sav
    FILES
        info.nut
        main.nut
)
