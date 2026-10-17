cmake_minimum_required(VERSION 3.25)

project(MHWFairTransmog VERSION 0.9.0 LANGUAGES CXX)
set(FAIR_TRANSMOG_VERSION "${PROJECT_VERSION}-beta")

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "MHW FairTransmog requires Windows x64 and MSVC.")
endif()

option(FAIR_TRANSMOG_DIAGNOSTICS "Enable diagnostic-only behavior" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(fair_transmog_target_defaults target)
    set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
        FAIR_TRANSMOG_VERSION="${FAIR_TRANSMOG_VERSION}")
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /EHsc /Zi)
    target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:/O2>)
    target_link_options(${target} PRIVATE /DEBUG)
    if(FAIR_TRANSMOG_DIAGNOSTICS)
        target_compile_definitions(${target} PRIVATE FAIR_TRANSMOG_DIAGNOSTICS=1)
    else()
        target_compile_definitions(${target} PRIVATE FAIR_TRANSMOG_DIAGNOSTICS=0)
    endif()
endfunction()

find_package(minhook CONFIG REQUIRED)

add_library(MHW-FairTransmog SHARED
    src/dllmain.cpp
    src/game/appearance.cpp
    src/game/layered_menu.cpp
    src/game/transmog.cpp
    src/runtime/log.cpp
    src/runtime/memory.cpp
    src/runtime/profile.cpp
)

target_link_libraries(MHW-FairTransmog PRIVATE bcrypt minhook::minhook)
target_include_directories(MHW-FairTransmog PRIVATE src)
fair_transmog_target_defaults(MHW-FairTransmog)
set_target_properties(MHW-FairTransmog PROPERTIES
    OUTPUT_NAME "MHW-FairTransmog"
    PDB_NAME "MHW-FairTransmog"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(test_memory
        tests/test_memory.cpp
        src/runtime/memory.cpp
    )
    target_include_directories(test_memory PRIVATE src)
    fair_transmog_target_defaults(test_memory)
    add_test(NAME memory COMMAND test_memory)

    add_executable(test_profile tests/test_profile.cpp src/runtime/profile.cpp)
    target_include_directories(test_profile PRIVATE src)
    target_link_libraries(test_profile PRIVATE bcrypt)
    fair_transmog_target_defaults(test_profile)
    add_test(NAME profile COMMAND test_profile)

    add_executable(test_bootstrap tests/test_bootstrap.cpp src/runtime/profile.cpp)
    target_include_directories(test_bootstrap PRIVATE src)
    target_link_libraries(test_bootstrap PRIVATE bcrypt)
    fair_transmog_target_defaults(test_bootstrap)
    add_dependencies(test_bootstrap MHW-FairTransmog)
    add_test(NAME bootstrap COMMAND test_bootstrap $<TARGET_FILE:MHW-FairTransmog>)
    set_tests_properties(bootstrap PROPERTIES TIMEOUT 30)

    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
    add_test(NAME armor_inspector
        COMMAND "${Python3_EXECUTABLE}" -I -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_inspect_armor.py"
    )

    find_program(PWSH_EXECUTABLE NAMES pwsh REQUIRED)
    add_test(NAME deploy
        COMMAND "${PWSH_EXECUTABLE}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_deploy.ps1"
    )
endif()
