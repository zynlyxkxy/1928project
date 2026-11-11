#include "mainwindow.h"
#include "ui_helpers.h"

#include <QCheckBox>
#include <QDate>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextStream>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const char *kPageTitles[] = {
    "工程概览",
    "一键全自动",
    "更新仓库代码",
    "构建 / 装配",
    "跨工程同步",
    "发布",
    "运行日志"
};

const char *kPageSubtitles[] = {
    "主仓、核心子仓、平台层当前状态",
    "还原子仓、编译、同步、装配 exe",
    "把代码仓库、平台仓库、第三方仓库拉到指定版本，不编译",
    "编译、装配 exe",
    "协议头、ReData、McData 的跨工程传递",
    "子仓打 tag、主仓绑定版本",
    "每次执行的命令、输出和结果"
};

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_root = findRoot();

    auto makeRepo = [](const QString &key, const QString &name, const QString &path,
                       const QString &tagPrefix) {
        RepoState state;
        state.key = key;
        state.name = name;
        state.path = path;
        state.tagPrefix = tagPrefix;
        return state;
    };
    m_mainRepo = makeRepo(QStringLiteral("main"), U("主仓"), QStringLiteral("."),
                          QStringLiteral("ICB3000M_WM"));
    m_repos = {
        makeRepo(QStringLiteral("io"), QStringLiteral("io"), QStringLiteral("module/io"), QStringLiteral("IO")),
        makeRepo(QStringLiteral("motion"), QStringLiteral("motion"), QStringLiteral("module/motion"), QStringLiteral("MOTION")),
        makeRepo(QStringLiteral("re"), QStringLiteral("re"), QStringLiteral("module/re"), QStringLiteral("RE")),
        makeRepo(QStringLiteral("mctrl"), QStringLiteral("mctrl"), QStringLiteral("mctrl"), QStringLiteral("MCTRL")),
        makeRepo(QStringLiteral("gui"), QStringLiteral("gui"), QStringLiteral("gui"), QStringLiteral("GUI")),
    };

    auto makePlatform = [](const QString &name, const QString &path) {
        PlatformState state;
        state.key = QStringLiteral("plat_") + name;
        state.name = name;
        state.path = path;
        return state;
    };
    m_platform = {
        makePlatform(QStringLiteral("basiclibrary_output"), QStringLiteral("platform_output/basiclibrary_output")),
        makePlatform(QStringLiteral("rpc_output"), QStringLiteral("platform_output/rpc_output")),
        makePlatform(QStringLiteral("exe_output"), QStringLiteral("platform_output/exe_output")),
    };

    m_autoTimer = new QTimer(this);
    m_autoTimer->setSingleShot(true);
    connect(m_autoTimer, &QTimer::timeout, this, [this]() { refreshStatus(false); });

    // 日志文件 5 秒批量落盘（界面仍逐条实时显示）
    m_logFileTimer = new QTimer(this);
    m_logFileTimer->setSingleShot(true);
    m_logFileTimer->setInterval(5000);
    connect(m_logFileTimer, &QTimer::timeout, this, &MainWindow::flushLogFile);

    m_refreshThread = new QThread(this);
    m_refreshWorker = new RefreshWorker;
    m_refreshWorker->moveToThread(m_refreshThread);
    connect(this, &MainWindow::refreshRequested, m_refreshWorker, &RefreshWorker::start);
    connect(m_refreshWorker, &RefreshWorker::finished, this, &MainWindow::onRefreshFinished);
    m_refreshThread->start();

    setWindowTitle(U("1928 仓库管理工具"));
    resize(1280, 840);
    setMinimumSize(1100, 720);

    auto *central = new QWidget(this);
    central->setObjectName(QStringLiteral("root"));
    auto *outer = new QHBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(buildSidebar());

    auto *right = new QWidget(central);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    rightLayout->addWidget(buildHeader());
    rightLayout->addWidget(buildRemoteBanner());

    m_stack = new QStackedWidget(right);
    m_stack->addWidget(buildOverviewPage());
    m_stack->addWidget(buildOneClickPage());
    m_stack->addWidget(buildUpdatePage());
    m_stack->addWidget(buildBuildPage());
    m_stack->addWidget(buildSyncPage());
    m_stack->addWidget(buildReleasePage());
    m_stack->addWidget(buildLogsPage());
    rightLayout->addWidget(m_stack, 1);
    rightLayout->addWidget(buildLogPanel());

    outer->addWidget(right, 1);
    setCentralWidget(central);

    connect(m_sidebar, &QListWidget::currentRowChanged, this, &MainWindow::setPage);
    m_sidebar->setCurrentRow(0);
    setPage(0);

    statusBar()->showMessage(QString());

    appendLog(U("[UI] 1928 仓库管理工具启动"));
    appendLog(U("[UI] 仓库根目录：") + m_root);
    appendLog(U("[UI] 概览页启动时全量读取；之后每 10 秒只刷当前位置"));
    refreshStatus(true);
}

MainWindow::~MainWindow()
{
    flushLogFile();
    if (m_refreshThread && m_refreshThread->isRunning()) {
        if (m_refreshWorker) {
            QMetaObject::invokeMethod(m_refreshWorker, "stop", Qt::BlockingQueuedConnection);
        }
        m_refreshThread->quit();
        if (!m_refreshThread->wait(5000)) {
            m_refreshThread->terminate();
            m_refreshThread->wait(1000);
        }
    }
    delete m_refreshWorker;
    m_refreshWorker = nullptr;
}

