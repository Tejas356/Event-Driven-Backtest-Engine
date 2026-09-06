# The engine itself. Headers live in include/backtest, implementation here.
add_library(backtest STATIC
    data_handler.cpp
    engine.cpp
    execution.cpp
    portfolio.cpp
    version.cpp
)

target_include_directories(backtest PUBLIC
    ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(backtest PRIVATE backtest_warnings)
