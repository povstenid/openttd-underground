cmake_minimum_required(VERSION 3.17)

if(NOT SCENARIO_TEST)
    message(FATAL_ERROR "Script needs SCENARIO_TEST defined")
endif()
if(NOT OPENTTD_EXECUTABLE)
    message(FATAL_ERROR "Script needs OPENTTD_EXECUTABLE defined")
endif()
if(NOT SCENARIO_CONFIG)
    message(FATAL_ERROR "Script needs SCENARIO_CONFIG defined")
endif()
if(NOT EXISTS "${SCENARIO_CONFIG}")
    message(FATAL_ERROR "Scenario config '${SCENARIO_CONFIG}' does not exist")
endif()

if(NOT SCENARIO_TICKS)
    set(SCENARIO_TICKS 30000)
endif()
if(NOT SCENARIO_TIMEOUT)
    set(SCENARIO_TIMEOUT 120)
endif()

function(ensure_scenario_graphics_set)
    set(SCENARIO_BASESET_DIR "${CMAKE_BINARY_DIR}/baseset")
    if(EXISTS "${SCENARIO_BASESET_DIR}/opengfx.obg" AND EXISTS "${SCENARIO_BASESET_DIR}/ogfx1_base.grf")
        return()
    endif()

    set(SCENARIO_BASESET_CANDIDATES)

    if(DEFINED ENV{OPENTTD_BASESET_DIR} AND NOT "$ENV{OPENTTD_BASESET_DIR}" STREQUAL "")
        list(APPEND SCENARIO_BASESET_CANDIDATES "$ENV{OPENTTD_BASESET_DIR}")
    endif()

    if(DEFINED ENV{HOME} AND NOT "$ENV{HOME}" STREQUAL "")
        list(APPEND SCENARIO_BASESET_CANDIDATES
            "$ENV{HOME}/.local/share/openttd/baseset"
            "$ENV{HOME}/Documents/OpenTTD/baseset"
        )
    endif()

    if(WIN32 AND DEFINED ENV{PUBLIC} AND NOT "$ENV{PUBLIC}" STREQUAL "")
        list(APPEND SCENARIO_BASESET_CANDIDATES "$ENV{PUBLIC}/Documents/OpenTTD/baseset")
    endif()

    foreach(SCENARIO_BASESET_CANDIDATE IN LISTS SCENARIO_BASESET_CANDIDATES)
        if(EXISTS "${SCENARIO_BASESET_CANDIDATE}/opengfx.obg" AND EXISTS "${SCENARIO_BASESET_CANDIDATE}/ogfx1_base.grf")
            file(MAKE_DIRECTORY "${SCENARIO_BASESET_DIR}")
            file(GLOB SCENARIO_OPEN_GFX_FILES
                "${SCENARIO_BASESET_CANDIDATE}/opengfx.obg"
                "${SCENARIO_BASESET_CANDIDATE}/ogfx*.grf"
            )
            file(COPY ${SCENARIO_OPEN_GFX_FILES} DESTINATION "${SCENARIO_BASESET_DIR}")
            return()
        endif()
    endforeach()
endfunction()

ensure_scenario_graphics_set()

set(SCENARIO_COMMAND
    ${OPENTTD_EXECUTABLE}
    -x
    -X
    -c ${SCENARIO_CONFIG}
    -snull
    -mnull
    -vnull:ticks=${SCENARIO_TICKS}
    -d script=2
    -Q
)

if(DEFINED SCENARIO_FIXTURE)
    if(NOT EXISTS "${SCENARIO_FIXTURE}")
        message(FATAL_ERROR "Scenario fixture '${SCENARIO_FIXTURE}' does not exist")
    endif()
    list(APPEND SCENARIO_COMMAND -g ${SCENARIO_FIXTURE})
elseif(DEFINED SCENARIO_SEED)
    list(APPEND SCENARIO_COMMAND -G ${SCENARIO_SEED} -g)
else()
    message(FATAL_ERROR "Scenario needs either SCENARIO_FIXTURE or SCENARIO_SEED")
endif()

file(GLOB CRASH_FILES "scenario/crash*")
if(CRASH_FILES)
    file(REMOVE ${CRASH_FILES})
endif()

execute_process(
    COMMAND ${SCENARIO_COMMAND}
    RESULT_VARIABLE SCENARIO_RESULT
    TIMEOUT ${SCENARIO_TIMEOUT}
    OUTPUT_VARIABLE SCENARIO_STDOUT
    ERROR_VARIABLE SCENARIO_STDERR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
)

file(GLOB CRASH_FILES "scenario/crash*.log")
if(CRASH_FILES)
    file(READ ${CRASH_FILES} CRASH_LOG)
    message(FATAL_ERROR "OpenTTD crashed: ${CRASH_LOG}")
endif()

if(SCENARIO_STDOUT)
    message(FATAL_ERROR "Unexpected output: ${SCENARIO_STDOUT}")
endif()

