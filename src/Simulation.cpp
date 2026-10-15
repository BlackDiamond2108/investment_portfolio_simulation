/*
Сценарии партии: рынок, налог, паи, фазы и статистика.
Порядок месяца: доход -> налог -> поступления/возврат паёв -> реструктуризация.
Потоки не входят в инвестиционную прибыль. Переход атомарный.
*/

#include "Simulation.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

namespace {

const Instrument* instrumentById(const MarketState& market, InstrumentId id)
{
    return Portfolio::findInstrument(market, id);
}

std::string instrumentName(const MarketState& market, InstrumentId id)
{
    const Instrument* instrument = instrumentById(market, id);
    return instrument == nullptr ? ("#" + std::to_string(id)) : instrument->name;
}

bool rangeOk(double minValue, double maxValue)
{
    return std::isfinite(minValue) && std::isfinite(maxValue) && minValue <= maxValue;
}

} // namespace

GameConfig Simulation::makeDefaultConfig()
{
    GameConfig config;
    config.monthCount = 18;
    config.initialCapital = 560000 * kKopecksPerRuble;
    config.taxRate = 0.17;
    config.flowMode = FlowMode::Manual;
    config.seed = 42;
    config.usdRub = 90 * kKopecksPerRuble;
    config.eurRub = 100 * kKopecksPerRuble;

    Instrument rubDeposit;
    rubDeposit.id = 1;
    rubDeposit.name = "Вклад RUB";
    rubDeposit.kind = InstrumentKind::Deposit;
    rubDeposit.currency = Currency::RUB;
    rubDeposit.annualRate = 0.12;
    rubDeposit.termMonths = 12;
    rubDeposit.minRate = 0.08;
    rubDeposit.maxRate = 0.18;
    rubDeposit.rateChangeMin = -0.01;
    rubDeposit.rateChangeMax = 0.01;

    Instrument usdDeposit;
    usdDeposit.id = 2;
    usdDeposit.name = "Вклад USD";
    usdDeposit.kind = InstrumentKind::Deposit;
    usdDeposit.currency = Currency::USD;
    usdDeposit.annualRate = 0.04;
    usdDeposit.termMonths = 6;
    usdDeposit.minRate = 0.01;
    usdDeposit.maxRate = 0.08;
    usdDeposit.rateChangeMin = -0.005;
    usdDeposit.rateChangeMax = 0.005;

    Instrument stockA;
    stockA.id = 3;
    stockA.name = "Акции Альфа";
    stockA.kind = InstrumentKind::Stock;
    stockA.currency = Currency::RUB;
    stockA.price = 100 * kKopecksPerRuble;
    stockA.minChange = -0.05;
    stockA.maxChange = 0.08;

    Instrument stockB;
    stockB.id = 4;
    stockB.name = "Акции Бета";
    stockB.kind = InstrumentKind::Stock;
    stockB.currency = Currency::RUB;
    stockB.price = 50 * kKopecksPerRuble;
    stockB.minChange = -0.12;
    stockB.maxChange = 0.18;

    Instrument gold;
    gold.id = 5;
    gold.name = "Золото";
    gold.kind = InstrumentKind::Metal;
    gold.currency = Currency::RUB;
    gold.price = 5000 * kKopecksPerRuble;
    gold.minChange = -0.03;
    gold.maxChange = 0.03;

    Instrument bond;
    bond.id = 6;
    bond.name = "ОФЗ";
    bond.kind = InstrumentKind::Bond;
    bond.currency = Currency::RUB;
    bond.faceValue = 1000 * kKopecksPerRuble;
    bond.annualRate = 0.08;
    bond.couponPeriodMonths = 6;
    bond.maturityMonth = 24;
    bond.accruedCoupon = 0;
    bond.minChange = 0.0;
    bond.maxChange = 0.0;

    config.instruments = {rubDeposit, usdDeposit, stockA, stockB, gold, bond};
    return config;
}

