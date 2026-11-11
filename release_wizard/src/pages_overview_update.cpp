#include "mainwindow.h"
#include "ui_helpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QStatusBar>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {

QLabel *valueLabel()
{
    auto *label = new QLabel(QStringLiteral("—"));
    label->setTextFormat(Qt::RichText);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(false);
    return label;
}

QLabel *repoTitleLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("repoTitle"));
    return label;
}

QLabel *subTitleLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("subTitle"));
    return label;
}

QFrame *makeSubCard(const QString &title, QVBoxLayout **body)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("subcard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 10, 12, 11);
    layout->setSpacing(5);
    layout->addWidget(subTitleLabel(title));
    *body = layout;
    return card;
}

QWidget *kvRow(const QString &key, QWidget *value, int keyWidth = 64,
               QLabel **keyLabel = nullptr)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *name = new QLabel(key);
    name->setObjectName(QStringLiteral("muted"));
    name->setFixedWidth(keyWidth);
    name->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    if (keyLabel) *keyLabel = name;
    layout->addWidget(name, 0, Qt::AlignVCenter);
    layout->addWidget(value, 1);
    return row;
}

QPushButton *viewButton()
{
    auto *button = new QPushButton(U("查看"));
    button->setObjectName(QStringLiteral("secondary"));
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(28);
    button->setFixedWidth(64);
    return button;
}

QWidget *kvButtonRow(const QString &key, QPushButton *button, int keyWidth = 64)
{
    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *name = new QLabel(key);
    name->setObjectName(QStringLiteral("muted"));
    name->setFixedWidth(keyWidth);
    name->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(name, 0, Qt::AlignVCenter);
    layout->addWidget(button, 0, Qt::AlignVCenter);
    layout->addStretch(1);
    return row;
}

} // namespace

