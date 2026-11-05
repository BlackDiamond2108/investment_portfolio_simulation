#include "DashboardWidget.h"
#include "Portfolio.h"
#include <QtWidgets>
#include <algorithm>
#include <array>
#include <cmath>

namespace {
const QColor ink("#202124"), muted("#80858f"), yellow("#ffdd2d"), green("#15966a"), red("#e34e54");
QString rub(Money value, int digits = 0)
{
    return QLocale().toString(value / 100.0, 'f', digits) + " ₽";
}
QString signedRub(Money value)
{
    return (value > 0 ? "+" : "") + rub(value, 2);
}
QString compact(double value)
{
    if (std::abs(value) >= 1e9)
        return QLocale().toString(value / 1e9, 'f', 1) + " млрд";
    if (std::abs(value) >= 1e6)
        return QLocale().toString(value / 1e6, 'f', 1) + " млн";
    if (std::abs(value) >= 1e3)
        return QLocale().toString(value / 1e3, 'f', 0) + " тыс.";
    return QLocale().toString(value, 'f', std::abs(value) < 10 ? 2 : 0);
}
QLabel* label(const QString& text, const char* role = "caption")
{
    auto* w = new QLabel(text);
    w->setTextFormat(Qt::PlainText);
    w->setProperty("role", role);
    w->setWordWrap(true);
    return w;
}
QFrame* card(QVBoxLayout*& layout)
{
    auto* w = new QFrame;
    w->setProperty("panel", true);
    layout = new QVBoxLayout(w);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(12);
    return w;
}
void clear(QLayout* layout)
{
    while (auto* item = layout->takeAt(0)) {
        delete item->widget();
        if (item->layout())
            clear(item->layout());
        delete item;
    }
}
void tone(QLabel* w, Money value)
{
    w->setStyleSheet(QString("color: %1;").arg(value < 0 ? red.name() : green.name()));
}
void painterText(QPainter& p, const QRectF& rect, const QString& value, int size, QColor color,
    int flags = Qt::AlignLeft | Qt::AlignVCenter, bool bold = false)
{
    auto f = p.font();
    f.setPixelSize(size);
    f.setBold(bold);
    p.setFont(f);
    p.setPen(color);
    p.drawText(rect, flags, value);
}
}

// График не сглаживает цены вымышленными точками: только реальные месяцы.
// Hover и клик используют те же координаты, что и отрисовка, в том числе после resize.
class PerformanceChart : public QWidget {
public:
    explicit PerformanceChart(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName("performanceChart");
        setMinimumHeight(235);
        setMouseTracking(true);
        setAccessibleName("Динамика капитала и чистой месячной прибыли");
        animation_.setDuration(400);
        animation_.setStartValue(0.0);
        animation_.setEndValue(1.0);
        connect(&animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            reveal_ = v.toDouble();
            update();
        });
    }
    void setSeries(QVector<DashboardPoint> values, bool profit, int lastMonths)
    {
        const bool changed = values.size() != all_.size()
            || (!values.isEmpty() && !all_.isEmpty()
                && (values.back().capital != all_.back().capital
                    || values.back().netProfit != all_.back().netProfit));
        all_ = std::move(values);
        points_ = all_;
        profit_ = profit;
        if (lastMonths > 0 && points_.size() > lastMonths + 1)
            points_ = points_.mid(points_.size() - lastMonths - 1);
        hovered_ = -1;
        if (changed && isVisible()) {
            animation_.stop();
            animation_.start();
        } else
            reveal_ = 1;
        setAccessibleDescription(QString("%1 точек. %2")
                .arg(points_.size())
                .arg(profit_ ? "Чистая прибыль после налога" : "Капитал с учётом потоков"));
        update();
    }
    std::function<void(int)> selectMonth;