Result<void> Simulation::validateConfig(const GameConfig& config)
{
    if (config.monthCount < 12 || config.monthCount > 30) {
        return Result<void>::failure("invalid_config", "Число месяцев M должно быть от 12 до 30");
    }
    if (config.initialCapital <= 0) {
        return Result<void>::failure("invalid_config", "Начальный капитал должен быть положительным");
    }
    if (config.initialCapital < kInitialUnitPrice || config.initialCapital % kInitialUnitPrice != 0) {
        return Result<void>::failure("invalid_config", "Капитал должен быть кратен начальной цене пая 1000 RUB");
    }
    if (!std::isfinite(config.taxRate) || config.taxRate < 0.0 || config.taxRate > 1.0) {
        return Result<void>::failure("invalid_config", "Ставка налога должна быть конечной долей от 0 до 1");
    }
    if (config.usdRub <= 0 || config.eurRub <= 0) {
        return Result<void>::failure("invalid_config", "Курсы валют должны быть положительными");
    }
    if (!rangeOk(config.usdMinChange, config.usdMaxChange) || config.usdMinChange <= -1.0) {
        return Result<void>::failure("invalid_config", "Некорректный диапазон изменения курса USD");
    }
    if (!rangeOk(config.eurMinChange, config.eurMaxChange) || config.eurMinChange <= -1.0) {
        return Result<void>::failure("invalid_config", "Некорректный диапазон изменения курса EUR");
    }
    if (!std::isfinite(config.flowSensitivity) || config.flowSensitivity < 0.0
        || !std::isfinite(config.flowNoise) || config.flowNoise < 0.0
        || !std::isfinite(config.maxOutflowFraction) || config.maxOutflowFraction < 0.0
        || config.maxOutflowFraction > 1.0
        || !std::isfinite(config.maxInflowFraction) || config.maxInflowFraction < 0.0) {
        return Result<void>::failure("invalid_config", "Некорректные параметры спроса на паи");
    }
    if (config.instruments.empty()) {
        return Result<void>::failure("invalid_config", "Нужен хотя бы один инструмент");
    }

    std::vector<InstrumentId> ids;
    bool hasRubDeposit = false;
    bool hasFxDeposit = false;
    bool hasStock = false;
    bool hasMetal = false;
    bool hasBond = false;
    int stockCount = 0;
    for (const Instrument& instrument : config.instruments) {
        if (instrument.id <= 0 || instrument.name.empty()) {
            return Result<void>::failure("invalid_config", "Инструмент должен иметь положительный ID и название");
        }
        if (std::find(ids.begin(), ids.end(), instrument.id) != ids.end()) {
            return Result<void>::failure("invalid_config", "Идентификаторы инструментов должны быть уникальными");
        }
        ids.push_back(instrument.id);
        if (!rangeOk(instrument.minChange, instrument.maxChange) || instrument.minChange <= -1.0) {
            return Result<void>::failure("invalid_config", "Нижняя граница изменения цены должна быть больше -1");
        }
        if (!std::isfinite(instrument.annualRate) || !std::isfinite(instrument.minRate)
            || !std::isfinite(instrument.maxRate) || !std::isfinite(instrument.rateChangeMin)
            || !std::isfinite(instrument.rateChangeMax)) {
            return Result<void>::failure("invalid_config", "Ставки и диапазоны должны быть конечными числами");
        }
        switch (instrument.kind) {
        case InstrumentKind::Deposit:
            if (instrument.termMonths <= 0 || instrument.annualRate < 0.0
                || instrument.minRate < 0.0 || instrument.maxRate < instrument.minRate) {
                return Result<void>::failure("invalid_config", "Некорректные параметры вклада");
            }
            if (instrument.currency == Currency::RUB) {
                hasRubDeposit = true;
            } else {
                hasFxDeposit = true;
            }
            break;
        case InstrumentKind::Bond:
            if (instrument.faceValue <= 0 || instrument.couponPeriodMonths <= 0
                || instrument.maturityMonth <= 0 || instrument.annualRate < 0.0) {
                return Result<void>::failure("invalid_config", "Некорректные параметры облигации");
            }
            hasBond = true;
            break;
        case InstrumentKind::Stock:
            if (instrument.price <= 0) {
                return Result<void>::failure("invalid_config", "Цена акции должна быть положительной");
            }
            hasStock = true;
            ++stockCount;
            break;
        case InstrumentKind::Metal:
            if (instrument.price <= 0) {
                return Result<void>::failure("invalid_config", "Цена металла должна быть положительной");
            }
            hasMetal = true;
            break;
        }
    }
    if (!(hasRubDeposit && hasFxDeposit && hasStock && hasMetal && hasBond && stockCount >= 2)) {
        return Result<void>::failure(
            "invalid_config",
            "Каталог должен содержать RUB-вклад, валютный вклад, две акции, металл и облигацию");
    }
    return Result<void>::success();
}

