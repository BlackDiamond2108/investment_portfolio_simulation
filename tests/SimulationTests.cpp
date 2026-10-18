/*
Расчётные проверки Qt Test. Поведения, а не копии формул реализации.
JSON проверяется во всех фазах, включая продолжение генератора после загрузки.
*/

#include "Simulation.h"
#include "GameStorage.h"
#include "MainWindow.h"
#include <QPushButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QLabel>
#include <QJsonDocument>
#include <QJsonObject>

#include <QtTest>

#include <cmath>
#include <limits>

class SimulationTests : public QObject {
    Q_OBJECT

private:
    static GameConfig defaultConfig()
    {
        return Simulation::makeDefaultConfig();
    }

    static void silenceMarket(GameConfig& config)
    {
        config.usdMinChange = 0.0;
        config.usdMaxChange = 0.0;
        config.eurMinChange = 0.0;
        config.eurMaxChange = 0.0;
        config.flowNoise = 0.0;
        for (Instrument& instrument : config.instruments) {
            instrument.minChange = 0.0;
            instrument.maxChange = 0.0;
            instrument.rateChangeMin = 0.0;
            instrument.rateChangeMax = 0.0;
        }
    }

    static const Instrument* byKind(const GameConfig& config, InstrumentKind kind, Currency currency)
    {
        for (const Instrument& instrument : config.instruments) {
            if (instrument.kind == kind && instrument.currency == currency) {
                return &instrument;
            }
        }
        return nullptr;
    }

    static const Instrument* firstKind(const GameConfig& config, InstrumentKind kind)
    {
        for (const Instrument& instrument : config.instruments) {
            if (instrument.kind == kind) {
                return &instrument;
            }
        }
        return nullptr;
    }

    static const Position* positionByInstrument(const GameState& state, InstrumentId id)
    {
        for (const Position& position : state.portfolio.positions) {
            if (position.instrumentId == id) {
                return &position;
            }
        }
        return nullptr;
    }

private slots:
    void configBounds();
    void fixedDeposit();
    void maturity();
    void trades();
    void flows();
    void liquidity();
    void market();
    void fullGame();
    void saveLoad();
    void windowGame();
    void terminalReports();
    void invalidRestores();
    void overflowAndAtomicity();
    void automaticFlowsContract();
    void bondAndCurrencyIncome();
};

void SimulationTests::configBounds()
{
    GameConfig ok12 = defaultConfig();
    ok12.monthCount = 12;
    QCOMPARE(Simulation::validateConfig(ok12).ok(), true);

    GameConfig ok30 = defaultConfig();
    ok30.monthCount = 30;
    QCOMPARE(Simulation::validateConfig(ok30).ok(), true);

    GameConfig tooSmall = defaultConfig();
    tooSmall.monthCount = 11;
    QCOMPARE(Simulation::validateConfig(tooSmall).ok(), false);

    GameConfig tooBig = defaultConfig();
    tooBig.monthCount = 31;
    QCOMPARE(Simulation::validateConfig(tooBig).ok(), false);

    GameConfig badCapital = defaultConfig();
    badCapital.initialCapital = 0;
    QCOMPARE(Simulation::validateConfig(badCapital).ok(), false);

    GameConfig notMultiple = defaultConfig();
    notMultiple.initialCapital = 1500 * kKopecksPerRuble;
    QCOMPARE(Simulation::validateConfig(notMultiple).ok(), true);

    GameConfig badTax = defaultConfig();
    badTax.taxRate = std::numeric_limits<double>::quiet_NaN();
    QCOMPARE(Simulation::validateConfig(badTax).ok(), false);

    Simulation simulation;
    QVERIFY(!simulation.start(tooSmall).ok());
    QVERIFY(simulation.start(ok12).ok());
}

void SimulationTests::fixedDeposit()
{
    GameConfig config = defaultConfig();
    silenceMarket(config);
    config.monthCount = 12;
    config.initialCapital = 12000 * kKopecksPerRuble;
    config.taxRate = 0.17;
    const Instrument* deposit = byKind(config, InstrumentKind::Deposit, Currency::RUB);
    QVERIFY(deposit != nullptr);
    for (Instrument& instrument : config.instruments) {
        if (instrument.id == deposit->id) {
            instrument.annualRate = 0.12;
            instrument.termMonths = 12;
        }
    }

    Simulation simulation;
    QVERIFY(simulation.start(config).ok());
    TradeRequest open;
    open.action = TradeAction::OpenDeposit;
    open.instrumentId = deposit->id;
    open.amount = 12000 * kKopecksPerRuble;
    QVERIFY(simulation.trade(open).ok());
    QVERIFY(simulation.confirmInitialAllocation().ok());
    QVERIFY(simulation.nextMonth().ok());

    const MonthReport& report = simulation.state().pending.report;
    QCOMPARE(static_cast<qlonglong>(report.profit), static_cast<qlonglong>(120 * kKopecksPerRuble));
    QCOMPARE(static_cast<qlonglong>(report.tax), static_cast<qlonglong>(2040));
    QCOMPARE(static_cast<qlonglong>(report.netProfit), static_cast<qlonglong>(9960));
}

