#pragma once

#include "refreshworker.h"

#include <QElapsedTimer>
#include <QHash>
#include <QMainWindow>
#include <QMap>
#include <QString>
#include <QVector>

class QCheckBox;
class QComboBox;
class QEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSaveFile;
class QStackedWidget;
class QTableWidget;
class QThread;
class QTimer;
class QVBoxLayout;

// VERSION.txt 里的一个版本块（主仓发布用）。
struct VersionBlock
{
    QString version;
    QString body;
};

// 一个子仓本次相对上一版绑定的变化。
struct RepoUpdate
{
    QString repo;
    QString prevTag;
    QString tag;
    QString prevVersion;
    QString version;
    QVector<VersionBlock> blocks;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    void selectPage(int index);

protected:
    void changeEvent(QEvent *event) override;

signals:
    void refreshRequested(int generation, const QVector<RefreshJob> &jobs,
                          const QString &root, int maxConcurrent);
    void firstRefreshDone();

private slots:
    void onRefreshFinished(int generation, const RefreshResultMap &results);

private:
    struct RepoWidgets
    {
        QLabel *branchKey = nullptr;
        QLabel *worktreeKey = nullptr;
        QLabel *remoteKey = nullptr;
        QLabel *branch = nullptr;
        QLabel *worktree = nullptr;
        QLabel *remote = nullptr;
        QLabel *developDate = nullptr;
        QPushButton *developView = nullptr;
        QLabel *tagVersion = nullptr;
        QLabel *tagName = nullptr;
        QLabel *tagDate = nullptr;
        QPushButton *tagView = nullptr;
    };

    struct RepoState
    {
        QString key;
        QString name;
        QString path;
        QString tagPrefix;

        // Current position (HEAD)
        bool statusOk = false;
        QString branch;
        bool hasUpstream = false;
        int ahead = 0;
        int behind = 0;
        int dirty = 0;
        int untracked = 0;
        bool conflict = false;
        bool initialized = true;  // 目录里有 .git；全新 clone 的子仓是 false
        bool fetchOk = false;     // 上一次远端同步（手动刷新）是否成功

        bool describeOk = false;
        QString describe;

        // Last commit on develop
        bool developOk = false;
        bool developMissing = false;
        QString developDate;
        QString developSubject;

        // Latest release tag
        bool tagsOk = false;
        bool hasTag = false;
        QString tagVersion;
        QString latestTag;
        QString latestTagDate;
        QString latestTagCommitDate;
        QString latestTagSubject;
        QString previousTag;
        QStringList tagNames;
        QStringList remoteTagNames;
        bool remoteTagsOk = false;
    };

    struct PlatformState
    {
        QString key;
        QString name;
        QString path;
        bool ok = false;
        QString version;
    };

    QWidget *buildSidebar();
    QWidget *buildHeader();
    QWidget *buildLogPanel();

    QWidget *buildOverviewPage();
    QWidget *buildOneClickPage();
    QWidget *buildUpdatePage();
    QWidget *buildBuildPage();
    QWidget *buildSyncPage();
    QWidget *buildReleasePage();
    void viewReleaseDoc();
    void viewSubVersionFile();
    void generateReleaseMap();
    void viewGeneratedChangelog();
    void openReleaseMap();
    void showMarkdownDialog(const QString &title, const QString &path, const QString &missingText);
    void showPlainTextDialog(const QString &title, const QString &path, const QString &missingText);
    QWidget *buildLogsPage();

    QWidget *buildRepoCard(const QString &title, RepoWidgets &widgets, bool isMain, int index);
    QWidget *buildPlatformCard();

    void setPage(int index);

    // Overview refresh. Async: git runs in QProcess instances, never on the UI thread.
    // full = also read develop commit / latest tag / platform versions (startup + manual).
    void refreshStatus(bool full, bool manual = false);
    void applyRefresh(const RefreshResultMap &results);
    void scheduleAutoRefresh();
    void updateOverview();
    void renderRepo(const RepoState &state, RepoWidgets &widgets);
    void worktreeText(const RepoState &state, QString *text, QString *color, QString *tip) const;
    void setBusy(bool busy);

