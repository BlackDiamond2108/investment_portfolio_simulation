#pragma once

/*
Единственный управляющий класс игры без Qt.
Владеет GameState и std::mt19937_64, использует Portfolio.
Окно получает const-состояние и не изменяет его напрямую.
Один переход выполняется на копии состояния и RNG и фиксируется только при успехе.
*/

#include "GameTypes.h"
#include "Portfolio.h"

#include <random>
#include <string>

class Simulation {
public:
    static GameConfig makeDefaultConfig();
    static Result<void> validateConfig(const GameConfig& config);

    Result<void> start(const GameConfig& config);
    const GameState& state() const;

    Result<TradePreview> previewTrade(const TradeRequest& request) const;
    Result<void> trade(const TradeRequest& request);
    Result<void> confirmInitialAllocation();

    Result<void> nextMonth();
    FlowRequest suggestedFlows() const;
    Result<void> applyFlows(const FlowRequest& request);
    Result<void> finishMonth();

    GameStatistics statistics() const;
    GameState exportState() const;
    Result<void> restoreState(const GameState& candidate);
    static Result<void> validateState(const GameState& candidate);

private:
    Result<void> tradeOn(GameState& state, const TradeRequest& request) const;
    Result<void> nextMonthOn(GameState& state, std::mt19937_64& rng) const;
    Result<void> applyFlowsOn(GameState& state, const FlowRequest& request) const;
    Result<void> finishMonthOn(GameState& state) const;

    Result<void> updateMarket(MarketState& market, std::mt19937_64& rng, const GameConfig& config) const;
    Result<void> updateBondQuotes(MarketState& market, int month) const;
    std::vector<IncomeRow> calculateIncome(
        const std::vector<Position>& beforePositions,
        const MarketState& beforeMarket,
        const PortfolioData& afterPortfolio,
        const MarketState& afterMarket,
        const AccrualReport& accrual) const;
    Result<Money> calculateTax(Money profit, double taxRate) const;
    FlowRequest proposeAutomaticFlows(
        const GameState& state,
        Money navAfterTax,
        double returnNet,
        std::mt19937_64& rng) const;
    void settleLiabilities(GameState& state) const;
    Money liquidAssets(const GameState& state) const;
    Result<void> checkSolvency(GameState& state, bool finishingMonth) const;
    void finalizeReport(GameState& state) const;
    void finishGame(GameState& state, const std::string& reason) const;
    Result<Money> navOf(const GameState& state) const;
    bool canBuy(const GameState& state) const;
    bool hasDebts(const GameState& state) const;

    static std::string serializeRandomState(const std::mt19937_64& rng);
    static Result<std::mt19937_64> restoreRandomState(const std::string& serialized);
    static double clampValue(double value, double low, double high);
    static Result<Money> applyPriceShock(Money price, double shock);

    GameState state_;
    std::mt19937_64 rng_;
};