void SimulationTests::maturity()
{
    GameConfig config = defaultConfig();
    silenceMarket(config);
    config.monthCount = 12;
    config.initialCapital = 13000 * kKopecksPerRuble;
    const Instrument* deposit = byKind(config, InstrumentKind::Deposit, Currency::RUB);
    QVERIFY(deposit != nullptr);
    InstrumentId depositId = deposit->id;
    for (Instrument& instrument : config.instruments) {
        if (instrument.id == depositId) {
            instrument.annualRate = 0.12;
            instrument.termMonths = 3;
            instrument.rateChangeMin = 0.01;
            instrument.rateChangeMax = 0.01;
            instrument.minRate = 0.08;
            instrument.maxRate = 0.18;
        }
    }

    Simulation simulation;
    QVERIFY(simulation.start(config).ok());
    TradeRequest open;
    open.action = TradeAction::OpenDeposit;
    open.instrumentId = depositId;
    open.amount = 12000 * kKopecksPerRuble;
    QVERIFY(simulation.trade(open).ok());
    QVERIFY(simulation.confirmInitialAllocation().ok());

    const Position* opened = positionByInstrument(simulation.state(), depositId);
    QVERIFY(opened != nullptr);
    const double locked = opened->lockedRate;
    QCOMPARE(locked, 0.12);

    QVERIFY(simulation.nextMonth().ok());
    QCOMPARE(static_cast<qlonglong>(simulation.state().pending.report.profit),
        static_cast<qlonglong>(120 * kKopecksPerRuble));
    QVERIFY(simulation.applyFlows(FlowRequest{}).ok());
    QVERIFY(simulation.finishMonth().ok());
    QCOMPARE(simulation.state().phase, Phase::ReadyForMonth);
    const Position* stillOpen = positionByInstrument(simulation.state(), depositId);
    QVERIFY(stillOpen != nullptr);
    QCOMPARE(stillOpen->lockedRate, 0.12);

    for (int i = 0; i < 2; ++i) {
        QVERIFY(simulation.nextMonth().ok());
        QCOMPARE(static_cast<qlonglong>(simulation.state().pending.report.profit),
            static_cast<qlonglong>(120 * kKopecksPerRuble));
        QVERIFY(simulation.applyFlows(FlowRequest{}).ok());
        QVERIFY(simulation.finishMonth().ok());
    }

    QVERIFY(positionByInstrument(simulation.state(), depositId) == nullptr);
    QVERIFY(simulation.state().portfolio.cashRUB > 12000 * kKopecksPerRuble);

    const Instrument* offer = Portfolio::findInstrument(simulation.state().market, depositId);
    QVERIFY(offer != nullptr);
    QVERIFY(offer->annualRate > 0.12);
}

void SimulationTests::trades()
{
    GameConfig config = defaultConfig();
    silenceMarket(config);
    config.monthCount = 12;
    Simulation simulation;
    QVERIFY(simulation.start(config).ok());

    const Instrument* rubDeposit = byKind(config, InstrumentKind::Deposit, Currency::RUB);
    const Instrument* stock = firstKind(config, InstrumentKind::Stock);
    const Instrument* metal = firstKind(config, InstrumentKind::Metal);
    const Instrument* bond = firstKind(config, InstrumentKind::Bond);
    QVERIFY(rubDeposit && stock && metal && bond);

    TradeRequest firstDeposit;
    firstDeposit.action = TradeAction::OpenDeposit;
    firstDeposit.instrumentId = rubDeposit->id;
    firstDeposit.amount = 10000 * kKopecksPerRuble;
    QVERIFY(simulation.trade(firstDeposit).ok());

    TradeRequest secondDeposit = firstDeposit;
    secondDeposit.amount = 5000 * kKopecksPerRuble;
    QVERIFY(simulation.trade(secondDeposit).ok());
    int depositCount = 0;
    for (const Position& position : simulation.state().portfolio.positions) {
        if (position.instrumentId == rubDeposit->id) {
            ++depositCount;
        }
    }
    QCOMPARE(depositCount, 2);

    TradeRequest buyStock;
    buyStock.action = TradeAction::Buy;
    buyStock.instrumentId = stock->id;
    buyStock.quantity = 10;
    QVERIFY(simulation.trade(buyStock).ok());

    TradeRequest buyMetal;
    buyMetal.action = TradeAction::Buy;
    buyMetal.instrumentId = metal->id;
    buyMetal.quantity = 2 * kMetalScale;
    QVERIFY(simulation.trade(buyMetal).ok());

    TradeRequest buyBond;
    buyBond.action = TradeAction::Buy;
    buyBond.instrumentId = bond->id;
    buyBond.quantity = 1;
    QVERIFY(simulation.trade(buyBond).ok());

    const Money cashBeforeFail = simulation.state().portfolio.cashRUB;
    TradeRequest tooExpensive = buyStock;
    tooExpensive.quantity = 1000000;
    QVERIFY(!simulation.trade(tooExpensive).ok());
    QCOMPARE(static_cast<qlonglong>(simulation.state().portfolio.cashRUB),
        static_cast<qlonglong>(cashBeforeFail));

    const Position* stockPosition = positionByInstrument(simulation.state(), stock->id);
    QVERIFY(stockPosition != nullptr);
    TradeRequest shortSale;
    shortSale.action = TradeAction::Sell;
    shortSale.positionId = stockPosition->id;
    shortSale.quantity = stockPosition->quantity + 1;
    QVERIFY(!simulation.trade(shortSale).ok());
    QCOMPARE(static_cast<qlonglong>(simulation.state().portfolio.cashRUB),
        static_cast<qlonglong>(cashBeforeFail));

    const Position* depositPosition = positionByInstrument(simulation.state(), rubDeposit->id);
    QVERIFY(depositPosition != nullptr);
    TradeRequest earlyClose;
    earlyClose.action = TradeAction::Sell;
    earlyClose.positionId = depositPosition->id;
    earlyClose.quantity = 1;
    QVERIFY(!simulation.trade(earlyClose).ok());
    QVERIFY(positionByInstrument(simulation.state(), rubDeposit->id) != nullptr);
}