QWidget *MainWindow::buildSidebar()
{
    auto *sidebar = new QWidget;
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(208);

    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(16, 22, 16, 16);
    layout->setSpacing(2);

    auto *title = new QLabel(U("1928"));
    title->setObjectName(QStringLiteral("appTitle"));
    layout->addWidget(title);

    auto *subtitle = new QLabel(U("仓库管理工具"));
    subtitle->setObjectName(QStringLiteral("appSubtitle"));
    layout->addWidget(subtitle);
    layout->addSpacing(18);

    m_sidebar = new QListWidget;
    m_sidebar->setObjectName(QStringLiteral("nav"));
    m_sidebar->setFrameShape(QFrame::NoFrame);
    m_sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const QStringList items = { U("概览"), U("一键全自动"), U("更新"), U("构建"),
                                U("同步"), U("发布"), U("日志") };
    for (const QString &item : items) {
        m_sidebar->addItem(item);
    }
    layout->addWidget(m_sidebar, 1);

    return sidebar;
}

QWidget *MainWindow::buildRemoteBanner()
{
    m_remoteBanner = new QLabel;
    m_remoteBanner->setObjectName(QStringLiteral("remoteBanner"));
    m_remoteBanner->setWordWrap(true);
    m_remoteBanner->setVisible(false);
    m_remoteBanner->setStyleSheet(QStringLiteral(
        "QLabel#remoteBanner{background:#FEF2F2;color:#B91C1C;border:1px solid #FECACA;"
        "border-radius:8px;padding:6px 12px;margin:0 24px 6px 24px;}"));
    m_remoteBanner->setToolTip(
        U("点右上角「刷新」重试；子仓未初始化时先执行「一键全自动」"));
    return m_remoteBanner;
}

void MainWindow::updateRemoteBanner()
{
    if (!m_remoteBanner) return;
    QStringList uninit;
    QStringList unknown;
    for (const RepoState &state : m_repos) {
        if (!state.initialized) uninit << state.name;
        else if (m_remoteChecked && !state.fetchOk) unknown << state.name;
    }
    QStringList lines;
    if (!uninit.isEmpty()) {
        lines << U("子仓未初始化：") + uninit.join(QStringLiteral("、"))
                     + U("（先一键全自动）");
    }
    if (!unknown.isEmpty()) {
        lines << U("远端未确认：") + unknown.join(QStringLiteral("、"))
                     + U("（点刷新重试）");
    }
    m_remoteBanner->setText(lines.join(QStringLiteral("　")));
    m_remoteBanner->setVisible(!lines.isEmpty());
}

QString MainWindow::repoStateProblem(const RepoState &state) const
{
    if (!state.initialized) return U("未初始化");
    if (!state.statusOk) return U("读不到 git 状态");
    if (state.branch != QStringLiteral("develop")) {
        return state.branch.isEmpty() ? U("当前分支未知") : U("当前分支不是 develop");
    }
    if (state.behind > 0) return U("落后 %1 个提交").arg(state.behind);
    return QString();
}

QWidget *MainWindow::buildHeader()
{
    auto *header = new QWidget;
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(24, 14, 24, 8);
    layout->setSpacing(14);

    auto *titles = new QVBoxLayout;
    titles->setSpacing(2);
    m_pageTitle = new QLabel(U("工程概览"));
    m_pageTitle->setObjectName(QStringLiteral("pageTitle"));
    m_pageSubtitle = new QLabel(U("主仓、核心子仓、平台层当前状态"));
    m_pageSubtitle->setObjectName(QStringLiteral("pageSubtitle"));
    titles->addWidget(m_pageTitle);
    titles->addWidget(m_pageSubtitle);
    layout->addLayout(titles, 1);

    // 全局刷新：所有页面都能看到 / 使用
    auto makeStatusGroup = [](const QString &label, QLabel **timeLabel, const QString &tip) {
        auto *group = new QWidget;
        group->setToolTip(tip);
        auto *row = new QHBoxLayout(group);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(6);

        auto *dot = new QLabel(QStringLiteral("●"));
        dot->setStyleSheet(QStringLiteral("color:#16A34A; font-size:11px;"));
        auto *name = new QLabel(label);
        name->setStyleSheet(QStringLiteral("color:#6B7280; font-size:11px;"));
        auto *time = new QLabel(QStringLiteral("—"));
        time->setStyleSheet(QStringLiteral("color:#111827; font-size:11px;"));
        time->setTextInteractionFlags(Qt::TextSelectableByMouse);
        row->addWidget(dot);
        row->addWidget(name);
        row->addWidget(time);
        *timeLabel = time;
        return group;
    };

    layout->addWidget(
        makeStatusGroup(U("当前 git 状态"), &m_statusHint,
                        U("分支、工作区、与远端的关系；每 10 秒自动刷新。")),
        0, Qt::AlignVCenter);
    layout->addWidget(
        makeStatusGroup(U("提交 / tag / 平台"), &m_fullHint,
                        U("最后一次 develop 提交、最新 release tag 和平台版本；"
                          "只在启动与手动刷新时更新。")),
        0, Qt::AlignVCenter);

    m_autoRefresh = new QCheckBox(U("自动 10s"));
    m_autoRefresh->setChecked(true);
    m_autoRefresh->setStyleSheet(QStringLiteral("font-size:11px;"));
    m_autoRefresh->setToolTip(
        U("每 10 秒只刷新“当前 git 所在位置”；提交、tag、平台版本只在启动和手动刷新时读取。"));
    layout->addWidget(m_autoRefresh, 0, Qt::AlignVCenter);

    m_refreshBtn = new QPushButton(U("刷新"));
    m_refreshBtn->setObjectName(QStringLiteral("secondary"));
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    m_refreshBtn->setStyleSheet(QStringLiteral("font-size:11px;"));
    m_refreshBtn->setFixedWidth(84);
    layout->addWidget(m_refreshBtn, 0, Qt::AlignVCenter);

    connect(m_refreshBtn, &QPushButton::clicked, this, [this]() { refreshStatus(true, true); });
    connect(m_autoRefresh, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled) scheduleAutoRefresh();
        else if (m_autoTimer) m_autoTimer->stop();
    });

    return header;
}

