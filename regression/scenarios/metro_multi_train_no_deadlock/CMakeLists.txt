include(CreateScenario)

create_scenario_test(
    SCRIPT_TYPE GS
    SEED 510099
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/scenario.cfg
    TICKS 90000
    SAVE_ARTIFACT metro_multi_train_no_deadlock_final.sav
    FILES
        info.nut
        main.nut
)