const GameState& Simulation::state() const
{
    return state_;
}

Result<void> Simulation::start(const GameConfig& config)
{
    const auto valid = validateConfig(config);
    if (!valid.ok()) {
        return valid;
    }

    GameState next;
    next.config = config;
    next.month = 0;
    next.phase = Phase::InitialAllocation;
    next.market.instruments = config.instruments;
    std::sort(next.market.instruments.begin(), next.market.instruments.end(),
        [](const Instrument& a, const Instrument& b) { return a.id < b.id; });
    next.market.usdRub = config.usdRub;
    next.market.eurRub = config.eurRub;
    next.portfolio.cashRUB = config.initialCapital;
    next.unitsOutstanding = (config.initialCapital / kInitialUnitPrice) * kUnitScale;
    next.nextPositionId = 1;
    next.started = true;
    next.returnIndex = 1.0;
    next.returnIndexPeak = 1.0;
    next.journal.push_back("Создана новая партия");

    std::mt19937_64 rng(config.seed);
    next.randomEngineState = serializeRandomState(rng);

    state_ = std::move(next);
    rng_ = rng;
    return Result<void>::success();
}

bool Simulation::hasDebts(const GameState& state) const
{
    return state.taxLiability > 0 || state.shareholderLiability > 0;
}

bool Simulation::canBuy(const GameState& state) const
{
    if (state.phase == Phase::Finished || hasDebts(state)) {
        return false;
    }
    if (state.phase == Phase::Restructuring && state.month >= state.config.monthCount) {
        return false;
    }
    return state.phase == Phase::InitialAllocation || state.phase == Phase::Restructuring;
}

Result<Money> Simulation::navOf(const GameState& state) const
{
    const auto assets = Portfolio::assetsValue(state.portfolio, state.market);
    if (!assets.ok()) {
        return assets;
    }
    Money nav = 0;
    if (!checkedSub(assets.value(), state.taxLiability, nav)
        || !checkedSub(nav, state.shareholderLiability, nav)) {
        return Result<Money>::failure("overflow", "Переполнение NAV");
    }
    return Result<Money>::success(nav);
}

void Simulation::settleLiabilities(GameState& state) const
{
    const Money payTax = std::min(state.portfolio.cashRUB, state.taxLiability);
    state.portfolio.cashRUB -= payTax;
    state.taxLiability -= payTax;
    const Money payShare = std::min(state.portfolio.cashRUB, state.shareholderLiability);
    state.portfolio.cashRUB -= payShare;
    state.shareholderLiability -= payShare;
}

Money Simulation::liquidAssets(const GameState& state) const
{
    Money liquid = state.portfolio.cashRUB;
    for (const Position& position : state.portfolio.positions) {
        const Instrument* instrument = instrumentById(state.market, position.instrumentId);
        if (instrument == nullptr || instrument->kind == InstrumentKind::Deposit) {
            continue;
        }
        const auto value = Portfolio::valuePosition(position, state.market);
        if (value.ok()) {
            liquid += value.value();
        }
    }
    return liquid;
}

void Simulation::finishGame(GameState& state, const std::string& reason) const
{
    state.phase = Phase::Finished;
    state.finishReason = reason;
    state.journal.push_back(reason);
}

Result<void> Simulation::checkSolvency(GameState& state, bool finishingMonth) const
{
    const auto nav = navOf(state);
    if (!nav.ok()) {
        return Result<void>::failure(nav.code, nav.message);
    }
    if (nav.value() <= 0) {
        finishGame(state, "Банкротство: стоимость чистых активов неположительна");
        return Result<void>::success();
    }
    if (state.unitsOutstanding <= 0) {
        finishGame(state, "Фонд закрыт: все паи погашены");
        return Result<void>::success();
    }
    if (!hasDebts(state)) {
        return Result<void>::success();
    }
    const Money debts = state.taxLiability + state.shareholderLiability;
    const Money liquid = liquidAssets(state);
    if (liquid < debts) {
        finishGame(state, "Неплатёжеспособность: недостаточно ликвидных активов для обязательств");
        return Result<void>::success();
    }
    if (finishingMonth) {
        return Result<void>::failure("outstanding_debt", "Сначала продайте активы и погасите обязательства");
    }
    return Result<void>::success();
}

