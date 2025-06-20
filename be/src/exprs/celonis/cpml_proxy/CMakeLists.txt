project(cpml_proxy DESCRIPTION "CPML proxy to hide glue code internals from SR code and to be flexible with the used C++ version in the CPML" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

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
    celonis_formatting_library
    celonis_process_mining_library
    celonis_template_library
)