QWidget *MainWindow::buildLogPanel()
{
    auto *wrapper = new QWidget;
    auto *outer = new QVBoxLayout(wrapper);
    outer->setContentsMargins(24, 0, 24, 16);

    auto *panel = new QFrame;
    panel->setObjectName(QStringLiteral("logPanel"));
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(6);

    auto *head = new QHBoxLayout;
    auto *title = new QLabel(U("运行日志"));
    title->setObjectName(QStringLiteral("logTitle"));
    head->addWidget(title);
    head->addStretch();
    auto *collapse = new QPushButton(U("收起"));
    collapse->setObjectName(QStringLiteral("logClear"));
    collapse->setCursor(Qt::PointingHandCursor);
    connect(collapse, &QPushButton::clicked, this, [this, collapse]() {
        const bool visible = m_log->isVisible();
        m_log->setVisible(!visible);
        collapse->setText(visible ? U("展开") : U("收起"));
    });
    head->addWidget(collapse);
    auto *clear = new QPushButton(U("清空"));
    clear->setObjectName(QStringLiteral("logClear"));
    clear->setCursor(Qt::PointingHandCursor);
    connect(clear, &QPushButton::clicked, this, [this]() {
        if (m_log) m_log->clear();
        if (m_logFull) m_logFull->clear();
    });
    head->addWidget(clear);
    layout->addLayout(head);

    m_log = new QPlainTextEdit;
    m_log->setObjectName(QStringLiteral("log"));
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(3000);
    m_log->setFixedHeight(96);
    layout->addWidget(m_log);

    outer->addWidget(panel);
    return wrapper;
}

void MainWindow::setPage(int index)
{
    if (!m_stack || !m_pageTitle || !m_pageSubtitle) return;
    if (index < 0 || index >= 7) return;
    if (m_stack->currentIndex() != index) m_stack->setCurrentIndex(index);
    if (m_sidebar && m_sidebar->currentRow() != index) m_sidebar->setCurrentRow(index);
    m_pageTitle->setText(U(kPageTitles[index]));
    m_pageSubtitle->setText(U(kPageSubtitles[index]));
}

void MainWindow::selectPage(int index)
{
    if (m_sidebar) m_sidebar->setCurrentRow(index);
}

QString MainWindow::findRoot() const
{
    QDir dir(QDir::currentPath());
    for (int i = 0; i < 8; ++i) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral(".git")))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) break;
    }

    QDir appDir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        if (QFileInfo::exists(appDir.filePath(QStringLiteral(".git")))) {
            return appDir.absolutePath();
        }
        if (!appDir.cdUp()) break;
    }
    return QDir::currentPath();
}

QString MainWindow::runGitSync(const QString &repo, const QStringList &args, bool *ok) const
{
    if (ok) *ok = false;
    QProcess process;
    process.setWorkingDirectory(m_root);
    QStringList full;
    full << QStringLiteral("-C") << repo;
    full << args;
    process.start(QStringLiteral("git"), full);
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(500);
        return QString();
    }
    const bool good = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (ok) *ok = good;
    if (!good) return QString();
    return QString::fromUtf8(process.readAllStandardOutput());
}

namespace {

struct TagPick
{
    bool has = false;
    QString name;
    QString date;
    QString commitDate;
    QString subject;
    bool hasPrev = false;
    QString previous;
};

bool parseTagParts(const QString &tag, const QString &prefix,
                   int *major, int *minor, int *patch, int *nn)
{
    const QRegularExpression re(QStringLiteral("^%1-(\\d+)\\.(\\d+)\\.(\\d+)(?:_Rel_\\d{8}(\\d{2}))?$")
                                    .arg(QRegularExpression::escape(prefix)));
    const QRegularExpressionMatch match = re.match(tag);
    if (!match.hasMatch()) return false;
    *major = match.captured(1).toInt();
    *minor = match.captured(2).toInt();
    *patch = match.captured(3).toInt();
    *nn = match.captured(4).isEmpty() ? 0 : match.captured(4).toInt();
    return true;
}

bool tagGreater(int a1, int b1, int c1, int n1, int a2, int b2, int c2, int n2)
{
    if (a1 != a2) return a1 > a2;
    if (b1 != b2) return b1 > b2;
    if (c1 != c2) return c1 > c2;
    return n1 > n2;
}

TagPick pickLatestTag(const QString &raw, const QString &prefix)
{
    TagPick best;
    TagPick second;
    bool bestSet = false;
    bool secondSet = false;
    int bestM = 0, bestm = 0, bestp = 0, bestn = 0;
    int secondM = 0, secondm = 0, secondp = 0, secondn = 0;

    const QStringList lines = raw.split(QLatin1Char('\n'), QString::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList parts = line.split(QLatin1Char('\t'));
        if (parts.size() < 4) continue;

        int major = 0, minor = 0, patch = 0, nn = 0;
        if (!parseTagParts(parts.at(0), prefix, &major, &minor, &patch, &nn)) continue;

        TagPick candidate;
        candidate.has = true;
        candidate.name = parts.at(0);
        candidate.date = parts.at(1);
        candidate.commitDate = parts.at(2);
        candidate.subject = parts.mid(3).join(QLatin1Char('\t'));

        if (!bestSet || tagGreater(major, minor, patch, nn, bestM, bestm, bestp, bestn)) {
            if (bestSet) {
                second = best;
                secondM = bestM;
                secondm = bestm;
                secondp = bestp;
                secondn = bestn;
                secondSet = true;
            }
            best = candidate;
            bestSet = true;
            bestM = major;
            bestm = minor;
            bestp = patch;
            bestn = nn;
        } else if (!secondSet || tagGreater(major, minor, patch, nn, secondM, secondm, secondp, secondn)) {
            second = candidate;
            secondSet = true;
            secondM = major;
            secondm = minor;
            secondp = patch;
            secondn = nn;
        }
    }

    if (bestSet && secondSet) {
        best.hasPrev = true;
        best.previous = second.name;
    }
    return best;
}

} // namespace