QWidget *MainWindow::buildRepoCard(const QString &title, RepoWidgets &w, bool isMain, int index)
{
    auto *card = new QFrame;
    card->setObjectName(isMain ? QStringLiteral("mainCard") : QStringLiteral("repoCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);
    layout->addWidget(repoTitleLabel(isMain ? U("1928project") : title));

    QVBoxLayout *body = nullptr;

    auto *current = makeSubCard(U("当前 git 所在位置"), &body);
    w.branch = valueLabel();
    w.worktree = valueLabel();
    w.remote = valueLabel();
    body->addWidget(kvRow(U("分支"), w.branch, 64, &w.branchKey));
    body->addWidget(kvRow(U("工作区"), w.worktree, 64, &w.worktreeKey));
    body->addWidget(kvRow(U("远端"), w.remote, 64, &w.remoteKey));
    layout->addWidget(current);

    auto *develop = makeSubCard(U("最后一次 develop 提交"), &body);
    w.developDate = valueLabel();
    w.developView = viewButton();
    body->addWidget(kvRow(U("提交时间"), w.developDate));
    body->addWidget(kvButtonRow(U("改动描述"), w.developView));
    layout->addWidget(develop);

    auto *tag = makeSubCard(U("最新 release tag"), &body);
    w.tagVersion = valueLabel();
    w.tagName = valueLabel();
    w.tagDate = valueLabel();
    w.tagView = viewButton();
    body->addWidget(kvRow(U("版本"), w.tagVersion));
    body->addWidget(kvRow(U("标签"), w.tagName));
    body->addWidget(kvRow(U("提交时间"), w.tagDate));
    body->addWidget(kvButtonRow(U("迭代内容"), w.tagView));
    layout->addWidget(tag);

    layout->addStretch();

    const QString repoName = isMain ? U("1928project") : title;
    const QString repoPath = isMain ? QStringLiteral(".") : m_repos.at(index).path;

    connect(w.developView, &QPushButton::clicked, this, [this, repoName, repoPath]() {
        showCommitDetail(repoName, repoPath);
    });
    connect(w.tagView, &QPushButton::clicked, this, [this, isMain, index, repoName]() {
        if (isMain) {
            showTagDetail(repoName, QStringLiteral("."), m_mainRepo.latestTag,
                          m_mainRepo.previousTag);
        } else {
            const RepoState &state = m_repos.at(index);
            showTagDetail(state.name, state.path, state.latestTag, state.previousTag);
        }
    });

    return card;
}

QWidget *MainWindow::buildPlatformCard()
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("repoCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);
    layout->addWidget(repoTitleLabel(U("平台层")));

    auto *row = new QHBoxLayout;
    row->setSpacing(12);
    m_platVersion.resize(m_platform.size());
    for (int i = 0; i < m_platform.size(); ++i) {
        QVBoxLayout *body = nullptr;
        auto *sub = makeSubCard(m_platform.at(i).name, &body);
        m_platVersion[i] = valueLabel();
        body->addWidget(kvRow(U("版本"), m_platVersion[i], 64));
        row->addWidget(sub, 1);
    }
    layout->addLayout(row);
    layout->addStretch();
    return card;
}

QWidget *MainWindow::buildOverviewPage()
{
    auto *page = new QWidget;
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 12, 24, 24);
    layout->setSpacing(12);

    m_repoWidgets.resize(m_repos.size());
    auto *grid = new QGridLayout;
    grid->setSpacing(12);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);

    // Row 1: 1928project + mctrl + gui
    grid->addWidget(buildRepoCard(U("1928project"), m_mainWidgets, true, -1), 0, 0);
    grid->addWidget(buildRepoCard(m_repos.at(3).name, m_repoWidgets[3], false, 3), 0, 1);
    grid->addWidget(buildRepoCard(m_repos.at(4).name, m_repoWidgets[4], false, 4), 0, 2);

    // Row 2: io + motion + re
    grid->addWidget(buildRepoCard(m_repos.at(0).name, m_repoWidgets[0], false, 0), 1, 0);
    grid->addWidget(buildRepoCard(m_repos.at(1).name, m_repoWidgets[1], false, 1), 1, 1);
    grid->addWidget(buildRepoCard(m_repos.at(2).name, m_repoWidgets[2], false, 2), 1, 2);

    // Row 3: platform layer, full width
    grid->addWidget(buildPlatformCard(), 2, 0, 1, 3);

    layout->addLayout(grid);
    layout->addStretch();

    pageLayout->addWidget(makeScrollPage(content), 1);
    return page;
}