Result<void> Simulation::tradeOn(GameState& state, const TradeRequest& request) const
{
    if (!state.started || state.phase == Phase::Finished) {
        return Result<void>::failure("invalid_phase", "Нет активной партии для сделки");
    }
    if (state.phase != Phase::InitialAllocation && state.phase != Phase::Restructuring) {
        return Result<void>::failure("invalid_phase", "Сделки доступны только при распределении и реструктуризации");
    }
    const bool buying = request.action == TradeAction::Buy || request.action == TradeAction::OpenDeposit;
    if (buying && !canBuy(state)) {
        if (hasDebts(state)) {
            return Result<void>::failure("debt_blocks_buy", "При долгах разрешены только продажи");
        }
        return Result<void>::failure("last_month_buy", "В последнем месяце новые покупки запрещены");
    }

    auto executed = Portfolio::executeTrade(
        state.portfolio, state.nextPositionId, state.market, request, state.month);
    if (!executed.ok()) {
        return executed;
    }
    if (!buying) {
        settleLiabilities(state);
    }
    state.journal.push_back("Сделка выполнена");
    return Result<void>::success();
}

Result<TradePreview> Simulation::previewTrade(const TradeRequest& request) const
{
    GameState copy = state_;
    const auto applied = tradeOn(copy, request);
    if (!applied.ok()) {
        return Result<TradePreview>::failure(applied.code, applied.message);
    }
    return Portfolio::previewTrade(state_.portfolio, state_.market, request);
}

Result<void> Simulation::trade(const TradeRequest& request)
{
    GameState copy = state_;
    const auto applied = tradeOn(copy, request);
    if (!applied.ok()) {
        return applied;
    }
    state_ = std::move(copy);
    return Result<void>::success();
}

Result<void> Simulation::confirmInitialAllocation()
{
    if (!state_.started || state_.phase != Phase::InitialAllocation) {
        return Result<void>::failure("invalid_phase", "Подтверждение старта доступно только в начальном распределении");
    }
    const auto nav = navOf(state_);
    if (!nav.ok()) {
        return Result<void>::failure(nav.code, nav.message);
    }
    if (nav.value() <= 0 || state_.unitsOutstanding <= 0) {
        return Result<void>::failure("insolvency", "Нельзя стартовать с неположительным NAV или без паёв");
    }
    state_.startingNav = nav.value();
    state_.phase = Phase::ReadyForMonth;
    state_.journal.push_back("Стартовый портфель зафиксирован");
    return Result<void>::success();
}

double Simulation::clampValue(double value, double low, double high)
{
    return std::max(low, std::min(high, value));
}

Money Simulation::applyPriceShock(Money price, double shock)
{
    const long double next = static_cast<long double>(price) * (1.0L + static_cast<long double>(shock));
    Money rounded = static_cast<Money>(std::llround(next));
    if (rounded < 1) {
        rounded = 1;
    }
    return rounded;
}

Result<void> Simulation::updateMarket(MarketState& market, std::mt19937_64& rng, const GameConfig& config) const
{
    auto applyFx = [&](Money& rate, double minChange, double maxChange) {
        if (minChange == 0.0 && maxChange == 0.0) {
            return;
        }
        double shock = minChange;
        if (minChange != maxChange) {
            std::uniform_real_distribution<double> dist(minChange, maxChange);
            shock = dist(rng);
        }
        rate = applyPriceShock(rate, shock);
    };
    applyFx(market.usdRub, config.usdMinChange, config.usdMaxChange);
    applyFx(market.eurRub, config.eurMinChange, config.eurMaxChange);

    std::sort(market.instruments.begin(), market.instruments.end(),
        [](const Instrument& a, const Instrument& b) { return a.id < b.id; });

    for (Instrument& instrument : market.instruments) {
        if (instrument.kind == InstrumentKind::Stock || instrument.kind == InstrumentKind::Metal) {
            if (instrument.minChange == 0.0 && instrument.maxChange == 0.0) {
                continue;
            }
            double shock = instrument.minChange;
            if (instrument.minChange != instrument.maxChange) {
                std::uniform_real_distribution<double> dist(instrument.minChange, instrument.maxChange);
                shock = dist(rng);
            }
            instrument.price = applyPriceShock(instrument.price, shock);
        } else if (instrument.kind == InstrumentKind::Deposit) {
            if (instrument.rateChangeMin == 0.0 && instrument.rateChangeMax == 0.0) {
                continue;
            }
            double delta = instrument.rateChangeMin;
            if (instrument.rateChangeMin != instrument.rateChangeMax) {
                std::uniform_real_distribution<double> dist(instrument.rateChangeMin, instrument.rateChangeMax);
                delta = dist(rng);
            }
            instrument.annualRate = clampValue(
                instrument.annualRate + delta, instrument.minRate, instrument.maxRate);
        }
    }
    return Result<void>::success();
}

