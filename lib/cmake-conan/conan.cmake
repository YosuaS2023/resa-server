# The MIT License (MIT)
#
# Copyright (c) 2024 JFrog
#
# This project still includes `conan.cmake`, but its custom integration now
# talks to Conan 2 directly from `conan-omp.cmake` instead of using the Conan 1
# `conan_cmake_*` helpers that used to live in this file.

include(CMakeParseArguments)

set(CONAN_MINIMUM_VERSION "2.0.0")

function(conan_get_version conan_command conan_current_version)
    execute_process(
        COMMAND ${conan_command} --version
        RESULT_VARIABLE conan_result
        OUTPUT_VARIABLE conan_output
        ERROR_VARIABLE conan_output
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(conan_result)
        message(FATAL_ERROR "Conan --version failed='${conan_result}'")
    endif()

    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" conan_version "${conan_output}")
    if(NOT conan_version)
        message(FATAL_ERROR "Unable to parse Conan version from '${conan_output}'")
    endif()

    set(${conan_current_version} "${conan_version}" PARENT_SCOPE)
endfunction()

function(conan_version_check)
    set(one_value_args MINIMUM CURRENT)
    cmake_parse_arguments(CONAN_VERSION_CHECK "" "${one_value_args}" "" ${ARGN})

    if(NOT CONAN_VERSION_CHECK_MINIMUM)
        message(FATAL_ERROR "conan_version_check requires MINIMUM")
    endif()
    if(NOT CONAN_VERSION_CHECK_CURRENT)
        message(FATAL_ERROR "conan_version_check requires CURRENT")
    endif()

    if(CONAN_VERSION_CHECK_CURRENT VERSION_LESS CONAN_VERSION_CHECK_MINIMUM)
        message(FATAL_ERROR "Conan version must be ${CONAN_VERSION_CHECK_MINIMUM} or later")
    endif()
endfunction()

macro(conan_check)
    set(options REQUIRED DETECT_QUIET)
    set(one_value_args VERSION)
    cmake_parse_arguments(CONAN "${options}" "${one_value_args}" "" ${ARGN})

    if(NOT CONAN_DETECT_QUIET)
        message(STATUS "Conan: checking conan executable")
    endif()

    find_program(CONAN_CMD conan)
    if(NOT CONAN_CMD)
        if(CONAN_REQUIRED)
            message(FATAL_ERROR "Conan executable not found! Please install Conan 2.")
        endif()
        return()
    endif()

    conan_get_version("${CONAN_CMD}" CONAN_CURRENT_VERSION)

    if(NOT CONAN_DETECT_QUIET)
        message(STATUS "Conan: Found program ${CONAN_CMD}")
        message(STATUS "Conan: Version found Conan version ${CONAN_CURRENT_VERSION}")
    endif()

    if(CONAN_VERSION)
        conan_version_check(MINIMUM "${CONAN_VERSION}" CURRENT "${CONAN_CURRENT_VERSION}")
    else()
        conan_version_check(MINIMUM "${CONAN_MINIMUM_VERSION}" CURRENT "${CONAN_CURRENT_VERSION}")
    endif()
endmacro()
