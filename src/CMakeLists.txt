# The engine itself. Headers live in include/backtest, implementation here.
add_library(backtest STATIC
    data_handler.cpp
    engine.cpp
    execution.cpp
    metrics.cpp
    portfolio.cpp
    summary.cpp
    version.cpp
)

target_include_directories(backtest PUBLIC
    ${CMAKE_SOURCE_DIR}/include)

# Absolute path to the CSV directory, so tests and apps can locate downloaded
# data without depending on the working directory they were launched from.
target_compile_definitions(backtest PUBLIC
    TSMOM_DATA_DIR="${CMAKE_SOURCE_DIR}/data")

target_link_libraries(backtest PRIVATE backtest_warnings)