void SimulationTests::flows()
{
    GameConfig config = defaultConfig();
    silenceMarket(config);
    config.monthCount = 12;
    config.initialCapital = 12000 * kKopecksPerRuble;
    const Instrument* deposit = byKind(config, InstrumentKind::Deposit, Currency::RUB);
    QVERIFY(deposit != nullptr);
    for (Instrument& instrument : config.instruments) {
        if (instrument.id == deposit->id) {
            instrument.annualRate = 0.12;
            instrument.termMonths = 12;
        }
    }

    Simulation simulation;
    QVERIFY(simulation.start(config).ok());
    TradeRequest open;
    open.action = TradeAction::OpenDeposit;
    open.instrumentId = deposit->id;
    open.amount = 12000 * kKopecksPerRuble;
    QVERIFY(simulation.trade(open).ok());
    QVERIFY(simulation.confirmInitialAllocation().ok());
    QVERIFY(simulation.nextMonth().ok());

    const Money profit = simulation.state().pending.report.profit;
    const Money navAfterTax = simulation.state().pending.report.navAfterTax;
    const Money unitPrice = simulation.state().pending.report.unitPrice;

    FlowRequest inflow;
    inflow.inflowRub = 2 * unitPrice;
    QVERIFY(simulation.applyFlows(inflow).ok());
    QCOMPARE(static_cast<qlonglong>(simulation.state().pending.report.profit), static_cast<qlonglong>(profit));
    QCOMPARE(static_cast<qlonglong>(simulation.state().pending.report.acceptedInflow),
        static_cast<qlonglong>(2 * unitPrice));
    QCOMPARE(static_cast<qlonglong>(simulation.state().pending.report.navEnd),
        static_cast<qlonglong>(navAfterTax + 2 * unitPrice));
    QVERIFY(simulation.finishMonth().ok());

    Simulation redeemSim;
    QVERIFY(redeemSim.start(config).ok());
    QVERIFY(redeemSim.trade(open).ok());
    QVERIFY(redeemSim.confirmInitialAllocation().ok());
    QVERIFY(redeemSim.nextMonth().ok());
    FlowRequest tooMany;
    tooMany.unitsToRedeem = redeemSim.state().unitsOutstanding + kUnitScale;
    QVERIFY(!redeemSim.applyFlows(tooMany).ok());
    QVERIFY(redeemSim.state().phase == Phase::AwaitingFlows);

    FlowRequest allUnits;
    allUnits.unitsToRedeem = redeemSim.state().unitsOutstanding;
    QVERIFY(redeemSim.applyFlows(allUnits).ok());
    QCOMPARE(redeemSim.state().phase, Phase::Finished);
    QVERIFY(redeemSim.state().unitsOutstanding == 0);
}