QWidget *MainWindow::buildOneClickPage()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(14);

    auto *cardContent = new QWidget;
    auto *cardLayout = new QVBoxLayout(cardContent);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(12);

    auto *description = new QLabel(
        U("把 5 个子仓库拉到当前主仓库提交绑定的版本（子仓不存在时会自动初始化），"
          "然后编译、跨工程同步、装配 exe。不会拉取 develop 最新代码。"));
    description->setWordWrap(true);
    description->setObjectName(QStringLiteral("muted"));
    cardLayout->addWidget(description);

    m_oneClickExecute = makeDisabledPrimary(U("执行"));
    m_oneClickExecute->setMinimumWidth(96);
    m_oneClickExecute->setEnabled(true);
    auto *actions = new QHBoxLayout;
    actions->addWidget(m_oneClickExecute);
    actions->addStretch();
    cardLayout->addLayout(actions);

    connect(m_oneClickExecute, &QPushButton::clicked, this, [this]() {
        runScript(QStringLiteral("engineering\\scripts\\init_update_build.bat"), {},
                  U("一键全自动"), m_oneClickExecute, U("执行中…"), U("执行"));
    });

    layout->addWidget(makeCard(U("一键全自动"),
                               U("还原子仓 → 编译 → 跨工程同步 → 装配 exe"),
                               cardContent));

    // ---- 运行程序 ----
    auto *runContent = new QWidget;
    auto *runLayout = new QVBoxLayout(runContent);
    runLayout->setContentsMargins(0, 0, 0, 0);
    runLayout->setSpacing(10);

    m_runSimMode = new QCheckBox(U("仿真模式"));
    m_runSimMode->setToolTip(U("勾选后启动时加 -s：同时拉起 simbridge relay 和 28 模型。"));
    runLayout->addWidget(m_runSimMode);

    m_runStart = makeDisabledPrimary(U("启动程序"));
    m_runStart->setMinimumWidth(96);
    m_runStop = makeSecondary(U("停止全部"));
    m_runStop->setMinimumWidth(96);
    auto *runActions = new QHBoxLayout;
    runActions->addWidget(m_runStart);
    runActions->addWidget(m_runStop);
    runActions->addStretch();
    runLayout->addLayout(runActions);

    connect(m_runStart, &QPushButton::clicked, this, [this]() {
        QStringList args;
        if (m_runSimMode && m_runSimMode->isChecked()) args << QStringLiteral("-s");
        runScript(QStringLiteral("exe\\start_all.bat"), args,
                  U("启动程序"), m_runStart, U("启动中…"), U("启动程序"));
    });

    connect(m_runStop, &QPushButton::clicked, this, [this]() {
        const auto answer = QMessageBox::question(
            this, U("停止全部"),
            U("会结束 GUI_WM、MCtrl_WM 和所有 CTL_* 进程，确定停止吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
        runScript(QStringLiteral("exe\\stop_all.bat"), {},
                  U("停止全部程序"), m_runStop, U("停止中…"), U("停止全部"));
    });

    layout->addWidget(makeCard(U("运行程序"),
                               U("启动 / 停止 exe 目录里的整套程序。"),
                               runContent));
    layout->addWidget(labelHint(U("提示：启动需要 exe 目录完整；关闭本界面不会结束已启动的程序。")));

    layout->addStretch();
    return makeScrollPage(content);
}

QWidget *MainWindow::buildUpdatePage()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(14);

    // ---- Code repositories ----
    auto *codeContent = new QWidget;
    auto *codeLayout = new QVBoxLayout(codeContent);
    codeLayout->setContentsMargins(0, 0, 0, 0);
    codeLayout->setSpacing(10);

    auto *repoRow = new QWidget;
    auto *repoLayout = new QHBoxLayout(repoRow);
    repoLayout->setContentsMargins(0, 0, 0, 0);
    repoLayout->setSpacing(10);

    auto *repoLabel = new QLabel(U("代码仓库"));
    repoLabel->setObjectName(QStringLiteral("muted"));
    repoLabel->setFixedWidth(96);
    repoLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    repoLayout->addWidget(repoLabel);

    m_updateChecks.clear();
    const QStringList repoNames = {QStringLiteral("io"), QStringLiteral("motion"),
                                   QStringLiteral("re"), QStringLiteral("mctrl"),
                                   QStringLiteral("gui")};
    for (const QString &name : repoNames) {
        auto *check = new QCheckBox(name);
        check->setChecked(true);
        m_updateChecks.append(check);
        repoLayout->addWidget(check);
    }
    repoLayout->addStretch();

    auto *selectAll = makeSecondary(U("全选"));
    auto *invert = makeSecondary(U("反选"));
    auto *selectNone = makeSecondary(U("全不选"));
    selectAll->setFixedWidth(76);
    invert->setFixedWidth(76);
    selectNone->setFixedWidth(76);
    repoLayout->addWidget(selectAll);
    repoLayout->addWidget(invert);
    repoLayout->addWidget(selectNone);
    codeLayout->addWidget(repoRow);

    m_updateExecute = makeDisabledPrimary(U("更新仓库"));
    m_updateExecute->setMinimumWidth(96);
    auto *codeActions = new QHBoxLayout;
    codeActions->addWidget(m_updateExecute);
    codeActions->addStretch();
    codeLayout->addLayout(codeActions);

    for (QCheckBox *check : m_updateChecks) {
        connect(check, &QCheckBox::toggled, this, [this](bool) { updateUpdateButtons(); });
    }
    connect(selectAll, &QPushButton::clicked, this, [this]() {
        for (QCheckBox *check : m_updateChecks) check->setChecked(true);
    });
    connect(invert, &QPushButton::clicked, this, [this]() {
        for (QCheckBox *check : m_updateChecks) check->setChecked(!check->isChecked());
    });
    connect(selectNone, &QPushButton::clicked, this, [this]() {
        for (QCheckBox *check : m_updateChecks) check->setChecked(false);
    });

    connect(m_updateExecute, &QPushButton::clicked, this, [this]() {
        QStringList selected;
        for (QCheckBox *check : m_updateChecks) {
            if (check->isChecked()) selected << check->text();
        }
        if (selected.isEmpty()) return;

        QStringList args;
        if (selected.size() != m_updateChecks.size()) args = selected;
        runScript(QStringLiteral("engineering\\scripts\\update_corecode.bat"), args,
                  U("核心代码仓库"), m_updateExecute, U("更新中…"), U("更新仓库"));
    });

    layout->addWidget(makeCard(U("核心代码仓库更新"),
                               U("勾选要更新的核心代码仓库；更新到 develop 的最新提交处。不更新平台仓库以及第三方库。"),
                               codeContent));

    // ---- Platform ----
    auto *platformContent = new QWidget;
    auto *platformLayout = new QVBoxLayout(platformContent);
    platformLayout->setContentsMargins(0, 0, 0, 0);
    platformLayout->setSpacing(10);

    auto *radioRow = new QWidget;
    auto *radioLayout = new QHBoxLayout(radioRow);
    radioLayout->setContentsMargins(0, 0, 0, 0);
    radioLayout->setSpacing(10);

    auto *platformLabel = new QLabel(U("更新方式"));
    platformLabel->setObjectName(QStringLiteral("muted"));
    platformLabel->setFixedWidth(96);
    platformLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    radioLayout->addWidget(platformLabel);

    m_platformLatest = new QRadioButton(U("更新到最新"));
    m_platformSpecific = new QRadioButton(U("更新到指定版本"));
    m_platformLatest->setChecked(true);
    m_platformVersion = new QLineEdit;
    m_platformVersion->setPlaceholderText(U("例如 2.0.5"));
    m_platformVersion->setMaximumWidth(200);
    m_platformVersion->setEnabled(false);

    radioLayout->addWidget(m_platformLatest);
    radioLayout->addWidget(m_platformSpecific);
    radioLayout->addWidget(m_platformVersion);
    radioLayout->addStretch();
    platformLayout->addWidget(radioRow);

    connect(m_platformSpecific, &QRadioButton::toggled, this, [this](bool on) {
        m_platformVersion->setEnabled(on);
        if (on) {
            m_platformVersion->setFocus();
            m_platformVersion->selectAll();
        }
        updateUpdateButtons();
    });
    connect(m_platformLatest, &QRadioButton::toggled, this, [this](bool) {
        updateUpdateButtons();
    });
    connect(m_platformVersion, &QLineEdit::textChanged, this, [this](const QString &) {
        updateUpdateButtons();
    });

    auto *platformActions = new QHBoxLayout;
    m_platformExecute = makeDisabledPrimary(U("更新仓库"));
    m_platformExecute->setMinimumWidth(96);
    m_platformQuery = makeSecondary(U("查询状态"));
    m_platformQuery->setFixedWidth(96);
    platformActions->addWidget(m_platformExecute);
    platformActions->addWidget(m_platformQuery);
    platformActions->addStretch();
    platformLayout->addLayout(platformActions);

    connect(m_platformQuery, &QPushButton::clicked, this, [this]() { showPlatformStatus(); });
    connect(m_platformExecute, &QPushButton::clicked, this, [this]() {
        QStringList args;
        if (m_platformSpecific->isChecked()) {
            const QString version = m_platformVersion->text().trimmed();
            if (version.isEmpty()) return;
            args << version;
        } else {
            args << QStringLiteral("latest");
        }
        runScript(QStringLiteral("engineering\\scripts\\update_platform.bat"), args,
                  U("平台仓库"), m_platformExecute, U("更新中…"), U("更新仓库"));
    });

    layout->addWidget(makeCard(U("平台仓库更新"),
                               U("把平台仓库挂载点对齐到最新 tag 或指定 tag。不更新核心代码仓库以及第三方库"),
                               platformContent));

    // ---- Third party ----
    auto *thirdContent = new QWidget;
    auto *thirdLayout = new QVBoxLayout(thirdContent);
    thirdLayout->setContentsMargins(0, 0, 0, 0);
    thirdLayout->setSpacing(10);

    auto *thirdActions = new QHBoxLayout;
    m_thirdExecute = makeDisabledPrimary(U("更新仓库"));
    m_thirdExecute->setMinimumWidth(96);
    thirdActions->addWidget(m_thirdExecute);
    thirdActions->addStretch();
    thirdLayout->addLayout(thirdActions);

    connect(m_thirdExecute, &QPushButton::clicked, this, [this]() {
        runScript(QStringLiteral("engineering\\scripts\\update_thirdparty.bat"), {},
                  U("第三方仓库"), m_thirdExecute, U("更新中…"), U("更新仓库"));
    });

    layout->addWidget(makeCard(U("第三方仓库更新"),
                               U("把第三方仓库拉到最新；不更新核心代码仓库以及平台仓库"),
                               thirdContent));

    // ---- 还原所有子仓库（特殊更新：回到当前绑定的提交） ----
    auto *restoreContent = new QWidget;
    auto *restoreLayout = new QVBoxLayout(restoreContent);
    restoreLayout->setContentsMargins(0, 0, 0, 0);
    restoreLayout->setSpacing(12);

    m_restoreExecute = makeDisabledPrimary(U("执行"));
    m_restoreExecute->setMinimumWidth(96);
    m_restoreExecute->setEnabled(true);
    auto *restoreActions = new QHBoxLayout;
    restoreActions->addWidget(m_restoreExecute);
    restoreActions->addStretch();
    restoreLayout->addLayout(restoreActions);

    connect(m_restoreExecute, &QPushButton::clicked, this, [this]() {
        runScript(QStringLiteral("engineering\\scripts\\init_submodules.bat"), {},
                  U("还原所有子仓库"), m_restoreExecute, U("执行中…"), U("执行"));
    });

    layout->addWidget(makeCard(U("还原所有子仓库"),
                               U("将所有子仓库git的位置，还原回顶层仓库绑定的位置"),
                               restoreContent));

    layout->addWidget(labelHint(U("提示：更新和编译是两件事。更新只负责把仓拉到目标版本，编译请去“构建”页。")));
    layout->addStretch();

    updateUpdateButtons();
    return makeScrollPage(content);
}