protected:
    QRectF plot() const { return QRectF(3, 15, std::max(1, width() - 78), std::max(1, height() - 49)); }
    QVector<QPointF> coordinates() const
    {
        QVector<QPointF> result;
        if (points_.isEmpty())
            return result;
        double low = 0, high = 0;
        if (!profit_)
            low = high = points_.first().capital / 100.0;
        for (const auto& point : points_) {
            double y = (profit_ ? point.netProfit : point.capital) / 100.0;
            low = std::min(low, y);
            high = std::max(high, y);
        }
        double padding = std::max((high - low) * .18, std::max(1.0, std::abs(high) * .005));
        low -= padding;
        high += padding;
        const auto area = plot();
        for (int i = 0; i < points_.size(); ++i) {
            double x
                = area.left() + area.width() * (points_.size() == 1 ? 0.0 : double(i) / (points_.size() - 1));
            double y = (profit_ ? points_[i].netProfit : points_[i].capital) / 100.0;
            result.push_back({ x, area.bottom() - (y - low) / (high - low) * area.height() });
        }
        return result;
    }
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        auto area = plot();
        if (points_.isEmpty()) {
            for (int i = 0; i < 4; ++i) {
                p.setPen(QPen(QColor("#edf0f3"), 1, Qt::DashLine));
                auto y = area.top() + i * area.height() / 3;
                p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
            }
            painterText(p, area, "Здесь появится история вашего фонда", 14, muted, Qt::AlignCenter);
            return;
        }
        double low = 0, high = 0;
        if (!profit_)
            low = high = points_.first().capital / 100.0;
        for (const auto& point : points_) {
            auto y = (profit_ ? point.netProfit : point.capital) / 100.0;
            low = std::min(low, y);
            high = std::max(high, y);
        }
        const double padding = std::max((high - low) * .18, std::max(1.0, std::abs(high) * .005));
        low -= padding;
        high += padding;
        for (int i = 0; i < 4; ++i) {
            const double y = area.top() + i * area.height() / 3;
            p.setPen(QPen(QColor("#eceef1"), 1, Qt::DashLine));
            p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
            painterText(p, QRectF(area.right() + 10, y - 10, 68, 20), compact(high - i * (high - low) / 3),
                11, muted);
        }
        const auto positions = coordinates();
        if (profit_) {
            const double zero = area.bottom() - (-low) / (high - low) * area.height();
            p.setPen(QPen(QColor("#c6cbd1"), 1));
            p.drawLine(QPointF(area.left(), zero), QPointF(area.right(), zero));
            const double barWidth
                = std::min(36.0, area.width() / std::max<qsizetype>(2, positions.size()) * .55);
            for (int i = 0; i < positions.size(); ++i) {
                if (points_[i].month == 0)
                    continue;
                const auto value = points_[i].netProfit;
                double y = zero + (positions[i].y() - zero) * reveal_;
                QColor c = value < 0 ? red : green;
                if (points_[i].provisional)
                    c.setAlpha(130);
                p.setPen(Qt::NoPen);
                p.setBrush(c);
                p.drawRoundedRect(QRectF(positions[i].x() - barWidth / 2, std::min(zero, y), barWidth,
                                      std::max(2.0, std::abs(y - zero))),
                    4, 4);
            }
        } else {
            p.save();
            p.setClipRect(
                QRectF(area.left() - 5, area.top() - 5, (area.width() + 10) * reveal_, area.height() + 10));
            QPainterPath line;
            line.moveTo(positions.first());
            for (int i = 1; i < positions.size(); ++i)
                line.lineTo(positions[i]);
            QPainterPath fill = line;
            fill.lineTo(positions.last().x(), area.bottom());
            fill.lineTo(positions.first().x(), area.bottom());
            fill.closeSubpath();
            QLinearGradient gradient(area.topLeft(), area.bottomLeft());
            gradient.setColorAt(0, QColor(255, 221, 45, 85));
            gradient.setColorAt(1, QColor(255, 221, 45, 0));
            p.fillPath(fill, gradient);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor("#edb900"), 2.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(line);
            for (int i = 0; i < positions.size(); ++i) {
                p.setPen(QPen(QColor("#d6a700"), 2));
                p.setBrush(points_[i].provisional ? Qt::white : yellow);
                p.drawEllipse(positions[i], 3.5, 3.5);
            }
            p.restore();
        }
        const int stride = std::max<qsizetype>(1, (points_.size() - 1) / 6);
        for (int i = 0; i < points_.size(); ++i)
            if (i == 0 || i == points_.size() - 1 || i % stride == 0) {
                painterText(p, QRectF(std::max(0.0, positions[i].x() - 22), area.bottom() + 12, 44, 20),
                    points_[i].month == 0 ? "Старт" : QString::number(points_[i].month), 11, muted,
                    Qt::AlignCenter);
            }
        if (points_.size() == 1)
            painterText(p, area.adjusted(30, 0, -10, 0), "Рассчитайте месяц, чтобы увидеть изменения", 12,
                muted, Qt::AlignCenter);
        if (hovered_ >= 0 && hovered_ < positions.size()) {
            auto point = positions[hovered_];
            p.setPen(QPen(QColor("#9da4ae"), 1, Qt::DashLine));
            p.drawLine(QPointF(point.x(), area.top()), QPointF(point.x(), area.bottom()));
            p.setPen(QPen(ink, 2));
            p.setBrush(Qt::white);
            p.drawEllipse(point, 5, 5);
        }
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        const auto coords = coordinates();
        if (coords.isEmpty())
            return;
        int nearest = 0;
        for (int i = 1; i < coords.size(); ++i)
            if (std::abs(event->position().x() - coords[i].x())
                < std::abs(event->position().x() - coords[nearest].x()))
                nearest = i;
        hovered_ = nearest;
        const auto& point = points_[nearest];
        QToolTip::showText(event->globalPosition().toPoint(),
            QString("%1%2\nКапитал: %3\nЧистая прибыль месяца: %4\nНажмите, чтобы открыть отчёт")
                .arg(point.month == 0 ? "Начало игры" : QString("Месяц %1").arg(point.month),
                    point.provisional ? " · предварительно" : "", rub(point.capital, 2),
                    signedRub(point.netProfit)),
            this);
        update();
    }
    void leaveEvent(QEvent*) override
    {
        hovered_ = -1;
        QToolTip::hideText();
        update();
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        mouseMoveEvent(event);
        if (hovered_ >= 0 && selectMonth)
            selectMonth(points_[hovered_].month);
    }

