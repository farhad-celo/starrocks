project(cpml_proxy DESCRIPTION "CPML proxy to hide glue code internals from SR code and to be flexible with the used C++ version in the CPML" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

# Import the pre-built CPML

set(CPML_DIR_NAME "CPML") # Must match with the naming used in 'dev-env.Dockerfile'
set(CPML_DIR_PATH "${THIRDPARTY_DIR}/${CPML_DIR_NAME}")
set(CPML_COMBINED_STATIC_LIBARY_NAME "cpml_combined_library.a")
set(CPML_STATIC_LIBRARY_PATH "${CPML_DIR_PATH}/lib/${CPML_COMBINED_STATIC_LIBARY_NAME}")
set(CPML_INCLUDE_PATH "${CPML_DIR_PATH}/include/")

IF(NOT EXISTS "${CPML_DIR_PATH}")
    message( FATAL_ERROR "Path to CPML '${CPML_DIR_PATH}' does not exist" )
ELSEIF(NOT EXISTS "${CPML_STATIC_LIBRARY_PATH}")
    message( FATAL_ERROR "Path to CPML static library artifact '${CPML_STATIC_LIBRARY_PATH}' does not exist" )
ELSEIF(NOT EXISTS "${CPML_INCLUDE_PATH}")
    message( FATAL_ERROR "Path to CPML includes '${CPML_INCLUDE_PATH}' does not exist" )
ENDIF()

add_library(cpml STATIC IMPORTED)
set_target_properties(cpml PROPERTIES
        IMPORTED_LOCATION "${CPML_STATIC_LIBRARY_PATH}"
        INTERFACE_INCLUDE_DIRECTORIES "${CPML_INCLUDE_PATH}"
)

# Define the CPML proxy library
add_library(${PROJECT_NAME} STATIC)
add_library(celonis::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/inductive_miner_proxy.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/process_tree_to_table.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/sr_context.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/sr_variant_accessor.cpp
)

target_include_directories(${PROJECT_NAME}
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include/
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/
)

target_link_libraries(${PROJECT_NAME} PRIVATE cpml)