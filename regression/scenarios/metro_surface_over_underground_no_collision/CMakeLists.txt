include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 610201
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 60000
    SAVE_ARTIFACT metro_surface_over_underground_no_collision_final.sav
    FILES
        info.nut
        main.nut
)
