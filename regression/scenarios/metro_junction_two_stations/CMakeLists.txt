include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 510777
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 110000
    SAVE_ARTIFACT metro_junction_two_stations_final.sav
    FILES
        info.nut
        main.nut
)
