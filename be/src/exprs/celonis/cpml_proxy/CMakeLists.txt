project(cpml_proxy DESCRIPTION "CPML proxy to hide glue code internals from SR code and to be flexible with the used C++ version in the CPML" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

# Set the root path to the CPML directory
set(CPML_DIR_PATH "${THIRDPARTY_DIR}CPML") # Must match with the naming used in 'dev-env.Dockerfile'
# Set paths for each library's include and build directories
set(CELONIS_LIBRARIES_INCLUDE_PATH "${CPML_DIR_PATH}/include/")
set(CPML_FORMATTING_PATH "${CPML_DIR_PATH}/lib/libformat.a")
set(CPML_PROCESS_MINNING_PATH "${CPML_DIR_PATH}/lib/libcpml.a")
set(CPML_TEMPLATE_PATH "${CPML_DIR_PATH}/lib/libctl.a")

# For additional libraries, add them here:

# Celonis Formatting Library
add_library(cpml_formatting STATIC IMPORTED)
set_target_properties(cpml_formatting PROPERTIES
    IMPORTED_LOCATION "${CPML_FORMATTING_PATH}"
    INTERFACE_INCLUDE_DIRECTORIES "${CELONIS_LIBRARIES_INCLUDE_PATH}"
)

# Celonis Process Mining Library
add_library(cpml_process_mining STATIC IMPORTED)
set_target_properties(cpml_process_mining PROPERTIES
    IMPORTED_LOCATION "${CPML_PROCESS_MINNING_PATH}"
    INTERFACE_INCLUDE_DIRECTORIES "${CELONIS_LIBRARIES_INCLUDE_PATH}"
)

# Celonis Template Library
add_library(cpml_template STATIC IMPORTED)
set_target_properties(cpml_template PROPERTIES
    IMPORTED_LOCATION "${CPML_TEMPLATE_PATH}"
    INTERFACE_INCLUDE_DIRECTORIES "${CELONIS_LIBRARIES_INCLUDE_PATH}"
)

# Define the CPML proxy library
add_library(${PROJECT_NAME} STATIC)
add_library(celonis::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# Set the source files for the proxy library
target_sources(${PROJECT_NAME}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/inductive_miner_proxy.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/process_tree_to_table.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/sr_context.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/src/sr_variant_accessor.cpp
)

# Set the include directories for the proxy library
target_include_directories(${PROJECT_NAME}
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include/
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/
)

# Link all the libraries to the proxy library
target_link_libraries(${PROJECT_NAME} PRIVATE
    cpml_formatting
    cpml_process_mining
    cpml_template
)