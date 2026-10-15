/*
Финансовая механика портфеля. Формулы: docs/simulation-rules.md.
Ошибка любой операции не должна частично менять переданные данные.
*/

#include "Portfolio.h"

#include <algorithm>
#include <string>

const Instrument* Portfolio::findInstrument(const MarketState& market, InstrumentId id)
{
    for (const Instrument& instrument : market.instruments) {
        if (instrument.id == id) {
            return &instrument;
        }
    }
    return nullptr;
}

const Position* Portfolio::findPosition(const PortfolioData& portfolio, PositionId id)
{
    for (const Position& position : portfolio.positions) {
        if (position.id == id) {
            return &position;
        }
    }
    return nullptr;
}

Result<Money> Portfolio::convertAmount(Money amount, Currency currency, const MarketState& market)
{
    return convertToRub(amount, currency, market.usdRub, market.eurRub);
}

Result<Money> Portfolio::monthlyCoupon(const Instrument& instrument)
{
    if (instrument.faceValue <= 0) {
        return Result<Money>::success(0);
    }
    return roundRateMoney(instrument.faceValue, instrument.annualRate / 12.0);
}

Result<Money> Portfolio::quoteBuyCost(
    const Instrument& instrument,
    Quantity quantity,
    const MarketState& market)
{
    if (quantity <= 0) {
        return Result<Money>::failure("invalid_request", "Количество должно быть положительным");
    }
    if (!instrument.tradable) {
        return Result<Money>::failure("instrument_not_found", "Инструмент недоступен для покупки");
    }

    Money unitCost = 0;
    if (instrument.kind == InstrumentKind::Bond) {
        Money couponAdd = instrument.accruedCoupon;
        if (!checkedAdd(instrument.faceValue, couponAdd, unitCost)) {
            return Result<Money>::failure("overflow", "Переполнение цены облигации");
        }
    } else if (instrument.kind == InstrumentKind::Metal) {
        const auto metal = mulDivRound(instrument.price, quantity, kMetalScale);
        if (!metal.ok()) {
            return metal;
        }
        return convertAmount(metal.value(), instrument.currency, market);
    } else if (instrument.kind == InstrumentKind::Stock) {
        unitCost = instrument.price;
    } else {
        return Result<Money>::failure("invalid_request", "Этот инструмент нельзя купить этой командой");
    }

    Money native = 0;
    if (instrument.kind != InstrumentKind::Metal) {
        const auto scaled = mulDivRound(unitCost, quantity, 1);
        if (!scaled.ok()) {
            return scaled;
        }
        native = scaled.value();
    } else {
        native = 0;
    }
    return convertAmount(native, instrument.currency, market);
}

Result<Money> Portfolio::valuePosition(const Position& position, const MarketState& market)
{
    const Instrument* instrument = findInstrument(market, position.instrumentId);
    if (instrument == nullptr) {
        return Result<Money>::failure("instrument_not_found", "Позиция ссылается на неизвестный инструмент");
    }

    if (instrument->kind == InstrumentKind::Deposit) {
        Money total = 0;
        if (!checkedAdd(position.principal, position.accruedInterest, total)) {
            return Result<Money>::failure("overflow", "Переполнение стоимости вклада");
        }
        return convertAmount(total, instrument->currency, market);
    }

    if (instrument->kind == InstrumentKind::Bond) {
        Money unit = 0;
        if (!checkedAdd(instrument->faceValue, position.accruedCoupon, unit)) {
            return Result<Money>::failure("overflow", "Переполнение котировки облигации");
        }
        const auto native = mulDivRound(unit, position.quantity, 1);
        if (!native.ok()) {
            return native;
        }
        return convertAmount(native.value(), instrument->currency, market);
    }

    if (instrument->kind == InstrumentKind::Metal) {
        const auto native = mulDivRound(instrument->price, position.quantity, kMetalScale);
        if (!native.ok()) {
            return native;
        }
        return convertAmount(native.value(), instrument->currency, market);
    }

    const auto native = mulDivRound(instrument->price, position.quantity, 1);
    if (!native.ok()) {
        return native;
    }
    return convertAmount(native.value(), instrument->currency, market);
}

