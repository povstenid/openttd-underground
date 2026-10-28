include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 510009
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 220000
    SAVE_ARTIFACT metro_loop_two_portals_dual_underground_final.sav
    FILES
        info.nut
        main.nut
)
