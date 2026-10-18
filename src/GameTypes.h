#pragma once

/*
Общие структуры игры; отдельного .cpp нет.
Это данные и небольшие перечисления, не набор классов с наследованием.

Деньги — int64 в копейках/центах. Паи — 6 знаков после запятой, металл — 0.001 г.
Ставки — конечные доли (0.17, а не 17). Курс — RUB (копейки) за 1.00 единицы валюты.
Ошибки везде через Result<T>. Portfolio и Simulation не зависят от Qt.
*/

#include <cstdint>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using InstrumentId = std::int64_t;
using PositionId = std::int64_t;
using Money = std::int64_t;
using Quantity = std::int64_t;

constexpr Money kKopecksPerRuble = 100;
constexpr Money kInitialUnitPrice = 1000 * kKopecksPerRuble;
constexpr Quantity kUnitScale = 1000000;
constexpr Quantity kMetalScale = 1000;

enum class Currency { RUB, USD, EUR };
enum class InstrumentKind { Deposit, Bond, Stock, Metal };
enum class Phase {
    InitialAllocation,
    ReadyForMonth,
    AwaitingFlows,
    Restructuring,
    Finished
};
enum class FlowMode { Manual, Automatic };
enum class TradeAction { Buy, Sell, OpenDeposit };

template <typename T>
struct Result {
    std::optional<T> data;
    std::string code;
    std::string message;

    bool ok() const { return data.has_value(); }
    const T& value() const { return *data; }

    static Result success(T value)
    {
        Result r;
        r.data = std::move(value);
        return r;
    }

    static Result failure(std::string errorCode, std::string errorMessage)
    {
        Result r;
        r.code = std::move(errorCode);
        r.message = std::move(errorMessage);
        return r;
    }
};

template <>
struct Result<void> {
    bool succeeded = false;
    std::string code;
    std::string message;

    bool ok() const { return succeeded; }

    static Result success()
    {
        Result r;
        r.succeeded = true;
        return r;
    }

    static Result failure(std::string errorCode, std::string errorMessage)
    {
        Result r;
        r.code = std::move(errorCode);
        r.message = std::move(errorMessage);
        return r;
    }
};

inline bool checkedAdd(Money a, Money b, Money& out)
{
    if ((b > 0 && a > std::numeric_limits<Money>::max() - b)
        || (b < 0 && a < std::numeric_limits<Money>::min() - b)) {
        return false;
    }
    out = a + b;
    return true;
}

inline bool checkedSub(Money a, Money b, Money& out)
{
    if ((b > 0 && a < std::numeric_limits<Money>::min() + b)
        || (b < 0 && a > std::numeric_limits<Money>::max() + b)) return false;
    out = a - b;
    return true;
}

inline Result<Money> mulDivRound(Money a, Money b, Money divisor)
{
    if (divisor <= 0) {
        return Result<Money>::failure("overflow", "Делитель должен быть положительным");
    }
    const __int128 n = static_cast<__int128>(a) * static_cast<__int128>(b);
    const __int128 d = divisor;
    const __int128 half = d / 2;
    const __int128 q = (n >= 0) ? ((n + half) / d) : ((n - half) / d);
    if (q > std::numeric_limits<Money>::max() || q < std::numeric_limits<Money>::min()) {
        return Result<Money>::failure("overflow", "Переполнение денежного расчёта");
    }
    return Result<Money>::success(static_cast<Money>(q));
}

inline Result<Money> roundRateMoney(Money amount, double rate)
{
    if (!std::isfinite(rate)) {
        return Result<Money>::failure("invalid_request", "Ставка не является конечным числом");
    }
    const long double v = static_cast<long double>(amount) * static_cast<long double>(rate);
    if (!std::isfinite(v)
        || v > static_cast<long double>(std::numeric_limits<Money>::max())
        || v < static_cast<long double>(std::numeric_limits<Money>::min())) {
        return Result<Money>::failure("overflow", "Переполнение при расчёте ставки");
    }
    return Result<Money>::success(static_cast<Money>(std::llround(v)));
}

// Усечение вниз для паёв; проверка до преобразования int128 -> int64.
inline Result<Money> mulDivFloor(Money a, Money b, Money divisor)
{
    if (a < 0 || b < 0 || divisor <= 0)
        return Result<Money>::failure("invalid_request", "Некорректные параметры количества паёв");
    const __int128 value = static_cast<__int128>(a) * b / divisor;
    if (value > std::numeric_limits<Money>::max())
        return Result<Money>::failure("overflow", "Переполнение количества или стоимости паёв");
    return Result<Money>::success(static_cast<Money>(value));
}
inline Result<Quantity> floorUnitsFromMoney(Money inflow, Money unitPrice)
{
    return mulDivFloor(inflow, kUnitScale, unitPrice);
}
inline Result<Money> moneyFromUnits(Quantity units, Money unitPrice)
{
    return mulDivFloor(units, unitPrice, kUnitScale);
}