Result<Money> Portfolio::assetsValue(const PortfolioData& portfolio, const MarketState& market)
{
    Money total = portfolio.cashRUB;
    for (const Position& position : portfolio.positions) {
        const auto value = valuePosition(position, market);
        if (!value.ok()) {
            return value;
        }
        if (!checkedAdd(total, value.value(), total)) {
            return Result<Money>::failure("overflow", "Переполнение стоимости портфеля");
        }
    }
    return Result<Money>::success(total);
}

Result<TradePreview> Portfolio::previewBuy(
    const PortfolioData& portfolio,
    const MarketState& market,
    const TradeRequest& request)
{
    const Instrument* instrument = findInstrument(market, request.instrumentId);
    if (instrument == nullptr) {
        return Result<TradePreview>::failure("instrument_not_found", "Инструмент не найден");
    }
    if (instrument->kind == InstrumentKind::Deposit) {
        return Result<TradePreview>::failure("invalid_request", "Вклад открывается отдельной операцией");
    }

    const auto cost = quoteBuyCost(*instrument, request.quantity, market);
    if (!cost.ok()) {
        return Result<TradePreview>::failure(cost.code, cost.message);
    }
    if (cost.value() > portfolio.cashRUB) {
        return Result<TradePreview>::failure("insufficient_cash", "Недостаточно свободных рублей для покупки");
    }

    TradePreview preview;
    preview.cashDeltaRub = -cost.value();
    preview.cashAfter = portfolio.cashRUB - cost.value();
    preview.positionValueRub = cost.value();
    preview.description = "Покупка " + instrument->name;
    return Result<TradePreview>::success(preview);
}

Result<TradePreview> Portfolio::previewSell(
    const PortfolioData& portfolio,
    const MarketState& market,
    const TradeRequest& request)
{
    const Position* position = findPosition(portfolio, request.positionId);
    if (position == nullptr) {
        return Result<TradePreview>::failure("position_not_found", "Позиция не найдена");
    }
    const Instrument* instrument = findInstrument(market, position->instrumentId);
    if (instrument == nullptr) {
        return Result<TradePreview>::failure("instrument_not_found", "Инструмент позиции не найден");
    }
    if (instrument->kind == InstrumentKind::Deposit) {
        return Result<TradePreview>::failure("early_deposit_close", "Досрочное закрытие вклада запрещено");
    }
    if (request.quantity <= 0) {
        return Result<TradePreview>::failure("invalid_request", "Количество продажи должно быть положительным");
    }
    if (request.quantity > position->quantity) {
        return Result<TradePreview>::failure("insufficient_quantity", "Нельзя продать больше, чем есть в позиции");
    }

    Position slice = *position;
    slice.quantity = request.quantity;
    if (instrument->kind == InstrumentKind::Bond) {
        slice.accruedCoupon = position->accruedCoupon;
    }
    const auto value = valuePosition(slice, market);
    if (!value.ok()) {
        return Result<TradePreview>::failure(value.code, value.message);
    }

    TradePreview preview;
    preview.cashDeltaRub = value.value();
    preview.cashAfter = portfolio.cashRUB + value.value();
    preview.positionValueRub = value.value();
    preview.description = "Продажа " + instrument->name;
    return Result<TradePreview>::success(preview);
}