void SimulationTests::liquidity()
{
    GameConfig config = defaultConfig();
    silenceMarket(config);
    config.monthCount = 12;
    config.initialCapital = 12000 * kKopecksPerRuble;
    const Instrument* deposit = byKind(config, InstrumentKind::Deposit, Currency::RUB);
    const Instrument* stock = firstKind(config, InstrumentKind::Stock);
    QVERIFY(deposit && stock);
    for (Instrument& instrument : config.instruments) {
        if (instrument.id == deposit->id) {
            instrument.annualRate = 0.12;
            instrument.termMonths = 12;
        }
    }

    Simulation bankrupt;
    QVERIFY(bankrupt.start(config).ok());
    TradeRequest open;
    open.action = TradeAction::OpenDeposit;
    open.instrumentId = deposit->id;
    open.amount = 12000 * kKopecksPerRuble;
    QVERIFY(bankrupt.trade(open).ok());
    QVERIFY(bankrupt.confirmInitialAllocation().ok());
    QVERIFY(bankrupt.nextMonth().ok());
    QVERIFY(bankrupt.state().taxLiability > 0);
    QVERIFY(bankrupt.applyFlows(FlowRequest{}).ok());

    TradeRequest blockedBuy;
    blockedBuy.action = TradeAction::Buy;
    blockedBuy.instrumentId = stock->id;
    blockedBuy.quantity = 1;
    QVERIFY(!bankrupt.trade(blockedBuy).ok());
    QCOMPARE(QString::fromStdString(bankrupt.trade(blockedBuy).code), QString("debt_blocks_buy"));

    QVERIFY(bankrupt.finishMonth().ok());
    QCOMPARE(bankrupt.state().phase, Phase::Finished);
    QVERIFY(!bankrupt.state().finishReason.empty());

    GameConfig stockConfig = defaultConfig();
    silenceMarket(stockConfig);
    stockConfig.monthCount = 12;
    stockConfig.usdMinChange = 0.0;
    stockConfig.usdMaxChange = 0.0;
    for (Instrument& instrument : stockConfig.instruments) {
        if (instrument.kind == InstrumentKind::Stock) {
            instrument.minChange = 0.10;
            instrument.maxChange = 0.10;
        }
    }
    Simulation taxable;
    QVERIFY(taxable.start(stockConfig).ok());
    TradeRequest buy;
    buy.action = TradeAction::Buy;
    buy.instrumentId = stock->id;
    buy.quantity = 100;
    QVERIFY(taxable.trade(buy).ok());
    const Money leftoverCash = taxable.state().portfolio.cashRUB;
    QVERIFY(leftoverCash >= 0);
    QVERIFY(taxable.confirmInitialAllocation().ok());
    QVERIFY(taxable.nextMonth().ok());
    QVERIFY(taxable.state().pending.report.profit > 0);
    QVERIFY(taxable.applyFlows(FlowRequest{}).ok());
    if (taxable.state().taxLiability > 0) {
        QVERIFY(!taxable.trade(buy).ok());
        const Position* held = positionByInstrument(taxable.state(), stock->id);
        QVERIFY(held != nullptr);
        TradeRequest sell;
        sell.action = TradeAction::Sell;
        sell.positionId = held->id;
        sell.quantity = 1;
        QVERIFY(taxable.trade(sell).ok());
        QVERIFY(taxable.state().taxLiability < taxable.state().pending.report.tax
            || taxable.state().taxLiability == 0);
    }
}

void SimulationTests::market()
{
    GameConfig config = defaultConfig();
    silenceMarket(config);
    config.monthCount = 12;
    config.seed = 7;
    const Instrument* stock = firstKind(config, InstrumentKind::Stock);
    const Instrument* usdDeposit = byKind(config, InstrumentKind::Deposit, Currency::USD);
    QVERIFY(stock && usdDeposit);

    Simulation a;
    Simulation b;
    QVERIFY(a.start(config).ok());
    QVERIFY(b.start(config).ok());
    QVERIFY(a.confirmInitialAllocation().ok());
    QVERIFY(b.confirmInitialAllocation().ok());
    QVERIFY(a.nextMonth().ok());
    QVERIFY(b.nextMonth().ok());
    const Instrument* stockA = Portfolio::findInstrument(a.state().market, stock->id);
    const Instrument* stockB = Portfolio::findInstrument(b.state().market, stock->id);
    QVERIFY(stockA && stockB);
    QCOMPARE(static_cast<qlonglong>(stockA->price), static_cast<qlonglong>(stock->price));
    QCOMPARE(static_cast<qlonglong>(stockA->price), static_cast<qlonglong>(stockB->price));
    QCOMPARE(static_cast<qlonglong>(a.state().market.usdRub), static_cast<qlonglong>(config.usdRub));

    GameConfig fxConfig = defaultConfig();
    silenceMarket(fxConfig);
    fxConfig.monthCount = 12;
    fxConfig.usdMinChange = 0.10;
    fxConfig.usdMaxChange = 0.10;
    for (Instrument& instrument : fxConfig.instruments) {
        if (instrument.id == usdDeposit->id) {
            instrument.annualRate = 0.0;
            instrument.termMonths = 12;
        }
    }
    Simulation fx;
    QVERIFY(fx.start(fxConfig).ok());
    const Money usdPrincipal = 100 * kKopecksPerRuble;
    TradeRequest openUsd;
    openUsd.action = TradeAction::OpenDeposit;
    openUsd.instrumentId = usdDeposit->id;
    openUsd.amount = usdPrincipal;
    QVERIFY(fx.trade(openUsd).ok());
    QVERIFY(fx.confirmInitialAllocation().ok());
    const Money valueBefore = Portfolio::valuePosition(
        *positionByInstrument(fx.state(), usdDeposit->id), fx.state().market).value();
    QVERIFY(fx.nextMonth().ok());
    QCOMPARE(static_cast<qlonglong>(fx.state().market.usdRub), static_cast<qlonglong>(9900));
    const Position* usdPos = positionByInstrument(fx.state(), usdDeposit->id);
    QVERIFY(usdPos != nullptr);
    const Money valueAfter = Portfolio::valuePosition(*usdPos, fx.state().market).value();
    QVERIFY(valueAfter > valueBefore);
    QVERIFY(fx.state().pending.report.profit > 0);
}

