target_sources(cpm-accelerator
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/base64/base64.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/json/json.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/utf/utf_utils.cpp
        )

target_include_directories(cpm-accelerator
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include/
        )

target_sources(cpm_test
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/test/json/json_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test/utf/utf_utils_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test/exception_to_json_test.cpp
        )
