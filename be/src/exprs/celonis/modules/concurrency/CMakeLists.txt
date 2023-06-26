# this "library" will be self contained at a later stage

target_sources(cpm-accelerator
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/collecting_thread_pool.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/concurrency_utils.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/detached_thread.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/repeatable_background_task.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/semaphore.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/thread_manager.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/thread_pool.cpp
        )

target_include_directories(cpm-accelerator
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include/
        )

target_sources(cpm_test
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/test/collecting_thread_pool_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test/concurrency_utils_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test/detached_thread_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test/repeatable_background_task_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test/semaphore_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test/tasks_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test/thread_manager_test.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/test/thread_pool_test.cpp
        )