inline Result<Money> convertToRub(Money amount, Currency currency, Money usdRub, Money eurRub)
{
    if (currency == Currency::RUB) {
        return Result<Money>::success(amount);
    }
    const Money rate = (currency == Currency::USD) ? usdRub : eurRub;
    if (rate <= 0) {
        return Result<Money>::failure("invalid_request", "Курс валюты должен быть положительным");
    }
    return mulDivRound(amount, rate, kKopecksPerRuble);
}

struct Instrument {
    InstrumentId id = 0;
    std::string name;
    InstrumentKind kind = InstrumentKind::Stock;
    Currency currency = Currency::RUB;
    Money price = 0;
    double annualRate = 0.0;
    Money faceValue = 0;
    int termMonths = 0;
    int maturityMonth = 0;
    int couponPeriodMonths = 0;
    Money accruedCoupon = 0;
    double minChange = 0.0;
    double maxChange = 0.0;
    double minRate = 0.0;
    double maxRate = 0.0;
    double rateChangeMin = 0.0;
    double rateChangeMax = 0.0;
    bool tradable = true;
};

struct GameConfig {
    int monthCount = 18;
    Money initialCapital = 560000 * kKopecksPerRuble;
    double taxRate = 0.17;
    FlowMode flowMode = FlowMode::Manual;
    std::uint64_t seed = 42;
    std::vector<Instrument> instruments;
    Money usdRub = 90 * kKopecksPerRuble;
    Money eurRub = 100 * kKopecksPerRuble;
    double usdMinChange = -0.03;
    double usdMaxChange = 0.03;
    double eurMinChange = -0.03;
    double eurMaxChange = 0.03;
    double flowSensitivity = 1.0;
    double flowNoise = 0.02;
    double maxOutflowFraction = 0.20;
    double maxInflowFraction = 0.30;
};

struct MarketState {
    std::vector<Instrument> instruments;
    Money usdRub = 0;
    Money eurRub = 0;
};

struct Position {
    PositionId id = 0;
    InstrumentId instrumentId = 0;
    Quantity quantity = 0;
    Money costBasisRub = 0;
    Money principal = 0;
    double lockedRate = 0.0;
    Money accruedInterest = 0;
    int openMonth = 0;
    int maturityMonth = 0;
    Money accruedCoupon = 0;
};

struct PortfolioData {
    Money cashRUB = 0;
    std::vector<Position> positions;
};

struct TradeRequest {
    TradeAction action = TradeAction::Buy;
    InstrumentId instrumentId = 0;
    PositionId positionId = 0;
    Money amount = 0;
    Quantity quantity = 0;
};

struct TradePreview {
    Money cashDeltaRub = 0;
    Money cashAfter = 0;
    Money positionValueRub = 0;
    std::string description;
};

struct FlowRequest {
    Money inflowRub = 0;
    Quantity unitsToRedeem = 0;
};

struct IncomeRow {
    PositionId positionId = 0;
    InstrumentId instrumentId = 0;
    std::string title;
    Money interestIncome = 0;
    Money revaluation = 0;
    Money total = 0;
};

struct MonthReport {
    int month = 0;
    Money navStart = 0;
    Money navAfterIncome = 0;
    Money navAfterTax = 0;
    Money navEnd = 0;
    Money profit = 0;
    Money tax = 0;
    Money netProfit = 0;
    double returnGross = 0.0;
    double returnNet = 0.0;
    bool returnDefined = true;
    Money acceptedInflow = 0;
    Money leftoverInflow = 0;
    Quantity unitsIssued = 0;
    Quantity unitsRedeemed = 0;
    Money redemptionValue = 0;
    Money unitPrice = 0;
    std::vector<IncomeRow> income;
};

struct PendingMonth {
    MonthReport report;
    FlowRequest suggested;
    bool flowsApplied = false;
};

struct GameState {
    GameConfig config;
    int month = 0;
    Phase phase = Phase::InitialAllocation;
    MarketState market;
    PortfolioData portfolio;
    Quantity unitsOutstanding = 0;
    Money taxLiability = 0;
    Money shareholderLiability = 0;
    std::vector<MonthReport> history;
    PendingMonth pending;
    std::vector<std::string> journal;
    std::string randomEngineState;
    std::string finishReason;
    Money startingNav = 0;
    PositionId nextPositionId = 1;
    double returnIndex = 1.0;
    double returnIndexPeak = 1.0;
    double maxDrawdown = 0.0;
    bool started = false;
};

struct GameStatistics {
    Money finalNav = 0;
    Money startingNav = 0;
    Money totalNetProfit = 0;
    Money totalGrossProfit = 0;
    Money totalTax = 0;
    Money totalInflow = 0;
    Money totalRedemption = 0;
    Quantity unitsOutstanding = 0;
    double cumulativeReturn = 0.0;
    double maxDrawdown = 0.0;
    std::string finishReason;
};