string(REPLACE "\r\n" "\n" SCENARIO_STDERR "${SCENARIO_STDERR}")
string(REPLACE "\r" "\n" SCENARIO_STDERR "${SCENARIO_STDERR}")
string(REPLACE "\n" ";" SCENARIO_LINES "${SCENARIO_STDERR}")

foreach(SCENARIO_LINE IN LISTS SCENARIO_LINES)
    if(SCENARIO_LINE MATCHES "^OTTD_TEST_RESULT (.+)$")
        list(APPEND SCENARIO_RESULTS "${CMAKE_MATCH_1}")
    endif()
endforeach()

list(LENGTH SCENARIO_RESULTS SCENARIO_RESULT_COUNT)
set(SCENARIO_REPORT_FILE "${CMAKE_CURRENT_BINARY_DIR}/scenario_${SCENARIO_TEST}_result.json")
set(SCENARIO_LOG_FILE "${CMAKE_CURRENT_BINARY_DIR}/scenario_${SCENARIO_TEST}_output.txt")
set(SCENARIO_ARTIFACT_FILE "${CMAKE_CURRENT_BINARY_DIR}/scenario_${SCENARIO_TEST}_artifact.sav")

if(SCENARIO_RESULT STREQUAL "Process terminated due to timeout")
    file(WRITE ${SCENARIO_LOG_FILE} "${SCENARIO_STDERR}")
    message(FATAL_ERROR "Scenario timed out after ${SCENARIO_TIMEOUT} seconds. Output in ${SCENARIO_LOG_FILE}")
endif()

if(NOT SCENARIO_RESULT EQUAL 0)
    file(WRITE ${SCENARIO_LOG_FILE} "${SCENARIO_STDERR}")
    message(FATAL_ERROR "Scenario exited with code ${SCENARIO_RESULT}. Output in ${SCENARIO_LOG_FILE}")
endif()

string(FIND "${SCENARIO_STDERR}" "Failed to find a graphics set" SCENARIO_MISSING_GRAPHICS_POS)
if(NOT SCENARIO_MISSING_GRAPHICS_POS EQUAL -1)
    file(WRITE ${SCENARIO_LOG_FILE} "${SCENARIO_STDERR}")
    message(FATAL_ERROR "Scenario requires a usable graphics set. Populate build/baseset or set OPENTTD_BASESET_DIR. Output in ${SCENARIO_LOG_FILE}")
endif()

if(SCENARIO_RESULT_COUNT EQUAL 0)
    file(WRITE ${SCENARIO_LOG_FILE} "${SCENARIO_STDERR}")
    message(FATAL_ERROR "Scenario produced no OTTD_TEST_RESULT line. Output in ${SCENARIO_LOG_FILE}")
endif()

if(NOT SCENARIO_RESULT_COUNT EQUAL 1)
    file(WRITE ${SCENARIO_LOG_FILE} "${SCENARIO_STDERR}")
    message(FATAL_ERROR "Scenario produced ${SCENARIO_RESULT_COUNT} OTTD_TEST_RESULT lines. Output in ${SCENARIO_LOG_FILE}")
endif()

list(GET SCENARIO_RESULTS 0 SCENARIO_RESULT_JSON)

foreach(REQUIRED_FRAGMENT
        "\"suite\":"
        "\"fixture\":"
        "\"seed\":"
        "\"tick\":"
        "\"status\":"
        "\"message\":"
        "\"checks\":"
        "\"metrics\":")
    string(FIND "${SCENARIO_RESULT_JSON}" "${REQUIRED_FRAGMENT}" SCENARIO_FRAGMENT_POS)
    if(SCENARIO_FRAGMENT_POS EQUAL -1)
        file(WRITE ${SCENARIO_LOG_FILE} "${SCENARIO_STDERR}")
        message(FATAL_ERROR "Scenario report is missing required fragment ${REQUIRED_FRAGMENT}. Output in ${SCENARIO_LOG_FILE}")
    endif()
endforeach()

file(WRITE ${SCENARIO_REPORT_FILE} "${SCENARIO_RESULT_JSON}\n")

if(DEFINED SCENARIO_SAVE_ARTIFACT)
    set(SCENARIO_SAVE_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/scenario/save/${SCENARIO_SAVE_ARTIFACT}")
    if(NOT EXISTS "${SCENARIO_SAVE_SOURCE}")
        file(WRITE ${SCENARIO_LOG_FILE} "${SCENARIO_STDERR}")
        message(FATAL_ERROR "Scenario expected save artifact '${SCENARIO_SAVE_SOURCE}' but it was not produced. Output in ${SCENARIO_LOG_FILE}")
    endif()

    file(COPY_FILE "${SCENARIO_SAVE_SOURCE}" "${SCENARIO_ARTIFACT_FILE}" ONLY_IF_DIFFERENT)
endif()

if(SCENARIO_RESULT_JSON MATCHES "\"status\":\"pass\",\"suite\":")
    return()
endif()

file(WRITE ${SCENARIO_LOG_FILE} "${SCENARIO_STDERR}")
message(FATAL_ERROR "Scenario failed. Report in ${SCENARIO_REPORT_FILE}, output in ${SCENARIO_LOG_FILE}")