void SimulationTests::fullGame()
{
    GameConfig config = defaultConfig();
    silenceMarket(config);
    config.monthCount = 12;
    Simulation simulation;
    QVERIFY(simulation.start(config).ok());
    QVERIFY(simulation.confirmInitialAllocation().ok());
    QCOMPARE(simulation.state().phase, Phase::ReadyForMonth);

    QVERIFY(!simulation.applyFlows(FlowRequest{}).ok());
    for (int month = 1; month <= 12; ++month) {
        QVERIFY(simulation.nextMonth().ok());
        QVERIFY(!simulation.nextMonth().ok());
        QVERIFY(simulation.applyFlows(FlowRequest{}).ok());
        QVERIFY(!simulation.applyFlows(FlowRequest{}).ok());
        if (month == 12) {
            const Instrument* stock = firstKind(config, InstrumentKind::Stock);
            TradeRequest buy;
            buy.action = TradeAction::Buy;
            buy.instrumentId = stock->id;
            buy.quantity = 1;
            QVERIFY(!simulation.trade(buy).ok());
        }
        QVERIFY(simulation.finishMonth().ok());
    }
    QCOMPARE(simulation.state().phase, Phase::Finished);
    QCOMPARE(static_cast<int>(simulation.state().history.size()), 12);
    QVERIFY(!simulation.nextMonth().ok());

    for (const MonthReport& report : simulation.state().history) {
        QCOMPARE(static_cast<qlonglong>(report.acceptedInflow), static_cast<qlonglong>(0));
        QCOMPARE(static_cast<qlonglong>(report.profit), static_cast<qlonglong>(report.netProfit + report.tax));
    }
}

void SimulationTests::saveLoad()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("game.json");
    GameConfig config = defaultConfig();
    config.seed = std::numeric_limits<std::uint64_t>::max();
    Simulation original;
    QVERIFY(original.start(config).ok());
    QVERIFY(original.trade({TradeAction::Buy, 3, 0, 0, 10}).ok());
    QVERIFY(original.trade({TradeAction::OpenDeposit, 2, 0, 10000, 0}).ok());
    auto roundTrip = [&]() {
        auto save = GameStorage::save(path, original.exportState());
        QVERIFY2(save.ok(), save.message.c_str());
        auto loaded = GameStorage::load(path);
        QVERIFY2(loaded.ok(), loaded.message.c_str());
        QCOMPARE(loaded.value().config.seed, config.seed);
        Simulation restored;
        QVERIFY(restored.restoreState(loaded.value()).ok());
        const auto secondPath = dir.filePath("copy.json");
        QVERIFY(GameStorage::save(secondPath, restored.exportState()).ok());
        QFile a(path), b(secondPath);
        QVERIFY(a.open(QIODevice::ReadOnly)); QVERIFY(b.open(QIODevice::ReadOnly));
        QCOMPARE(a.readAll(), b.readAll());
        if (original.state().phase == Phase::ReadyForMonth) {
            Simulation forward;
            QVERIFY(forward.restoreState(original.exportState()).ok());
            QVERIFY(forward.nextMonth().ok()); QVERIFY(restored.nextMonth().ok());
            QCOMPARE(forward.state().randomEngineState, restored.state().randomEngineState);
            QCOMPARE(forward.state().pending.report.profit, restored.state().pending.report.profit);
            QCOMPARE(forward.state().market.usdRub, restored.state().market.usdRub);
        }
    };
    roundTrip();
    QVERIFY(original.confirmInitialAllocation().ok());
    for (int i = 0; i < config.monthCount; ++i) {
        roundTrip();
        QVERIFY(original.nextMonth().ok()); roundTrip();
        QVERIFY(original.applyFlows({}).ok()); roundTrip();
        QVERIFY(original.finishMonth().ok());
    }
    roundTrip();
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll(); file.close();
    auto corrupt = [&](QJsonObject root) {
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(root).toJson()); file.close();
        QVERIFY(!GameStorage::load(path).ok());
    };
    auto root = QJsonDocument::fromJson(bytes).object();
    auto badVersion = root; badVersion["schemaVersion"] = 99; corrupt(badVersion);
    auto state = root["state"].toObject(); state["phase"] = "unknown";
    auto bad = root; bad["state"] = state; corrupt(bad);
    state = root["state"].toObject(); state.remove("pending");
    bad["state"] = state; corrupt(bad);
    state = root["state"].toObject(); state["unitsOutstanding"] = "9223372036854775808";
    bad["state"] = state; corrupt(bad);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("{broken"); file.close();
    QVERIFY(!GameStorage::load(path).ok());
    QVERIFY(!GameStorage::load(dir.filePath("absent.json")).ok());
    QVERIFY(!GameStorage::save(dir.path(), original.exportState()).ok());
    QVERIFY(GameStorage::save(path, original.exportState()).ok());
    auto invalid = original.exportState(); invalid.month = 31;
    QVERIFY(!GameStorage::save(path, invalid).ok());
    QVERIFY(GameStorage::load(path).ok());
}