private:
    QVector<DashboardPoint> all_, points_;
    bool profit_ = false;
    int hovered_ = -1;
    double reveal_ = 1;
    QVariantAnimation animation_;
};

class AllocationChart : public QWidget {
public:
    explicit AllocationChart(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName("allocationChart");
        setFixedHeight(172);
        setMinimumWidth(200);
        setMouseTracking(true);
        setAccessibleName("Состав активов фонда");
    }
    void setData(QVector<DashboardAllocation> values)
    {
        values_ = std::move(values);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const double side = 142;
        QRectF ring((width() - side) / 2.0, 15, side, side);
        long double total = 0;
        for (const auto& v : values_)
            total += v.amount;
        p.setPen(QPen(QColor("#eef0f3"), 22));
        p.drawEllipse(ring);
        if (total > 0) {
            double angle = 90;
            for (const auto& v : values_) {
                if (v.amount <= 0)
                    continue;
                const double span = 360 * static_cast<long double>(v.amount) / total;
                p.setPen(QPen(v.color, 22, Qt::SolidLine, Qt::FlatCap));
                p.drawArc(ring, qRound(angle * 16), qRound(-span * 16));
                angle -= span;
            }
        }
        painterText(p, ring.adjusted(12, 40, -12, -52),
            total > 0 ? QString::number(std::count_if(
                            values_.begin(), values_.end(), [](auto& v) { return v.amount > 0; }))
                      : "—",
            28, ink, Qt::AlignCenter, true);
        painterText(p, ring.adjusted(12, 78, -12, -20), "классов активов", 11, muted, Qt::AlignCenter);
    }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        QStringList lines;
        long double total = 0;
        for (const auto& v : values_)
            total += v.amount;
        for (const auto& v : values_)
            if (v.amount > 0)
                lines << v.name + ": " + rub(v.amount, 2)
                        + QString(" (%1%)").arg(double(v.amount / total * 100), 0, 'f', 1);
        if (!lines.isEmpty())
            QToolTip::showText(e->globalPosition().toPoint(), lines.join('\n'), this);
    }

