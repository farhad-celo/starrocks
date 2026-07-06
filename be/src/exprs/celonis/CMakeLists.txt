set(LIBRARY_OUTPUT_PATH "${BUILD_DIR}/src/exprs/celonis")
set(EXECUTABLE_OUTPUT_PATH "${BUILD_DIR}/src/exprs/celonis")
set(CMAKE_CXX_STANDARD 20)

set(SOURCES
        agg/variant.cpp
        )

set(HEADERS
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        modules/thirdparty/ska_flat_hash_map/
        )

function(add_main_source path)
    string(REPLACE "." "_" path_variable_name ${path})
    string(TOUPPER ${path_variable_name} path_variable_name)
    file(GLOB ${path_variable_name} "modules/${path}/*.cc" "modules/${path}/*.cpp" "modules/${path}/*.hpp" "modules/${path}/*.h")
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

add_main_source(common)
add_main_source(common/date)
add_main_source(common/tracing)
add_main_source(common/tracing/propagation)
add_main_source(cube)
add_main_source(cube/execution/tracking)
add_main_source(memory)
add_main_source(memory/builders)
add_main_source(memory/cache)
add_main_source(memory/management)
add_main_source(memory/tracking)
add_main_source(memory/transform)
add_main_source(operators/aggregation)
add_main_source(operators/process/align_model)
list(APPEND SOURCES
        modules/operators/mo/mo_bpmn_graph_helper.cpp
        modules/operators/mo/mo_bpmn_graph_operator.cpp
        modules/operators/process/alignment/input_output_mapper.cpp
        modules/operators/process/alignment/log_aligner.cpp
        modules/operators/process/alignment/sequence_aligner.cpp
        modules/operators/process/alignment/trace_alignment.cpp
        modules/operators/process/bpmn/bpmn_graph.cpp
        modules/operators/process/bpmn/bpmn_graph_builder.cpp
        modules/operators/process/bpmn/bpmn_graph_to_tables.cpp
        modules/operators/process/bpmn/bpmn_graph_with_block_structure.cpp
        modules/operators/process/bpmn/bpmn_graph_with_block_structure_utils.cpp
        modules/operators/process/bpmn/bpmn_overlay_graphs.cpp
        modules/operators/process/bpmn/bpmn_remap_task_ids.cpp
        modules/operators/process/bpmn/bpmn_to_pn.cpp
        modules/operators/process/bpmn/bpmn_validation.cpp
        modules/operators/process/bpmn/process_tree_to_bpmn.cpp
        modules/operators/process/bpmn/replay_types.cpp
        modules/operators/process/bpmn/replay_utils.cpp
)
add_main_source(operators/process/alignment/fallback)
add_main_source(operators/process/alignment/fitting_prefix)
add_main_source(operators/process/alignment/optimal)
add_main_source(operators/process/alignment/petri_net)
add_main_source(operators/process/alignment/petri_net/a_star)
add_main_source(operators/process/alignment/petri_net/murata)
add_main_source(operators/process/alignment/petri_net/unfolding)
add_main_source(operators/process/alignment/rl_align)
add_main_source(operators/process/alignment/rl_align/rl_statistics)
add_main_source(operators/process/inductive_miner)
add_main_source(operators/process/inductive_miner/base_case_strategies)
add_main_source(operators/process/inductive_miner/cut_strategies)
add_main_source(operators/process/inductive_miner/fallback_strategies)
add_main_source(operators/process/inductive_miner/noisy_cut_strategies)

# modularization split
add_module("concurrency")
add_module("ctl")
add_module("format")
add_module("logging")
add_module("types")

# sharedresources
file(GLOB_RECURSE SHAREDRESOURCES "sharedresources/*.cpp")
list(APPEND SOURCES ${SHAREDRESOURCES})
list(APPEND HEADERS $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sharedresources/>)

add_library(Celonis ${SOURCES})
target_include_directories(Celonis PRIVATE ${HEADERS})
target_compile_definitions(Celonis PRIVATE CELOSTAR)

# Proto sources
message(STATUS "Proto sources will be created at ${BUILD_DIR}/generated/modules/query/")
foreach (name operators celonis_types)
    list(APPEND PROTO_GENERATED_SRCS "${BUILD_DIR}/generated/modules/query/${name}.pb.cc")
    list(APPEND PROTO_GENERATED_HDRS "${BUILD_DIR}/generated/modules/query/${name}.pb.h")
    list(APPEND PROTO_SRCS "${SRC_DIR}/exprs/celonis/protos/${name}.proto")
endforeach()

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated/modules/query")
add_custom_command(
        OUTPUT ${PROTO_GENERATED_SRCS} ${PROTO_GENERATED_HDRS}
        COMMAND ${THIRDPARTY_DIR}/bin/protoc -I=${SRC_DIR}/exprs/celonis/protos --cpp_out=${CMAKE_BINARY_DIR}/generated/modules/query ${PROTO_SRCS}
        DEPENDS ${PROTO_SRCS}
)
add_library(celonis_protos ${PROTO_GENERATED_SRCS} ${PROTO_GENERATED_HDRS})
target_include_directories(Celonis PRIVATE "${BUILD_DIR}/generated")

# Boost
find_package(Boost 1.80.0 COMPONENTS container)
target_include_directories(Celonis PRIVATE ${Boost_INCLUDE_DIR})

# onetbb does not support static build.
# ${THIRDPARTY_DIR}/lib should be added to ${LD_LIBRARY_PATH} or be copied in runtime.
add_library(onetbb SHARED IMPORTED)
set_target_properties(onetbb PROPERTIES IMPORTED_LOCATION ${THIRDPARTY_DIR}/lib/libtbb.so)

target_link_libraries(Celonis
        PRIVATE
        celonis_protos
        ${Boost_LIBRARIES}
        onetbb
)

# Test
if (${MAKE_TEST} STREQUAL "ON")
    add_library(gtest_main STATIC IMPORTED)
    set_target_properties(gtest_main PROPERTIES IMPORTED_LOCATION ${THIRDPARTY_DIR}/lib/libgtest_main.a)

    set(EXEC_FILES
            ${TEST_DIR}/exprs/celonis/inductive_miner/directly_follows_graph_test.cpp
            )
    add_executable(celonis_test ${EXEC_FILES})
    target_compile_definitions(celonis_test PRIVATE CELOSTAR)

    target_link_libraries(celonis_test
            PRIVATE ${TEST_LINK_LIBS}
            PRIVATE gtest_main
            PRIVATE Celonis)
    target_include_directories(celonis_test
            PRIVATE $<TARGET_PROPERTY:Celonis,INCLUDE_DIRECTORIES>
            )
    set_target_properties(celonis_test PROPERTIES COMPILE_FLAGS "-fno-access-control")
endif ()