void MainWindow::updateUpdateButtons()
{
    if (m_oneClickExecute) m_oneClickExecute->setEnabled(!m_scriptRunning);
    if (m_restoreExecute) m_restoreExecute->setEnabled(!m_scriptRunning);
    if (m_buildExecute) {
        int buildChecked = 0;
        for (QCheckBox *check : m_buildTargets) {
            if (check->isChecked()) ++buildChecked;
        }
        m_buildExecute->setEnabled(!m_scriptRunning && buildChecked > 0);
    }
    if (m_syncExecute) {
        int syncChecked = 0;
        for (QCheckBox *check : m_syncChecks) {
            if (check->isChecked()) ++syncChecked;
        }
        m_syncExecute->setEnabled(!m_scriptRunning && syncChecked > 0);
        if (m_syncPreview) m_syncPreview->setEnabled(!m_scriptRunning && syncChecked > 0);
    }
    if (m_runSimMode) m_runSimMode->setEnabled(!m_scriptRunning);
    if (m_runStart) {
        const bool exeReady = QFileInfo::exists(m_root + QStringLiteral("/exe/start_all.bat"));
        m_runStart->setEnabled(!m_scriptRunning && exeReady);
    }
    if (m_runStop) m_runStop->setEnabled(!m_scriptRunning);
    const bool idle = !m_scriptRunning;
    if (m_mainViewSection) m_mainViewSection->setEnabled(idle);
    if (m_mainPublish) {
        const bool bodyReady = !m_mainSectionText.trimmed().isEmpty();
        m_mainPublish->setEnabled(idle && bodyReady && !m_mainBlocked);
        m_mainPublish->setToolTip(m_mainBlocked ? m_mainBlockDetail : QString());
    }
    for (MainBindRow &row : m_mainRows) {
        if (row.pick) row.pick->setEnabled(idle);
    }
    for (const SubForm &form : m_subForms) {
        if (!form.publish) continue;
        const bool pushPending = form.action == SubActionPush;
        form.publish->setEnabled(idle && form.action != SubActionNone && !form.blocked);
        form.publish->setToolTip(form.blocked ? form.blockReason : QString());
        if (!idle) {
            form.publish->setText(pushPending ? U("推送中…") : U("发布中…"));
        } else {
            form.publish->setText(pushPending ? U("推送") : U("发布"));
        }
        const bool formOn = idle && !pushPending;
        form.body->setEnabled(formOn);
        form.modeNew->setEnabled(formOn);
        form.modeIterate->setEnabled(formOn && form.iteratePossible);
        form.version->setEnabled(formOn && !form.iterateMode);
        if (form.push) form.push->setEnabled(formOn);
    }
    if (!m_updateExecute || !m_platformExecute || !m_thirdExecute) return;

    int checked = 0;
    for (QCheckBox *check : m_updateChecks) {
        if (check->isChecked()) ++checked;
    }
    const bool platformOk =
        m_platformLatest->isChecked()
        || (m_platformSpecific->isChecked() && !m_platformVersion->text().trimmed().isEmpty());

    m_updateExecute->setEnabled(!m_scriptRunning && checked > 0);
    m_platformExecute->setEnabled(!m_scriptRunning && platformOk);
    m_thirdExecute->setEnabled(!m_scriptRunning);
    if (m_platformQuery) m_platformQuery->setEnabled(!m_scriptRunning);
}

