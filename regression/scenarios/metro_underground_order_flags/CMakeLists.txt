include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 510902
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 90000
    SAVE_ARTIFACT metro_underground_order_flags_final.sav
    FILES
        info.nut
        main.nut
)
