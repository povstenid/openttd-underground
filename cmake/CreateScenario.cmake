# Macro that creates a scenario test which expects a structured JSON report.
#
# create_scenario_test(
#   SCRIPT_TYPE <AI|GS>
#   SEED <seed> | FIXTURE <path>
#   CONFIG <cfg>
#   [TICKS <ticks>]
#   [SAVE_ARTIFACT <filename.sav>]
#   [TIMEOUT <seconds>]   (wall clock limit of the run, default 120)
#   FILES <file1> [file2 ...]
# )

include(CMakeParseArguments)

function(create_scenario_test)
    set(options)
    set(one_value_args SCRIPT_TYPE SEED FIXTURE CONFIG TICKS SAVE_ARTIFACT TIMEOUT)
    set(multi_value_args FILES)
    cmake_parse_arguments(SCENARIO "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT SCENARIO_SCRIPT_TYPE)
        message(FATAL_ERROR "create_scenario_test requires SCRIPT_TYPE")
    endif()

    if(NOT SCENARIO_CONFIG)
        message(FATAL_ERROR "create_scenario_test requires CONFIG")
    endif()

    if(NOT SCENARIO_FILES)
        message(FATAL_ERROR "create_scenario_test requires FILES")
    endif()

    if(NOT SCENARIO_SEED AND NOT SCENARIO_FIXTURE)
        message(FATAL_ERROR "create_scenario_test requires either SEED or FIXTURE")
    endif()

    if(SCENARIO_SEED AND SCENARIO_FIXTURE)
        message(FATAL_ERROR "create_scenario_test accepts either SEED or FIXTURE, not both")
    endif()

    if(NOT SCENARIO_TICKS)
        set(SCENARIO_TICKS 30000)
    endif()

    string(TOLOWER "${SCENARIO_SCRIPT_TYPE}" SCENARIO_SCRIPT_TYPE_LOWER)
    if(NOT SCENARIO_SCRIPT_TYPE_LOWER STREQUAL "ai" AND NOT SCENARIO_SCRIPT_TYPE_LOWER STREQUAL "gs")
        message(FATAL_ERROR "SCRIPT_TYPE must be AI or GS")
    endif()

    if(SCENARIO_SCRIPT_TYPE_LOWER STREQUAL "ai")
        set(SCENARIO_SCRIPT_DIR "ai")
    else()
        set(SCENARIO_SCRIPT_DIR "game")
    endif()

    get_filename_component(SCENARIO_TEST_NAME "${CMAKE_CURRENT_SOURCE_DIR}" NAME)

    foreach(SCENARIO_SOURCE_FILE IN LISTS SCENARIO_FILES)
        set(SCENARIO_SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${SCENARIO_SOURCE_FILE}")
        set(SCENARIO_BINARY_FILE "${CMAKE_BINARY_DIR}/${SCENARIO_SCRIPT_DIR}/${SCENARIO_TEST_NAME}/${SCENARIO_SOURCE_FILE}")
        get_filename_component(SCENARIO_BINARY_DIR "${SCENARIO_BINARY_FILE}" DIRECTORY)

        add_custom_command(OUTPUT ${SCENARIO_BINARY_FILE}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${SCENARIO_BINARY_DIR}
                COMMAND ${CMAKE_COMMAND} -E copy
                        ${SCENARIO_SOURCE_PATH}
                        ${SCENARIO_BINARY_FILE}
                MAIN_DEPENDENCY ${SCENARIO_SOURCE_PATH}
                COMMENT "Copying scenario file ${SCENARIO_TEST_NAME}/${SCENARIO_SOURCE_FILE}"
        )

        list(APPEND SCENARIO_BINARY_FILES ${SCENARIO_BINARY_FILE})
    endforeach()

    if(IS_ABSOLUTE "${SCENARIO_CONFIG}")
        set(SCENARIO_CONFIG_SOURCE_PATH "${SCENARIO_CONFIG}")
    else()
        set(SCENARIO_CONFIG_SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${SCENARIO_CONFIG}")
    endif()

    set(SCENARIO_CONFIG_BINARY_FILE "${CMAKE_BINARY_DIR}/scenario/${SCENARIO_TEST_NAME}.cfg")
    add_custom_command(OUTPUT ${SCENARIO_CONFIG_BINARY_FILE}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/scenario"
            COMMAND ${CMAKE_COMMAND} -E copy
                    ${SCENARIO_CONFIG_SOURCE_PATH}
                    ${SCENARIO_CONFIG_BINARY_FILE}
            MAIN_DEPENDENCY ${SCENARIO_CONFIG_SOURCE_PATH}
            COMMENT "Copying scenario config ${SCENARIO_TEST_NAME}.cfg"
    )
    list(APPEND SCENARIO_BINARY_FILES ${SCENARIO_CONFIG_BINARY_FILE})

    if(SCENARIO_FIXTURE)
        if(IS_ABSOLUTE "${SCENARIO_FIXTURE}")
            set(SCENARIO_FIXTURE_SOURCE_PATH "${SCENARIO_FIXTURE}")
        else()
            set(SCENARIO_FIXTURE_SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${SCENARIO_FIXTURE}")
        endif()
        get_filename_component(SCENARIO_FIXTURE_NAME "${SCENARIO_FIXTURE}" NAME)
        set(SCENARIO_FIXTURE_BINARY_FILE "${CMAKE_BINARY_DIR}/scenario/${SCENARIO_TEST_NAME}/${SCENARIO_FIXTURE_NAME}")
        add_custom_command(OUTPUT ${SCENARIO_FIXTURE_BINARY_FILE}
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/scenario/${SCENARIO_TEST_NAME}"
                COMMAND ${CMAKE_COMMAND} -E copy
                        ${SCENARIO_FIXTURE_SOURCE_PATH}
                        ${SCENARIO_FIXTURE_BINARY_FILE}
                MAIN_DEPENDENCY ${SCENARIO_FIXTURE_SOURCE_PATH}
                COMMENT "Copying scenario fixture ${SCENARIO_FIXTURE_NAME}"
        )
        list(APPEND SCENARIO_BINARY_FILES ${SCENARIO_FIXTURE_BINARY_FILE})
    endif()

    add_custom_target(scenario_${SCENARIO_TEST_NAME}_files
            DEPENDS ${SCENARIO_BINARY_FILES}
    )

    add_dependencies(scenario_files scenario_${SCENARIO_TEST_NAME}_files)

    set(SCENARIO_RUNNER_ARGS
        -DOPENTTD_EXECUTABLE=$<TARGET_FILE:openttd>
        -DSCENARIO_TEST=${SCENARIO_TEST_NAME}
        -DSCENARIO_CONFIG=${SCENARIO_CONFIG_BINARY_FILE}
        -DSCENARIO_TICKS=${SCENARIO_TICKS}
    )

    if(SCENARIO_SEED)
        list(APPEND SCENARIO_RUNNER_ARGS -DSCENARIO_SEED=${SCENARIO_SEED})
    endif()

    if(SCENARIO_FIXTURE)
        list(APPEND SCENARIO_RUNNER_ARGS -DSCENARIO_FIXTURE=${SCENARIO_FIXTURE_BINARY_FILE})
    endif()

    if(SCENARIO_SAVE_ARTIFACT)
        list(APPEND SCENARIO_RUNNER_ARGS -DSCENARIO_SAVE_ARTIFACT=${SCENARIO_SAVE_ARTIFACT})
    endif()

    if(SCENARIO_TIMEOUT)
        list(APPEND SCENARIO_RUNNER_ARGS -DSCENARIO_TIMEOUT=${SCENARIO_TIMEOUT})
    endif()

    add_custom_target(scenario_${SCENARIO_TEST_NAME}
            COMMAND ${CMAKE_COMMAND}
                    ${SCENARIO_RUNNER_ARGS}
                    -P "${CMAKE_SOURCE_DIR}/cmake/scripts/Scenario.cmake"
            DEPENDS openttd scenario_${SCENARIO_TEST_NAME}_files
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running scenario test ${SCENARIO_TEST_NAME}"
    )

    add_test(NAME scenario_${SCENARIO_TEST_NAME}
            COMMAND ${CMAKE_COMMAND}
                    ${SCENARIO_RUNNER_ARGS}
                    -P "${CMAKE_SOURCE_DIR}/cmake/scripts/Scenario.cmake"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )

    set_property(TEST scenario_${SCENARIO_TEST_NAME} PROPERTY RUN_SERIAL TRUE)

    add_dependencies(scenario scenario_${SCENARIO_TEST_NAME})
endfunction()
