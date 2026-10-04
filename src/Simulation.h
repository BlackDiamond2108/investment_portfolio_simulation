/*
Единственный управляющий класс игры без Qt.
Владеет GameState и std::mt19937_64, использует Portfolio. Окно получает const-состояние
или копию, никогда не изменяет state напрямую.
Публичные функции:
makeDefaultConfig(), validateConfig(config), start(config), state().
previewTrade(request), trade(request), confirmInitialAllocation().
nextMonth(), suggestedFlows(), applyFlows(request), finishMonth().
statistics(), exportState(), restoreState(candidate), validateState(candidate).
Внутренние функции:
updateMarket, calculateIncome, calculateTax, proposeAutomaticFlows,
applyFundFlows, settleLiabilities, checkSolvency, finishGame,
serializeRandomState, restoreRandomState.
Один переход выполняется на копии состояния и RNG и фиксируется только при успехе.
MainWindow вызывает методы напрямую; отдельные Controller/Service не нужны.
Здесь будут объявления, необходимые include и pragma once. Сейчас только комментарий.
*/