Result<void> Simulation::updateBondQuotes(MarketState& market, int month) const
{
    for (Instrument& instrument : market.instruments) {
        if (instrument.kind != InstrumentKind::Bond) {
            continue;
        }
        if (instrument.maturityMonth > 0 && month >= instrument.maturityMonth) {
            instrument.tradable = false;
            instrument.accruedCoupon = 0;
            continue;
        }
        const auto monthly = roundRateMoney(instrument.faceValue, instrument.annualRate / 12.0);
        if (!monthly.ok()) {
            return Result<void>::failure(monthly.code, monthly.message);
        }
        const bool couponDue = instrument.couponPeriodMonths > 0 && month > 0
            && (month % instrument.couponPeriodMonths) == 0;
        if (couponDue) {
            instrument.accruedCoupon = 0;
        } else if (instrument.couponPeriodMonths > 0) {
            instrument.accruedCoupon = monthly.value() * (month % instrument.couponPeriodMonths);
        }
    }
    return Result<void>::success();
}

std::vector<IncomeRow> Simulation::calculateIncome(
    const std::vector<Position>& beforePositions,
    const MarketState& beforeMarket,
    const PortfolioData& afterPortfolio,
    const MarketState& afterMarket,
    const AccrualReport& accrual) const
{
    std::vector<IncomeRow> rows;
    for (const Position& before : beforePositions) {
        IncomeRow row;
        row.positionId = before.id;
        row.instrumentId = before.instrumentId;
        row.title = instrumentName(beforeMarket, before.instrumentId);
        const auto oldValue = Portfolio::valuePosition(before, beforeMarket);
        Money oldV = oldValue.ok() ? oldValue.value() : 0;
        Money newV = 0;
        for (const Position& after : afterPortfolio.positions) {
            if (after.id == before.id) {
                const auto value = Portfolio::valuePosition(after, afterMarket);
                if (value.ok()) {
                    newV = value.value();
                }
                break;
            }
        }
        const Money cash = accrual.cashRubByPosition.count(before.id)
            ? accrual.cashRubByPosition.at(before.id)
            : 0;
        row.total = (newV - oldV) + cash;
        row.interestIncome = accrual.interestRubByPosition.count(before.id)
            ? accrual.interestRubByPosition.at(before.id)
            : 0;
        row.revaluation = row.total - row.interestIncome;
        rows.push_back(row);
    }
    return rows;
}

Result<Money> Simulation::calculateTax(Money profit, double taxRate) const
{
    if (profit <= 0) {
        return Result<Money>::success(0);
    }
    return roundRateMoney(profit, taxRate);
}

FlowRequest Simulation::proposeAutomaticFlows(
    const GameState& state,
    Money navAfterTax,
    double returnNet,
    std::mt19937_64& rng) const
{
    FlowRequest request;
    if (state.unitsOutstanding <= 0 || navAfterTax <= 0) {
        return request;
    }
    double noise = 0.0;
    if (state.config.flowNoise > 0.0) {
        std::uniform_real_distribution<double> dist(-state.config.flowNoise, state.config.flowNoise);
        noise = dist(rng);
    }
    const double d = clampValue(
        state.config.flowSensitivity * returnNet + noise,
        -state.config.maxOutflowFraction,
        state.config.maxInflowFraction);
    if (d >= 0.0) {
        const auto inflow = roundRateMoney(navAfterTax, d);
        request.inflowRub = inflow.ok() ? inflow.value() : 0;
    } else {
        const long double units = static_cast<long double>(state.unitsOutstanding) * static_cast<long double>(-d);
        request.unitsToRedeem = static_cast<Quantity>(std::llround(units));
    }
    return request;
}

