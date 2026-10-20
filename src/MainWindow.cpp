// Экран игры и редактор сценария. Все команды проходят проверки Simulation.
#include "MainWindow.h"
#include "ui_MainWindow.h"
#include <QtWidgets>
#include <algorithm>

namespace {
QString text(const std::string& s)
{
    return QString::fromStdString(s);
}
QString money(Money value)
{
    return QLocale().toString(value / 100.0, 'f', 2);
}
QString percent(double value)
{
    return QLocale().toString(value * 100, 'f', 2) + " %";
}
QString currency(Currency c)
{
    return c == Currency::RUB ? "RUB" : c == Currency::USD ? "USD" : "EUR";
}
QString kind(InstrumentKind k)
{
    switch (k) {
    case InstrumentKind::Deposit:
        return "Вклад";
    case InstrumentKind::Bond:
        return "Облигация";
    case InstrumentKind::Stock:
        return "Акция";
    case InstrumentKind::Metal:
        return "Металл";
    }
    return { };
}
QString phaseText(Phase p)
{
    switch (p) {
    case Phase::InitialAllocation:
        return "Распределение капитала";
    case Phase::ReadyForMonth:
        return "Готов к следующему месяцу";
    case Phase::AwaitingFlows:
        return "Поступления и возврат паёв";
    case Phase::Restructuring:
        return "Реструктуризация портфеля";
    case Phase::Finished:
        return "Партия завершена";
    }
    return { };
}
QDoubleSpinBox* number(const QString& name, double min, double max, int digits = 2)
{
    auto* w = new QDoubleSpinBox;
    w->setObjectName(name);
    w->setDecimals(digits);
    w->setRange(min, max);
    w->setGroupSeparatorShown(true);
    w->setMinimumWidth(150);
    w->setKeyboardTracking(false);
    return w;
}
QTableWidget* table(const QString& name, const QStringList& headers)
{
    auto* w = new QTableWidget(0, headers.size());
    w->setObjectName(name);
    w->setHorizontalHeaderLabels(headers);
    w->setEditTriggers(QAbstractItemView::NoEditTriggers);
    w->setSelectionBehavior(QAbstractItemView::SelectRows);
    w->setSelectionMode(QAbstractItemView::SingleSelection);
    w->setAlternatingRowColors(true);
    w->verticalHeader()->hide();
    w->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    w->horizontalHeader()->setStretchLastSection(true);
    w->setShowGrid(false);
    return w;
}
void cell(QTableWidget* w, int row, int column, const QString& value)
{
    auto* item = new QTableWidgetItem(value);
    if (column > 1)
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    w->setItem(row, column, item);
}
QWidget* page(QTabWidget* tabs, const QString& title, QVBoxLayout*& layout)
{
    auto* w = new QWidget;
    layout = new QVBoxLayout(w);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    tabs->addTab(w, title);
    return w;
}
QLabel* hint(const QString& value)
{
    auto* w = new QLabel(value);
    w->setWordWrap(true);
    w->setProperty("hint", true);
    return w;
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(std::make_unique<Ui::MainWindow>())
{
    ui->setupUi(this);
    setMinimumSize(1000, 760);
    ui->phaseLabel->setMinimumHeight(70);
    ui->tabs->tabBar()->setUsesScrollButtons(false);
    ui->tabs->tabBar()->setElideMode(Qt::ElideNone);
    setStyleSheet(R"(
        QMainWindow { background: #eef3f5; }
        QWidget { font-family: "DejaVu Sans"; font-size: 12px; color: #18333e; }
        QLabel#titleLabel { font-size: 26px; font-weight: 700; }
        QLabel#subtitleLabel, QLabel[hint="true"] { color: #526d78; }
        QLabel[card="true"] { background: white; padding: 16px; border: 1px solid #d6e3e7; border-radius: 8px; font-size: 15px; }
        QLabel#phaseLabel { background: #dceeea; color: #16574d; padding: 12px; border-radius: 6px; }
        QTabWidget::pane { background: white; border: 1px solid #d6e3e7; border-radius: 6px; }
        QTabBar::tab { padding: 11px 18px; background: #e3ecef; border: none; }
        QTabBar::tab:selected { background: white; color: #087d6c; font-weight: bold; }
        QPushButton { background: #087d6c; color: white; border: none; border-radius: 5px; padding: 10px 14px; }
        QPushButton:hover { background: #096555; }
        QPushButton:disabled { background: #d9e2e5; color: #6e8088; }
        QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { padding: 6px; background: white; border: 1px solid #b9cdd4; border-radius: 4px; }
        QTableWidget { background: white; alternate-background-color: #f4f8f9; border: 1px solid #dce6e9; selection-background-color: #cfeae5; selection-color: #153f38; }
        QHeaderView::section { background: #edf4f6; padding: 9px; border: none; color: #49636e; }
        QGroupBox { font-weight: bold; border: 1px solid #d6e3e7; border-radius: 6px; margin-top: 12px; padding: 15px; }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; }
        QPlainTextEdit { background: #f8fafb; border: 1px solid #dce6e9; }
    )");
    buildPages();
    setupConnections();
    fillSettings(Simulation::makeDefaultConfig());
    refreshView();
    ui->tabs->setCurrentIndex(4);
}
MainWindow::~MainWindow() = default;

void MainWindow::buildPages()
{
    auto card = [&](const char* name) {
        auto* label = new QLabel;
        label->setObjectName(name);
        label->setProperty("card", true);
        label->setTextFormat(Qt::RichText);
        ui->summaryLayout->addWidget(label, 1);
        return label;
    };
    navLabel_ = card("navLabel");
    cashLabel_ = card("cashLabel");
    debtLabel_ = card("debtLabel");
    returnLabel_ = card("returnLabel");
    QVBoxLayout* layout;
    page(ui->tabs, "Портфель", layout);
    layout->addWidget(hint("Вложения можно менять до старта и после расчёта потоков. Срочные вклады "
                           "возвращаются автоматически по окончании срока."));
    portfolioTable_ = table("portfolioTable",
        { "Позиция", "Инструмент", "Тип", "Валюта", "Количество / сумма", "Ставка", "Погашение",
            "Стоимость, RUB" });
    layout->addWidget(portfolioTable_);
    layout->addWidget(hint("Выберите позицию двойным щелчком, чтобы перейти к её продаже. Доход по всем "
                           "позициям, включая погашенные, находится в истории."));

    page(ui->tabs, "Рынок", layout);
    layout->addWidget(hint("Учебные котировки, не реальные биржевые данные. Цена металла указана за грамм. "
                           "Ставка вклада действует для нового договора."));
    marketRates_ = hint("");
    layout->addWidget(marketRates_);
    marketTable_ = table("marketTable",
        { "Инструмент", "Тип", "Валюта", "Цена / номинал", "Годовая ставка", "Срок / погашение",
            "Изменение за месяц", "Доступен" });
    layout->addWidget(marketTable_);

    page(ui->tabs, "Операции", layout);
    auto* tradeBox = new QGroupBox("Покупка, продажа и открытие вклада");
    auto* form = new QFormLayout(tradeBox);
    tradeAction_ = new QComboBox;
    tradeAction_->setObjectName("tradeAction");
    tradeAction_->addItems({ "Купить актив", "Продать позицию", "Открыть вклад" });
    tradeTarget_ = new QComboBox;
    tradeTarget_->setObjectName("tradeTarget");
    tradeValue_ = number("tradeValue", 0, 1e9, 3);
    tradeValue_->setValue(1);
    tradeUnit_ = new QLabel("Количество");
    form->addRow("Операция", tradeAction_);
    form->addRow("Инструмент / позиция", tradeTarget_);
    form->addRow(tradeUnit_, tradeValue_);
    tradePreview_ = hint("");
    tradePreview_->setObjectName("tradePreview");
    form->addRow(tradePreview_);
    tradeButton_ = new QPushButton("Выполнить операцию");
    tradeButton_->setObjectName("tradeButton");
    form->addRow(tradeButton_);
    layout->addWidget(tradeBox);
    auto* flowBox = new QGroupBox("Поступления и возврат паёв");
    form = new QFormLayout(flowBox);
    flowInfo_ = hint("");
    flowInfo_->setObjectName("flowInfo");
    form->addRow(flowInfo_);
    inflow_ = number("inflow", 0, 1e12);
    redeem_ = number("redeem", 0, 1e12, 6);
    form->addRow("Новые средства, RUB", inflow_);
    form->addRow("Вернуть паёв, шт.", redeem_);
    flowsButton_ = new QPushButton("Подтвердить потоки месяца");
    flowsButton_->setObjectName("flowsButton");
    form->addRow(flowsButton_);
    layout->addWidget(flowBox);
    layout->addStretch();

    page(ui->tabs, "История и итоги", layout);
    statsLabel_ = hint("");
    statsLabel_->setObjectName("statsLabel");
    layout->addWidget(statsLabel_);
    historyTable_ = table("historyTable",
        { "Месяц", "Статус", "Капитал начала", "Прибыль", "Налог", "Доходность", "Приток", "Возврат",
            "Капитал конца" });
    layout->addWidget(historyTable_, 3);
    incomeTable_
        = table("incomeTable", { "Позиция", "Инструмент", "Проценты / купон", "Переоценка", "Доход, RUB" });
    layout->addWidget(incomeTable_, 2);
    journal_ = new QPlainTextEdit;
    journal_->setReadOnly(true);
    journal_->setMaximumHeight(95);
    layout->addWidget(journal_, 1);

    auto* settingsPage = page(ui->tabs, "Новая игра", layout);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    auto* settings = new QVBoxLayout(content);
    scroll->setWidget(content);
    layout->addWidget(scroll);
    auto* columns = new QHBoxLayout;
    auto* general = new QGroupBox("Параметры фонда");
    auto* g = new QFormLayout(general);
    months_ = new QSpinBox;
    months_->setObjectName("months");
    months_->setRange(12, 30);
    capital_ = number("capital", 0.01, 1e12);
    capital_->setSingleStep(1000);
    tax_ = number("tax", 0, 100);
    seed_ = new QLineEdit;
    seed_->setObjectName("seed");
    seed_->setValidator(new QRegularExpressionValidator(QRegularExpression("[0-9]{1,20}"), seed_));
    flowMode_ = new QComboBox;
    flowMode_->setObjectName("flowMode");
    flowMode_->addItems({ "Ручные потоки", "Автоматические потоки" });
    g->addRow("Месяцев", months_);
    g->addRow("Капитал, RUB", capital_);
    g->addRow("Налог на прибыль, %", tax_);
    g->addRow("Seed — повторяемость", seed_);
    g->addRow("Паи", flowMode_);
    columns->addWidget(general);
    auto* fx = new QGroupBox("Валюты и спрос на паи");
    auto* x = new QFormLayout(fx);
    usd_ = number("usd", 0.01, 1e6);
    eur_ = number("eur", 0.01, 1e6);
    usdMin_ = number("usdMin", -99.99, 100);
    usdMax_ = number("usdMax", -99.99, 100);
    eurMin_ = number("eurMin", -99.99, 100);
    eurMax_ = number("eurMax", -99.99, 100);
    auto range = [](QDoubleSpinBox* a, QDoubleSpinBox* b) {
        auto* w = new QWidget;
        auto* h = new QHBoxLayout(w);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(a);
        h->addWidget(new QLabel("…"));
        h->addWidget(b);
        return w;
    };
    x->addRow("USD / EUR, RUB", range(usd_, eur_));
    x->addRow("Изменение USD, %", range(usdMin_, usdMax_));
    x->addRow("Изменение EUR, %", range(eurMin_, eurMax_));
    sensitivity_ = number("sensitivity", 0, 100);
    noise_ = number("noise", 0, 100);
    maxIn_ = number("maxIn", 0, 100);
    maxOut_ = number("maxOut", 0, 100);
    x->addRow("Чувствительность / шум, %", range(sensitivity_, noise_));
    sensitivity_->setToolTip("Коэффициент k, без процентов. Шум справа — в процентах.");
    x->addRow("Макс. приток / отток, %", range(maxIn_, maxOut_));
    columns->addWidget(fx);
    settings->addLayout(columns);
    settings->addWidget(hint("Начальный портфель собирается после создания игры. Изменение этих полей не "
                             "влияет на уже начатую партию."));
    catalogTable_ = table(
        "catalogTable", { "Название", "Тип", "Валюта", "Цена / номинал", "Ставка", "Срок", "Разброс цены" });
    catalogTable_->setMinimumHeight(235);
    settings->addWidget(catalogTable_);
    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton("Добавить инструмент");
    auto* edit = new QPushButton("Изменить выбранный");
    auto* remove = new QPushButton("Удалить выбранный");
    buttons->addWidget(add);
    buttons->addWidget(edit);
    buttons->addWidget(remove);
    settings->addLayout(buttons);
    auto* create = new QPushButton("Создать новую игру");
    create->setObjectName("newGameButton");
    layout->addWidget(create);
    connect(create, &QPushButton::clicked, this, &MainWindow::onNewGame);
    connect(add, &QPushButton::clicked, this, [this] { editInstrument(-1); });
    connect(edit, &QPushButton::clicked, this, [this] {
        if (catalogTable_->currentRow() >= 0)
            editInstrument(catalogTable_->currentRow());
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        int row = catalogTable_->currentRow();
        if (row >= 0) {
            draft_.instruments.erase(draft_.instruments.begin() + row);
            refreshCatalog();
        }
    });
    connect(
        catalogTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { editInstrument(row); });
    Q_UNUSED(settingsPage);
}

void MainWindow::setupConnections()
{
    connect(ui->initialButton, &QPushButton::clicked, this, &MainWindow::onConfirmInitialAllocation);
    connect(ui->nextButton, &QPushButton::clicked, this, &MainWindow::onNextMonth);
    connect(ui->finishButton, &QPushButton::clicked, this, &MainWindow::onFinishMonth);
    connect(tradeButton_, &QPushButton::clicked, this, &MainWindow::onTrade);
    connect(flowsButton_, &QPushButton::clicked, this, &MainWindow::onApplyFlows);
    connect(tradeAction_, &QComboBox::currentIndexChanged, this, [this] { refreshTradeChoices(); });
    connect(tradeTarget_, &QComboBox::currentIndexChanged, this, &MainWindow::refreshPreview);
    connect(tradeValue_, &QDoubleSpinBox::valueChanged, this, &MainWindow::refreshPreview);
    connect(historyTable_, &QTableWidget::itemSelectionChanged, this, &MainWindow::refreshIncome);
    connect(ui->actionNew, &QAction::triggered, this, [this] { ui->tabs->setCurrentIndex(4); });
    connect(ui->actionSave, &QAction::triggered, this, &MainWindow::onSave);
    connect(ui->actionSaveAs, &QAction::triggered, this, [this] {
        auto old = filePath_;
        filePath_.clear();
        if (!saveGame())
            filePath_ = old;
    });
    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::onLoad);
    connect(ui->actionExit, &QAction::triggered, this, &QWidget::close);
    connect(portfolioTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        auto id = portfolioTable_->item(row, 0)->data(Qt::UserRole);
        tradeAction_->setCurrentIndex(1);
        refreshTradeChoices();
        tradeTarget_->setCurrentIndex(tradeTarget_->findData(id));
        ui->tabs->setCurrentIndex(2);
    });
}

void MainWindow::fillSettings(const GameConfig& c)
{
    draft_ = c;
    months_->setValue(c.monthCount);
    capital_->setValue(c.initialCapital / 100.0);
    tax_->setValue(c.taxRate * 100);
    seed_->setText(QString::number(c.seed));
    flowMode_->setCurrentIndex(static_cast<int>(c.flowMode));
    usd_->setValue(c.usdRub / 100.0);
    eur_->setValue(c.eurRub / 100.0);
    usdMin_->setValue(c.usdMinChange * 100);
    usdMax_->setValue(c.usdMaxChange * 100);
    eurMin_->setValue(c.eurMinChange * 100);
    eurMax_->setValue(c.eurMaxChange * 100);
    sensitivity_->setValue(c.flowSensitivity);
    noise_->setValue(c.flowNoise * 100);
    maxIn_->setValue(c.maxInflowFraction * 100);
    maxOut_->setValue(c.maxOutflowFraction * 100);
    refreshCatalog();
}
Result<GameConfig> MainWindow::collectConfig() const
{
    GameConfig c = draft_;
    bool ok = false;
    c.seed = seed_->text().toULongLong(&ok);
    if (!ok)
        return Result<GameConfig>::failure(
            "seed", "Seed должен быть целым числом от 0 до 18446744073709551615");
    c.monthCount = months_->value();
    c.initialCapital = std::llround(capital_->value() * 100);
    c.taxRate = tax_->value() / 100;
    c.flowMode = static_cast<FlowMode>(flowMode_->currentIndex());
    c.usdRub = std::llround(usd_->value() * 100);
    c.eurRub = std::llround(eur_->value() * 100);
    c.usdMinChange = usdMin_->value() / 100;
    c.usdMaxChange = usdMax_->value() / 100;
    c.eurMinChange = eurMin_->value() / 100;
    c.eurMaxChange = eurMax_->value() / 100;
    c.flowSensitivity = sensitivity_->value();
    c.flowNoise = noise_->value() / 100;
    c.maxInflowFraction = maxIn_->value() / 100;
    c.maxOutflowFraction = maxOut_->value() / 100;
    const auto valid = Simulation::validateConfig(c);
    if (!valid.ok())
        return Result<GameConfig>::failure(valid.code, valid.message);
    return Result<GameConfig>::success(c);
}
void MainWindow::refreshCatalog()
{
    catalogTable_->setRowCount(static_cast<int>(draft_.instruments.size()));
    int row = 0;
    for (const auto& i : draft_.instruments) {
        cell(catalogTable_, row, 0, text(i.name));
        cell(catalogTable_, row, 1, kind(i.kind));
        cell(catalogTable_, row, 2, currency(i.currency));
        cell(catalogTable_, row, 3,
            i.kind == InstrumentKind::Deposit
                ? "—"
                : money(i.kind == InstrumentKind::Bond ? i.faceValue : i.price));
        cell(catalogTable_, row, 4, percent(i.annualRate));
        cell(catalogTable_, row, 5,
            QString::number(i.kind == InstrumentKind::Bond ? i.maturityMonth : i.termMonths));
        cell(catalogTable_, row, 6, percent(i.minChange) + " … " + percent(i.maxChange));
        ++row;
    }
}
void MainWindow::editInstrument(int row)
{
    Instrument i;
    if (row >= 0)
        i = draft_.instruments.at(row);
    else {
        for (const auto& old : draft_.instruments)
            i.id = std::max(i.id, old.id);
        ++i.id;
        i.name = "Новый инструмент";
        i.price = 10000;
        i.faceValue = 100000;
        i.annualRate = .1;
        i.minRate = 0;
        i.maxRate = .3;
        i.termMonths = 12;
        i.maturityMonth = 24;
        i.couponPeriodMonths = 6;
    }
    QDialog dialog(this);
    dialog.setWindowTitle("Условия инструмента");
    auto* f = new QFormLayout(&dialog);
    auto* name = new QLineEdit(text(i.name));
    auto* type = new QComboBox;
    auto* curr = new QComboBox;
    type->addItems({ "Вклад", "Облигация", "Акция", "Металл" });
    type->setCurrentIndex(static_cast<int>(i.kind));
    curr->addItems({ "RUB", "USD", "EUR" });
    curr->setCurrentIndex(static_cast<int>(i.currency));
    f->addRow("Название / банк", name);
    f->addRow("Тип", type);
    f->addRow("Валюта", curr);
    auto field = [&](const QString& title, double value, double min, double max, int decimals = 2) {
        auto* w = number("", min, max, decimals);
        w->setValue(value);
        f->addRow(title, w);
        return w;
    };
    auto* price = field("Цена / номинал в валюте",
        (i.kind == InstrumentKind::Bond ? i.faceValue : i.price) / 100.0, .01, 1e9);
    auto* rate = field("Годовая ставка, %", i.annualRate * 100, 0, 100);
    auto* term = field("Срок вклада, месяцев", i.termMonths, 1, 360, 0);
    auto* maturity = field("Месяц погашения облигации", i.maturityMonth, 1, 360, 0);
    auto* coupon = field("Купон каждые N месяцев", i.couponPeriodMonths, 1, 360, 0);
    auto* low = field("Мин. изменение цены, %", i.minChange * 100, -99.99, 100);
    auto* high = field("Макс. изменение цены, %", i.maxChange * 100, -99.99, 100);
    auto* minRate = field("Мин. ставка новых вкладов, %", i.minRate * 100, 0, 100);
    auto* maxRate = field("Макс. ставка новых вкладов, %", i.maxRate * 100, 0, 100);
    auto* rateLow = field("Мин. изменение ставки, п.п.", i.rateChangeMin * 100, -100, 100);
    auto* rateHigh = field("Макс. изменение ставки, п.п.", i.rateChangeMax * 100, -100, 100);
    auto update = [&] {
        auto k = static_cast<InstrumentKind>(type->currentIndex());
        bool dep = k == InstrumentKind::Deposit, bond = k == InstrumentKind::Bond;
        price->setEnabled(!dep);
        rate->setEnabled(dep || bond);
        term->setEnabled(dep);
        maturity->setEnabled(bond);
        coupon->setEnabled(bond);
        low->setEnabled(!dep && !bond);
        high->setEnabled(!dep && !bond);
        for (auto* w : { minRate, maxRate, rateLow, rateHigh })
            w->setEnabled(dep);
    };
    connect(type, &QComboBox::currentIndexChanged, &dialog, update);
    update();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    f->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    i.name = name->text().trimmed().toStdString();
    i.kind = static_cast<InstrumentKind>(type->currentIndex());
    i.currency = static_cast<Currency>(curr->currentIndex());
    i.price = std::llround(price->value() * 100);
    i.faceValue = i.price;
    i.annualRate = rate->value() / 100;
    i.termMonths = static_cast<int>(term->value());
    i.maturityMonth = static_cast<int>(maturity->value());
    i.couponPeriodMonths = static_cast<int>(coupon->value());
    i.minChange = low->value() / 100;
    i.maxChange = high->value() / 100;
    i.minRate = minRate->value() / 100;
    i.maxRate = maxRate->value() / 100;
    i.rateChangeMin = rateLow->value() / 100;
    i.rateChangeMax = rateHigh->value() / 100;
    if (row >= 0)
        draft_.instruments[row] = i;
    else
        draft_.instruments.push_back(i);
    refreshCatalog();
}

void MainWindow::refreshTradeChoices()
{
    const auto selected = tradeTarget_->currentData();
    QSignalBlocker blocker(tradeTarget_);
    tradeTarget_->clear();
    const auto& s = simulation_.state();
    auto action = static_cast<TradeAction>(tradeAction_->currentIndex());
    if (action == TradeAction::Sell) {
        for (const auto& p : s.portfolio.positions) {
            auto* i = Portfolio::findInstrument(s.market, p.instrumentId);
            if (i && i->kind != InstrumentKind::Deposit)
                tradeTarget_->addItem(
                    QString("#%1 · %2").arg(p.id).arg(text(i->name)), QVariant::fromValue<qlonglong>(p.id));
        }
    } else {
        for (const auto& i : s.market.instruments)
            if (i.tradable && ((action == TradeAction::OpenDeposit) == (i.kind == InstrumentKind::Deposit)))
                tradeTarget_->addItem(
                    text(i.name) + " · " + currency(i.currency), QVariant::fromValue<qlonglong>(i.id));
    }
    int index = tradeTarget_->findData(selected);
    if (index >= 0)
        tradeTarget_->setCurrentIndex(index);
    refreshPreview();
}
TradeRequest MainWindow::collectTradeRequest() const
{
    TradeRequest request;
    request.action = static_cast<TradeAction>(tradeAction_->currentIndex());
    const auto id = tradeTarget_->currentData().toLongLong();
    const auto& s = simulation_.state();
    const Instrument* instrument = nullptr;
    if (request.action == TradeAction::Sell) {
        request.positionId = id;
        for (const auto& p : s.portfolio.positions)
            if (p.id == id)
                instrument = Portfolio::findInstrument(s.market, p.instrumentId);
    } else {
        request.instrumentId = id;
        instrument = Portfolio::findInstrument(s.market, id);
    }
    if (request.action == TradeAction::OpenDeposit)
        request.amount = std::llround(tradeValue_->value() * 100);
    else
        request.quantity = std::llround(tradeValue_->value()
            * (instrument && instrument->kind == InstrumentKind::Metal ? kMetalScale : 1));
    return request;
}
void MainWindow::refreshPreview()
{
    const auto request = collectTradeRequest();
    const auto& s = simulation_.state();
    const Instrument* i = Portfolio::findInstrument(s.market, request.instrumentId);
    if (request.action == TradeAction::Sell)
        for (const auto& p : s.portfolio.positions)
            if (p.id == request.positionId)
                i = Portfolio::findInstrument(s.market, p.instrumentId);
    {
        QSignalBlocker blocker(tradeValue_);
        int decimals = request.action == TradeAction::OpenDeposit
            ? 2
            : (i && i->kind == InstrumentKind::Metal ? 3 : 0);
        tradeValue_->setDecimals(decimals);
        tradeValue_->setSingleStep(decimals == 3 ? .1 : 1);
    }
    tradeUnit_->setText(request.action == TradeAction::OpenDeposit
            ? "Сумма вклада, " + (i ? currency(i->currency) : QString())
            : i && i->kind == InstrumentKind::Metal ? "Количество, граммы"
                                                    : "Количество, штуки");
    const auto preview = simulation_.previewTrade(collectTradeRequest());
    tradeButton_->setEnabled(preview.ok());
    tradePreview_->setText(preview.ok()
            ? QString("%1. Изменение денег: %2 RUB. Остаток: %3 RUB. Выручка сначала погашает обязательства.")
                  .arg(text(preview.value().description), money(preview.value().cashDeltaRub),
                      money(preview.value().cashAfter))
            : (s.started ? text(preview.message) : "Сначала создайте игру."));
}

void MainWindow::refreshView()
{
    const auto& s = simulation_.state();
    const auto stats = simulation_.statistics();
    navLabel_->setText("Капитал фонда<br><b>" + (s.started ? money(stats.finalNav) : "—") + " RUB</b>");
    cashLabel_->setText("Свободные деньги<br><b>" + money(s.portfolio.cashRUB) + " RUB</b>");
    debtLabel_->setText("Обязательства<br><b>" + money(s.taxLiability + s.shareholderLiability) + " RUB</b>");
    returnLabel_->setText("Доходность закрытых месяцев<br><b>" + percent(stats.cumulativeReturn) + "</b>");
    QString instruction;
    if (s.started)
        switch (s.phase) {
        case Phase::InitialAllocation:
            instruction = "Соберите портфель на вкладке «Операции» и подтвердите его. Можно оставить часть "
                          "капитала в деньгах.";
            break;
        case Phase::ReadyForMonth:
            instruction = "Портфель зафиксирован. Рассчитайте следующий месяц.";
            break;
        case Phase::AwaitingFlows:
            instruction
                = "Доход и налог рассчитаны. Подтвердите поступления и возврат паёв на вкладке «Операции».";
            break;
        case Phase::Restructuring:
            instruction = "При необходимости измените вложения и завершите месяц. Для оплаты долгов продайте "
                          "ликвидные активы.";
            break;
        case Phase::Finished:
            instruction = text(s.finishReason);
            break;
        }
    ui->phaseLabel->setText(s.started
            ? QString("Месяц %1 / %2 · %3\n%4")
                  .arg(s.month)
                  .arg(s.config.monthCount)
                  .arg(phaseText(s.phase), instruction)
            : "Создайте игру на вкладке «Новая игра». Все котировки в симуляторе учебные.");
    ui->initialButton->setEnabled(s.started && s.phase == Phase::InitialAllocation);
    ui->nextButton->setEnabled(s.started && s.phase == Phase::ReadyForMonth);
    ui->finishButton->setEnabled(s.started && s.phase == Phase::Restructuring);
    ui->actionSave->setEnabled(s.started);
    ui->actionSaveAs->setEnabled(s.started);
    portfolioTable_->setRowCount(static_cast<int>(s.portfolio.positions.size()));
    int row = 0;
    for (const auto& p : s.portfolio.positions) {
        const auto* i = Portfolio::findInstrument(s.market, p.instrumentId);
        if (!i)
            continue;
        cell(portfolioTable_, row, 0, QString::number(p.id));
        portfolioTable_->item(row, 0)->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(p.id));
        cell(portfolioTable_, row, 1, text(i->name));
        cell(portfolioTable_, row, 2, kind(i->kind));
        cell(portfolioTable_, row, 3, currency(i->currency));
        cell(portfolioTable_, row, 4,
            i->kind == InstrumentKind::Deposit     ? money(p.principal)
                : i->kind == InstrumentKind::Metal ? QLocale().toString(p.quantity / 1000.0, 'f', 3)
                                                   : QString::number(p.quantity));
        cell(portfolioTable_, row, 5,
            i->kind == InstrumentKind::Deposit    ? percent(p.lockedRate)
                : i->kind == InstrumentKind::Bond ? percent(i->annualRate)
                                                  : "—");
        cell(portfolioTable_, row, 6,
            i->kind == InstrumentKind::Deposit    ? QString::number(p.maturityMonth)
                : i->kind == InstrumentKind::Bond ? QString::number(i->maturityMonth)
                                                  : "—");
        const auto v = Portfolio::valuePosition(p, s.market);
        cell(portfolioTable_, row, 7, v.ok() ? money(v.value()) : "Ошибка оценки");
        ++row;
    }
    marketRates_->setText(QString("Текущие курсы: 1 USD = %1 RUB · 1 EUR = %2 RUB")
            .arg(money(s.market.usdRub), money(s.market.eurRub)));
    marketTable_->setRowCount(static_cast<int>(s.market.instruments.size()));
    row = 0;
    for (const auto& i : s.market.instruments) {
        cell(marketTable_, row, 0, text(i.name));
        cell(marketTable_, row, 1, kind(i.kind));
        cell(marketTable_, row, 2, currency(i.currency));
        cell(marketTable_, row, 3,
            i.kind == InstrumentKind::Deposit
                ? "—"
                : money(i.kind == InstrumentKind::Bond ? i.faceValue + i.accruedCoupon : i.price));
        cell(marketTable_, row, 4,
            i.kind == InstrumentKind::Deposit || i.kind == InstrumentKind::Bond ? percent(i.annualRate)
                                                                                : "—");
        cell(marketTable_, row, 5,
            i.kind == InstrumentKind::Deposit    ? QString("%1 мес.").arg(i.termMonths)
                : i.kind == InstrumentKind::Bond ? QString("Месяц %1").arg(i.maturityMonth)
                                                 : "—");
        cell(marketTable_, row, 6, percent(i.minChange) + " … " + percent(i.maxChange));
        cell(marketTable_, row, 7, i.tradable ? "Да" : "Нет");
        ++row;
    }
    const bool pending = s.pending.report.month > 0;
    {
        QSignalBlocker blocker(historyTable_);
        historyTable_->setRowCount(static_cast<int>(s.history.size()) + (pending ? 1 : 0));
        row = 0;
        auto addReport = [&](const MonthReport& report, const QString& status) {
            cell(historyTable_, row, 0, QString::number(report.month));
            cell(historyTable_, row, 1, status);
            cell(historyTable_, row, 2, money(report.navStart));
            cell(historyTable_, row, 3, money(report.profit));
            cell(historyTable_, row, 4, money(report.tax));
            cell(historyTable_, row, 5, report.returnDefined ? percent(report.returnNet) : "—");
            cell(historyTable_, row, 6, money(report.acceptedInflow));
            cell(historyTable_, row, 7, money(report.redemptionValue));
            cell(historyTable_, row, 8,
                money(status == "Ожидает потоков" ? report.navAfterTax : report.navEnd));
            ++row;
        };
        for (const auto& report : s.history)
            addReport(report, "Закрыт");
        if (pending)
            addReport(s.pending.report, s.pending.flowsApplied ? "Реструктуризация" : "Ожидает потоков");
        if (row > 0)
            historyTable_->selectRow(row - 1);
    }
    refreshIncome();
    statsLabel_->setText(QString("Чистая прибыль: %1 RUB · Налоги: %2 RUB · Внесено: %3 RUB · Возвращено: %4 "
                                 "RUB\nПаи в обращении: %5 · Макс. просадка: %6 · USD: %7 RUB · EUR: %8 RUB")
            .arg(money(stats.totalNetProfit), money(stats.totalTax), money(stats.totalInflow),
                money(stats.totalRedemption), QLocale().toString(s.unitsOutstanding / 1e6, 'f', 6),
                percent(stats.maxDrawdown), money(s.market.usdRub), money(s.market.eurRub)));
    statsLabel_->setText("Итоги закрытых месяцев\n" + statsLabel_->text()
        + QString("\nПродано паёв: %1 · Погашено паёв: %2 (без начального выпуска)")
            .arg(QLocale().toString(stats.totalUnitsIssued / 1e6, 'f', 6),
                QLocale().toString(stats.totalUnitsRedeemed / 1e6, 'f', 6)));
    QStringList lines;
    for (const auto& entry : s.journal)
        lines << text(entry);
    journal_->setPlainText(lines.join('\n'));
    journal_->moveCursor(QTextCursor::End);
    flowInfo_->setText(QString("Цена пая после налога: %1 RUB. В обращении: %2 паёв. Налоговый долг: %3 RUB, "
                               "долг пайщикам: %4 RUB.\n%5")
            .arg(money(s.pending.report.unitPrice), QLocale().toString(s.unitsOutstanding / 1e6, 'f', 6),
                money(s.taxLiability), money(s.shareholderLiability),
                s.config.flowMode == FlowMode::Automatic
                    ? "Автопредложение рассчитано один раз. Подтвердите его, чтобы продолжить."
                    : "Введите поступления и количество паёв для возврата. Нулевые значения допустимы."));
    const MonthReport* flowReport
        = s.pending.flowsApplied ? &s.pending.report : (!s.history.empty() ? &s.history.back() : nullptr);
    if (flowReport && s.phase != Phase::AwaitingFlows)
        flowInfo_->setText(flowInfo_->text()
            + QString("\nПоследние потоки: принято %1 RUB, остаток вне фонда %2 RUB, возврат %3 RUB.")
                .arg(money(flowReport->acceptedInflow), money(flowReport->leftoverInflow),
                    money(flowReport->redemptionValue)));
    bool await = s.started && s.phase == Phase::AwaitingFlows;
    inflow_->setEnabled(await && s.config.flowMode == FlowMode::Manual);
    redeem_->setEnabled(inflow_->isEnabled());
    flowsButton_->setEnabled(await);
    refreshTradeChoices();
    setWindowModified(dirty_);
    statusBar()->showMessage(filePath_.isEmpty() ? "Партия ещё не сохранена" : filePath_);
}
void MainWindow::refreshIncome()
{
    const auto& s = simulation_.state();
    int row = historyTable_->currentRow();
    const MonthReport* report = nullptr;
    if (row >= 0 && row < static_cast<int>(s.history.size()))
        report = &s.history[row];
    else if (row == static_cast<int>(s.history.size()) && s.pending.report.month > 0)
        report = &s.pending.report;
    incomeTable_->setRowCount(report ? static_cast<int>(report->income.size()) : 0);
    if (!report)
        return;
    int n = 0;
    for (const auto& item : report->income) {
        cell(incomeTable_, n, 0, QString::number(item.positionId));
        cell(incomeTable_, n, 1, text(item.title));
        cell(incomeTable_, n, 2, money(item.interestIncome));
        cell(incomeTable_, n, 3, money(item.revaluation));
        cell(incomeTable_, n, 4, money(item.total));
        ++n;
    }
}

void MainWindow::showError(const std::string& message)
{
    QMessageBox::warning(this, "Операция не выполнена", text(message));
}
void MainWindow::applyResult(const Result<void>& result)
{
    if (!result.ok()) {
        showError(result.message);
        return;
    }
    dirty_ = true;
    refreshView();
    if (simulation_.state().phase == Phase::Finished)
        ui->tabs->setCurrentIndex(3);
}
void MainWindow::onNewGame()
{
    auto c = collectConfig();
    if (!c.ok()) {
        showError(c.message);
        return;
    }
    if (!confirmDiscard())
        return;
    const auto result = simulation_.start(c.value());
    if (!result.ok()) {
        showError(result.message);
        return;
    }
    filePath_.clear();
    inflow_->setValue(0);
    redeem_->setValue(0);
    applyResult(result);
    ui->tabs->setCurrentIndex(2);
}
void MainWindow::onTrade()
{
    applyResult(simulation_.trade(collectTradeRequest()));
}
void MainWindow::onConfirmInitialAllocation()
{
    applyResult(simulation_.confirmInitialAllocation());
}
void MainWindow::onNextMonth()
{
    const auto result = simulation_.nextMonth();
    if (result.ok()) {
        const auto f = simulation_.suggestedFlows();
        inflow_->setValue(f.inflowRub / 100.0);
        redeem_->setValue(f.unitsToRedeem / 1e6);
        ui->tabs->setCurrentIndex(2);
    }
    applyResult(result);
}
void MainWindow::onApplyFlows()
{
    auto f = simulation_.state().config.flowMode == FlowMode::Automatic
        ? simulation_.suggestedFlows()
        : FlowRequest { std::llround(inflow_->value() * 100), std::llround(redeem_->value() * 1e6) };
    applyResult(simulation_.applyFlows(f));
}
void MainWindow::onFinishMonth()
{
    applyResult(simulation_.finishMonth());
}
bool MainWindow::confirmDiscard()
{
    if (!dirty_)
        return true;
    const auto answer
        = QMessageBox::question(this, "Несохранённая партия", "Сохранить текущую партию перед продолжением?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    return answer == QMessageBox::Discard || saveGame();
}
bool MainWindow::saveGame()
{
    if (!simulation_.state().started)
        return false;
    auto path = filePath_;
    if (path.isEmpty()) {
        const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(directory);
        path = QFileDialog::getSaveFileName(
            this, "Сохранить партию", directory + "/portfolio.json", "Партия (*.json)");
        if (path.isEmpty())
            return false;
        if (!path.endsWith(".json", Qt::CaseInsensitive))
            path += ".json";
    }
    const auto result = GameStorage::save(path, simulation_.exportState());
    if (!result.ok()) {
        showError(result.message);
        return false;
    }
    filePath_ = path;
    dirty_ = false;
    refreshView();
    return true;
}
void MainWindow::onSave()
{
    saveGame();
}
void MainWindow::onLoad()
{
    const auto path = QFileDialog::getOpenFileName(this, "Открыть партию",
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation), "Партия (*.json)");
    if (path.isEmpty())
        return;
    const auto loaded = GameStorage::load(path);
    if (!loaded.ok()) {
        showError(loaded.message);
        return;
    }
    if (!confirmDiscard())
        return;
    const auto result = simulation_.restoreState(loaded.value());
    if (!result.ok()) {
        showError(result.message);
        return;
    }
    filePath_ = path;
    dirty_ = false;
    fillSettings(simulation_.state().config);
    const auto f = simulation_.suggestedFlows();
    inflow_->setValue(f.inflowRub / 100.0);
    redeem_->setValue(f.unitsToRedeem / 1e6);
    refreshView();
    ui->tabs->setCurrentIndex(simulation_.state().phase == Phase::Finished ? 3 : 0);
}
void MainWindow::closeEvent(QCloseEvent* event)
{
    if (confirmDiscard())
        event->accept();
    else
        event->ignore();
}
