cmake_minimum_required(VERSION 3.20)
project(FIXTradingGateway VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")

find_package(Threads REQUIRED)
find_package(Boost 1.75 REQUIRED)

set(BOOST_INCLUDE_DIRS ${Boost_INCLUDE_DIRS})
set(BOOST_LIBRARIES ${Boost_LIBRARIES})
find_package(nlohmann_json 3.2.0 REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(fix_engine_lib
    src/fix_engine/fix_message.cpp
    src/fix_engine/fix_session.cpp
    src/fix_engine/fix_session_manager.cpp
    src/fix_engine/message_parser.cpp
    src/order_manager/order_book.cpp
    src/order_manager/position_tracker.cpp
    src/order_manager/order_validator.cpp
    src/order_manager/execution_report_generator.cpp
    src/order_manager/order_manager.cpp
    src/market_data/order_book_level.cpp
    src/market_data/price_generator.cpp
    src/market_data/matching_engine.cpp
    src/market_data/market_data_manager.cpp
    src/market_data/symbol_manager.cpp
    src/market_data/simulated_exchange.cpp
    src/contestant_engine/tcp_ingress.cpp
    src/shadow_engine/telemetry_bridge.cpp
    src/risk/risk_types.cpp
    src/risk/risk_manager.cpp
    src/metrics/metrics_collector.cpp
    src/admin/admin_server.cpp
    src/persistence/message_store.cpp
    src/utils/logger.cpp
    src/utils/config_loader.cpp
)

target_link_libraries(fix_engine_lib
    PUBLIC
        Threads::Threads
)

target_include_directories(fix_engine_lib
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${Boost_INCLUDE_DIRS}
        /opt/homebrew/include
)

add_executable(fix_gateway
    src/main.cpp
)

target_link_libraries(fix_gateway
    PRIVATE
        fix_engine_lib
)

option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    find_package(GTest)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.14.0
        )
        FetchContent_MakeAvailable(googletest)
    endif()
    include(GoogleTest)

    add_executable(fix_engine_tests
        tests/test_fix_message.cpp
        tests/test_fix_session.cpp
        tests/test_message_parser.cpp
        tests/test_config_loader.cpp
        tests/test_order_book.cpp
        tests/test_position_tracker.cpp
        tests/test_order_validator.cpp
        tests/test_order_manager.cpp
        tests/test_price_generator.cpp
        tests/test_order_book_level.cpp
        tests/test_matching_engine.cpp
        tests/test_symbol_manager.cpp
        tests/test_risk_manager.cpp
        tests/test_metrics_collector.cpp
        tests/test_message_store.cpp
        tests/test_performance.cpp
    )

    target_link_libraries(fix_engine_tests
        PRIVATE
            fix_engine_lib
            GTest::gtest
            GTest::gtest_main
    )

    gtest_discover_tests(fix_engine_tests)
endif()

option(BUILD_BENCHMARKS "Build benchmarks" ON)
if(BUILD_BENCHMARKS)
    add_executable(performance_benchmark
        benchmarks/performance_benchmark.cpp
    )

    target_link_libraries(performance_benchmark
        PRIVATE
            fix_engine_lib
    )
endif()
