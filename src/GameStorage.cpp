// Схема v1: int64/uint64 сохраняются строками, enum — именами, числа — без NaN.
#include "GameStorage.h"
#include "Simulation.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <stdexcept>
#include <type_traits>

namespace {
// Перечень полей общий для записи и чтения: новое поле не потеряется в одной из сторон.
template <class Visitor> void fields(Instrument& v, Visitor visit)
{
    visit("id", v.id);
    visit("name", v.name);
    visit("kind", v.kind);
    visit("currency", v.currency);
    visit("price", v.price);
    visit("annualRate", v.annualRate);
    visit("faceValue", v.faceValue);
    visit("termMonths", v.termMonths);
    visit("maturityMonth", v.maturityMonth);
    visit("couponPeriodMonths", v.couponPeriodMonths);
    visit("accruedCoupon", v.accruedCoupon);
    visit("minChange", v.minChange);
    visit("maxChange", v.maxChange);
    visit("minRate", v.minRate);
    visit("maxRate", v.maxRate);
    visit("rateChangeMin", v.rateChangeMin);
    visit("rateChangeMax", v.rateChangeMax);
    visit("tradable", v.tradable);
}
template <class Visitor> void fields(GameConfig& v, Visitor visit)
{
    visit("monthCount", v.monthCount);
    visit("initialCapital", v.initialCapital);
    visit("taxRate", v.taxRate);
    visit("flowMode", v.flowMode);
    visit("seed", v.seed);
    visit("instruments", v.instruments);
    visit("usdRub", v.usdRub);
    visit("eurRub", v.eurRub);
    visit("usdMinChange", v.usdMinChange);
    visit("usdMaxChange", v.usdMaxChange);
    visit("eurMinChange", v.eurMinChange);
    visit("eurMaxChange", v.eurMaxChange);
    visit("flowSensitivity", v.flowSensitivity);
    visit("flowNoise", v.flowNoise);
    visit("maxOutflowFraction", v.maxOutflowFraction);
    visit("maxInflowFraction", v.maxInflowFraction);
}
template <class Visitor> void fields(MarketState& v, Visitor visit)
{
    visit("instruments", v.instruments);
    visit("usdRub", v.usdRub);
    visit("eurRub", v.eurRub);
}
template <class Visitor> void fields(Position& v, Visitor visit)
{
    visit("id", v.id);
    visit("instrumentId", v.instrumentId);
    visit("quantity", v.quantity);
    visit("costBasisRub", v.costBasisRub);
    visit("principal", v.principal);
    visit("lockedRate", v.lockedRate);
    visit("accruedInterest", v.accruedInterest);
    visit("openMonth", v.openMonth);
    visit("maturityMonth", v.maturityMonth);
    visit("accruedCoupon", v.accruedCoupon);
}
template <class Visitor> void fields(PortfolioData& v, Visitor visit)
{
    visit("cashRUB", v.cashRUB);
    visit("positions", v.positions);
}
template <class Visitor> void fields(FlowRequest& v, Visitor visit)
{
    visit("inflowRub", v.inflowRub);
    visit("unitsToRedeem", v.unitsToRedeem);
}
template <class Visitor> void fields(IncomeRow& v, Visitor visit)
{
    visit("positionId", v.positionId);
    visit("instrumentId", v.instrumentId);
    visit("title", v.title);
    visit("interestIncome", v.interestIncome);
    visit("revaluation", v.revaluation);
    visit("total", v.total);
}
template <class Visitor> void fields(MonthReport& v, Visitor visit)
{
    visit("month", v.month);
    visit("navStart", v.navStart);
    visit("navAfterIncome", v.navAfterIncome);
    visit("navAfterTax", v.navAfterTax);
    visit("navEnd", v.navEnd);
    visit("profit", v.profit);
    visit("tax", v.tax);
    visit("netProfit", v.netProfit);
    visit("returnGross", v.returnGross);
    visit("returnNet", v.returnNet);
    visit("returnDefined", v.returnDefined);
    visit("acceptedInflow", v.acceptedInflow);
    visit("leftoverInflow", v.leftoverInflow);
    visit("unitsIssued", v.unitsIssued);
    visit("unitsRedeemed", v.unitsRedeemed);
    visit("redemptionValue", v.redemptionValue);
    visit("unitPrice", v.unitPrice);
    visit("income", v.income);
}
template <class Visitor> void fields(PendingMonth& v, Visitor visit)
{
    visit("report", v.report);
    visit("suggested", v.suggested);
    visit("flowsApplied", v.flowsApplied);
}
template <class Visitor> void fields(GameState& v, Visitor visit)
{
    visit("config", v.config);
    visit("month", v.month);
    visit("phase", v.phase);
    visit("market", v.market);
    visit("portfolio", v.portfolio);
    visit("unitsOutstanding", v.unitsOutstanding);
    visit("taxLiability", v.taxLiability);
    visit("shareholderLiability", v.shareholderLiability);
    visit("history", v.history);
    visit("pending", v.pending);
    visit("journal", v.journal);
    visit("randomEngineState", v.randomEngineState);
    visit("finishReason", v.finishReason);
    visit("startingNav", v.startingNav);
    visit("nextPositionId", v.nextPositionId);
    visit("returnIndex", v.returnIndex);
    visit("returnIndexPeak", v.returnIndexPeak);
    visit("maxDrawdown", v.maxDrawdown);
    visit("started", v.started);
}

template <class T> struct IsVector : std::false_type { };
template <class T> struct IsVector<std::vector<T>> : std::true_type { };
template <class T> QStringList enumNames();
template <> QStringList enumNames<Currency>()
{
    return { "RUB", "USD", "EUR" };
}
template <> QStringList enumNames<InstrumentKind>()
{
    return { "Deposit", "Bond", "Stock", "Metal" };
}
template <> QStringList enumNames<FlowMode>()
{
    return { "Manual", "Automatic" };
}
template <> QStringList enumNames<Phase>()
{
    return { "InitialAllocation", "ReadyForMonth", "AwaitingFlows", "Restructuring", "Finished" };
}
[[noreturn]] void invalid(const QString& path, const QString& reason)
{
    throw std::runtime_error((path + ": " + reason).toStdString());
}
template <class T> QJsonValue encode(const T& value)
{
    if constexpr (std::is_same_v<T, bool>)
        return value;
    else if constexpr (std::is_enum_v<T>) {
        const auto names = enumNames<T>();
        const int index = static_cast<int>(value);
        if (index < 0 || index >= names.size())
            invalid("enum", "неизвестное значение");
        return names[index];
    } else if constexpr (std::is_integral_v<T>) {
        if constexpr (sizeof(T) >= 8)
            return QString::number(value);
        else
            return static_cast<int>(value);
    } else if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(value))
            invalid("number", "неконечное число");
        return value;
    } else if constexpr (std::is_same_v<T, std::string>)
        return QString::fromStdString(value);
    else if constexpr (IsVector<T>::value) {
        QJsonArray array;
        for (const auto& item : value)
            array.append(encode(item));
        return array;
    } else {
        QJsonObject object;
        auto copy = value;
        fields(copy, [&](const char* name, const auto& item) { object[name] = encode(item); });
        return object;
    }
}
template <class T> void decode(const QJsonValue& json, T& out, const QString& path)
{
    if constexpr (std::is_same_v<T, bool>) {
        if (!json.isBool())
            invalid(path, "ожидалось логическое значение");
        out = json.toBool();
    } else if constexpr (std::is_enum_v<T>) {
        const auto names = enumNames<T>();
        const int i = names.indexOf(json.toString());
        if (!json.isString() || i < 0)
            invalid(path, "неизвестное значение перечисления");
        out = static_cast<T>(i);
    } else if constexpr (std::is_integral_v<T> && sizeof(T) >= 8) {
        static const QRegularExpression integer("^-?(0|[1-9][0-9]*)$");
        if (!json.isString() || !integer.match(json.toString()).hasMatch())
            invalid(path, "ожидалась десятичная строка целого числа");
        bool ok = false;
        if constexpr (std::is_unsigned_v<T>) {
            if (json.toString().startsWith('-'))
                invalid(path, "отрицательное беззнаковое число");
            out = json.toString().toULongLong(&ok);
        } else
            out = json.toString().toLongLong(&ok);
        if (!ok)
            invalid(path, "число вне диапазона int64/uint64");
    } else if constexpr (std::is_arithmetic_v<T>) {
        if (!json.isDouble() || !std::isfinite(json.toDouble()))
            invalid(path, "ожидалось конечное число");
        const double n = json.toDouble();
        if constexpr (std::is_integral_v<T>) {
            if (std::floor(n) != n || n < std::numeric_limits<T>::min() || n > std::numeric_limits<T>::max())
                invalid(path, "целое число вне диапазона");
        }
        out = static_cast<T>(n);
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (!json.isString())
            invalid(path, "ожидалась строка");
        out = json.toString().toStdString();
    } else if constexpr (IsVector<T>::value) {
        if (!json.isArray() || json.toArray().size() > 10000)
            invalid(path, "неверный или слишком большой массив");
        out.clear();
        int i = 0;
        for (const auto item : json.toArray()) {
            typename T::value_type value;
            decode(item, value, path + "[" + QString::number(i++) + "]");
            out.push_back(std::move(value));
        }
    } else {
        if (!json.isObject())
            invalid(path, "ожидался объект");
        const auto object = json.toObject();
        fields(out, [&](const char* name, auto& field) {
            if (!object.contains(name))
                invalid(path + "." + name, "отсутствует обязательное поле");
            decode(object[name], field, path + "." + name);
        });
    }
}
constexpr qint64 maxFileBytes = 10 * 1024 * 1024;
}