Result<TradePreview> Portfolio::previewOpenDeposit(
    const PortfolioData& portfolio,
    const MarketState& market,
    const TradeRequest& request)
{
    const Instrument* instrument = findInstrument(market, request.instrumentId);
    if (instrument == nullptr) {
        return Result<TradePreview>::failure("instrument_not_found", "Инструмент не найден");
    }
    if (instrument->kind != InstrumentKind::Deposit) {
        return Result<TradePreview>::failure("invalid_request", "Открыть вклад можно только по депозитному инструменту");
    }
    if (!instrument->tradable) {
        return Result<TradePreview>::failure("instrument_not_found", "Предложение вклада недоступно");
    }
    if (request.amount <= 0) {
        return Result<TradePreview>::failure("invalid_request", "Сумма вклада должна быть положительной");
    }
    if (instrument->termMonths <= 0) {
        return Result<TradePreview>::failure("invalid_request", "Срок вклада должен быть положительным");
    }

    const auto cost = convertAmount(request.amount, instrument->currency, market);
    if (!cost.ok()) {
        return Result<TradePreview>::failure(cost.code, cost.message);
    }
    if (cost.value() > portfolio.cashRUB) {
        return Result<TradePreview>::failure("insufficient_cash", "Недостаточно свободных рублей для открытия вклада");
    }

    TradePreview preview;
    preview.cashDeltaRub = -cost.value();
    preview.cashAfter = portfolio.cashRUB - cost.value();
    preview.positionValueRub = cost.value();
    preview.description = "Открытие вклада " + instrument->name;
    return Result<TradePreview>::success(preview);
}

Result<TradePreview> Portfolio::previewTrade(
    const PortfolioData& portfolio,
    const MarketState& market,
    const TradeRequest& request)
{
    switch (request.action) {
    case TradeAction::Buy:
        return previewBuy(portfolio, market, request);
    case TradeAction::Sell:
        return previewSell(portfolio, market, request);
    case TradeAction::OpenDeposit:
        return previewOpenDeposit(portfolio, market, request);
    }
    return Result<TradePreview>::failure("invalid_request", "Неизвестное действие сделки");
}

Result<void> Portfolio::applyBuy(
    PortfolioData& portfolio,
    PositionId& nextPositionId,
    const MarketState& market,
    const TradeRequest& request,
    int month)
{
    (void)month;
    const auto preview = previewBuy(portfolio, market, request);
    if (!preview.ok()) {
        return Result<void>::failure(preview.code, preview.message);
    }
    const Instrument* instrument = findInstrument(market, request.instrumentId);
    Position position;
    position.id = nextPositionId++;
    position.instrumentId = instrument->id;
    position.quantity = request.quantity;
    position.costBasisRub = -preview.value().cashDeltaRub;
    if (instrument->kind == InstrumentKind::Bond) {
        position.accruedCoupon = instrument->accruedCoupon;
    }
    portfolio.cashRUB = preview.value().cashAfter;
    portfolio.positions.push_back(position);
    return Result<void>::success();
}