void MainWindow::runScript(const QString &script, const QStringList &args,
                           const QString &title, QPushButton *button,
                           const QString &busyText, const QString &idleText)
{
    if (m_scriptRunning) return;

    m_scriptRunning = true;
    if (button) button->setText(busyText);
    updateUpdateButtons();
    if (m_autoTimer) m_autoTimer->stop();
    statusBar()->showMessage(U("正在执行：%1").arg(title));
    appendLog(U("[UI] 开始执行：") + title);

    auto *process = new QProcess(this);
    process->setWorkingDirectory(m_root);
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setProperty("handled", false);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("_SCRIPT_NO_HOLD"), QStringLiteral("1"));
    process->setProcessEnvironment(env);

    connect(process, &QProcess::readyRead, this, [this, process]() {
        QString chunk = QString::fromLocal8Bit(process->readAll());
        chunk.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        chunk.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        if (!chunk.trimmed().isEmpty()) appendLog(chunk.trimmed());
    });

    auto finish = [this, process, title, button, idleText](bool ok) {
        if (process->property("handled").toBool()) return;
        process->setProperty("handled", true);

        m_scriptRunning = false;
        if (button) button->setText(idleText);
        updateUpdateButtons();

        appendLog(ok ? U("[UI] 执行完成：") + title : U("[UI] 执行失败：") + title);
        flushLogFile();
        statusBar()->showMessage(ok ? U("执行完成：%1").arg(title)
                                    : U("执行失败：%1").arg(title),
                                 8000);
        if (ok) refreshStatus(true, false);
        else scheduleAutoRefresh();
        process->deleteLater();
    };

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [finish](int code, QProcess::ExitStatus status) {
                finish(status == QProcess::NormalExit && code == 0);
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, process, finish](QProcess::ProcessError) {
                if (process->state() == QProcess::NotRunning) {
                    appendLog(U("[UI] 无法启动脚本：") + process->errorString());
                    finish(false);
                }
            });

    // A full build can take a while; this is only a safety net for a hung script.
    QTimer::singleShot(1800000, process, [process]() {
        if (process->state() != QProcess::NotRunning) process->kill();
    });

    QStringList full;
    full << QStringLiteral("/c") << script << args;
    process->start(QStringLiteral("cmd"), full);
}

