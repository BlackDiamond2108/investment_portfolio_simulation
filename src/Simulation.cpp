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
    if (!floorUnitsFromMoney(config.initialCapital, kInitialUnitPrice).ok())
        return Result<void>::failure("invalid_config", "Начальный капитал слишком велик для количества паёв");
    if (config.flowMode != FlowMode::Manual && config.flowMode != FlowMode::Automatic)
        return Result<void>::failure("invalid_config", "Неизвестный режим потоков");
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
        if (instrument.currency < Currency::RUB || instrument.currency > Currency::EUR
            || instrument.kind < InstrumentKind::Deposit || instrument.kind > InstrumentKind::Metal
            || instrument.accruedCoupon < 0)
            return Result<void>::failure("invalid_config", "Некорректный тип, валюта или купон инструмента");
        if (instrument.rateChangeMin > instrument.rateChangeMax)
            return Result<void>::failure("invalid_config", "Нижняя граница изменения ставки больше верхней");
        switch (instrument.kind) {
        case InstrumentKind::Deposit:
            if (instrument.termMonths <= 0 || instrument.termMonths > 1200 || instrument.annualRate < 0.0
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
                || instrument.maturityMonth <= 0 || instrument.maturityMonth > 1200
                || instrument.couponPeriodMonths > 1200 || instrument.annualRate < 0.0) {
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
    next.unitsOutstanding = floorUnitsFromMoney(config.initialCapital, kInitialUnitPrice).value();
    next.nextPositionId = 1;
    next.started = true;
    next.startingNav = config.initialCapital;
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
    finalizeReport(state);
}

Result<void> Simulation::checkSolvency(GameState& state, bool finishingMonth) const
{
    const auto nav = navOf(state);
    if (!nav.ok()) {
        return Result<void>::failure(nav.code, nav.message);
    }
    if (state.unitsOutstanding <= 0 && !hasDebts(state)) {
        finishGame(state, "Фонд закрыт: все паи погашены");
        return Result<void>::success();
    }
    // При возврате всех паёв разрешаем продать активы для расчёта с пайщиками.
    if (state.unitsOutstanding > 0 && nav.value() <= 0) {
        finishGame(state, "Банкротство: стоимость чистых активов неположительна");
        return Result<void>::success();
    }
    if (!hasDebts(state)) {
        return Result<void>::success();
    }
    Money debts = 0;
    if (!checkedAdd(state.taxLiability, state.shareholderLiability, debts))
        return Result<void>::failure("overflow", "Переполнение обязательств");
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
    auto preview = Portfolio::previewTrade(state_.portfolio, state_.market, request);
    if (preview.ok()) preview.data->cashAfter = copy.portfolio.cashRUB;
    return preview;
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

Result<Money> Simulation::applyPriceShock(Money price, double shock)
{
    auto next = roundRateMoney(price, 1.0 + shock);
    if (!next.ok()) return next;
    return Result<Money>::success(std::max<Money>(1, next.value()));
}

Result<void> Simulation::updateMarket(MarketState& market, std::mt19937_64& rng, const GameConfig& config) const
{
    auto applyFx = [&](Money& rate, double minChange, double maxChange) -> Result<void> {
        if (minChange == 0.0 && maxChange == 0.0) return Result<void>::success();
        double shock = minChange;
        if (minChange != maxChange) {
            std::uniform_real_distribution<double> dist(minChange, maxChange);
            shock = dist(rng);
        }
        const auto changed = applyPriceShock(rate, shock);
        if (!changed.ok()) return Result<void>::failure(changed.code, changed.message);
        rate = changed.value();
        return Result<void>::success();
    };
    const auto usd = applyFx(market.usdRub, config.usdMinChange, config.usdMaxChange);
    if (!usd.ok()) return usd;
    const auto eur = applyFx(market.eurRub, config.eurMinChange, config.eurMaxChange);
    if (!eur.ok()) return eur;

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
            const auto changed = applyPriceShock(instrument.price, shock);
            if (!changed.ok()) return Result<void>::failure(changed.code, changed.message);
            instrument.price = changed.value();
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
            const auto coupon = mulDivRound(monthly.value(), month % instrument.couponPeriodMonths, 1);
            if (!coupon.ok()) return Result<void>::failure(coupon.code, coupon.message);
            instrument.accruedCoupon = coupon.value();
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
        const auto price = mulDivFloor(navAfterTax.value(), kUnitScale, state.unitsOutstanding);
        if (!price.ok()) return Result<void>::failure(price.code, price.message);
        report.unitPrice = price.value();
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
    if (state.config.flowMode == FlowMode::Automatic
        && (request.inflowRub != state.pending.suggested.inflowRub
            || request.unitsToRedeem != state.pending.suggested.unitsToRedeem))
        return Result<void>::failure("invalid_request", "В автоматическом режиме нужно подтвердить рассчитанные потоки");
    if (request.inflowRub < 0 || request.unitsToRedeem < 0) {
        return Result<void>::failure("invalid_request", "Поступление и возврат паёв не могут быть отрицательными");
    }

    MonthReport& report = state.pending.report;
    if (state.unitsOutstanding <= 0 || report.unitPrice <= 0) {
        finishGame(state, "Фонд закрыт: нельзя рассчитать цену пая");
        return Result<void>::success();
    }

    const auto issueResult = floorUnitsFromMoney(request.inflowRub, report.unitPrice);
    if (!issueResult.ok()) return Result<void>::failure(issueResult.code, issueResult.message);
    const Quantity issued = issueResult.value();
    const auto acceptedResult = moneyFromUnits(issued, report.unitPrice);
    if (!acceptedResult.ok()) return Result<void>::failure(acceptedResult.code, acceptedResult.message);
    const Money accepted = acceptedResult.value();
    if (!checkedAdd(state.portfolio.cashRUB, accepted, state.portfolio.cashRUB)) {
        return Result<void>::failure("overflow", "Переполнение при поступлении средств");
    }
    if (!checkedAdd(state.unitsOutstanding, issued, state.unitsOutstanding))
        return Result<void>::failure("overflow", "Переполнение количества паёв");
    report.unitsIssued = issued;
    report.acceptedInflow = accepted;
    report.leftoverInflow = request.inflowRub - accepted;

    Quantity redeem = request.unitsToRedeem;
    if (redeem > state.unitsOutstanding) {
        return Result<void>::failure("invalid_request", "Нельзя вернуть больше паёв, чем выпущено");
    }
    const auto redemption = moneyFromUnits(redeem, report.unitPrice);
    if (!redemption.ok()) return Result<void>::failure(redemption.code, redemption.message);
    const Money redemptionValue = redemption.value();
    if (state.portfolio.cashRUB >= redemptionValue) {
        state.portfolio.cashRUB -= redemptionValue;
    } else {
        if (!checkedAdd(state.shareholderLiability, redemptionValue - state.portfolio.cashRUB,
                        state.shareholderLiability))
            return Result<void>::failure("overflow", "Переполнение долга пайщикам");
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

    settleLiabilities(state);
    if (state.unitsOutstanding <= 0) {
        return checkSolvency(state, false);
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

void Simulation::finalizeReport(GameState& state) const
{
    if (state.pending.report.month == 0) return;
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

    finalizeReport(state);
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
    in >> std::ws;
    if (!in.eof()) return Result<std::mt19937_64>::failure("invalid_state", "Лишние данные в состоянии генератора");
    return Result<std::mt19937_64>::success(rng);
}

Result<void> Simulation::validateState(const GameState& c)
{
    auto bad = [](const std::string& reason) { return Result<void>::failure("invalid_state", reason); };
    if (!c.started) return bad("Состояние не содержит начатой партии");
    const auto config = validateConfig(c.config);
    if (!config.ok()) return config;
    if (c.month < 0 || c.month > c.config.monthCount || c.unitsOutstanding < 0
        || c.taxLiability < 0 || c.shareholderLiability < 0 || c.startingNav <= 0)
        return bad("Некорректный месяц, капитал, паи или обязательства");
    if (c.phase < Phase::InitialAllocation || c.phase > Phase::Finished)
        return bad("Неизвестная фаза партии");
    GameConfig current = c.config;
    current.instruments = c.market.instruments;
    current.usdRub = c.market.usdRub; current.eurRub = c.market.eurRub;
    const auto market = validateConfig(current);
    if (!market.ok()) return market;
    if (c.market.instruments.size() != c.config.instruments.size())
        return bad("Рынок не соответствует исходному каталогу");
    for (const auto& i : c.market.instruments) {
        auto original = std::find_if(c.config.instruments.begin(), c.config.instruments.end(),
            [&](const Instrument& value) { return value.id == i.id; });
        if (original == c.config.instruments.end() || original->kind != i.kind || original->currency != i.currency
            || original->name != i.name || original->faceValue != i.faceValue
            || original->termMonths != i.termMonths || original->maturityMonth != i.maturityMonth
            || original->couponPeriodMonths != i.couponPeriodMonths
            || original->minChange != i.minChange || original->maxChange != i.maxChange
            || original->minRate != i.minRate || original->maxRate != i.maxRate
            || original->rateChangeMin != i.rateChangeMin || original->rateChangeMax != i.rateChangeMax
            || (i.kind != InstrumentKind::Deposit && original->annualRate != i.annualRate))
            return bad("Изменены фиксированные условия инструмента");
        if (i.kind == InstrumentKind::Bond && i.maturityMonth <= c.month && i.tradable)
            return bad("Погашенная облигация не может быть доступна для покупки");
    }
    const auto portfolio = Portfolio::validate(c.portfolio, c.market);
    if (!portfolio.ok()) return portfolio;
    PositionId maxId = 0;
    for (const auto& p : c.portfolio.positions) {
        maxId = std::max(maxId, p.id);
        const auto* i = Portfolio::findInstrument(c.market, p.instrumentId);
        if (p.openMonth < 0 || p.openMonth > c.month) return bad("Некорректный месяц открытия позиции");
        if (i->kind == InstrumentKind::Deposit
            && (p.quantity != 1 || p.maturityMonth <= c.month
                || p.maturityMonth != p.openMonth + i->termMonths))
            return bad("Некорректный срок или количество вклада");
        if (i->kind == InstrumentKind::Bond
            && (i->maturityMonth <= c.month || p.accruedCoupon != i->accruedCoupon))
            return bad("Некорректное состояние облигации");
    }
    if (c.nextPositionId <= maxId || c.nextPositionId <= 0)
        return bad("Следующий ID позиции уже занят");
    const auto assets = Portfolio::assetsValue(c.portfolio, c.market);
    Money nav = 0;
    if (!assets.ok() || !checkedSub(assets.value(), c.taxLiability, nav)
        || !checkedSub(nav, c.shareholderLiability, nav)) return bad("Переполнение оценки фонда");
    if (c.phase != Phase::Finished && c.phase != Phase::Restructuring && (nav <= 0 || c.unitsOutstanding <= 0))
        return bad("Активная партия должна иметь положительный капитал и паи");
    const bool pending = c.phase == Phase::AwaitingFlows || c.phase == Phase::Restructuring;
    if (c.history.size() != static_cast<std::size_t>(c.month - (pending ? 1 : 0)))
        return bad("История не соответствует месяцу и фазе");
    if (pending) {
        if (c.month == 0 || c.pending.report.month != c.month
            || c.pending.flowsApplied != (c.phase == Phase::Restructuring))
            return bad("Неверная фаза промежуточного отчёта");
        if (c.pending.suggested.inflowRub < 0 || c.pending.suggested.unitsToRedeem < 0)
            return bad("Отрицательное предложение потоков");
        if (c.phase == Phase::AwaitingFlows
            && (c.pending.report.acceptedInflow || c.pending.report.redemptionValue
                || c.pending.report.unitsIssued || c.pending.report.unitsRedeemed))
            return bad("Потоки уже учтены до подтверждения");
    } else if (c.pending.report.month != 0 || c.pending.flowsApplied) return bad("Лишний промежуточный месяц");
    if ((c.phase == Phase::InitialAllocation && c.month != 0)
        || (c.phase == Phase::ReadyForMonth && (c.month >= c.config.monthCount || c.taxLiability || c.shareholderLiability))
        || (c.phase == Phase::Finished && c.finishReason.empty())
        || (c.phase != Phase::Finished && !c.finishReason.empty()))
        return bad("Фаза и причина завершения противоречат состоянию");

    auto near = [](double a, double b) { return std::isfinite(a) && std::abs(a-b) <= 1e-10 * std::max(1.0, std::abs(b)); };
    auto reportOk = [&](const MonthReport& report) {
        if (report.navStart <= 0 || report.tax < 0 || report.acceptedInflow < 0 || report.leftoverInflow < 0
            || report.unitsIssued < 0 || report.unitsRedeemed < 0 || report.redemptionValue < 0 || report.unitPrice < 0)
            return false;
        if (static_cast<__int128>(report.navStart) + report.profit != report.navAfterIncome
            || static_cast<__int128>(report.profit) - report.tax != report.netProfit
            || static_cast<__int128>(report.navAfterIncome) - report.tax != report.navAfterTax)
            return false;
        auto tax = roundRateMoney(std::max<Money>(0, report.profit), c.config.taxRate);
        if (!tax.ok() || tax.value() != report.tax || !report.returnDefined
            || !near(report.returnGross, static_cast<double>(report.profit)/report.navStart)
            || !near(report.returnNet, static_cast<double>(report.netProfit)/report.navStart)) return false;
        __int128 income = 0;
        for (const auto& row : report.income) {
            if (static_cast<__int128>(row.interestIncome) + row.revaluation != row.total) return false;
            if (row.instrumentId != 0 && !Portfolio::findInstrument(c.market, row.instrumentId)) return false;
            income += row.total;
        }
        return income == report.profit;
    };
    Money previousNav = c.startingNav;
    int month = 0;
    double index = 1, peak = 1, drawdown = 0;
    for (const auto& report : c.history) {
        if (report.month != ++month || report.navStart != previousNav || !reportOk(report))
            return bad("Нарушена последовательность или арифметика отчётов");
        previousNav = report.navEnd;
        index *= 1 + report.returnNet; peak = std::max(peak, index);
        drawdown = std::max(drawdown, 1 - index/peak);
    }
    if (pending && (c.pending.report.navStart != previousNav || !reportOk(c.pending.report)))
        return bad("Неверный расчёт промежуточного месяца");
    if (!pending && c.phase != Phase::InitialAllocation && previousNav != nav)
        return bad("Капитал не совпадает с последним отчётом");
    if (!near(c.returnIndex, index) || !near(c.returnIndexPeak, peak) || !near(c.maxDrawdown, drawdown))
        return bad("Статистика не соответствует истории");
    const auto rng = restoreRandomState(c.randomEngineState);
    if (!rng.ok()) return bad(rng.message);
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
