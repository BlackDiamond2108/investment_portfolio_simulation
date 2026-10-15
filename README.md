<!--
Система управления инвестиционным портфелем

Стек: C++17, Qt 6 Widgets, CMake, Qt Test. Одно окно, два класса игровой логики
(Portfolio и Simulation), класс сохранений GameStorage и общие структуры GameTypes.

Реализовано (участник 1, расчёты):
GameTypes.h, Portfolio.h/.cpp, Simulation.h/.cpp, расчётные слоты SimulationTests,
сборка portfolio_core и simulation_tests.

Ещё не реализовано (участник 2, окно и сохранения):
main.cpp, MainWindow, GameStorage, цель приложения, слот saveLoad.

Структура:
CMakeLists.txt
README.md
AGENTS.md
docs/
  project-plan.md
  simulation-rules.md
src/
  main.cpp
  GameTypes.h
  Portfolio.h / Portfolio.cpp
  Simulation.h / Simulation.cpp
  GameStorage.h / GameStorage.cpp
  MainWindow.h / MainWindow.cpp / MainWindow.ui
tests/
  SimulationTests.cpp

Правила совместной работы — AGENTS.md, распределение — docs/project-plan.md.
Формулы — docs/simulation-rules.md.
Сборка расчётов: cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt6>
cmake --build build; ctest --test-dir build --output-on-failure.
-->