void MainWindow::showScriptOutput(const QString &title, const QString &heading,
                                  const QString &rerunText, const QString &script,
                                  const QStringList &args)
{
    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(760, 480);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(8);

    layout->addWidget(labelTitle(heading));

    auto *text = new QPlainTextEdit;
    text->setObjectName(QStringLiteral("bom"));
    text->setReadOnly(true);
    text->setPlainText(U("执行中…"));
    layout->addWidget(text, 1);

    auto *actions = new QHBoxLayout;
    auto *rerun = makeSecondary(rerunText);
    auto *close = makeSecondary(U("关闭"));
    actions->addWidget(rerun);
    actions->addStretch();
    actions->addWidget(close);
    layout->addLayout(actions);

    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);

    auto startQuery = [this, &dialog, text, rerun, script, args]() {
        rerun->setEnabled(false);
        text->clear();
        text->setPlainText(U("执行中…"));

        auto *process = new QProcess(&dialog);
        process->setWorkingDirectory(m_root);
        process->setProcessChannelMode(QProcess::MergedChannels);

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("_SCRIPT_NO_HOLD"), QStringLiteral("1"));
        process->setProcessEnvironment(env);

        connect(process, &QProcess::readyRead, &dialog, [process, text]() {
            const QString chunk = QString::fromLocal8Bit(process->readAll());
            if (chunk.isEmpty()) return;
            if (text->toPlainText() == U("执行中…")) text->clear();
            text->moveCursor(QTextCursor::End);
            text->insertPlainText(chunk);
            text->moveCursor(QTextCursor::End);
        });
        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                &dialog, [process, text, rerun](int code, QProcess::ExitStatus status) {
                    text->moveCursor(QTextCursor::End);
                    if (status != QProcess::NormalExit || code != 0) {
                        text->insertPlainText(U("\n[执行失败] 退出码 %1\n").arg(code));
                    } else {
                        text->insertPlainText(U("\n[执行完成]\n"));
                    }
                    text->moveCursor(QTextCursor::End);
                    rerun->setEnabled(true);
                    process->deleteLater();
                });
        connect(process, &QProcess::errorOccurred, &dialog,
                [process, text, rerun](QProcess::ProcessError) {
                    text->moveCursor(QTextCursor::End);
                    text->insertPlainText(U("\n[无法启动脚本] ") + process->errorString()
                                          + QLatin1Char('\n'));
                    text->moveCursor(QTextCursor::End);
                    rerun->setEnabled(true);
                });

        QTimer::singleShot(120000, process, [process]() {
            if (process->state() != QProcess::NotRunning) process->kill();
        });

        process->start(QStringLiteral("cmd"),
                       QStringList() << QStringLiteral("/c") << script << args);
    };

    connect(rerun, &QPushButton::clicked, &dialog, [startQuery]() { startQuery(); });

    startQuery();
    dialog.exec();
}

