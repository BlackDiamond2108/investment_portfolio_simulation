/*
Единственное окно приложения, наследник QMainWindow.
Владеет Ui::MainWindow, Simulation, GameStorage и флагом несохранённых изменений.
Настройки, рынок, портфель, операции и отчёт разместить во вкладках одного окна.
QTableWidget достаточно для небольшого числа инструментов; своих моделей таблиц пока нет.
Функции:
setupConnections(), collectConfig(), collectTradeRequest(), collectFlowRequest().
onNewGame(), onConfirmInitialAllocation(), onTradePreview(), onTrade(), onNextMonth(),
onApplyFlows(), onFinishMonth(), onSave(), onLoad(), closeEvent(event).
refreshView(), refreshTables(), refreshSummary(), updateEnabledActions(), showError().
Функции переводят поля форм в GameTypes и вызывают Simulation; экономических формул здесь нет.
Простые QFileDialog/QMessageBox можно использовать без отдельных классов/файлов.
Здесь будут объявления, необходимые include и pragma once. Сейчас только комментарий.
*/