    void appendLog(const QString &text);
    void flushLogFile();
    QString logDirectory() const;
    void showCommitDetail(const QString &repo, const QString &path);
    void showTagDetail(const QString &repo, const QString &path, const QString &tag,
                       const QString &previousTag);
    void showPlatformStatus();
    void showSyncPreview();
    void showScriptOutput(const QString &title, const QString &heading,
                          const QString &rerunText, const QString &script,
                          const QStringList &args);
    void runScript(const QString &script, const QStringList &args, const QString &title,
                   QPushButton *button, const QString &busyText, const QString &idleText);
    void updateUpdateButtons();

    // 主仓发布（绑定选择 / 预发布内容 / 发布）
    void updateMainRelease();
    void syncMainDraftToLatest();
    bool mainIterateMode() const;
    void syncMainVersionField();
    void refreshPreviousBindingCache();
    QWidget *buildRemoteBanner();
    void updateRemoteBanner();
    QString repoStateProblem(const RepoState &state) const;
    void openMainVersionPicker(const QString &repo);
    void openMainSectionDialog();
    void updateMainSectionStatus();
    void publishMainRelease();
    QString buildMainSectionHeader(const QString &version, const QString &date) const;
    QVector<RepoUpdate> collectRepoUpdates(QString *error);
    QString tagVersionText(const QString &repoPath, const QString &tag, QString *error);
    QString buildMainSectionText(const QString &version, const QString &date, QString *error);
    bool readChangelogSectionBody(const QString &version, QString *body, bool *found) const;
    bool writeReleaseDoc(const QString &version, const QString &section,
                         bool replaceExisting, QString *error) const;
    bool buildMainChangelog(const QString &version, const QString &date, const QString &section,
                            bool replaceExisting, QString *out, QString *error) const;
    int repoIndexByName(const QString &name) const;

    QWidget *buildSubRepoForm(int index);
    void updateSubRelease(int index);
    void updateAllSubReleases();
    void resetSubForm(int index);
    void switchSubMode(int index, bool toNew);
    void publishSubRepo(int index);
    bool writeSubVersionFile(const QString &repoPath, const QString &version,
                             const QString &body, QString *error);
    bool commitSubVersion(const QString &repoPath, const QString &version, QString *error);

    QString findRoot() const;
    QString runGitSync(const QString &repo, const QStringList &args, bool *ok = nullptr) const;

    QString m_root;
    QListWidget *m_sidebar = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_pageTitle = nullptr;
    QLabel *m_pageSubtitle = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QPlainTextEdit *m_logFull = nullptr;
    QString m_logFilePending;      // 待落盘的日志（5 秒批量写一次）
    QTimer *m_logFileTimer = nullptr;

    // Overview widgets
    RepoWidgets m_mainWidgets;
    QVector<RepoWidgets> m_repoWidgets;
    QVector<QLabel *> m_platVersion;

    // Overview refresh controls
    QPushButton *m_refreshBtn = nullptr;
    QCheckBox *m_autoRefresh = nullptr;
    QLabel *m_statusHint = nullptr;
    QLabel *m_fullHint = nullptr;
    QTimer *m_autoTimer = nullptr;
    QElapsedTimer m_refreshClock;
    qint64 m_lastRefreshMs = 0;
    bool m_manualRefresh = false;
    bool m_refreshFull = true;
    bool m_firstFullDone = false;

    int m_refreshGeneration = 0;
    RefreshWorker *m_refreshWorker = nullptr;
    QThread *m_refreshThread = nullptr;

    QVector<RepoState> m_repos;
    RepoState m_mainRepo;
    QVector<PlatformState> m_platform;

    // Update
    QVector<QCheckBox *> m_updateChecks;
    QPushButton *m_updateExecute = nullptr;
    QRadioButton *m_platformLatest = nullptr;
    QRadioButton *m_platformSpecific = nullptr;
    QLineEdit *m_platformVersion = nullptr;
    QPushButton *m_platformExecute = nullptr;
    QPushButton *m_platformQuery = nullptr;
    QPushButton *m_thirdExecute = nullptr;
    bool m_scriptRunning = false;

