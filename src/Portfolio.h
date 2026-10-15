#pragma once

/*
Портфель: сделки, оценка активов и фиксированный доход.
Класс без Qt и без собственной копии всей игры. Методы получают PortfolioData и рынок.

previewTrade не меняет данные. executeTrade атомарно обновляет cash и позиции
либо оставляет портфель без изменений. Досрочного закрытия вклада нет.
Несколько вкладов одного банка разрешены; коротких продаж нет.
*/

#include "GameTypes.h"

#include <map>

struct AccrualReport {
    std::map<PositionId, Money> cashRubByPosition;
    std::map<PositionId, Money> interestRubByPosition;
};

class Portfolio {
public:
    static Result<TradePreview> previewTrade(
        const PortfolioData& portfolio,
        const MarketState& market,
        const TradeRequest& request);

    static Result<void> executeTrade(
        PortfolioData& portfolio,
        PositionId& nextPositionId,
        const MarketState& market,
        const TradeRequest& request,
        int month);

    static Result<Money> valuePosition(const Position& position, const MarketState& market);
    static Result<Money> assetsValue(const PortfolioData& portfolio, const MarketState& market);
    static Result<AccrualReport> accrueAndRedeem(
        PortfolioData& portfolio,
        const MarketState& market,
        int month);
    static Result<void> validate(const PortfolioData& portfolio, const MarketState& market);

    static const Instrument* findInstrument(const MarketState& market, InstrumentId id);
    static Result<Money> convertAmount(Money amount, Currency currency, const MarketState& market);

private:
    static Result<TradePreview> previewBuy(
        const PortfolioData& portfolio,
        const MarketState& market,
        const TradeRequest& request);
    static Result<TradePreview> previewSell(
        const PortfolioData& portfolio,
        const MarketState& market,
        const TradeRequest& request);
    static Result<TradePreview> previewOpenDeposit(
        const PortfolioData& portfolio,
        const MarketState& market,
        const TradeRequest& request);

    static Result<void> applyBuy(
        PortfolioData& portfolio,
        PositionId& nextPositionId,
        const MarketState& market,
        const TradeRequest& request,
        int month);
    static Result<void> applySell(
        PortfolioData& portfolio,
        const MarketState& market,
        const TradeRequest& request);
    static Result<void> applyOpenDeposit(
        PortfolioData& portfolio,
        PositionId& nextPositionId,
        const MarketState& market,
        const TradeRequest& request,
        int month);

    static const Position* findPosition(const PortfolioData& portfolio, PositionId id);
    static Result<Money> quoteBuyCost(const Instrument& instrument, Quantity quantity, const MarketState& market);
    static Result<Money> monthlyCoupon(const Instrument& instrument);
};