namespace {

void setValue(QLabel *label, const QString &text, const QString &color,
              const QString &tip = QString(), bool mono = false)
{
    if (!label) return;
    const QString style = mono
                              ? QStringLiteral("color:%1;font-family:Consolas;font-weight:600;").arg(color)
                              : QStringLiteral("color:%1;").arg(color);
    const QString html = QStringLiteral("<span style=\"%1\">%2</span>").arg(style, text.toHtmlEscaped());
    // Most fields do not change between two 10 s polls. Touching QLabel with rich text
    // re-parses the HTML and relayouts the card, so only write when the value really changed.
    if (label->text() != html) label->setText(html);
    if (label->toolTip() != tip) label->setToolTip(tip);
}

QString tagVersionOf(const QString &tag, const QString &prefix)
{
    if (tag.isEmpty() || prefix.isEmpty()) return QString();
    const QRegularExpression re(QStringLiteral("^%1-(\\d+\\.\\d+\\.\\d+)(?:_Rel_|$)")
                                    .arg(QRegularExpression::escape(prefix)));
    const QRegularExpressionMatch match = re.match(tag);
    return match.hasMatch() ? match.captured(1) : QString();
}

QString shortDate(const QString &date)
{
    // "2026-11-03" -> "11-03"
    return date.size() >= 5 ? date.right(5) : date;
}

} // namespace

void MainWindow::refreshStatus(bool full, bool manual)
{
    m_manualRefresh = manual;
    m_refreshFull = full;
    ++m_refreshGeneration;
    const int generation = m_refreshGeneration;

    if (m_autoTimer) m_autoTimer->stop();
    m_refreshClock.start();

    // Only the full refresh is slow enough to need a visible busy state; the 10 s status
    // poll is quick and must not make the button flicker.
    if (full) setBusy(true);

    QVector<RefreshJob> jobs;

    // 先排探测/拉取任务：并发时它们先启动，phase 0 结束前不读 tag。
    auto addFetchJob = [&jobs, this](const RepoState &state) {
        if (!QFileInfo::exists(
                QDir(m_root).filePath(state.path + QStringLiteral("/.git")))) {
            return;  // 未初始化：没有 .git，探测必然失败
        }
        jobs.append(RefreshJob{state.key, QStringLiteral("fetch"), state.path,
                               {QStringLiteral("/c"),
                                QStringLiteral("release_wizard\\fetch_if_stale.bat"),
                                state.path},
                               0, 25000, QStringLiteral("cmd")});
    };
    if (full && manual) {
        addFetchJob(m_mainRepo);
        for (const RepoState &state : m_repos) addFetchJob(state);
    }

    auto addRepoJobs = [&jobs, full](const RepoState &state) {
        // status / lastcommit 不依赖远端，和探测并行跑（phase 0）。
        jobs.append(RefreshJob{state.key, QStringLiteral("status"), state.path,
                               {QStringLiteral("status"), QStringLiteral("--porcelain=v2"),
                                QStringLiteral("--branch"), QStringLiteral("--untracked-files=normal"),
                                QStringLiteral("--ignore-submodules=all")},
                               0});
        if (!full) return;

        // describe / tags 依赖 fetch 之后的 refs（phase 1）。
        jobs.append(RefreshJob{state.key, QStringLiteral("lastcommit"), state.path,
                               {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("develop"),
                                QStringLiteral("--format=%cd%x09%s"),
                                QStringLiteral("--date=format:%m-%d %H:%M")},
                               0});
        if (!state.tagPrefix.isEmpty()) {
            jobs.append(RefreshJob{state.key, QStringLiteral("tags"), state.path,
                                   {QStringLiteral("for-each-ref"),
                                    QStringLiteral("--format=%(refname:short)%09%(creatordate:short)%09%(*committerdate:short)%09%(contents:subject)"),
                                    QStringLiteral("refs/tags")}});
        }
    };

    addRepoJobs(m_mainRepo);
    for (const RepoState &state : m_repos) addRepoJobs(state);

    if (full) {
        for (const PlatformState &state : m_platform) {
            jobs.append(RefreshJob{state.key, QStringLiteral("version"), state.path,
                                   {QStringLiteral("describe"), QStringLiteral("--tags"),
                                    QStringLiteral("--abbrev=0")}});
        }
    }

    // Queued to the worker thread; the UI thread returns immediately.
    emit refreshRequested(generation, jobs, m_root, 8);
}

void MainWindow::onRefreshFinished(int generation,
                                   const RefreshResultMap &results)
{
    if (generation != m_refreshGeneration) return;
    applyRefresh(results);
}

