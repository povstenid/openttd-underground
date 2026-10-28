include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 610205
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 60000
    SAVE_ARTIFACT metro_block_signals_above_underground_final.sav
    FILES
        info.nut
        main.nut
)