Result<void> Simulation::nextMonthOn(GameState& state, std::mt19937_64& rng) const
{
    if (!state.started || state.phase != Phase::ReadyForMonth) {
        return Result<void>::failure("invalid_phase", "Новый месяц можно начать только из состояния ReadyForMonth");
    }
    if (state.month >= state.config.monthCount) {
        return Result<void>::failure("invalid_phase", "Все месяцы партии уже проведены");
    }
    if (hasDebts(state)) {
        return Result<void>::failure("outstanding_debt", "Нельзя начать месяц при непогашенных обязательствах");
    }

    const auto navStart = navOf(state);
    if (!navStart.ok()) {
        return Result<void>::failure(navStart.code, navStart.message);
    }
    if (navStart.value() <= 0) {
        finishGame(state, "Неплатёжеспособность: начальный NAV месяца неположителен");
        return Result<void>::success();
    }

    const std::vector<Position> beforePositions = state.portfolio.positions;
    const MarketState beforeMarket = state.market;
    const int newMonth = state.month + 1;

    const auto marketUpdated = updateMarket(state.market, rng, state.config);
    if (!marketUpdated.ok()) {
        return marketUpdated;
    }
    auto accrual = Portfolio::accrueAndRedeem(state.portfolio, state.market, newMonth);
    if (!accrual.ok()) {
        return Result<void>::failure(accrual.code, accrual.message);
    }
    const auto quotes = updateBondQuotes(state.market, newMonth);
    if (!quotes.ok()) {
        return quotes;
    }

    const auto navAfterIncome = navOf(state);
    if (!navAfterIncome.ok()) {
        return Result<void>::failure(navAfterIncome.code, navAfterIncome.message);
    }
    const Money profit = navAfterIncome.value() - navStart.value();
    const auto tax = calculateTax(profit, state.config.taxRate);
    if (!tax.ok()) {
        return Result<void>::failure(tax.code, tax.message);
    }
    if (state.portfolio.cashRUB >= tax.value()) {
        state.portfolio.cashRUB -= tax.value();
    } else {
        state.taxLiability += tax.value() - state.portfolio.cashRUB;
        state.portfolio.cashRUB = 0;
    }

    const auto navAfterTax = navOf(state);
    if (!navAfterTax.ok()) {
        return Result<void>::failure(navAfterTax.code, navAfterTax.message);
    }

    MonthReport report;
    report.month = newMonth;
    report.navStart = navStart.value();
    report.navAfterIncome = navAfterIncome.value();
    report.navAfterTax = navAfterTax.value();
    report.profit = profit;
    report.tax = tax.value();
    report.netProfit = profit - tax.value();
    report.returnDefined = report.navStart > 0;
    if (report.returnDefined) {
        report.returnGross = static_cast<double>(report.profit) / static_cast<double>(report.navStart);
        report.returnNet = static_cast<double>(report.netProfit) / static_cast<double>(report.navStart);
    }
    if (state.unitsOutstanding > 0 && navAfterTax.value() > 0) {
        report.unitPrice = static_cast<Money>(
            (static_cast<__int128>(navAfterTax.value()) * kUnitScale) / state.unitsOutstanding);
    }
    report.income = calculateIncome(
        beforePositions, beforeMarket, state.portfolio, state.market, accrual.value());

    Money incomeSum = 0;
    for (const IncomeRow& row : report.income) {
        incomeSum += row.total;
    }
    if (incomeSum != report.profit) {
        IncomeRow rounding;
        rounding.title = "Округление";
        rounding.total = report.profit - incomeSum;
        rounding.revaluation = rounding.total;
        report.income.push_back(rounding);
    }

    FlowRequest suggested;
    if (state.config.flowMode == FlowMode::Automatic) {
        suggested = proposeAutomaticFlows(state, navAfterTax.value(), report.returnNet, rng);
    }

    state.month = newMonth;
    state.pending.report = report;
    state.pending.suggested = suggested;
    state.pending.flowsApplied = false;
    state.phase = Phase::AwaitingFlows;
    state.journal.push_back("Завершён расчёт месяца " + std::to_string(newMonth));

    if (navAfterTax.value() <= 0) {
        finishGame(state, "Банкротство после налога");
    }
    return Result<void>::success();
}

