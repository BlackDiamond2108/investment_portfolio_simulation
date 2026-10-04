# Единственная конфигурация сборки проекта.
# Реализовать C++17, find_package(Qt6 REQUIRED COMPONENTS Widgets Test), AUTOMOC/AUTOUIC.
# Цель portfolio_core: GameTypes.h, Portfolio.h/.cpp и Simulation.h/.cpp; стандартный C++ без Qt.
# Цель portfolio_storage: GameStorage.h/.cpp; portfolio_core + Qt6::Core.
# Исполняемая цель portfolio_simulator: main.cpp, MainWindow.h/.cpp/.ui;
# связать с portfolio_core, portfolio_storage и Qt6::Widgets, добавить src в include-пути.
# При BUILD_TESTING создать simulation_tests из tests/SimulationTests.cpp,
# связать с portfolio_core, portfolio_storage, Qt6::Test, зарегистрировать через add_test.
# Функции: cmake_minimum_required, project, add_library, qt_add_executable,
# target_link_libraries, target_include_directories, include(CTest), add_test.
# Подключать исходники по мере реализации; сейчас .ui — комментарий, приложение не собирается.
# Будущие команды: cmake -S . -B build; cmake --build build;
# ctest --test-dir build --output-on-failure. Qt можно настроить через kit Qt Creator.
