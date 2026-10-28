include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 510901
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 90000
    SAVE_ARTIFACT metro_station_stop_persists_final.sav
    FILES
        info.nut
        main.nut
)
