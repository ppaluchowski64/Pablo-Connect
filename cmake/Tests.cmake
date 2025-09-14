include(GoogleTest)

function(BuildTestProgram ExecutableName Path)
    add_executable(${ExecutableName} ${CMAKE_CURRENT_SOURCE_DIR}/tests/${Path})
    target_link_libraries(${ExecutableName} PRIVATE ${ARGN})
endfunction()

function(BuildTests ExecutableName PathInTestFolder)
    file(GLOB_RECURSE TEST_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/${PathInTestFolder}/*.cpp
    )

    add_executable(${ExecutableName} ${TEST_FILES})

    target_link_libraries(${ExecutableName} PRIVATE
            ${ARGN}
            gtest_main
            GTest::gtest_main
    )

    gtest_discover_tests(${ExecutableName})
endfunction()
