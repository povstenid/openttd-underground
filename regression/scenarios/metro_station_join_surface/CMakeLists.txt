include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 510093
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 20000
    SAVE_ARTIFACT metro_station_join_surface_before.sav
    FILES
        info.nut
        main.nut
)