void SimulationTests::terminalReports()
{
    QTemporaryDir dir;
    auto config = defaultConfig(); silenceMarket(config); config.taxRate = 0;
    config.initialCapital = 150050; // Не требуется целое число паёв или кратность 1000 RUB.
    Simulation cash;
    QVERIFY(cash.start(config).ok());
    QCOMPARE(cash.state().unitsOutstanding, Quantity(1500500));
    QVERIFY(cash.confirmInitialAllocation().ok()); QVERIFY(cash.nextMonth().ok());
    QVERIFY(cash.applyFlows({0, cash.state().unitsOutstanding}).ok());
    QCOMPARE(cash.state().phase, Phase::Finished);
    QCOMPARE(cash.state().history.size(), std::size_t(1));
    QCOMPARE(cash.statistics().totalRedemption, config.initialCapital);
    QCOMPARE(cash.statistics().finalNav, Money(0));
    QVERIFY(GameStorage::save(dir.filePath("closed.json"), cash.exportState()).ok());
    QVERIFY(GameStorage::load(dir.filePath("closed.json")).ok());

    config = defaultConfig(); silenceMarket(config); config.taxRate = 0;
    config.initialCapital = 10000 * kKopecksPerRuble;
    Simulation stocks;
    QVERIFY(stocks.start(config).ok());
    QVERIFY(stocks.trade({TradeAction::Buy, 3, 0, 0, 100}).ok());
    QVERIFY(stocks.confirmInitialAllocation().ok()); QVERIFY(stocks.nextMonth().ok());
    QVERIFY(stocks.applyFlows({0, stocks.state().unitsOutstanding}).ok());
    QCOMPARE(stocks.state().phase, Phase::Restructuring);
    QCOMPARE(stocks.state().shareholderLiability, config.initialCapital);
    QVERIFY(!stocks.finishMonth().ok()); // Долги нельзя забыть при возврате всех паёв.
    auto id = stocks.state().portfolio.positions.front().id;
    QVERIFY(stocks.trade({TradeAction::Sell, 0, id, 0, 100}).ok());
    QVERIFY(stocks.finishMonth().ok());
    QCOMPARE(stocks.state().phase, Phase::Finished);
    QCOMPARE(stocks.state().shareholderLiability, Money(0));
    QCOMPARE(stocks.state().history.size(), std::size_t(1));
    QVERIFY(Simulation::validateState(stocks.exportState()).ok());

    config = defaultConfig(); silenceMarket(config); config.initialCapital = 1200000;
    Simulation deposit;
    QVERIFY(deposit.start(config).ok());
    QVERIFY(deposit.trade({TradeAction::OpenDeposit, 1, 0, 1200000, 0}).ok());
    QVERIFY(deposit.confirmInitialAllocation().ok()); QVERIFY(deposit.nextMonth().ok());
    QVERIFY(deposit.state().taxLiability > 0);
    QVERIFY(deposit.applyFlows({10000, 0}).ok());
    QCOMPARE(deposit.state().taxLiability, Money(0)); // Новые средства оплачивают налог.
    QVERIFY(deposit.finishMonth().ok());
    QVERIFY(Simulation::validateState(deposit.exportState()).ok());
}