Result<void> Simulation::nextMonth()
{
    GameState copy = state_;
    std::mt19937_64 rngCopy = rng_;
    const auto applied = nextMonthOn(copy, rngCopy);
    if (!applied.ok()) {
        return applied;
    }
    copy.randomEngineState = serializeRandomState(rngCopy);
    state_ = std::move(copy);
    rng_ = rngCopy;
    return Result<void>::success();
}

FlowRequest Simulation::suggestedFlows() const
{
    return state_.pending.suggested;
}

Result<void> Simulation::applyFlowsOn(GameState& state, const FlowRequest& request) const
{
    if (!state.started || state.phase != Phase::AwaitingFlows) {
        return Result<void>::failure("invalid_phase", "Потоки можно применить только после расчёта месяца");
    }
    if (state.pending.flowsApplied) {
        return Result<void>::failure("flows_already_applied", "Потоки этого месяца уже применены");
    }
    if (request.inflowRub < 0 || request.unitsToRedeem < 0) {
        return Result<void>::failure("invalid_request", "Поступление и возврат паёв не могут быть отрицательными");
    }

    MonthReport& report = state.pending.report;
    if (state.unitsOutstanding <= 0 || report.unitPrice <= 0) {
        finishGame(state, "Фонд закрыт: нельзя рассчитать цену пая");
        return Result<void>::success();
    }

    const Quantity issued = floorUnitsFromMoney(request.inflowRub, report.unitPrice);
    const Money accepted = moneyFromUnits(issued, report.unitPrice);
    if (!checkedAdd(state.portfolio.cashRUB, accepted, state.portfolio.cashRUB)) {
        return Result<void>::failure("overflow", "Переполнение при поступлении средств");
    }
    state.unitsOutstanding += issued;
    report.unitsIssued = issued;
    report.acceptedInflow = accepted;
    report.leftoverInflow = request.inflowRub - accepted;

    Quantity redeem = request.unitsToRedeem;
    if (redeem > state.unitsOutstanding) {
        return Result<void>::failure("invalid_request", "Нельзя вернуть больше паёв, чем выпущено");
    }
    const Money redemptionValue = moneyFromUnits(redeem, report.unitPrice);
    if (state.portfolio.cashRUB >= redemptionValue) {
        state.portfolio.cashRUB -= redemptionValue;
    } else {
        state.shareholderLiability += redemptionValue - state.portfolio.cashRUB;
        state.portfolio.cashRUB = 0;
    }
    state.unitsOutstanding -= redeem;
    report.unitsRedeemed = redeem;
    report.redemptionValue = redemptionValue;

    const auto navEnd = navOf(state);
    if (!navEnd.ok()) {
        return Result<void>::failure(navEnd.code, navEnd.message);
    }
    report.navEnd = navEnd.value();
    state.pending.flowsApplied = true;
    state.phase = Phase::Restructuring;
    state.journal.push_back("Потоки месяца применены");

    if (state.unitsOutstanding <= 0) {
        finishGame(state, "Фонд закрыт: все паи погашены");
    } else if (navEnd.value() <= 0) {
        finishGame(state, "Банкротство после потоков");
    }
    return Result<void>::success();
}

Result<void> Simulation::applyFlows(const FlowRequest& request)
{
    GameState copy = state_;
    const auto applied = applyFlowsOn(copy, request);
    if (!applied.ok()) {
        return applied;
    }
    state_ = std::move(copy);
    return Result<void>::success();
}

