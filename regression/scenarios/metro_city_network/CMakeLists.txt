include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 510901
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 400000
    TIMEOUT 300
    SAVE_ARTIFACT metro_city_network_final.sav
    FILES
        info.nut
        main.nut
)
