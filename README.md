<!--
Система управления инвестиционным портфелем — упрощённый каркас

18 файлов; код ещё не реализован. Все файлы содержат только комментарии.
Стек: C++17, Qt 6 Widgets, CMake, Qt Test. Одно окно, два класса игровой логики
(Portfolio и Simulation), класс сохранений GameStorage и общие структуры GameTypes.

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

Начать чтение с docs/project-plan.md: там каждый файл, связи и порядок реализации.
Затем docs/simulation-rules.md: формулы и принятые допущения.
В исходниках подробно перечислены функции и проверки.
Проект разрабатывают два человека. Правила совместной работы — в AGENTS.md,
предлагаемое распределение задач — в docs/project-plan.md.
Документация тоже внутри HTML-комментариев: открывать исходный текст, не Markdown preview.
.ui содержит только комментарий, поэтому пока не открывается как готовая форма Designer.
CMake также пока не содержит команд; сборка и тесты появятся при реализации.
-->