private:
    QVector<DashboardAllocation> values_;
};

class MonthTimeline : public QWidget {
public:
    explicit MonthTimeline(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName("monthTimeline");
        setFixedHeight(44);
        setAccessibleName("Прогресс по месяцам");
    }
    void setProgress(int month, int count, bool closed)
    {
        month_ = month;
        count_ = count;
        closed_ = closed;
        setAccessibleDescription(QString("Месяц %1 из %2").arg(month).arg(count));
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (count_ <= 0)
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const double gap = 4, step = double(width()) / count_;
        for (int i = 0; i < count_; ++i) {
            bool done = i < month_ - (closed_ ? 0 : 1);
            bool current = i == month_ - 1;
            p.setPen(Qt::NoPen);
            p.setBrush(done ? yellow : current ? QColor("#25272b") : QColor("#e7e9ed"));
            p.drawRoundedRect(QRectF(i * step, 2, std::max(1.0, step - gap), 6), 3, 3);
            if (i == 0 || i == count_ - 1 || i == month_ - 1 || (count_ <= 18 && (i + 1) % 3 == 0))
                painterText(p, QRectF(i * step, 15, step - gap, 18), QString::number(i + 1), 10,
                    current ? ink : muted, Qt::AlignCenter);
        }
    }

private:
    int month_ = 0, count_ = 18;
    bool closed_ = true;
};

class FlowChart : public QWidget {
public:
    explicit FlowChart(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName("monthFlowChart");
        setMinimumHeight(168);
        setAccessibleName("Прибыль, налог и денежные потоки месяца");
    }
    void setReport(const MonthReport* r, bool applied)
    {
        report_ = r ? std::optional<MonthReport>(*r) : std::nullopt;
        applied_ = applied;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (!report_) {
            painterText(p, rect(), "Первый отчёт появится после расчёта месяца", 12, muted, Qt::AlignCenter);
            return;
        }
        const auto& r = *report_;
        std::array<Money, 4> values { r.profit, -r.tax, r.acceptedInflow, -r.redemptionValue };
        QStringList titles { "Доход активов", "Налог", "Поступления", "Возврат паёв" };
        long double max = 1;
        for (auto v : values)
            max = std::max(max, std::abs(static_cast<long double>(v)));
        const int nameWidth = 122;
        const double amountWidth = 135, barWidth = std::max(10.0, width() - nameWidth - amountWidth - 16.0);
        for (int i = 0; i < 4; ++i) {
            double top = i * 40;
            painterText(p, QRectF(0, top, nameWidth, 28), titles[i], 12, muted);
            QRectF track(nameWidth, top + 10, barWidth, 7);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#eef0f3"));
            p.drawRoundedRect(track, 3, 3);
            QColor color = i == 0 ? (values[i] >= 0 ? green : red)
                : i == 1          ? red
                : i == 2          ? QColor("#5682d9")
                                  : QColor("#a5aab4");
            p.setBrush(color);
            p.drawRoundedRect(QRectF(track.left(), track.top(),
                                  barWidth * std::abs(static_cast<long double>(values[i])) / max, 7),
                3, 3);
            painterText(p, QRectF(width() - amountWidth, top, amountWidth, 28),
                i >= 2 && !applied_ ? "Ожидают решения" : signedRub(values[i]), 12, color,
                Qt::AlignRight | Qt::AlignVCenter, true);
        }
    }

private:
    std::optional<MonthReport> report_;
    bool applied_ = false;
};