Result<void> GameStorage::save(const QString& path, const GameState& state)
{
    const auto valid = Simulation::validateState(state);
    if (!valid.ok())
        return valid;
    try {
        const QJsonObject root { { "schemaVersion", 1 }, { "state", encode(state) } };
        const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
        if (bytes.size() > maxFileBytes)
            return Result<void>::failure("file_size", "Партия превышает лимит 10 МиБ");
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            return Result<void>::failure("write_error", file.errorString().toStdString());
        return Result<void>::success();
    } catch (const std::exception& error) {
        return Result<void>::failure("invalid_state", error.what());
    }
}

Result<GameState> GameStorage::load(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return Result<GameState>::failure("read_error", file.errorString().toStdString());
    if (file.size() > maxFileBytes)
        return Result<GameState>::failure("file_size", "Файл превышает лимит 10 МиБ");
    QJsonParseError error;
    const auto bytes = file.read(maxFileBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > maxFileBytes)
        return Result<GameState>::failure(
            "read_error", "Не удалось прочитать файл целиком в пределах лимита");
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return Result<GameState>::failure(
            "invalid_json", "Некорректный JSON: " + error.errorString().toStdString());
    if (doc.object()["schemaVersion"] != QJsonValue(1))
        return Result<GameState>::failure("schema_version", "Неподдерживаемая версия сохранения");
    try {
        GameState state;
        decode(doc.object()["state"], state, "state");
        const auto valid = Simulation::validateState(state);
        if (!valid.ok())
            return Result<GameState>::failure(valid.code, valid.message);
        return Result<GameState>::success(std::move(state));
    } catch (const std::exception& exception) {
        return Result<GameState>::failure("invalid_format", exception.what());
    }
}
