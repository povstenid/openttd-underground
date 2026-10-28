include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 610204
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 30000
    SAVE_ARTIFACT metro_portal_collision_control_final.sav
    FILES
        info.nut
        main.nut
)
