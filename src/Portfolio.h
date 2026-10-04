/*
Портфель: сделки, оценка активов и фиксированный доход.
Класс без Qt и без собственной копии всей игры. Методы получают PortfolioData и рынок.
Публичные функции:
previewTrade(portfolio, market, request) -> Result<TradePreview>.
executeTrade(portfolio, market, request, month) -> Result<void>.
valuePosition(position, market), assetsValue(portfolio, market): стоимость в RUB.
accrueAndRedeem(portfolio, market, month): проценты, купоны и погашение сроков.
validate(portfolio, market): корректность остатков, ID, количества и дат.
Simulation проверяет фазу и долги перед вызовами, затем оплачивает долги из cash.
Внутренние функции: buy, sell, openDeposit, convertToRub, checkedAdd, roundMoney.
Правила: несколько вкладов одного банка разрешены; коротких продаж нет;
ставка открытого вклада фиксирована, досрочного закрытия нет.
Здесь будут объявления, необходимые include и pragma once. Сейчас только комментарий.
*/