DashboardWidget::DashboardWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("dashboard");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(18);
    auto* hero = new QHBoxLayout;
    auto* heading = new QVBoxLayout;
    heading->setSpacing(4);
    heading->addWidget(label("МОЙ ПОРТФЕЛЬ", "eyebrow"));
    balance_ = label("Начните инвестировать", "balance");
    balance_->setObjectName("dashboardBalance");
    heading->addWidget(balance_);
    performance_ = label("Соберите фонд и посмотрите, как он изменится со временем");
    heading->addWidget(performance_);
    hero->addLayout(heading, 1);
    auto* actions = new QVBoxLayout;
    step_ = new QPushButton;
    step_->setObjectName("dashboardStepButton");
    step_->setMinimumWidth(240);
    actions->addWidget(step_);
    auto* secondary = new QHBoxLayout;
    buy_ = new QPushButton("+  Купить активы");
    buy_->setProperty("secondary", true);
    secondary->addWidget(buy_);
    example_ = new QPushButton("Открыть пример");
    example_->setObjectName("exampleButton");
    example_->setProperty("secondary", true);
    example_->setToolTip(
        "Создать настоящую партию с разными активами и рассчитать первые 6 месяцев. Можно продолжить игру.");
    secondary->addWidget(example_);
    actions->addLayout(secondary);
    hero->addLayout(actions);
    root->addLayout(hero);
    connect(step_, &QPushButton::clicked, this, &DashboardWidget::nextStepRequested);
    connect(buy_, &QPushButton::clicked, this, &DashboardWidget::marketRequested);
    connect(example_, &QPushButton::clicked, this, &DashboardWidget::exampleRequested);

    auto* metrics = new QHBoxLayout;
    metrics->setSpacing(14);
    auto metric = [&](QString title, const char* name) {
        QVBoxLayout* l;
        auto* box = card(l);
        l->setContentsMargins(18, 13, 18, 13);
        l->setSpacing(5);
        l->addWidget(label(title));
        auto* value = label("—", "metric");
        value->setObjectName(name);
        l->addWidget(value);
        metrics->addWidget(box, 1);
        return value;
    };
    cash_ = metric("Доступно для сделок", "dashboardCash");
    profit_ = metric("Чистая прибыль за игру", "dashboardProfit");
    debt_ = metric("Обязательства фонда", "dashboardDebt");
    drawdown_ = metric("Максимальная просадка", "dashboardDrawdown");
    root->addLayout(metrics);

    chartRow_ = new QBoxLayout(QBoxLayout::LeftToRight);
    chartRow_->setSpacing(18);
    QVBoxLayout* chartLayout;
    auto* chartCard = card(chartLayout);
    auto* chartHeader = new QHBoxLayout;
    chartHeader->addWidget(label("Динамика портфеля", "section"), 1);
    chartMode_ = new QComboBox;
    chartMode_->setObjectName("chartMode");
    chartMode_->addItems({ "Капитал", "Прибыль за месяц" });
    chartHeader->addWidget(chartMode_);
    period_ = new QComboBox;
    period_->setObjectName("chartPeriod");
    period_->addItem("Весь период", 0);
    period_->addItem("6 месяцев", 6);
    period_->addItem("12 месяцев", 12);
    chartHeader->addWidget(period_);
    chartLayout->addLayout(chartHeader);
    chartLayout->addWidget(label(
        "Капитал включает поступления и возвраты. Прибыль — результат вложений после налога.", "small"));
    chart_ = new PerformanceChart;
    chart_->selectMonth = [this](int month) {
        if (month > 0)
            emit monthRequested(month);
    };
    chartLayout->addWidget(chart_, 1);
    auto* legend = label("●  Капитал фонда     ○  Текущий, ещё не закрытый месяц", "small");
    chartLayout->addWidget(legend);
    connect(chartMode_, &QComboBox::currentIndexChanged, this, [this, legend] {
        legend->setText(chartMode_->currentIndex() == 0
                ? "●  Капитал фонда     ○  Текущий, ещё не закрытый месяц"
                : "Зелёный — прибыль  ·  Красный — убыток  ·  Бледный — незакрытый месяц");
        refreshChart();
    });
    connect(period_, &QComboBox::currentIndexChanged, this, [this] { refreshChart(); });
    chartRow_->addWidget(chartCard, 5);
    QVBoxLayout* allocationLayout;
    auto* allocationCard = card(allocationLayout);
    allocationCard->setMinimumWidth(285);
    allocationLayout->addWidget(label("Во что вложены деньги", "section"));
    donut_ = new AllocationChart;
    allocationLayout->addWidget(donut_);
    legend_ = new QVBoxLayout;
    legend_->setSpacing(8);
    allocationLayout->addLayout(legend_);
    allocationNote_ = label("Доли от общей стоимости активов", "small");
    allocationLayout->addWidget(allocationNote_);
    allocationLayout->addStretch();
    chartRow_->addWidget(allocationCard, 2);
    root->addLayout(chartRow_);

    QVBoxLayout* timeLayout;
    auto* timeCard = card(timeLayout);
    timeLayout->setContentsMargins(20, 12, 20, 8);
    timeLayout->setSpacing(4);
    phase_ = label("Ваша симуляция · 12–30 месяцев", "section");
    phase_->setObjectName("dashboardPhase");
    timeLayout->addWidget(phase_);
    timeline_ = new MonthTimeline;
    timeLayout->addWidget(timeline_);
    root->addWidget(timeCard);

    detailsRow_ = new QBoxLayout(QBoxLayout::LeftToRight);
    detailsRow_->setSpacing(18);
    QVBoxLayout* holdingsLayout;
    auto* holdingsCard = card(holdingsLayout);
    holdingsTitle_ = label("Мои активы", "section");
    holdingsLayout->addWidget(holdingsTitle_);
    holdings_ = new QVBoxLayout;
    holdings_->setSpacing(8);
    holdingsLayout->addLayout(holdings_);
    holdingsLayout->addStretch();
    detailsRow_->addWidget(holdingsCard, 5);
    QVBoxLayout* flowLayout;
    auto* flowCard = card(flowLayout);
    flowCard->setMinimumWidth(355);
    monthTitle_ = label("Результат месяца", "section");
    flowLayout->addWidget(monthTitle_);
    monthNote_ = label("Доходы и движение денег в одном месте", "small");
    flowLayout->addWidget(monthNote_);
    flow_ = new FlowChart;
    flowLayout->addWidget(flow_);
    flowLayout->addStretch();
    detailsRow_->addWidget(flowCard, 4);
    root->addLayout(detailsRow_);
}

