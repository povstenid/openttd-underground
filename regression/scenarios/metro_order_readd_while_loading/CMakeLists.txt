include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 510903
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 120000
    SAVE_ARTIFACT metro_order_readd_while_loading_final.sav
    FILES
        info.nut
        main.nut
)