    // Build
    QVector<QCheckBox *> m_buildTargets;
    QCheckBox *m_buildClean = nullptr;
    QCheckBox *m_buildNoSync = nullptr;
    QPushButton *m_restoreExecute = nullptr;
    QPushButton *m_buildExecute = nullptr;
    QPushButton *m_oneClickExecute = nullptr;

    // Sync
    QVector<QCheckBox *> m_syncChecks;
    QPushButton *m_syncExecute = nullptr;
    QPushButton *m_syncPreview = nullptr;
    QCheckBox *m_runSimMode = nullptr;
    QPushButton *m_runStart = nullptr;
    QPushButton *m_runStop = nullptr;

    // Release
    struct MainBindRow
    {
        QString repo;
        QLabel *prevVersion = nullptr;
        QLabel *latestVersion = nullptr;
        QPushButton *pick = nullptr;
        QLabel *status = nullptr;

        // Cached result of the git probes for the current binding.
        QString probeKey;
        QString statusText;
        QString statusTip;
        QString statusColor;
    };

    // 版本页：只查看和生成，不在这里改标签/版本文件
    QComboBox *m_releaseSubPick = nullptr;
    QPushButton *m_releaseGenerate = nullptr;

    QLineEdit *m_mainVersion = nullptr;
    QRadioButton *m_mainModeNew = nullptr;
    QRadioButton *m_mainModeIterate = nullptr;
    QString m_mainVersionAuto;          // 上次自动算出的版本（用于判断是否被手动改过）
    bool m_mainVersionTouched = false;  // 用户手动改过
    bool m_mainVersionUpdating = false; // 程序正在写输入框
    QString m_mainNewDraft;             // 切到迭代前的版本草稿
    QCheckBox *m_mainPush = nullptr;
    QVector<MainBindRow> m_mainRows;
    QMap<QString, QString> m_mainBindDraft;  // repo -> tag
    QHash<QString, QString> m_tagVersionCache;  // repoPath|tag -> 该 tag 的 VERSION.txt 全文
    QString m_mainSectionText;               // 预发布内容（编辑后保存）
    QString m_mainSectionVersion;            // 这份内容对应的版本
    bool m_mainSectionIterate = false;       // 这份内容是否是同版本迭代
    QLabel *m_mainSectionHint = nullptr;
    QPushButton *m_mainViewSection = nullptr;
    QPushButton *m_mainPublish = nullptr;
    QLabel *m_mainStateHint = nullptr;   // 版本卡片下面的一行状态
    QLabel *m_remoteBanner = nullptr;    // 顶部全局横幅（未初始化 / 远端未确认）
    bool m_mainBlocked = false;
    QString m_mainBlockReason;
    QString m_mainBlockDetail;
    bool m_remoteChecked = false;  // 启动后是否已经成功同步过一次远端
    // “上一版绑定”缓存：按最新主仓 tag 的 gitlink 反查，tag 不变就不重算
    QString m_prevCacheTag;
    QMap<QString, QString> m_prevTagByRepo;
    QMap<QString, QString> m_prevVersionByRepo;
    QStringList m_prevUnresolved;

    enum SubAction { SubActionNone, SubActionTag, SubActionPush };

    struct SubForm
    {
        QString repoName;
        QString repoPath;
        QLabel *branch = nullptr;
        QLabel *worktree = nullptr;
        QLabel *latestVersion = nullptr;
        QRadioButton *modeNew = nullptr;
        QRadioButton *modeIterate = nullptr;
        QLineEdit *version = nullptr;
        QPlainTextEdit *body = nullptr;
        QLabel *writePreview = nullptr;
        QLabel *tagPreview = nullptr;
        QLabel *hint = nullptr;
        QCheckBox *push = nullptr;
        QPushButton *publish = nullptr;

        QString baseVersion;
        QString baseBody;
        bool baseTagged = false;
        bool iteratePossible = false;
        bool iterateMode = false;
        bool updating = false;

        bool newDraftReady = false;
        QString newVersion;
        QString newBody;
        bool iterateDraftReady = false;
        QString iterateBody;

        SubAction action = SubActionNone;
        bool blocked = false;     // 发布前置条件未满足（分支/落后/远端/初始化）
        QString blockReason;      // 一句话原因，用于按钮 tooltip 和提示行
        QString pushTag;
    };

    QVector<SubForm> m_subForms;
};