Result<void> Portfolio::applySell(
    PortfolioData& portfolio,
    const MarketState& market,
    const TradeRequest& request)
{
    const auto preview = previewSell(portfolio, market, request);
    if (!preview.ok()) {
        return Result<void>::failure(preview.code, preview.message);
    }

    for (std::size_t i = 0; i < portfolio.positions.size(); ++i) {
        Position& position = portfolio.positions[i];
        if (position.id != request.positionId) {
            continue;
        }
        if (request.quantity == position.quantity) {
            portfolio.positions.erase(portfolio.positions.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            const Money oldBasis = position.costBasisRub;
            const auto newBasis = mulDivRound(oldBasis, position.quantity - request.quantity, position.quantity);
            if (!newBasis.ok()) {
                return Result<void>::failure(newBasis.code, newBasis.message);
            }
            position.quantity -= request.quantity;
            position.costBasisRub = newBasis.value();
        }
        portfolio.cashRUB = preview.value().cashAfter;
        return Result<void>::success();
    }
    return Result<void>::failure("position_not_found", "Позиция не найдена");
}

Result<void> Portfolio::applyOpenDeposit(
    PortfolioData& portfolio,
    PositionId& nextPositionId,
    const MarketState& market,
    const TradeRequest& request,
    int month)
{
    const auto preview = previewOpenDeposit(portfolio, market, request);
    if (!preview.ok()) {
        return Result<void>::failure(preview.code, preview.message);
    }
    const Instrument* instrument = findInstrument(market, request.instrumentId);

    Position position;
    position.id = nextPositionId++;
    position.instrumentId = instrument->id;
    position.quantity = 1;
    position.costBasisRub = -preview.value().cashDeltaRub;
    position.principal = request.amount;
    position.lockedRate = instrument->annualRate;
    position.accruedInterest = 0;
    position.openMonth = month;
    position.maturityMonth = month + instrument->termMonths;
    portfolio.cashRUB = preview.value().cashAfter;
    portfolio.positions.push_back(position);
    return Result<void>::success();
}

Result<void> Portfolio::executeTrade(
    PortfolioData& portfolio,
    PositionId& nextPositionId,
    const MarketState& market,
    const TradeRequest& request,
    int month)
{
    PortfolioData copy = portfolio;
    PositionId nextId = nextPositionId;
    Result<void> applied = Result<void>::failure("invalid_request", "Неизвестное действие сделки");
    switch (request.action) {
    case TradeAction::Buy:
        applied = applyBuy(copy, nextId, market, request, month);
        break;
    case TradeAction::Sell:
        applied = applySell(copy, market, request);
        break;
    case TradeAction::OpenDeposit:
        applied = applyOpenDeposit(copy, nextId, market, request, month);
        break;
    }
    if (!applied.ok()) {
        return applied;
    }
    portfolio = std::move(copy);
    nextPositionId = nextId;
    return Result<void>::success();
}

Result<AccrualReport> Portfolio::accrueAndRedeem(
    PortfolioData& portfolio,
    const MarketState& market,
    int month)
{
    AccrualReport report;
    std::vector<Position> remaining;
    remaining.reserve(portfolio.positions.size());

    for (Position position : portfolio.positions) {
        const Instrument* instrument = findInstrument(market, position.instrumentId);
        if (instrument == nullptr) {
            return Result<AccrualReport>::failure("instrument_not_found", "Позиция ссылается на неизвестный инструмент");
        }

        if (instrument->kind == InstrumentKind::Deposit) {
            if (month > position.openMonth && month <= position.maturityMonth) {
                const auto interest = roundRateMoney(position.principal, position.lockedRate / 12.0);
                if (!interest.ok()) {
                    return Result<AccrualReport>::failure(interest.code, interest.message);
                }
                if (!checkedAdd(position.accruedInterest, interest.value(), position.accruedInterest)) {
                    return Result<AccrualReport>::failure("overflow", "Переполнение процентов по вкладу");
                }
                const auto interestRub = convertAmount(interest.value(), instrument->currency, market);
                if (!interestRub.ok()) {
                    return Result<AccrualReport>::failure(interestRub.code, interestRub.message);
                }
                report.interestRubByPosition[position.id] += interestRub.value();
            }
            if (month == position.maturityMonth) {
                Money total = 0;
                if (!checkedAdd(position.principal, position.accruedInterest, total)) {
                    return Result<AccrualReport>::failure("overflow", "Переполнение погашения вклада");
                }
                const auto cash = convertAmount(total, instrument->currency, market);
                if (!cash.ok()) {
                    return Result<AccrualReport>::failure(cash.code, cash.message);
                }
                if (!checkedAdd(portfolio.cashRUB, cash.value(), portfolio.cashRUB)) {
                    return Result<AccrualReport>::failure("overflow", "Переполнение денег при погашении вклада");
                }
                report.cashRubByPosition[position.id] += cash.value();
                continue;
            }
            remaining.push_back(position);
            continue;
        }

        if (instrument->kind == InstrumentKind::Bond) {
            const auto monthly = monthlyCoupon(*instrument);
            if (!monthly.ok()) {
                return Result<AccrualReport>::failure(monthly.code, monthly.message);
            }
            const bool couponDue = instrument->couponPeriodMonths > 0 && month > 0
                && (month % instrument->couponPeriodMonths) == 0;
            const bool matured = instrument->maturityMonth > 0 && month >= instrument->maturityMonth;

            const auto interestNative = mulDivRound(monthly.value(), position.quantity, 1);
            if (!interestNative.ok()) {
                return Result<AccrualReport>::failure(interestNative.code, interestNative.message);
            }
            const auto interestRub = convertAmount(interestNative.value(), instrument->currency, market);
            if (!interestRub.ok()) {
                return Result<AccrualReport>::failure(interestRub.code, interestRub.message);
            }
            report.interestRubByPosition[position.id] += interestRub.value();

            int monthsInPeriod = instrument->couponPeriodMonths > 0
                ? (month % instrument->couponPeriodMonths)
                : 0;
            if (couponDue) {
                monthsInPeriod = instrument->couponPeriodMonths;
            }
            position.accruedCoupon = monthly.value() * static_cast<Money>(monthsInPeriod);

            if (couponDue || matured) {
                const auto couponNative = mulDivRound(position.accruedCoupon, position.quantity, 1);
                if (!couponNative.ok()) {
                    return Result<AccrualReport>::failure(couponNative.code, couponNative.message);
                }
                const auto couponRub = convertAmount(couponNative.value(), instrument->currency, market);
                if (!couponRub.ok()) {
                    return Result<AccrualReport>::failure(couponRub.code, couponRub.message);
                }
                if (!checkedAdd(portfolio.cashRUB, couponRub.value(), portfolio.cashRUB)) {
                    return Result<AccrualReport>::failure("overflow", "Переполнение при выплате купона");
                }
                report.cashRubByPosition[position.id] += couponRub.value();
                position.accruedCoupon = 0;
            }
            if (matured) {
                const auto faceNative = mulDivRound(instrument->faceValue, position.quantity, 1);
                if (!faceNative.ok()) {
                    return Result<AccrualReport>::failure(faceNative.code, faceNative.message);
                }
                const auto faceRub = convertAmount(faceNative.value(), instrument->currency, market);
                if (!faceRub.ok()) {
                    return Result<AccrualReport>::failure(faceRub.code, faceRub.message);
                }
                if (!checkedAdd(portfolio.cashRUB, faceRub.value(), portfolio.cashRUB)) {
                    return Result<AccrualReport>::failure("overflow", "Переполнение при погашении облигации");
                }
                report.cashRubByPosition[position.id] += faceRub.value();
                continue;
            }
            remaining.push_back(position);
            continue;
        }

        remaining.push_back(position);
    }

    portfolio.positions = std::move(remaining);
    return Result<AccrualReport>::success(report);
}

Result<void> Portfolio::validate(const PortfolioData& portfolio, const MarketState& market)
{
    if (portfolio.cashRUB < 0) {
        return Result<void>::failure("invalid_state", "Отрицательный остаток денег");
    }
    std::vector<PositionId> ids;
    ids.reserve(portfolio.positions.size());
    for (const Position& position : portfolio.positions) {
        if (position.id <= 0 || position.instrumentId <= 0) {
            return Result<void>::failure("invalid_state", "Некорректный идентификатор позиции");
        }
        if (std::find(ids.begin(), ids.end(), position.id) != ids.end()) {
            return Result<void>::failure("invalid_state", "Повторяющийся идентификатор позиции");
        }
        ids.push_back(position.id);
        const Instrument* instrument = findInstrument(market, position.instrumentId);
        if (instrument == nullptr) {
            return Result<void>::failure("invalid_state", "Позиция ссылается на неизвестный инструмент");
        }
        if (position.quantity <= 0) {
            return Result<void>::failure("invalid_state", "Количество в позиции должно быть положительным");
        }
        if (instrument->kind == InstrumentKind::Deposit) {
            if (position.principal <= 0 || position.maturityMonth <= position.openMonth) {
                return Result<void>::failure("invalid_state", "Некорректные параметры вклада");
            }
        }
        const auto value = valuePosition(position, market);
        if (!value.ok()) {
            return Result<void>::failure(value.code, value.message);
        }
    }
    return Result<void>::success();
}