Result<void> Simulation::finishMonthOn(GameState& state) const
{
    if (!state.started || state.phase != Phase::Restructuring || !state.pending.flowsApplied) {
        return Result<void>::failure("invalid_phase", "Месяц можно закрыть только после потоков в реструктуризации");
    }

    settleLiabilities(state);
    const auto solvency = checkSolvency(state, true);
    if (!solvency.ok()) {
        return solvency;
    }

    MonthReport report = state.pending.report;
    const auto navEnd = navOf(state);
    if (navEnd.ok()) {
        report.navEnd = navEnd.value();
    }
    if (report.returnDefined && report.navStart > 0) {
        state.returnIndex *= (1.0 + report.returnNet);
        if (state.returnIndex > state.returnIndexPeak) {
            state.returnIndexPeak = state.returnIndex;
        }
        if (state.returnIndexPeak > 0.0) {
            const double drawdown = 1.0 - state.returnIndex / state.returnIndexPeak;
            state.maxDrawdown = std::max(state.maxDrawdown, drawdown);
        }
    }
    state.history.push_back(report);
    state.pending = PendingMonth{};
    if (state.phase != Phase::Finished) {
        if (state.month >= state.config.monthCount) {
            finishGame(state, "Партия завершена после M месяцев");
        } else {
            state.phase = Phase::ReadyForMonth;
            state.journal.push_back("Месяц закрыт, можно переходить к следующему");
        }
    }
    return Result<void>::success();
}

Result<void> Simulation::finishMonth()
{
    GameState copy = state_;
    const auto applied = finishMonthOn(copy);
    if (!applied.ok()) {
        return applied;
    }
    state_ = std::move(copy);
    return Result<void>::success();
}

GameStatistics Simulation::statistics() const
{
    GameStatistics stats;
    stats.startingNav = state_.startingNav;
    stats.unitsOutstanding = state_.unitsOutstanding;
    stats.finishReason = state_.finishReason;
    stats.cumulativeReturn = state_.returnIndex - 1.0;
    stats.maxDrawdown = state_.maxDrawdown;
    const auto nav = navOf(state_);
    stats.finalNav = nav.ok() ? nav.value() : 0;
    for (const MonthReport& report : state_.history) {
        stats.totalGrossProfit += report.profit;
        stats.totalNetProfit += report.netProfit;
        stats.totalTax += report.tax;
        stats.totalInflow += report.acceptedInflow;
        stats.totalRedemption += report.redemptionValue;
    }
    return stats;
}

std::string Simulation::serializeRandomState(const std::mt19937_64& rng)
{
    std::ostringstream out;
    out << rng;
    return out.str();
}

Result<std::mt19937_64> Simulation::restoreRandomState(const std::string& serialized)
{
    std::mt19937_64 rng;
    std::istringstream in(serialized);
    in >> rng;
    if (!in) {
        return Result<std::mt19937_64>::failure("invalid_state", "Не удалось прочитать состояние генератора");
    }
    return Result<std::mt19937_64>::success(rng);
}

Result<void> Simulation::validateState(const GameState& candidate)
{
    if (!candidate.started) {
        return Result<void>::failure("invalid_state", "Состояние не содержит начатой партии");
    }
    const auto configOk = validateConfig(candidate.config);
    if (!configOk.ok()) {
        return configOk;
    }
    const auto portfolioOk = Portfolio::validate(candidate.portfolio, candidate.market);
    if (!portfolioOk.ok()) {
        return portfolioOk;
    }
    if (candidate.month < 0 || candidate.month > candidate.config.monthCount) {
        return Result<void>::failure("invalid_state", "Номер месяца вне допустимого диапазона");
    }
    if (candidate.unitsOutstanding < 0 || candidate.taxLiability < 0 || candidate.shareholderLiability < 0) {
        return Result<void>::failure("invalid_state", "Отрицательные паи или обязательства");
    }
    if (static_cast<int>(candidate.history.size()) > candidate.config.monthCount) {
        return Result<void>::failure("invalid_state", "Слишком много месячных отчётов");
    }
    const auto rng = restoreRandomState(candidate.randomEngineState);
    if (!rng.ok()) {
        return Result<void>::failure(rng.code, rng.message);
    }
    return Result<void>::success();
}

Result<void> Simulation::restoreState(const GameState& candidate)
{
    const auto valid = validateState(candidate);
    if (!valid.ok()) {
        return valid;
    }
    const auto rng = restoreRandomState(candidate.randomEngineState);
    if (!rng.ok()) {
        return Result<void>::failure(rng.code, rng.message);
    }
    state_ = candidate;
    rng_ = rng.value();
    return Result<void>::success();
}

GameState Simulation::exportState() const
{
    GameState copy = state_;
    copy.randomEngineState = serializeRandomState(rng_);
    return copy;
}
