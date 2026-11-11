#include "mainwindow.h"
#include "ui_helpers.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <QDesktopServices>
#include <QDir>
#include <QUrl>
#include <QWidget>

QWidget *MainWindow::buildLogsPage()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(14);

    auto *cardContent = new QWidget;
    auto *cardLayout = new QVBoxLayout(cardContent);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(8);

    m_logFull = new QPlainTextEdit;
    m_logFull->setObjectName(QStringLiteral("log"));
    m_logFull->setReadOnly(true);
    m_logFull->setMaximumBlockCount(5000);
    m_logFull->setMinimumHeight(420);
    cardLayout->addWidget(m_logFull);

    auto *actions = new QHBoxLayout;
    auto *clear = makeSecondary(U("清空"));
    connect(clear, &QPushButton::clicked, this, [this]() {
        if (m_log) m_log->clear();
        if (m_logFull) m_logFull->clear();
    });
    auto *openDir = makeSecondary(U("打开日志目录"));
    connect(openDir, &QPushButton::clicked, this, [this]() {
        QDir().mkpath(logDirectory());
        QDesktopServices::openUrl(QUrl::fromLocalFile(logDirectory()));
    });
    actions->addWidget(clear);
    actions->addWidget(openDir);
    actions->addStretch();
    cardLayout->addLayout(actions);

    cardLayout->addWidget(labelHint(U("日志同时按天写入 ")
                                    + QDir::toNativeSeparators(logDirectory())
                                    + U("；界面里的内容只是当前会话。")));

    layout->addWidget(makeCard(U("完整日志"), U("命令、工作目录、开始/结束时间、退出码和输出；失败时保留现场。"), cardContent));
    layout->addStretch();

    return makeScrollPage(content);
}
