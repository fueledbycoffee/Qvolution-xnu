cmake_minimum_required(VERSION 3.13)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(darling_native_tests NONE)
endif()

enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME exec-signal-libc-independence
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/exec-signal-native.py"
        --out "${CMAKE_CURRENT_BINARY_DIR}/exec-signal")
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

# Keep the native receive regression in test-enabled ordinary builds as well as CTest.
set(s2c_result "${CMAKE_CURRENT_BINARY_DIR}/s2c-receive-capacity/passed.stamp")
add_custom_command(OUTPUT "${s2c_result}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/s2c-receive-capacity.py"
        "${DARLING_SUPERPROJECT}" --darlingserver "${DARLINGSERVER_SOURCE}"
        --out "${CMAKE_CURRENT_BINARY_DIR}/s2c-receive-capacity"
    COMMAND "${CMAKE_COMMAND}" -E touch "${s2c_result}"
    DEPENDS s2c-receive-capacity.py
        ../src/libsystem_kernel/emulation/include/linux_premigration/resources/dserver-rpc-defs.h
        "${DARLINGSERVER_SOURCE}/scripts/generate-rpc-wrappers.py"
    VERBATIM)
add_custom_target(s2c-receive-capacity-check ALL DEPENDS "${s2c_result}")