void SimulationTests::invalidRestores()
{
    Simulation simulation;
    QVERIFY(simulation.start(defaultConfig()).ok());
    QVERIFY(simulation.trade({TradeAction::OpenDeposit, 1, 0, 100000, 0}).ok());
    QVERIFY(simulation.confirmInitialAllocation().ok());
    QVERIFY(simulation.nextMonth().ok());
    const auto before = simulation.exportState();
    auto reject = [&](GameState bad) {
        QVERIFY(!simulation.restoreState(bad).ok());
        QCOMPARE(simulation.state().month, before.month);
        QCOMPARE(simulation.state().portfolio.cashRUB, before.portfolio.cashRUB);
        QCOMPARE(simulation.exportState().randomEngineState, before.randomEngineState);
    };
    auto bad = before; bad.phase = Phase::ReadyForMonth; reject(bad);
    bad = before; bad.market.usdRub = 0; reject(bad);
    bad = before; bad.market.instruments[2].price = -1; reject(bad);
    bad = before; bad.market.instruments[2].id = bad.market.instruments[0].id; reject(bad);
    bad = before; bad.market.instruments[0].currency = static_cast<Currency>(55); reject(bad);
    bad = before; bad.portfolio.positions[0].lockedRate = -1; reject(bad);
    bad = before; bad.portfolio.positions[0].openMonth = 4; reject(bad);
    bad = before; bad.nextPositionId = 1; reject(bad);
    bad = before; bad.pending.report.profit++; reject(bad);
    bad = before; bad.pending.flowsApplied = true; reject(bad);
    bad = before; bad.returnIndex = 2; reject(bad);
    bad = before; bad.randomEngineState += " junk"; reject(bad);
    QVERIFY(simulation.applyFlows({}).ok()); QVERIFY(simulation.finishMonth().ok());
    bad = simulation.exportState(); bad.history[0].month = 2;
    QVERIFY(!Simulation::validateState(bad).ok());
    bad = simulation.exportState(); bad.history[0].netProfit++;
    QVERIFY(!Simulation::validateState(bad).ok());
    bad = simulation.exportState(); bad.history[0].navEnd++;
    QVERIFY(!Simulation::validateState(bad).ok());
}

void SimulationTests::overflowAndAtomicity()
{
    Money result = 0;
    const auto min = std::numeric_limits<Money>::min();
    const auto max = std::numeric_limits<Money>::max();
    QVERIFY(!checkedSub(0, min, result));
    QVERIFY(checkedSub(min, min, result)); QCOMPARE(result, Money(0));
    QVERIFY(!floorUnitsFromMoney(max, 1).ok());
    QVERIFY(!moneyFromUnits(max, max).ok());
    QVERIFY(!mulDivRound(1, 2, -1).ok());
    auto config = defaultConfig(); silenceMarket(config);
    Simulation simulation; QVERIFY(simulation.start(config).ok());
    QVERIFY(simulation.confirmInitialAllocation().ok()); QVERIFY(simulation.nextMonth().ok());
    auto before = simulation.exportState();
    QVERIFY(!simulation.applyFlows({max, 0}).ok());
    QCOMPARE(simulation.state().portfolio.cashRUB, before.portfolio.cashRUB);
    QCOMPARE(simulation.state().unitsOutstanding, before.unitsOutstanding);
    config.instruments[2].price = max;
    config.instruments[2].minChange = 1; config.instruments[2].maxChange = 1;
    QVERIFY(simulation.start(config).ok()); QVERIFY(simulation.confirmInitialAllocation().ok());
    before = simulation.exportState();
    QVERIFY(!simulation.nextMonth().ok());
    QCOMPARE(simulation.state().month, before.month);
    QCOMPARE(simulation.exportState().randomEngineState, before.randomEngineState);
    config = defaultConfig(); config.instruments[0].rateChangeMin = .5; config.instruments[0].rateChangeMax = -.5;
    QVERIFY(!Simulation::validateConfig(config).ok());
}

void SimulationTests::automaticFlowsContract()
{
    auto config = defaultConfig(); config.flowMode = FlowMode::Automatic;
    Simulation simulation; QVERIFY(simulation.start(config).ok());
    QVERIFY(simulation.confirmInitialAllocation().ok()); QVERIFY(simulation.nextMonth().ok());
    const auto proposal = simulation.suggestedFlows();
    auto altered = proposal; ++altered.inflowRub;
    QVERIFY(!simulation.applyFlows(altered).ok());
    QCOMPARE(simulation.state().phase, Phase::AwaitingFlows);
    QVERIFY(simulation.applyFlows(proposal).ok());
    QVERIFY(!simulation.applyFlows(proposal).ok());
}

