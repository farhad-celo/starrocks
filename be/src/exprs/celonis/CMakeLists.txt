set(LIBRARY_OUTPUT_PATH "${BUILD_DIR}/src/exprs/celonis")
set(EXECUTABLE_OUTPUT_PATH "${BUILD_DIR}/src/exprs/celonis")
set(CMAKE_CXX_STANDARD 20)

set(SOURCES
        variant.cpp
        )

set(HEADERS
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        )

function(add_main_source path)
    string(REPLACE "." "_" path_variable_name ${path})
    string(TOUPPER ${path_variable_name} path_variable_name)
    file(GLOB ${path_variable_name} "${path}/*.cc" "${path}/*.cpp" "${path}/*.hpp" "${path}/*.h")
    list(APPEND SOURCES ${${path_variable_name}})
    set(SOURCES ${SOURCES} PARENT_SCOPE)
endfunction()

function(add_module path)
    string(REPLACE "." "_" path_variable_name ${path})
    string(TOUPPER ${path_variable_name} path_variable_name)
    file(GLOB_RECURSE ${path_variable_name} "modules/${path}/src/*.cpp")
    list(APPEND SOURCES ${${path_variable_name}})
    set(SOURCES ${SOURCES} PARENT_SCOPE)
    list(APPEND HEADERS $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/modules/${path}/include/>)
    set(HEADERS ${HEADERS} PARENT_SCOPE)
endfunction()

add_main_source(modules/common)
add_main_source(inductive_miner)
add_main_source(inductive_miner/base_case_strategies)
add_main_source(inductive_miner/cut_strategies)
add_main_source(inductive_miner/fallback_strategies)
add_main_source(inductive_miner/noisy_cut_strategies)
add_main_source(modules/memory)

# modularization split
add_module("concurrency")
add_module("ctl")
add_module("format")
add_module("logging")
add_module("types")

add_library(Celonis ${SOURCES})
target_include_directories(Celonis PRIVATE ${HEADERS})
target_compile_definitions(Celonis PRIVATE CELOSTAR)

# Exclude files from compilation until supported
set_source_files_properties(
        inductive_miner/replay_on_process_tree.cpp
        PROPERTIES
            HEADER_FILE_ONLY ON
)

# onetbb does not support static build.
# ${THIRDPARTY_DIR}/lib should be added to ${LD_LIBRARY_PATH} or be copied in runtime.
add_library(onetbb SHARED IMPORTED)
set_target_properties(onetbb PROPERTIES IMPORTED_LOCATION ${THIRDPARTY_DIR}/lib/libtbb.so)
target_link_libraries(Celonis PRIVATE onetbb)