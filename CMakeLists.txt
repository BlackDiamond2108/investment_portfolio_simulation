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

add_library(portfolio_storage src/GameStorage.cpp)
target_link_libraries(portfolio_storage PUBLIC portfolio_core Qt6::Core)

include(CTest)
if(BUILD_TESTING)
    add_executable(simulation_tests tests/SimulationTests.cpp)
    target_link_libraries(simulation_tests PRIVATE portfolio_ui Qt6::Test)
    set_target_properties(simulation_tests PROPERTIES AUTOMOC ON)
    add_test(NAME simulation_tests COMMAND simulation_tests)
    set_tests_properties(simulation_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
endif()

add_library(portfolio_ui src/MainWindow.cpp src/MainWindow.h src/MainWindow.ui)
target_link_libraries(portfolio_ui PUBLIC portfolio_storage Qt6::Widgets)
set_target_properties(portfolio_ui PROPERTIES AUTOMOC ON AUTOUIC ON)
add_executable(portfolio_simulator src/main.cpp)
target_link_libraries(portfolio_simulator PRIVATE portfolio_ui)
