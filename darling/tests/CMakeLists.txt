cmake_minimum_required(VERSION 3.13)
project(darling_native_tests NONE)

enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(DARLING_SUPERPROJECT "" CACHE PATH "Darling superproject source tree")
if(NOT DARLING_SUPERPROJECT)
    message(FATAL_ERROR "Set DARLING_SUPERPROJECT to the Darling source tree")
endif()
set(DARLINGSERVER_SOURCE "${DARLING_SUPERPROJECT}/src/external/darlingserver"
    CACHE PATH "Darlingserver source tree containing the RPC generator")

add_test(NAME s2c-receive-capacity
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/s2c-receive-capacity.py"
        "${DARLING_SUPERPROJECT}" --darlingserver "${DARLINGSERVER_SOURCE}"
        --out "${CMAKE_CURRENT_BINARY_DIR}/s2c-receive-capacity")
