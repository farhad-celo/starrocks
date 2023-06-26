target_sources(cpm-accelerator
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/uuid/uuid_storage.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/uuid/uuid_utils.cpp
        )

target_include_directories(cpm-accelerator
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include/
        )

target_sources(cpm_test
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/test/uuid/uuid_storage_test.cpp
        )