void SimulationTests::bondAndCurrencyIncome()
{
    auto config = defaultConfig(); silenceMarket(config); config.taxRate = 0;
    auto& bond = config.instruments[5]; bond.annualRate = .12; bond.maturityMonth = 8;
    Simulation simulation; QVERIFY(simulation.start(config).ok());
    QVERIFY(simulation.confirmInitialAllocation().ok());
    for (int month = 1; month <= 8; ++month) {
        QVERIFY(simulation.nextMonth().ok());
        const Money expected = month <= 2 ? 0 : 5000;
        QCOMPARE(simulation.state().pending.report.profit, expected);
        Money sum = 0;
        for (const auto& row : simulation.state().pending.report.income) sum += row.total;
        QCOMPARE(sum, expected);
        QVERIFY(simulation.applyFlows({}).ok());
        if (month == 2) {
            const auto preview = simulation.previewTrade({TradeAction::Buy, 6, 0, 0, 5});
            QVERIFY(preview.ok()); QCOMPARE(preview.value().cashDeltaRub, Money(-510000));
            QVERIFY(simulation.trade({TradeAction::Buy, 6, 0, 0, 5}).ok());
        }
        QVERIFY(simulation.finishMonth().ok());
        QVERIFY(Simulation::validateState(simulation.exportState()).ok());
    }
    QCOMPARE(simulation.state().portfolio.positions.size(), std::size_t(0));
    QCOMPARE(simulation.statistics().totalNetProfit, Money(30000));
    QCOMPARE(simulation.statistics().finalNav, config.initialCapital + 30000);

    config = defaultConfig(); silenceMarket(config); config.taxRate = 0;
    config.usdMinChange = .1; config.usdMaxChange = .1;
    config.instruments[1].annualRate = .12; config.instruments[1].termMonths = 2;
    QVERIFY(simulation.start(config).ok());
    QVERIFY(simulation.trade({TradeAction::OpenDeposit, 2, 0, 10000, 0}).ok());
    QVERIFY(simulation.confirmInitialAllocation().ok());
    QVERIFY(simulation.nextMonth().ok());
    QCOMPARE(simulation.state().pending.report.profit, Money(99900));
    QVERIFY(simulation.applyFlows({}).ok()); QVERIFY(simulation.finishMonth().ok());
    QVERIFY(simulation.nextMonth().ok());
    QCOMPARE(simulation.state().pending.report.profit, Money(110880));
    QVERIFY(simulation.applyFlows({}).ok()); QVERIFY(simulation.finishMonth().ok());
    QCOMPARE(simulation.state().portfolio.positions.size(), std::size_t(0));
    QCOMPARE(simulation.statistics().totalNetProfit, Money(210780));
}

void SimulationTests::windowGame()
{
    for (int mode = 0; mode < 2; ++mode) {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto button = [&](const char* name) { return window.findChild<QPushButton*>(name); };
        auto* tabs = window.findChild<QTabWidget*>("tabs");
        auto* months = window.findChild<QSpinBox*>("months");
        auto* modeBox = window.findChild<QComboBox*>("flowMode");
        QVERIFY(tabs && months && modeBox);
        const int count = mode == 0 ? 12 : 30;
        months->setValue(count); modeBox->setCurrentIndex(mode);
        QVERIFY(button("newGameButton"));
        button("newGameButton")->click();
        QCOMPARE(tabs->currentIndex(), 2);
        auto* value = window.findChild<QDoubleSpinBox*>("tradeValue");
        QVERIFY(value); value->setValue(10);
        QVERIFY(button("tradeButton")->isEnabled());
        button("tradeButton")->click();
        auto* positions = window.findChild<QTableWidget*>("portfolioTable");
        QVERIFY(positions); QCOMPARE(positions->rowCount(), 1);
        button("initialButton")->click();
        QVERIFY(button("nextButton")->isEnabled());
        QVERIFY(!button("tradeButton")->isEnabled());
        for (int month = 1; month <= count; ++month) {
            button("nextButton")->click();
            QVERIFY(!button("nextButton")->isEnabled());
            QVERIFY(button("flowsButton")->isEnabled());
            auto* inflow = window.findChild<QDoubleSpinBox*>("inflow");
            QCOMPARE(inflow->isEnabled(), mode == 0);
            if (mode == 0 && month == 1) inflow->setValue(1000);
            button("flowsButton")->click();
            QVERIFY(!button("flowsButton")->isEnabled());
            QVERIFY(button("finishButton")->isEnabled());
            button("finishButton")->click();
            QCOMPARE(window.findChild<QTableWidget*>("historyTable")->rowCount(), month);
        }
        QCOMPARE(tabs->currentIndex(), 3);
        QVERIFY(!button("nextButton")->isEnabled());
        QVERIFY(!button("finishButton")->isEnabled());
        QVERIFY(window.findChild<QLabel*>("phaseLabel")->text().contains("Партия завершена"));
        QApplication::processEvents();
        if (mode == 0 && !qEnvironmentVariableIsEmpty("PORTFOLIO_SCREENSHOT"))
            QVERIFY(window.grab().save(qEnvironmentVariable("PORTFOLIO_SCREENSHOT")));
    }
}

QTEST_MAIN(SimulationTests)
#include "SimulationTests.moc"
