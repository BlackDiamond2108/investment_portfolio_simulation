/*
Расчётные проверки Qt Test. Поведения, а не копии формул реализации.
Проверка saveLoad оставлена для GameStorage (второй участник).
*/

#include "Simulation.h"

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
    QCOMPARE(Simulation::validateConfig(notMultiple).ok(), false);

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
    QSKIP("Проверки JSON-сохранений выполняет участник с GameStorage");
}

QTEST_GUILESS_MAIN(SimulationTests)
#include "SimulationTests.moc"