void MainWindow::applyRefresh(const RefreshResultMap &resultsIn)
{
    RefreshResultMap results = resultsIn;
    // fetch_if_stale.bat 一次 ls-remote 就拿到了全部 refs：直接用它生成远端 tag
    // 结果，省掉每个仓额外一次 ls-remote 往返。
    auto synthRemoteTags = [&results](const RepoState &state) {
        const QString src = state.key + QStringLiteral("/fetch");
        if (!results.contains(src)) return;
        const RefreshResult fetch = results.value(src);
        if (!fetch.ok) return;
        QStringList keep;
        for (const QString &line : fetch.out.split(QLatin1Char('\n'))) {
            if (line.contains(QStringLiteral("refs/tags/"))) keep << line.trimmed();
        }
        RefreshResult remote;
        remote.ok = true;
        remote.out = keep.join(QLatin1Char('\n'));
        results.insert(state.key + QStringLiteral("/remotetags"), remote);
    };
    synthRemoteTags(m_mainRepo);
    for (const RepoState &state : m_repos) synthRemoteTags(state);

    auto idOf = [](const RepoState &state, const QString &kind) {
        return state.key + QLatin1Char('/') + kind;
    };
    auto resultOf = [&results, &idOf](const RepoState &state, const QString &kind) {
        return results.value(idOf(state, kind));
    };

    auto applyRepo = [this, &resultOf](RepoState &state) {
        state.initialized = QFileInfo::exists(
            QDir(m_root).filePath(state.path + QStringLiteral("/.git")));
        // Current position is refreshed on every poll.
        state.statusOk = resultOf(state, QStringLiteral("status")).ok;
        state.branch.clear();
        state.hasUpstream = false;
        state.ahead = 0;
        state.behind = 0;
        state.dirty = 0;
        state.untracked = 0;
        state.conflict = false;

        if (state.statusOk) {
            const QStringList lines = resultOf(state, QStringLiteral("status"))
                                          .out.split(QLatin1Char('\n'));
            for (const QString &line : lines) {
                if (line.startsWith(QStringLiteral("# branch.head "))) {
                    state.branch = line.mid(14).trimmed();
                } else if (line.startsWith(QStringLiteral("# branch.upstream "))) {
                    state.hasUpstream = true;
                } else if (line.startsWith(QStringLiteral("# branch.ab "))) {
                    const QStringList parts = line.mid(12).split(QLatin1Char(' '), QString::SkipEmptyParts);
                    for (const QString &part : parts) {
                        if (part.startsWith(QLatin1Char('+'))) state.ahead = part.mid(1).toInt();
                        else if (part.startsWith(QLatin1Char('-'))) state.behind = part.mid(1).toInt();
                    }
                } else if (line.startsWith(QStringLiteral("? "))) {
                    ++state.untracked;
                } else if (line.startsWith(QStringLiteral("u "))) {
                    state.conflict = true;
                    ++state.dirty;
                } else if (line.startsWith(QStringLiteral("1 ")) || line.startsWith(QStringLiteral("2 "))) {
                    ++state.dirty;
                }
            }
        }

        // Everything below changes slowly: only read it on a full refresh.
        if (!m_refreshFull) return;

        state.describeOk = resultOf(state, QStringLiteral("describe")).ok;
        state.developOk = resultOf(state, QStringLiteral("lastcommit")).ok;
        state.tagsOk = state.tagPrefix.isEmpty()
                           ? false
                           : resultOf(state, QStringLiteral("tags")).ok;

        state.describe.clear();
        if (state.describeOk) {
            const QString text = resultOf(state, QStringLiteral("describe")).out.trimmed();
            if (!QRegularExpression(QStringLiteral("^[0-9a-f]{7,40}$")).match(text).hasMatch()) {
                state.describe = text;
            }
        }

        state.developMissing = false;
        state.developDate.clear();
        state.developSubject.clear();
        if (state.developOk) {
            const QString text = resultOf(state, QStringLiteral("lastcommit")).out.trimmed();
            const int tab = text.indexOf(QLatin1Char('\t'));
            if (tab >= 0) {
                state.developDate = text.left(tab);
                state.developSubject = text.mid(tab + 1);
            } else {
                state.developDate = text;
            }
        } else {
            const QString err = resultOf(state, QStringLiteral("lastcommit")).err;
            state.developMissing = err.contains(QStringLiteral("unknown revision"))
                                   || err.contains(QStringLiteral("ambiguous argument"))
                                   || err.contains(QStringLiteral("Needed a single revision"));
        }

        state.hasTag = false;
        state.tagVersion.clear();
        state.latestTag.clear();
        state.latestTagDate.clear();
        state.latestTagCommitDate.clear();
        state.latestTagSubject.clear();
        state.previousTag.clear();
        state.tagNames.clear();
        state.remoteTagNames.clear();
        state.remoteTagsOk = false;
        if (state.tagsOk) {
            const QString &raw = resultOf(state, QStringLiteral("tags")).out;
            const QStringList lines = raw.split(QLatin1Char('\n'), QString::SkipEmptyParts);
            for (const QString &line : lines) {
                const QString name = line.section(QLatin1Char('\t'), 0, 0).trimmed();
                if (!name.isEmpty()) state.tagNames.append(name);
            }
            state.tagNames.removeDuplicates();

            const TagPick pick = pickLatestTag(raw, state.tagPrefix);
            state.hasTag = pick.has;
            state.latestTag = pick.name;
            state.latestTagDate = shortDate(pick.date);
            state.latestTagCommitDate = shortDate(pick.commitDate);
            state.latestTagSubject = pick.subject;
            state.previousTag = pick.hasPrev ? pick.previous : QString();
            state.tagVersion = tagVersionOf(pick.name, state.tagPrefix);
        }

        const RefreshResult remoteResult = resultOf(state, QStringLiteral("remotetags"));
        state.remoteTagsOk = remoteResult.ok;
        if (remoteResult.ok) {
            const QStringList lines = remoteResult.out.split(QLatin1Char('\n'),
                                                             QString::SkipEmptyParts);
            for (const QString &line : lines) {
                const QStringList parts = line.split(QLatin1Char('\t'));
                if (parts.size() < 2) continue;
                const QString &ref = parts.at(1);
                if (!ref.startsWith(QStringLiteral("refs/tags/"))) continue;
                QString name = ref.mid(10);
                if (name.endsWith(QStringLiteral("^{}"))) name.chop(3);
                if (!name.isEmpty()) state.remoteTagNames.append(name);
            }
            state.remoteTagNames.removeDuplicates();
        }
    };

    applyRepo(m_mainRepo);
    for (RepoState &state : m_repos) applyRepo(state);

    if (m_refreshFull && m_manualRefresh) {
        QStringList fetchFailed;
        auto applyFetch = [&](RepoState &state) {
            if (!state.initialized) { state.fetchOk = false; return; }
            const RefreshResult result = resultOf(state, QStringLiteral("fetch"));
            state.fetchOk = result.ok;
            if (!result.ok) {
                QString why = result.err.trimmed().section(QLatin1Char('\n'), 0, 0);
                if (why.isEmpty()) {
                    why = result.out.trimmed().section(QLatin1Char('\n'), 0, 0);
                }
                if (why.isEmpty()) why = U("超时或没有输出");
                fetchFailed << state.name + QStringLiteral("(") + why.left(120) + QStringLiteral(")");
            }
        };
        applyFetch(m_mainRepo);
        for (RepoState &state : m_repos) applyFetch(state);
        m_remoteChecked = true;
        // tag 可能刚被 fetch 下来：让“上一版绑定”缓存失效，重新反查。
        m_prevCacheTag.clear();
        if (!fetchFailed.isEmpty()) {
            appendLog(U("[UI] fetch 失败（继续用本地数据）：")
                      + fetchFailed.join(QStringLiteral("、")));
        }
    }
    updateRemoteBanner();

    if (m_refreshFull) {
        for (PlatformState &state : m_platform) {
            const QString id = state.key + QStringLiteral("/version");
            const RefreshResult result = results.value(id);
            state.ok = result.ok;
            state.version = result.ok ? result.out.trimmed() : QString();
        }
    }

    // 全量刷新后，主仓发布页的绑定选择跟到各仓最新的 tag；要绑旧版本就手动选。
    if (m_refreshFull) syncMainDraftToLatest();

    updateOverview();
    updateAllSubReleases();
    updateMainRelease();

    if (m_refreshFull && !m_firstFullDone) {
        m_firstFullDone = true;
        emit firstRefreshDone();
    }

    const qint64 cost = m_refreshClock.elapsed();
    m_lastRefreshMs = cost;
    const QString now = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    if (m_statusHint) m_statusHint->setText(now);
    if (m_refreshFull && m_fullHint) m_fullHint->setText(now);

    setBusy(false);

    if (m_refreshFull) {
        appendLog(U("[UI] 全量刷新%1（%2 ms）")
                      .arg(m_manualRefresh ? U(" · 手动") : U(" · 启动"))
                      .arg(cost));
        m_manualRefresh = false;
    }
    scheduleAutoRefresh();
}

