cmake_minimum_required(VERSION 3.20)
project(blackrock_ps3 LANGUAGES CXX)





set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC d3d11 dxgi d3dcompiler)





set(BRPS3_GROUPS patches player movement spawn hopons inventory loot activity lab)
set(BRPS3_ONLY "" CACHE STRING "Build only these feature groups (empty = all)")
function(brps3_filter out_var)
    set(kept)
    foreach(f ${ARGN})
        get_filename_component(n ${f} NAME)
        set(drop FALSE)
        if(BRPS3_ONLY)
            foreach(g ${BRPS3_GROUPS})
                if(n MATCHES "^${g}" AND NOT n MATCHES "^loot_events" AND NOT g IN_LIST BRPS3_ONLY)
                    set(drop TRUE)
                endif()
            endforeach()
        endif()
        if(NOT drop)
            list(APPEND kept ${f})
        endif()
    endforeach()
    set(${out_var} ${kept} PARENT_SCOPE)
endfunction()


file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/game/*.cpp)
brps3_filter(CORE_SOURCES ${CORE_SOURCES})
add_library(brps3-core STATIC ${CORE_SOURCES})
target_include_directories(brps3-core PUBLIC src)
target_compile_definitions(brps3-core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
target_link_libraries(brps3-core PUBLIC ws2_32 shlwapi)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/features/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp)
brps3_filter(APP_SOURCES ${APP_SOURCES})
add_executable(blackrock-ps3 WIN32 src/main.cpp ${APP_SOURCES})
target_link_libraries(blackrock-ps3 PRIVATE brps3-core imgui dwmapi shell32 ole32 uxtheme)

if(MSVC)
    foreach(t brps3-core blackrock-ps3)
        target_compile_options(${t} PRIVATE /W4 /wd4100 /wd4244 /wd4267 /utf-8 /permissive- /MP)
    endforeach()
    target_compile_options(imgui PRIVATE /utf-8 /MP)
    set_property(TARGET blackrock-ps3 brps3-core imgui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(BRPS3_OUT ${CMAKE_CURRENT_SOURCE_DIR}/out CACHE PATH "Where blackrock-ps3.exe and data/ go (use a private one per build dir)")
set_target_properties(blackrock-ps3 PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${BRPS3_OUT})
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(blackrock-ps3 PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} ${BRPS3_OUT})
endforeach()
add_custom_command(TARGET blackrock-ps3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data ${BRPS3_OUT}/data)


enable_testing()
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*_tests.cpp)
brps3_filter(TEST_SOURCES ${TEST_SOURCES})
foreach(test_src ${TEST_SOURCES})
    get_filename_component(name ${test_src} NAME_WE)
    if(BRPS3_ONLY AND name STREQUAL "integration_tests")
        continue()
    endif()
    if(BRPS3_ONLY AND name STREQUAL "cmd_tests" AND NOT "spawn" IN_LIST BRPS3_ONLY)
        continue()
    endif()
    add_executable(${name} ${test_src})
    target_link_libraries(${name} PRIVATE brps3-core)
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8)
        set_property(TARGET ${name} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
    add_test(NAME ${name} COMMAND ${name} ${CMAKE_CURRENT_SOURCE_DIR})
endforeach()


add_test(NAME app_selfcheck COMMAND blackrock-ps3 --selfcheck ${CMAKE_CURRENT_BINARY_DIR}/selfcheck.txt
         --items ${CMAKE_CURRENT_BINARY_DIR}/items.tsv)
