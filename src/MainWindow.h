#pragma once

#include "Simulation.h"
#include "GameStorage.h"
#include <QMainWindow>
#include <memory>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
class QTableWidget;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QPlainTextEdit;
QT_END_NAMESPACE

// Одно окно: только ввод/отображение. Финансовые правила остаются в Simulation.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
protected:
    void closeEvent(QCloseEvent* event) override;
private slots:
    void onNewGame();
    void onTrade();
    void onNextMonth();
    void onApplyFlows();
    void onFinishMonth();
    void onConfirmInitialAllocation();
    void onSave();
    void onLoad();
    void refreshPreview();
    void refreshIncome();
private:
    void buildPages();
    void setupConnections();
    void refreshView();
    void refreshTradeChoices();
    void refreshCatalog();
    void editInstrument(int row);
    void fillSettings(const GameConfig& config);
    Result<GameConfig> collectConfig() const;
    TradeRequest collectTradeRequest() const;
    void applyResult(const Result<void>& result);
    bool confirmDiscard();
    bool saveGame();
    void showError(const std::string& message);

    std::unique_ptr<Ui::MainWindow> ui;
    Simulation simulation_;
    GameConfig draft_;
    bool dirty_ = false;
    QString filePath_;
    QTableWidget *portfolioTable_, *marketTable_, *historyTable_, *incomeTable_, *catalogTable_;
    QComboBox *tradeAction_, *tradeTarget_, *flowMode_;
    QDoubleSpinBox *tradeValue_, *inflow_, *redeem_, *capital_, *tax_, *usd_, *eur_;
    QDoubleSpinBox *usdMin_, *usdMax_, *eurMin_, *eurMax_, *sensitivity_, *noise_, *maxIn_, *maxOut_;
    QSpinBox* months_;
    QLineEdit* seed_;
    QLabel *navLabel_, *cashLabel_, *debtLabel_, *returnLabel_, *tradePreview_, *tradeUnit_, *flowInfo_, *statsLabel_;
    QPushButton *tradeButton_, *flowsButton_;
    QPlainTextEdit* journal_;
};