void MainWindow::showPlatformStatus()
{
    showScriptOutput(U("平台层状态"), U("平台层各挂载点版本"), U("重新查询"),
                     QStringLiteral("engineering\\scripts\\update_platform.bat"),
                     {QStringLiteral("show")});
}

QWidget *MainWindow::buildBuildPage()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(14);

    const int kLabelWidth = 96;

    // ---- 编译 / 装配 ----
    auto *buildContent = new QWidget;
    auto *buildLayout = new QVBoxLayout(buildContent);
    buildLayout->setContentsMargins(0, 0, 0, 0);
    buildLayout->setSpacing(12);

    auto *targetRow = new QWidget;
    auto *targetLayout = new QHBoxLayout(targetRow);
    targetLayout->setContentsMargins(0, 0, 0, 0);
    targetLayout->setSpacing(10);

    auto *targetLabel = new QLabel(U("工程"));
    targetLabel->setObjectName(QStringLiteral("muted"));
    targetLabel->setFixedWidth(kLabelWidth);
    targetLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    targetLayout->addWidget(targetLabel);

    m_buildTargets.clear();
    const QStringList buildNames = {QStringLiteral("io"), QStringLiteral("motion"),
                                    QStringLiteral("re"), QStringLiteral("mctrl"),
                                    QStringLiteral("gui")};
    for (const QString &name : buildNames) {
        auto *check = new QCheckBox(name);
        check->setChecked(true);
        m_buildTargets.append(check);
        targetLayout->addWidget(check);
        connect(check, &QCheckBox::toggled, this, [this](bool) { updateUpdateButtons(); });
    }
    targetLayout->addStretch();

    auto *selectAll = makeSecondary(U("全选"));
    auto *invert = makeSecondary(U("反选"));
    auto *selectNone = makeSecondary(U("全不选"));
    selectAll->setFixedWidth(76);
    invert->setFixedWidth(76);
    selectNone->setFixedWidth(76);
    targetLayout->addWidget(selectAll);
    targetLayout->addWidget(invert);
    targetLayout->addWidget(selectNone);
    buildLayout->addWidget(targetRow);

    connect(selectAll, &QPushButton::clicked, this, [this]() {
        for (QCheckBox *check : m_buildTargets) check->setChecked(true);
    });
    connect(invert, &QPushButton::clicked, this, [this]() {
        for (QCheckBox *check : m_buildTargets) check->setChecked(!check->isChecked());
    });
    connect(selectNone, &QPushButton::clicked, this, [this]() {
        for (QCheckBox *check : m_buildTargets) check->setChecked(false);
    });

    auto *optionRow = new QWidget;
    auto *optionLayout = new QHBoxLayout(optionRow);
    optionLayout->setContentsMargins(0, 0, 0, 0);
    optionLayout->setSpacing(10);

    auto *optionLabel = new QLabel(U("选项"));
    optionLabel->setObjectName(QStringLiteral("muted"));
    optionLabel->setFixedWidth(kLabelWidth);
    optionLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    optionLayout->addWidget(optionLabel);

    m_buildClean = new QCheckBox(U("清理上一次编译内容"));
    m_buildNoSync = new QCheckBox(U("跳过工程间协议的同步"));
    m_buildClean->setToolTip(U("删除所选工程的编译目录后重新编译。"));
    m_buildNoSync->setToolTip(U("跳过跨工程同步，仅用于就地调试。"));
    optionLayout->addWidget(m_buildClean);
    optionLayout->addWidget(m_buildNoSync);
    optionLayout->addStretch();
    buildLayout->addWidget(optionRow);

    m_buildExecute = makeDisabledPrimary(U("执行"));
    m_buildExecute->setMinimumWidth(96);
    m_buildExecute->setEnabled(true);
    auto *buildActions = new QHBoxLayout;
    buildActions->addWidget(m_buildExecute);
    buildActions->addStretch();
    buildLayout->addLayout(buildActions);

    connect(m_buildExecute, &QPushButton::clicked, this, [this]() {
        QStringList targets;
        for (QCheckBox *check : m_buildTargets) {
            if (check->isChecked()) targets << check->text();
        }
        if (targets.isEmpty()) return;

        QStringList args;
        if (targets.size() != m_buildTargets.size()) args = targets;
        if (m_buildClean->isChecked()) args << QStringLiteral("-c");
        if (m_buildNoSync->isChecked()) args << QStringLiteral("--no-sync");

        runScript(QStringLiteral("engineering\\scripts\\build_exe.bat"), args,
                  U("编译 / 装配"), m_buildExecute, U("构建中…"), U("执行"));
    });

    layout->addWidget(makeCard(U("编译 / 装配"),
                               U("直接release编译io/motion/re/mctrl/gui文件夹下代码 → 编译simbridge → 装配编译成果到exe"),
                               buildContent));

    layout->addWidget(labelHint(U("提示：编译不会拉代码。要更新版本，请先回“更新”页；同步顺序已内嵌在流程里。")));

    layout->addStretch();
    updateUpdateButtons();
    return makeScrollPage(content);
}
