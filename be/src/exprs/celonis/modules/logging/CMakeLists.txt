target_sources(cpm-accelerator
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/builder/sinks/console_sink_builder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/builder/sinks/file_sink_builder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/builder/sinks/no_log_sink_builder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/builder/sinks/ostream_sink_builder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/builder/builder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/builder/level_conversion.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/factories/builder_factory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/factories/logger_factory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utils/log_cleaner/background_task.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/log.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/metadata.cpp
        )

target_include_directories(cpm-accelerator
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include/
        )

target_sources(cpm_test
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/test/json_logging_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test/metadata_test.cpp
        )
