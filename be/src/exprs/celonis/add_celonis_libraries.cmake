# Set the root path to the Celonis libraries directory
set(CELONIS_LIBRARIES_DIR_PATH "${THIRDPARTY_DIR}CPML") # Must match with the naming used in 'dev-env.Dockerfile'
# Set paths for each library's include and lib directories
set(CELONIS_LIBRARIES_INCLUDE_PATH "${CELONIS_LIBRARIES_DIR_PATH}/include/")
set(CELONIS_FORMATTING_LIBRARY_PATH "${CELONIS_LIBRARIES_DIR_PATH}/lib/libcelonis-formatting-library.a")
set(CELONIS_PROCESS_MINING_LIBRARY_PATH "${CELONIS_LIBRARIES_DIR_PATH}/lib/libcelonis-process-mining-library.a")
set(CELONIS_TEMPLATE_LIBRARY_PATH "${CELONIS_LIBRARIES_DIR_PATH}/lib/libcelonis-template-library.a")

# Celonis Formatting Library
add_library(celonis_formatting_library STATIC IMPORTED)
set_target_properties(celonis_formatting_library PROPERTIES
        IMPORTED_LOCATION "${CELONIS_FORMATTING_LIBRARY_PATH}"
        INTERFACE_INCLUDE_DIRECTORIES "${CELONIS_LIBRARIES_INCLUDE_PATH}"
)

# Celonis Template Library (CTL)
add_library(celonis_template_library STATIC IMPORTED)
set_target_properties(celonis_template_library PROPERTIES
        IMPORTED_LOCATION "${CELONIS_TEMPLATE_LIBRARY_PATH}"
        INTERFACE_INCLUDE_DIRECTORIES "${CELONIS_LIBRARIES_INCLUDE_PATH}"
)

# Celonis Process Mining Library (CPML)
add_library(celonis_process_mining_library STATIC IMPORTED)
set_target_properties(celonis_process_mining_library PROPERTIES
        IMPORTED_LOCATION "${CELONIS_PROCESS_MINING_LIBRARY_PATH}"
        INTERFACE_INCLUDE_DIRECTORIES "${CELONIS_LIBRARIES_INCLUDE_PATH}"
)