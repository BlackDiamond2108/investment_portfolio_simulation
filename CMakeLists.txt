cmake_minimum_required(VERSION 3.16)
project(investment_portfolio_simulation LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)

add_library(portfolio_core
    src/Portfolio.cpp
    src/Simulation.cpp
)
target_include_directories(portfolio_core PUBLIC src)

include(CTest)
if(BUILD_TESTING)
    add_executable(simulation_tests tests/SimulationTests.cpp)
    target_link_libraries(simulation_tests PRIVATE portfolio_core Qt6::Test)
    set_target_properties(simulation_tests PROPERTIES AUTOMOC ON)
    add_test(NAME simulation_tests COMMAND simulation_tests)
endif()

# Цели portfolio_storage и portfolio_simulator подключаются, когда
# GameStorage, MainWindow и main будут реализованы вторым участником.
