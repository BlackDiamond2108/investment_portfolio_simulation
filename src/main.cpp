#include "MainWindow.h"
#include <QApplication>
#include <QLocale>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("InvestmentPortfolioSimulator");
    QApplication::setOrganizationName("InvestmentPortfolio");
    QLocale::setDefault(QLocale(QLocale::Russian, QLocale::Russia));
    QApplication::setStyle("Fusion");
    MainWindow window;
    window.show();
    return app.exec();
}
