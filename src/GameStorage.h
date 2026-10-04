/*
Простой класс хранения без абстрактного интерфейса и дополнительного слоя.
Функции: save(path, const GameState&) -> Result<void>;
load(path) -> Result<GameState>. Внутренние: toJson, fromJson, validateFormat.
Использует GameTypes.h и Qt Core: QJsonDocument, QFile, QSaveFile.
MainWindow выбирает путь, вызывает save/load и передаёт загруженное состояние
в Simulation.restoreState. Хранилище не меняет активную игру и не знает об окнах.
Здесь будут объявления, необходимые include и pragma once. Сейчас только комментарий.
*/
