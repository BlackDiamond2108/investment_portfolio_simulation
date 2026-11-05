#pragma once

#include "GameTypes.h"
#include <QColor>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;
class QBoxLayout;
class QComboBox;
class PerformanceChart;
class AllocationChart;
class MonthTimeline;
class FlowChart;

// Только данные отображения. Значения взяты из отчётов и оценки Portfolio;
// предварительный месяц отмечен отдельно и не дублирует закрытый отчёт.
struct DashboardPoint {
    int month = 0;
    Money capital = 0;
    Money netProfit = 0;
    bool provisional = false;
};
struct DashboardAllocation {
    QString name;
    Money amount = 0;
    QColor color;
};

class DashboardWidget : public QWidget {
    Q_OBJECT
public:
    explicit DashboardWidget(QWidget* parent = nullptr);
    void setState(const GameState& state, const GameStatistics& stats);
    const QVector<DashboardPoint>& displayedSeries() const { return series_; }
    const QVector<DashboardAllocation>& displayedAllocation() const { return allocation_; }
signals:
    void exampleRequested();
    void nextStepRequested();
    void marketRequested();
    void positionRequested(qint64 positionId);
    void monthRequested(int month);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void refreshChart();
    QLabel *balance_, *performance_, *phase_, *cash_, *debt_, *profit_, *drawdown_, *allocationNote_;
    QLabel *monthTitle_, *monthNote_, *holdingsTitle_;
    QPushButton *step_, *example_, *buy_;
    QComboBox *chartMode_, *period_;
    PerformanceChart* chart_;
    AllocationChart* donut_;
    MonthTimeline* timeline_;
    FlowChart* flow_;
    QVBoxLayout *legend_, *holdings_;
    QBoxLayout *chartRow_, *detailsRow_;
    QVector<DashboardPoint> series_;
    QVector<DashboardAllocation> allocation_;
};