void DashboardWidget::refreshChart()
{
    chart_->setSeries(series_, chartMode_->currentIndex() == 1, period_->currentData().toInt());
}
void DashboardWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    const auto direction = width() < 900 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
    chartRow_->setDirection(direction);
    detailsRow_->setDirection(direction);
}

void DashboardWidget::setState(const GameState& state, const GameStatistics& stats)
{
    balance_->setText(state.started ? rub(stats.finalNav, 2) : "Ваш первый портфель");
    const auto returnText = QLocale().toString(stats.cumulativeReturn * 100, 'f', 2) + " %";
    performance_->setText(state.started
            ? QString("%1  ·  %2 за закрытые месяцы").arg(signedRub(stats.totalNetProfit), returnText)
            : "Управляйте фондом, наблюдайте рынок и проверяйте свои решения");
    if (state.started)
        tone(performance_, stats.totalNetProfit);
    else
        performance_->setStyleSheet("color: #80858f;");
    cash_->setText(state.started ? rub(state.portfolio.cashRUB) : "—");
    profit_->setText(state.started ? signedRub(stats.totalNetProfit) : "—");
    tone(profit_, stats.totalNetProfit);
    debt_->setText(state.started ? rub(state.taxLiability + state.shareholderLiability) : "—");
    debt_->setStyleSheet(
        state.taxLiability + state.shareholderLiability > 0 ? "color: #e34e54;" : "color: #202124;");
    drawdown_->setText(state.started ? QLocale().toString(stats.maxDrawdown * 100, 'f', 2) + " %" : "—");
    buy_->setVisible(state.started);
    example_->setVisible(!state.started);
    QString step, description;
    if (!state.started) {
        step = "Создать свою симуляцию";
        description = "Сначала создайте фонд или откройте готовый пример";
    } else
        switch (state.phase) {
        case Phase::InitialAllocation:
            step = "Начать моделирование  →";
            description = "Шаг 1 · соберите начальный портфель";
            break;
        case Phase::ReadyForMonth:
            step = "Рассчитать месяц " + QString::number(state.month + 1) + "  →";
            description = "Рынок готов к следующему шагу";
            break;
        case Phase::AwaitingFlows:
            step = "Управлять потоками  →";
            description = "Доход и налог рассчитаны · подтвердите поступления и возвраты";
            break;
        case Phase::Restructuring:
            step = "Завершить месяц  →";
            description = "Можно изменить вложения и закрыть месяц";
            break;
        case Phase::Finished:
            step = "Создать новую игру  →";
            description = QString::fromStdString(state.finishReason);
            break;
        }
    step_->setText(step);
    phase_->setText(state.started
            ? QString("Месяц %1 из %2  ·  %3").arg(state.month).arg(state.config.monthCount).arg(description)
            : description);
    timeline_->setProgress(state.month, state.started ? state.config.monthCount : 18,
        state.phase == Phase::ReadyForMonth || state.phase == Phase::Finished);
    series_.clear();
    if (state.started) {
        series_.push_back({ 0, state.month == 0 ? stats.finalNav : state.startingNav, 0, false });
        for (const auto& r : state.history)
            series_.push_back({ r.month, r.navEnd, r.netProfit, false });
        if (state.pending.report.month > 0)
            series_.push_back({ state.month, stats.finalNav, state.pending.report.netProfit, true });
    }
    refreshChart();
    allocation_ = { { "Акции", 0, QColor("#ffdd2d") }, { "Облигации", 0, QColor("#7094ec") },
        { "Вклады", 0, QColor("#a68ad9") }, { "Металлы", 0, QColor("#edac64") },
        { "Деньги", state.portfolio.cashRUB, QColor("#c6cbd4") } };
    clear(holdings_);
    holdingsTitle_->setText(QString("Мои активы%1")
            .arg(state.portfolio.positions.empty() ? ""
                                                   : QString(" · %1").arg(state.portfolio.positions.size())));
    for (const auto& p : state.portfolio.positions) {
        const auto* i = Portfolio::findInstrument(state.market, p.instrumentId);
        const auto value = Portfolio::valuePosition(p, state.market);
        if (!i || !value.ok())
            continue;
        int group = i->kind == InstrumentKind::Stock ? 0
            : i->kind == InstrumentKind::Bond        ? 1
            : i->kind == InstrumentKind::Deposit     ? 2
                                                     : 3;
        allocation_[group].amount += value.value();
        auto* row = new QFrame;
        row->setProperty("assetRow", true);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(10, 10, 10, 10);
        h->setSpacing(12);
        auto* badge = label(i->kind == InstrumentKind::Deposit ? "₽"
                : i->kind == InstrumentKind::Bond              ? "ОФЗ"
                : i->kind == InstrumentKind::Metal             ? "Au"
                                                               : QString::fromStdString(i->name).left(1),
            "badge");
        badge->setAlignment(Qt::AlignCenter);
        badge->setFixedSize(42, 42);
        badge->setStyleSheet("background: " + allocation_[group].color.name()
            + ";border-radius: 12px;color: #202124;font-weight: 700;");
        h->addWidget(badge);
        auto* titles = new QVBoxLayout;
        titles->setSpacing(4);
        titles->addWidget(label(QString::fromStdString(i->name), "assetName"));
        const QString detail = i->kind == InstrumentKind::Deposit
            ? QString("Автозакрытие: месяц %1 · %2 % годовых")
                  .arg(p.maturityMonth)
                  .arg(p.lockedRate * 100, 0, 'f', 1)
            : i->kind == InstrumentKind::Metal ? QLocale().toString(p.quantity / 1000.0, 'f', 3) + " г"
                                               : QString::number(p.quantity) + " шт.";
        titles->addWidget(label(detail, "small"));
        h->addLayout(titles, 1);
        auto* valueLabel = label(rub(value.value(), 2), "assetName");
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        h->addWidget(valueLabel);
        auto* action = new QPushButton(i->kind == InstrumentKind::Deposit ? "Условия" : "Продать");
        action->setProperty("secondary", true);
        action->setObjectName("position_" + QString::number(p.id));
        action->setEnabled(i->kind == InstrumentKind::Deposit || state.phase == Phase::InitialAllocation
            || state.phase == Phase::Restructuring);
        action->setToolTip(i->kind == InstrumentKind::Deposit
                ? "Посмотреть срок и порядок автоматического закрытия вклада"
                : "Перейти к продаже этой позиции");
        connect(action, &QPushButton::clicked, this, [this, id = p.id] { emit positionRequested(id); });
        h->addWidget(action);
        holdings_->addWidget(row);
    }
    if (state.portfolio.positions.empty()) {
        auto* empty
            = label(state.started ? "Пока всё в деньгах. Добавьте акции, облигации, вклады или золото."
                                  : "Здесь появятся ваши вложения — с суммой, сроком и быстрыми действиями.");
        empty->setMinimumHeight(80);
        holdings_->addWidget(empty);
    }
    donut_->setData(allocation_);
    clear(legend_);
    long double total = 0;
    for (const auto& item : allocation_)
        total += item.amount;
    for (const auto& item : allocation_) {
        auto* line = new QWidget;
        auto* l = new QHBoxLayout(line);
        l->setContentsMargins(0, 0, 0, 0);
        auto* dot = new QLabel;
        dot->setFixedSize(8, 8);
        dot->setStyleSheet("background:" + item.color.name() + ";border-radius:4px;");
        l->addWidget(dot);
        l->addWidget(label(item.name, "small"), 1);
        l->addWidget(label(
            total > 0 ? QLocale().toString(double(item.amount / total * 100), 'f', 1) + " %" : "—", "small"));
        legend_->addWidget(line);
    }
    allocationNote_->setText(state.taxLiability + state.shareholderLiability > 0
            ? "Доли от активов до вычета обязательств"
            : "Доли от общей стоимости активов");
    const MonthReport* report = state.pending.report.month > 0 ? &state.pending.report
        : state.history.empty()                                ? nullptr
                                                               : &state.history.back();
    monthTitle_->setText(report ? QString("Результат месяца %1").arg(report->month) : "Результат месяца");
    monthNote_->setText(report ? (state.pending.report.month > 0 ? "Предварительно · месяц ещё не закрыт"
                                                                 : "Месяц закрыт · денежные суммы в рублях")
                               : "После первого шага здесь появится разбор изменений");
    flow_->setReport(report, state.pending.report.month == 0 || state.pending.flowsApplied);
}