void MainWindow::setBusy(bool busy)
{
    if (!m_refreshBtn) return;
    m_refreshBtn->setEnabled(!busy);
    m_refreshBtn->setText(busy ? U("刷新中…") : U("刷新"));
    m_refreshBtn->repaint();
}

void MainWindow::scheduleAutoRefresh()
{
    if (!m_autoTimer || !m_autoRefresh || !m_autoRefresh->isChecked()) return;
    if (isMinimized()) return;

    // The periodic poll only reads the current position, so a flat 10 s is enough.
    m_autoTimer->start(10000);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange) return;

    if (isMinimized()) {
        if (m_autoTimer) m_autoTimer->stop();
    } else if (m_refreshBtn && m_refreshBtn->isEnabled()) {
        // 恢复窗口只做一次轻量状态刷新，不触发全量刷新（会取消进行中的刷新）。
        refreshStatus(false);
    }
}

void MainWindow::worktreeText(const RepoState &state, QString *text, QString *color,
                              QString *tip) const
{
    if (!state.statusOk) {
        *text = U("读取失败");
        *color = QStringLiteral("#DC2626");
        *tip = U("git status 读取失败，无法判断工作区状态。");
        return;
    }
    if (state.conflict) {
        *text = U("冲突");
        *color = QStringLiteral("#DC2626");
        *tip = U("当前工作区有未解决的冲突。");
        return;
    }
    if (state.dirty > 0) {
        *color = QStringLiteral("#D97706");
        *tip = U("当前工作区有 %1 个跟踪文件改动（含新增 / 删除 / 重命名）。").arg(state.dirty);
        if (state.untracked > 0) {
            *text = U("%1 项改动 + 未跟踪 %2").arg(state.dirty).arg(state.untracked);
            *tip += U("另有 %1 个未跟踪文件。").arg(state.untracked);
        } else {
            *text = U("%1 项改动").arg(state.dirty);
        }
        return;
    }
    if (state.untracked > 0) {
        *text = U("未跟踪 %1").arg(state.untracked);
        *color = QStringLiteral("#D97706");
        *tip = U("当前工作区有 %1 个未跟踪文件，尚未加入 git。").arg(state.untracked);
        return;
    }
    *text = U("干净");
    *color = QStringLiteral("#16A34A");
    *tip = U("当前工作区干净：没有跟踪文件改动，也没有未跟踪文件。");
}

