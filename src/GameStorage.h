#pragma once

#include "GameTypes.h"
#include <QString>

// JSON-сохранение всей партии. Загрузка возвращает проверенный кандидат,
// не изменяя активную Simulation. Запись заменяет файл только после успеха.
class GameStorage {
public:
    static Result<void> save(const QString& path, const GameState& state);
    static Result<GameState> load(const QString& path);
};
