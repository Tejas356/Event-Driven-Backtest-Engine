# Executables. run_backtest becomes the real runner in Step 12; sweep in Step 13.
add_executable(run_backtest run_backtest.cpp)
target_link_libraries(run_backtest PRIVATE backtest strategies backtest_warnings)