void MainWindow::renderRepo(const RepoState &state, RepoWidgets &w)
{
    // 当前 git 所在位置：分支
    QString branchText = U("—");
    QString branchColor = QStringLiteral("#1F2937");
    QString branchTip;
    if (!state.statusOk) {
        branchText = U("读取失败");
        branchColor = QStringLiteral("#DC2626");
        branchTip = U("git status 读取失败，无法判断当前所在位置。");
    } else if (state.branch == QStringLiteral("(detached)")) {
        branchText = U("detached");
        branchColor = QStringLiteral("#DC2626");
        branchTip = U("当前 git 不在任何分支上（detached）。在这种状态下提交的代码很容易丢失，先切回 develop。");
        if (!state.describe.isEmpty()) branchTip += U(" HEAD 停在 ") + state.describe;
    } else if (state.branch.isEmpty()) {
        branchText = U("—");
        branchColor = QStringLiteral("#6B7280");
        branchTip = U("读不到当前分支。");
    } else if (state.branch == QStringLiteral("develop")) {
        branchText = state.branch;
        branchColor = QStringLiteral("#16A34A");
        branchTip = U("当前 git 在 develop 分支上。");
    } else {
        branchText = state.branch;
        branchColor = QStringLiteral("#D97706");
        branchTip = U("当前 git 在 %1 分支上，不是 develop / release。").arg(state.branch);
    }
    setValue(w.branch, branchText, branchColor, branchTip);
    if (w.branchKey) w.branchKey->setToolTip(branchTip);

    // 当前 git 所在位置：工作区
    QString worktree;
    QString worktreeColor;
    QString worktreeTip;
    worktreeText(state, &worktree, &worktreeColor, &worktreeTip);
    setValue(w.worktree, worktree, worktreeColor, worktreeTip);
    if (w.worktreeKey) w.worktreeKey->setToolTip(worktreeTip);

    // 当前 git 所在位置：远端（origin）
    QString remoteText = U("—");
    QString remoteColor = QStringLiteral("#6B7280");
    QString remoteTip;
    if (!state.statusOk) {
        remoteText = U("读取失败");
        remoteColor = QStringLiteral("#DC2626");
        remoteTip = U("git status 读取失败，无法和远端比较。");
    } else if (state.branch == QStringLiteral("(detached)")) {
        remoteText = U("无远端");
        remoteTip = U("detached 状态没有远端跟踪分支，无法比较领先 / 落后。");
    } else if (!state.hasUpstream) {
        remoteText = U("无远端");
        remoteTip = U("当前分支没有设置远端跟踪分支，无法比较领先 / 落后。");
    } else {
        const QString upstream = U("origin/%1").arg(state.branch);
        if (state.ahead == 0 && state.behind == 0) {
            remoteText = U("同步");
            remoteColor = QStringLiteral("#16A34A");
            remoteTip = U("当前 git 与 %1 一致。").arg(upstream);
        } else {
            QStringList parts;
            if (state.ahead > 0) parts << U("领先 %1").arg(state.ahead);
            if (state.behind > 0) parts << U("落后 %1").arg(state.behind);
            remoteText = parts.join(U(" / "));
            remoteColor = QStringLiteral("#D97706");

            if (state.ahead > 0 && state.behind == 0) {
                remoteTip = U("当前 git 比 %1 领先 %2 个提交，尚未推送。")
                                .arg(upstream)
                                .arg(state.ahead);
            } else if (state.behind > 0 && state.ahead == 0) {
                remoteTip = U("当前 git 比 %1 落后 %2 个提交，远端有新提交。")
                                .arg(upstream)
                                .arg(state.behind);
            } else {
                remoteTip = U("当前 git 比 %1 领先 %2 个提交、落后 %3 个提交。")
                                .arg(upstream)
                                .arg(state.ahead)
                                .arg(state.behind);
            }
        }
    }
    setValue(w.remote, remoteText, remoteColor, remoteTip);
    if (w.remoteKey) w.remoteKey->setToolTip(remoteTip);

    // 最后一次 develop 提交
    if (!state.developOk) {
        if (state.developMissing) {
            setValue(w.developDate, U("无 develop"), QStringLiteral("#6B7280"),
                     U("本地没有 develop 分支"));
        } else {
            setValue(w.developDate, U("读取失败"), QStringLiteral("#DC2626"));
        }
        if (w.developView) w.developView->setEnabled(false);
    } else {
        setValue(w.developDate, state.developDate, QStringLiteral("#1F2937"),
                 state.developSubject);
        if (w.developView) w.developView->setEnabled(true);
    }

    // 最新 release tag
    if (!state.tagsOk) {
        setValue(w.tagVersion, U("读取失败"), QStringLiteral("#DC2626"));
        setValue(w.tagName, U("读取失败"), QStringLiteral("#DC2626"));
        setValue(w.tagDate, U("—"), QStringLiteral("#6B7280"));
        if (w.tagView) w.tagView->setEnabled(false);
    } else if (!state.hasTag) {
        setValue(w.tagVersion, U("—"), QStringLiteral("#6B7280"));
        setValue(w.tagName, U("未发布"), QStringLiteral("#6B7280"));
        setValue(w.tagDate, U("—"), QStringLiteral("#6B7280"));
        if (w.tagView) w.tagView->setEnabled(false);
    } else {
        setValue(w.tagVersion, state.tagVersion.isEmpty() ? U("—") : state.tagVersion,
                 QStringLiteral("#1F2937"));
        setValue(w.tagName, state.latestTag, QStringLiteral("#1D4ED8"),
                 state.latestTagSubject, true);
        setValue(w.tagDate, state.latestTagCommitDate.isEmpty() ? U("—") : state.latestTagCommitDate,
                 QStringLiteral("#1F2937"), state.latestTagSubject);
        if (w.tagView) w.tagView->setEnabled(true);
    }
}

