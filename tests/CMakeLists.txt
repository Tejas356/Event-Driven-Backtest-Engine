include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE
)

# Keep gtest from overriding this project's compiler/runtime settings.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# Every test executable is registered through this helper so that adding a test
# file in a later step is a one-line change.
function(add_backtest_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE backtest strategies GTest::gtest_main backtest_warnings)
    gtest_discover_tests(${name})
endfunction()

add_backtest_test(test_types test_types.cpp)
add_backtest_test(test_event_queue test_event_queue.cpp)
add_backtest_test(test_statistics test_statistics.cpp)
add_backtest_test(test_data_handler test_data_handler.cpp)
add_backtest_test(test_lookahead test_lookahead.cpp)
add_backtest_test(test_execution test_execution.cpp)
add_backtest_test(test_engine test_engine.cpp)
add_backtest_test(test_buy_and_hold test_buy_and_hold.cpp)
add_backtest_test(test_metrics test_metrics.cpp)
