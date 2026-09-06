# Strategies built on top of the engine. Populated from Step 7 (buy-and-hold)
# and Step 9 (tsmom); the library is header-only until then.
add_library(strategies INTERFACE)

target_include_directories(strategies INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(strategies INTERFACE backtest)