void MainWindow::updateOverview()
{
    renderRepo(m_mainRepo, m_mainWidgets);

    for (int i = 0; i < m_repos.size() && i < m_repoWidgets.size(); ++i) {
        renderRepo(m_repos.at(i), m_repoWidgets[i]);
    }

    for (int i = 0; i < m_platform.size() && i < m_platVersion.size(); ++i) {
        const PlatformState &state = m_platform.at(i);
        if (!state.ok) {
            setValue(m_platVersion[i], U("读取失败"), QStringLiteral("#DC2626"));
        } else if (state.version.isEmpty()) {
            setValue(m_platVersion[i], U("—"), QStringLiteral("#6B7280"));
        } else {
            setValue(m_platVersion[i], state.version, QStringLiteral("#1F2937"));
        }
    }
}

void MainWindow::showCommitDetail(const QString &repo, const QString &path)
{
    QDialog dialog(this);
    dialog.setWindowTitle(U("改动描述 · ") + repo);
    dialog.resize(620, 440);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(8);

    layout->addWidget(labelTitle(repo + U("  ·  develop 最新提交")));

    const QString message = runGitSync(
        path,
        {QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("develop"),
         QStringLiteral("--format=%H%n%an  %ad%n%n%B"),
         QStringLiteral("--date=format:%Y-%m-%d %H:%M")});
    const QString stat = runGitSync(
        path, {QStringLiteral("show"), QStringLiteral("--stat"), QStringLiteral("--format="),
               QStringLiteral("develop")});

    auto *text = new QPlainTextEdit;
    text->setObjectName(QStringLiteral("bom"));
    text->setReadOnly(true);

    QString all = message.trimmed();
    if (!stat.trimmed().isEmpty()) {
        all += QStringLiteral("\n\n---- changed files ----\n") + stat.trimmed();
    }
    text->setPlainText(all);
    layout->addWidget(text, 1);

    auto *close = makeSecondary(U("关闭"));
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    auto *actions = new QHBoxLayout;
    actions->addStretch();
    actions->addWidget(close);
    layout->addLayout(actions);

    dialog.exec();
}

void MainWindow::showTagDetail(const QString &repo, const QString &path, const QString &tag,
                               const QString &previousTag)
{
    QDialog dialog(this);
    dialog.setWindowTitle(U("最新 tag · ") + repo);
    dialog.resize(620, 440);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(8);

    layout->addWidget(labelTitle(repo + QStringLiteral("  ·  ") + tag));

    QString meta = U("对比基准：") + (previousTag.isEmpty() ? U("无（这是第一个 tag）") : previousTag);
    if (!previousTag.isEmpty()) {
        bool ok = false;
        const QString count = runGitSync(
            path,
            {QStringLiteral("rev-list"), QStringLiteral("--count"),
             previousTag + QStringLiteral("..") + tag},
            &ok).trimmed();
        if (ok && !count.isEmpty()) meta += U(" · 新增 %1 个提交").arg(count);
    }
    layout->addWidget(labelMuted(meta));

    QString body = runGitSync(
        path, {QStringLiteral("tag"), QStringLiteral("-l"), QStringLiteral("--format=%(contents)"), tag});
    if (body.trimmed().isEmpty()) {
        body = runGitSync(path, {QStringLiteral("log"), QStringLiteral("-1"),
                                 QStringLiteral("--format=%B"), tag});
    }

    QString commits;
    if (!previousTag.isEmpty()) {
        commits = runGitSync(path, {QStringLiteral("log"), QStringLiteral("--oneline"),
                                    QStringLiteral("--no-decorate"),
                                    previousTag + QStringLiteral("..") + tag});
    }

    auto *text = new QPlainTextEdit;
    text->setObjectName(QStringLiteral("bom"));
    text->setReadOnly(true);
    QString all = body.trimmed();
    if (!commits.trimmed().isEmpty()) {
        all += QStringLiteral("\n\n---- commits ----\n") + commits.trimmed();
    }
    text->setPlainText(all);
    layout->addWidget(text, 1);

    auto *close = makeSecondary(U("关闭"));
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    auto *actions = new QHBoxLayout;
    actions->addStretch();
    actions->addWidget(close);
    layout->addLayout(actions);

    dialog.exec();
}

QString MainWindow::logDirectory() const
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return base + QStringLiteral("/logs");
}

void MainWindow::appendLog(const QString &text)
{
    const QString line = QTime::currentTime().toString(QStringLiteral("HH:mm:ss")) + QStringLiteral("  ") + text;
    if (m_log) {
        m_log->appendPlainText(line);
        m_log->moveCursor(QTextCursor::End);
    }
    if (m_logFull) {
        m_logFull->appendPlainText(line);
        m_logFull->moveCursor(QTextCursor::End);
    }

    // 界面照旧逐条实时刷新；文件按天、5 秒批量落盘，减少频繁 IO。
    m_logFilePending += QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))
                        + QLatin1Char(' ') + line + QLatin1Char('\n');
    if (m_logFilePending.size() >= 4 * 1024 * 1024) {
        flushLogFile();
    } else if (m_logFileTimer && !m_logFileTimer->isActive()) {
        m_logFileTimer->start();
    }
}

void MainWindow::flushLogFile()
{
    if (m_logFileTimer) m_logFileTimer->stop();
    if (m_logFilePending.isEmpty()) return;

    QDir().mkpath(logDirectory());
    QFile file(logDirectory() + QStringLiteral("/ui-")
               + QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"))
               + QStringLiteral(".log"));
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        // 打开失败就留着下次再试，不丢日志
        if (m_logFileTimer) m_logFileTimer->start();
        return;
    }
    QTextStream out(&file);
    out.setCodec("UTF-8");
    out << m_logFilePending;
    out.flush();
    m_logFilePending.clear();
}
