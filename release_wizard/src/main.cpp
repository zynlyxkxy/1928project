#include "mainwindow.h"
#include "refreshworker.h"
#include "theme.h"

#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFont>
#include <QMetaType>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QProxyStyle>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplashScreen>
#include <QStackedWidget>
#include <QTabWidget>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTimer>
#include <QWidget>

namespace {

class AppStyle : public QProxyStyle
{
public:
    AppStyle()
        : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion")))
    {
    }

    int styleHint(StyleHint hint, const QStyleOption *option = nullptr,
                  const QWidget *widget = nullptr,
                  QStyleHintReturn *returnData = nullptr) const override
    {
        // Show the description bubble after ~0.8 s, hide it as soon as the mouse leaves.
        if (hint == SH_ToolTip_WakeUpDelay) return 800;
        if (hint == SH_ToolTip_FallAsleepDelay) return 200;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }

    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr,
                    const QWidget *widget = nullptr) const override
    {
        if (metric == PM_IndicatorWidth || metric == PM_IndicatorHeight
            || metric == PM_ExclusiveIndicatorWidth
            || metric == PM_ExclusiveIndicatorHeight) {
            return 16;
        }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget = nullptr) const override
    {
        if (element == PE_IndicatorCheckBox) {
            drawIndicator(option, painter, false);
            return;
        }
        if (element == PE_IndicatorRadioButton) {
            drawIndicator(option, painter, true);
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

private:
    static void drawIndicator(const QStyleOption *option, QPainter *painter, bool radio)
    {
        if (!option || !painter) return;

        const bool on = option->state & State_On;
        const bool enabled = option->state & State_Enabled;
        const bool hover = option->state & State_MouseOver;
        const bool pressed = option->state & State_Sunken;

        const qreal side = 16.0;
        QRectF rect(0, 0, side, side);
        rect.moveCenter(QRectF(option->rect).center());

        QColor border = enabled ? QColor(0xCB, 0xD5, 0xE1) : QColor(0xE2, 0xE8, 0xF0);
        QColor fill = enabled ? QColor(Qt::white) : QColor(0xF8, 0xFA, 0xFC);
        if (hover && enabled) border = QColor(0x25, 0x63, 0xEB);
        if (on) {
            border = enabled ? QColor(0x25, 0x63, 0xEB) : QColor(0xBF, 0xDB, 0xFE);
            if (!radio) {
                fill = enabled
                           ? (pressed ? QColor(0x1D, 0x4E, 0xD8) : QColor(0x25, 0x63, 0xEB))
                           : QColor(0xBF, 0xDB, 0xFE);
            }
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(border, 1));
        painter->setBrush(fill);

        const QRectF shape = rect.adjusted(0.5, 0.5, -0.5, -0.5);
        if (radio) {
            painter->drawEllipse(shape);
            if (on) {
                painter->setPen(Qt::NoPen);
                painter->setBrush(enabled ? QColor(0x25, 0x63, 0xEB)
                                          : QColor(0xBF, 0xDB, 0xFE));
                painter->drawEllipse(shape.adjusted(4.5, 4.5, -4.5, -4.5));
            }
        } else {
            painter->drawRoundedRect(shape, 4, 4);
            if (on) {
                QPainterPath check;
                check.moveTo(shape.left() + 3.6, shape.center().y());
                check.lineTo(shape.left() + 6.4, shape.bottom() - 3.6);
                check.lineTo(shape.right() - 3.4, shape.top() + 4.0);
                painter->setPen(
                    QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter->drawPath(check);
            }
        }
        painter->restore();
    }
};

class ComboWheelGuard : public QObject
{
public:
    explicit ComboWheelGuard(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        // A closed combo must not change its selection when the page is scrolled.
        if (event->type() == QEvent::Wheel && qobject_cast<QComboBox *>(watched)) return true;
        return QObject::eventFilter(watched, event);
    }
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ISTAR"));
    QCoreApplication::setApplicationName(QStringLiteral("1928RepoManager"));
    app.setStyle(new AppStyle);
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    app.setStyleSheet(QString::fromUtf8(kAppStyle));
    app.installEventFilter(new ComboWheelGuard(&app));

    qRegisterMetaType<QVector<RefreshJob>>("QVector<RefreshJob>");
    qRegisterMetaType<RefreshResultMap>("RefreshResultMap");

    const QStringList args = app.arguments();
    const bool screenshotMode = args.contains(QStringLiteral("--screenshot"));

    MainWindow window;
    if (screenshotMode) window.show();

    const int sizeIndex = args.indexOf(QStringLiteral("--window-size"));
    if (sizeIndex >= 0 && sizeIndex + 1 < args.size()) {
        const QStringList parts = args.at(sizeIndex + 1).split(QLatin1Char('x'));
        if (parts.size() == 2) {
            bool okW = false;
            bool okH = false;
            const int width = parts.at(0).toInt(&okW);
            const int height = parts.at(1).toInt(&okH);
            if (okW && okH && width > 0 && height > 0) window.resize(width, height);
        }
    }

    if (args.contains(QStringLiteral("--scroll-bottom"))) {
        QTimer::singleShot(5000, &window, [&window]() {
            auto *stack = window.findChild<QStackedWidget *>();
            if (!stack || !stack->currentWidget()) return;
            auto *area = qobject_cast<QScrollArea *>(stack->currentWidget());
            if (!area) area = stack->currentWidget()->findChild<QScrollArea *>();
            if (area) area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());
        });
    }

    const int pageIndex = args.indexOf(QStringLiteral("--page"));
    if (pageIndex >= 0 && pageIndex + 1 < args.size()) {
        bool ok = false;
        const int page = args.at(pageIndex + 1).toInt(&ok);
        if (ok) window.selectPage(page);
    }

    // 调试 / 截图用：--release-tab N 切到发布页内部的第 N 个 tab（0 = 主仓发布）
    const int releaseTabIndex = args.indexOf(QStringLiteral("--release-tab"));
    if (releaseTabIndex >= 0 && releaseTabIndex + 1 < args.size()) {
        bool ok = false;
        const int tab = args.at(releaseTabIndex + 1).toInt(&ok);
        if (ok) {
            window.selectPage(5);
            auto *tabs = window.findChild<QTabWidget *>();
            if (tabs && tab >= 0 && tab < tabs->count()) tabs->setCurrentIndex(tab);
        }
    }

    const int shotIndex = args.indexOf(QStringLiteral("--screenshot"));
    if (shotIndex >= 0 && shotIndex + 1 < args.size()) {
        const QString path = args.at(shotIndex + 1);
        int delay = 2500;
        const int delayIndex = args.indexOf(QStringLiteral("--screenshot-delay"));
        if (delayIndex >= 0 && delayIndex + 1 < args.size()) {
            bool ok = false;
            const int value = args.at(delayIndex + 1).toInt(&ok);
            if (ok && value > 0) delay = value;
        }
        QTimer::singleShot(delay, &app, [&window, path]() {
            window.grab().save(path);
            QCoreApplication::quit();
        });
    }

    if (!screenshotMode) {
        QPixmap pixmap(360, 110);
        pixmap.fill(QColor(Qt::white));
        QPainter splashPainter(&pixmap);
        splashPainter.setPen(QColor(0xE5, 0xE7, 0xEB));
        splashPainter.drawRect(0, 0, pixmap.width() - 1, pixmap.height() - 1);
        splashPainter.end();

        QPointer<QSplashScreen> splash = new QSplashScreen(pixmap);
        QFont splashFont = splash->font();
        splashFont.setPointSize(10);
        splash->setFont(splashFont);
        splash->showMessage(QString::fromUtf8("1928 仓库管理工具\n正在读取仓库…"),
                            Qt::AlignCenter, QColor(0x1F, 0x29, 0x37));
        splash->show();

        QObject::connect(&window, &MainWindow::firstRefreshDone, &app,
                         [&window, splash]() {
                             window.show();
                             if (splash) splash->close();
                         });
        QTimer::singleShot(10000, &window, [&window, splash]() {
            window.show();
            if (splash && splash->isVisible()) splash->close();
        });
    }

    return app.exec();
}
