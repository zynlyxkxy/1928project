#include "mainwindow.h"
#include "ui_helpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace {

QString todayText()
{
    return QDate::currentDate().toString(QStringLiteral("yyyyMMdd"));
}

QString makeTag(const QString &prefix, const QString &version, const QString &nn)
{
    return prefix + QLatin1Char('-') + version + QStringLiteral("_Rel_") + todayText() + nn;
}

QString normalizeBody(const QString &body)
{
    QString text = body;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    while (text.startsWith(QLatin1Char('\n'))) text.remove(0, 1);
    while (text.endsWith(QLatin1Char('\n'))) text.chop(1);
    return text;
}

bool bodyHasBoundary(const QString &body)
{
    const QStringList lines = body.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.trimmed().startsWith(QStringLiteral(">>>>>"))) return true;
    }
    return false;
}

bool isValidVersion(const QString &version)
{
    static const QRegularExpression re(
        QStringLiteral("^(0|[1-9]\\d*)\\.(0|[1-9]\\d*)\\.(0|[1-9]\\d*)$"));
    return re.match(version).hasMatch();
}

bool versionGreater(const QString &a, const QString &b)
{
    const QStringList pa = a.split(QLatin1Char('.'));
    const QStringList pb = b.split(QLatin1Char('.'));
    if (pa.size() != 3 || pb.size() != 3) return false;
    for (int i = 0; i < 3; ++i) {
        const int va = pa.at(i).toInt();
        const int vb = pb.at(i).toInt();
        if (va != vb) return va > vb;
    }
    return false;
}

QString bumpPatch(const QString &version)
{
    const QStringList parts = version.split(QLatin1Char('.'));
    if (parts.size() != 3) return version;
    return parts.at(0) + QLatin1Char('.') + parts.at(1) + QLatin1Char('.')
           + QString::number(parts.at(2).toInt() + 1);
}

struct VersionFileInfo
{
    bool ok = false;
    QStringList versions;
    QString firstBody;
    int firstBoundaryLineEnd = 0;
    int secondBoundaryStart = -1;
};

VersionFileInfo parseVersionFile(const QString &text)
{
    VersionFileInfo info;
    static const QRegularExpression boundary(
        QStringLiteral("^\\s*>>>>>\\s+(\\d+\\.\\d+\\.\\d+)\\s*$"));
    QStringList bodyLines;
    bool inFirst = false;
    int pos = 0;
    const int n = text.size();
    while (pos <= n) {
        int eol = text.indexOf(QLatin1Char('\n'), pos);
        if (eol < 0) eol = n;
        QString line = text.mid(pos, eol - pos);
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        const QRegularExpressionMatch match = boundary.match(line);
        if (match.hasMatch()) {
            if (info.versions.isEmpty()) {
                info.firstBoundaryLineEnd = (eol < n) ? eol + 1 : n;
                inFirst = true;
            } else if (info.secondBoundaryStart < 0) {
                info.secondBoundaryStart = pos;
                inFirst = false;
            }
            info.versions.append(match.captured(1));
        } else if (inFirst) {
            bodyLines.append(line);
        }
        if (eol >= n) break;
        pos = eol + 1;
    }
    info.ok = !info.versions.isEmpty();
    if (info.secondBoundaryStart < 0) info.secondBoundaryStart = n;
    info.firstBody = normalizeBody(bodyLines.join(QLatin1Char('\n')));
    return info;
}

int nnOfTag(const QString &tag, const QString &prefix, const QString &version)
{
    // 月度结算标签就是这个版本本身，没有流水号。
    if (tag == prefix + QLatin1Char('-') + version) return 0;
    const QString head = prefix + QLatin1Char('-') + version + QStringLiteral("_Rel_");
    if (!tag.startsWith(head)) return -1;
    const QString tail = tag.mid(head.size());
    if (tail.size() < 2) return -1;
    bool ok = false;
    const int nn = tail.right(2).toInt(&ok);
    return ok ? nn : -1;
}

QStringList tagsForVersion(const QStringList &tags, const QString &prefix,
                           const QString &version)
{
    QStringList out;
    for (const QString &tag : tags) {
        if (nnOfTag(tag, prefix, version) >= 0) out.append(tag);
    }
    return out;
}

QStringList mergedTags(const QStringList &local, const QStringList &remote)
{
    QStringList all = local;
    for (const QString &tag : remote) {
        if (!all.contains(tag)) all.append(tag);
    }
    return all;
}

QString newestUnpushedTag(const QStringList &local, const QStringList &remote,
                          const QString &prefix)
{
    QString best;
    QString bestVersion;
    int bestNn = -1;
    for (const QString &tag : local) {
        if (remote.contains(tag)) continue;
        if (!tag.startsWith(prefix + QLatin1Char('-'))) continue;
        const QString version = tag.mid(prefix.size() + 1)
                                    .section(QStringLiteral("_Rel_"), 0, 0);
        const int nn = tag.contains(QStringLiteral("_Rel_")) ? tag.right(2).toInt() : 0;
        if (best.isEmpty() || versionGreater(version, bestVersion)
            || (version == bestVersion && nn > bestNn)) {
            best = tag;
            bestVersion = version;
            bestNn = nn;
        }
    }
    return best;
}

void setColored(QLabel *label, const QString &text, const QString &color)
{
    if (!label) return;
    label->setText(text);
    label->setStyleSheet(QStringLiteral("color:%1;").arg(color));
}

// ---------------------------------------------------------------
// 主仓发布：绑定 / 生成内容 / 发布
// ---------------------------------------------------------------

// Fixed order used by the UI and the generated CHANGELOG table.
const QStringList &mainReleaseRepos()
{
    static const QStringList repos = {QStringLiteral("gui"), QStringLiteral("mctrl"),
                                      QStringLiteral("re"), QStringLiteral("io"),
                                      QStringLiteral("motion")};
    return repos;
}

// tag 内容不可变，缓存 key 用 仓库路径 + tag。
QString tagCacheKey(const QString &repoPath, const QString &tag)
{
    return repoPath + QLatin1Char('|') + tag;
}

struct GitRun
{
    bool ok = false;
    QString out;
    QString err;
};

// Reference: MainWindow::runGitSync. One difference: stderr is kept for messages.
GitRun runGit(const QString &root, const QString &repo, const QStringList &args,
              int timeoutMs = 10000)
{
    GitRun result;
    QProcess process;
    process.setWorkingDirectory(root);
    QStringList full;
    if (!repo.isEmpty()) full << QStringLiteral("-C") << repo;
    full << args;
    process.start(QStringLiteral("git"), full);
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(500);
        result.err = U("git 超时");
        return result;
    }
    result.out = QString::fromUtf8(process.readAllStandardOutput());
    result.err = QString::fromUtf8(process.readAllStandardError()).trimmed();
    result.ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    return result;
}

QString versionOfTag(const QString &tag, const QString &prefix)
{
    // 月度结算的标签没有后缀（GUI-2.1.0），月中的临时标签带 _Rel_日期NN。
    static const QRegularExpression re(
        QStringLiteral("^([0-9]+\\.[0-9]+\\.[0-9]+)(?:_Rel_[0-9]{8}[0-9]{2})?$"));
    const QString head = prefix + QLatin1Char('-');
    if (!tag.startsWith(head)) return QString();
    const QRegularExpressionMatch match = re.match(tag.mid(head.size()));
    return match.hasMatch() ? match.captured(1) : QString();
}

// 该仓所有合法 tag，版本优先、同版本 NN 最大，最新的在最前。
QStringList tagsOfRepo(const QStringList &tags, const QString &prefix)
{
    QStringList out;
    for (const QString &tag : tags) {
        if (!versionOfTag(tag, prefix).isEmpty() && !out.contains(tag)) out.append(tag);
    }
    std::sort(out.begin(), out.end(), [prefix](const QString &a, const QString &b) {
        const QString va = versionOfTag(a, prefix);
        const QString vb = versionOfTag(b, prefix);
        if (va != vb) return versionGreater(va, vb);
        return nnOfTag(a, prefix, va) > nnOfTag(b, prefix, vb);
    });
    return out;
}

// 可选的 tag：从上一版绑定 tag 的版本（含）到最新；没有上一版就是全部。
QStringList selectableTags(const QStringList &tagsDesc, const QString &prefix,
                           const QString &prevTag)
{
    const QString prevVersion = versionOfTag(prevTag, prefix);
    if (prevVersion.isEmpty()) return tagsDesc;
    const int prevNn = nnOfTag(prevTag, prefix, prevVersion);

    QStringList out;
    for (const QString &tag : tagsDesc) {
        const QString version = versionOfTag(tag, prefix);
        if (version.isEmpty()) continue;
        if (versionGreater(prevVersion, version)) continue;  // 比上一版还老
        if (version == prevVersion && nnOfTag(tag, prefix, version) < prevNn) continue;  // 同版本流水号回退
        out.append(tag);
    }
    return out;
}

QVector<VersionBlock> parseVersionBlocks(const QString &text)
{
    QVector<VersionBlock> blocks;
    static const QRegularExpression boundary(
        QStringLiteral("^\\s*>>>>>\\s+(\\d+\\.\\d+\\.\\d+)\\s*$"));
    QString currentVersion;
    QStringList current;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        const QRegularExpressionMatch match = boundary.match(line);
        if (match.hasMatch()) {
            if (!currentVersion.isEmpty()) {
                VersionBlock block;
                block.version = currentVersion;
                block.body = normalizeBody(current.join(QLatin1Char('\n')));
                blocks.append(block);
            }
            currentVersion = match.captured(1);
            current.clear();
        } else if (!currentVersion.isEmpty()) {
            current.append(line);
        }
    }
    if (!currentVersion.isEmpty()) {
        VersionBlock block;
        block.version = currentVersion;
        block.body = normalizeBody(current.join(QLatin1Char('\n')));
        blocks.append(block);
    }
    return blocks;
}

// VERSION.txt 的一个版本块 -> CHANGELOG 里的段落（4 空格缩进）。
QString repoBlockText(const VersionBlock &block)
{
    QString text = U("    版本 ") + block.version + QLatin1Char('\n');
    text += U("    更新内容：") + QLatin1Char('\n');
    const QStringList bodyLines = block.body.split(QLatin1Char('\n'));
    for (const QString &line : bodyLines) {
        if (line.isEmpty()) text += QLatin1Char('\n');
        else text += U("    ") + line + QLatin1Char('\n');
    }
    return text;
}

struct PrevBinding
{
    bool has = false;
    QString mainTag;
    QString mainVersion;
    QString mainDate;
    QHash<QString, QString> versionByRepo;
    QHash<QString, QString> tagByRepo;
};

bool writeTextFile(const QString &path, const QString &text, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = U("写不了 ") + path;
        return false;
    }
    file.write(text.toUtf8());
    if (!file.commit()) {
        if (error) *error = U("写入失败 ") + path;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------
// 轻量 Markdown 预览：只认发布节的格式
//   ##/###/#### 标题、| 表格、4 空格代码块、**加粗**、1. 列表、空行、---
// ---------------------------------------------------------------

bool mdSeparatorRow(const QString &row)
{
    static const QRegularExpression re(QStringLiteral("^\\|[\\s\\-:|]+\\|$"));
    return re.match(row.trimmed()).hasMatch();
}

QStringList mdSplitRow(const QString &row)
{
    QString text = row.trimmed();
    if (text.startsWith(QLatin1Char('|'))) text.remove(0, 1);
    if (text.endsWith(QLatin1Char('|'))) text.chop(1);
    QStringList cells = text.split(QLatin1Char('|'));
    for (QString &cell : cells) cell = cell.trimmed();
    return cells;
}

QString mdInline(const QString &text)
{
    QString html = text.toHtmlEscaped();
    static const QRegularExpression bold(QStringLiteral("\\*\\*(.+?)\\*\\*"));
    html.replace(bold, QStringLiteral("<b>\\1</b>"));
    return html;
}

QString mdHeadingBar(const QString &text, const QString &background, const QString &accent,
                     const QString &color, int pointSize)
{
    return QStringLiteral("<table width=\"100%\" border=\"0\" cellspacing=\"0\" cellpadding=\"6\" bgcolor=\"")
           + background
           + QStringLiteral("\"><tr><td width=\"4\" bgcolor=\"")
           + accent
           + QStringLiteral("\"></td><td><span style=\"font-size:")
           + QString::number(pointSize)
           + QStringLiteral("pt; color:")
           + color
           + QStringLiteral("; font-weight:600;\">")
           + mdInline(text)
           + QStringLiteral("</span></td></tr></table><br/>");
}

QString mdTableHtml(const QStringList &rows)
{
    QString html =
        QStringLiteral("<table width=\"100%\" border=\"1\" cellspacing=\"0\" cellpadding=\"6\" "
                       "bordercolor=\"#E2E8F0\" style=\"border-color:#E2E8F0; "
                       "border-style:solid; border-width:1px;\">");
    bool header = true;
    for (const QString &row : rows) {
        if (mdSeparatorRow(row)) continue;
        html += QStringLiteral("<tr>");
        for (const QString &cell : mdSplitRow(row)) {
            const QString style = QStringLiteral(
                "border-color:#E2E8F0; border-style:solid; border-width:1px;");
            if (header) {
                html += QStringLiteral("<td align=\"center\" bgcolor=\"#F8FAFC\" style=\"")
                        + style + QStringLiteral("\"><b>") + mdInline(cell)
                        + QStringLiteral("</b></td>");
            } else {
                html += QStringLiteral("<td align=\"center\" style=\"") + style
                        + QStringLiteral("\">") + mdInline(cell) + QStringLiteral("</td>");
            }
        }
        html += QStringLiteral("</tr>");
        header = false;
    }
    html += QStringLiteral("</table><br/>");
    return html;
}

QString mdCodeHtml(const QString &text)
{
    return QStringLiteral("<table width=\"100%\" border=\"0\" cellspacing=\"0\" cellpadding=\"8\" "
                          "bgcolor=\"#F6F8FA\"><tr><td><pre style=\"font-family:Consolas,"
                          "'Courier New',monospace; font-size:9pt; color:#334155;\">")
           + text.toHtmlEscaped()
           + QStringLiteral("</pre></td></tr></table><br/>");
}

QString renderMainSectionHtml(const QString &md)
{
    QString html;
    const QStringList lines = normalizeBody(md).split(QLatin1Char('\n'));
    static const QRegularExpression listRe(QStringLiteral("^\\d+\\.\\s+(.*)$"));
    int i = 0;
    while (i < lines.size()) {
        const QString raw = lines.at(i);
        const QString line = raw.trimmed();
        if (line.isEmpty()) {
            ++i;
            continue;
        }
        if (line.startsWith(QStringLiteral("<a ")) || line.startsWith(QStringLiteral("</a"))) {
            ++i;
            continue;
        }
        if (line == QStringLiteral("---")) {
            html += QStringLiteral("<hr/>");
            ++i;
            continue;
        }
        if (line.startsWith(QStringLiteral("#### "))) {
            html += QStringLiteral("<p><b>") + mdInline(line.mid(5)) + QStringLiteral("</b></p>");
            ++i;
            continue;
        }
        if (line.startsWith(QStringLiteral("### "))) {
            html += mdHeadingBar(line.mid(4), QStringLiteral("#F1F5F9"),
                                 QStringLiteral("#CBD5E1"), QStringLiteral("#334155"), 11);
            ++i;
            continue;
        }
        if (line.startsWith(QStringLiteral("## "))) {
            html += mdHeadingBar(line.mid(3), QStringLiteral("#EFF6FF"),
                                 QStringLiteral("#2563EB"), QStringLiteral("#1D4ED8"), 13);
            ++i;
            continue;
        }
        if (line.startsWith(QLatin1Char('|'))) {
            QStringList rows;
            while (i < lines.size() && lines.at(i).trimmed().startsWith(QLatin1Char('|'))) {
                rows << lines.at(i).trimmed();
                ++i;
            }
            html += mdTableHtml(rows);
            continue;
        }
        if (raw.startsWith(QStringLiteral("    "))) {
            QStringList code;
            while (i < lines.size()) {
                const QString current = lines.at(i);
                if (current.trimmed().isEmpty()) {
                    code << QString();
                    ++i;
                    continue;
                }
                if (!current.startsWith(QStringLiteral("    "))) break;
                code << current.mid(4);
                ++i;
            }
            while (!code.isEmpty() && code.last().trimmed().isEmpty()) code.removeLast();
            html += mdCodeHtml(code.join(QLatin1Char('\n')));
            continue;
        }
        if (listRe.match(line).hasMatch()) {
            html += QStringLiteral("<ol>");
            while (i < lines.size()) {
                const QRegularExpressionMatch match = listRe.match(lines.at(i).trimmed());
                if (!match.hasMatch()) break;
                html += QStringLiteral("<li>") + mdInline(match.captured(1))
                        + QStringLiteral("</li>");
                ++i;
            }
            html += QStringLiteral("</ol>");
            continue;
        }

        QStringList paragraph;
        while (i < lines.size()) {
            const QString current = lines.at(i);
            const QString text = current.trimmed();
            if (text.isEmpty() || text.startsWith(QLatin1Char('#'))
                || text.startsWith(QLatin1Char('|')) || text == QStringLiteral("---")
                || current.startsWith(QStringLiteral("    "))
                || listRe.match(text).hasMatch()
                || text.startsWith(QStringLiteral("<a "))) {
                break;
            }
            paragraph << text;
            ++i;
        }
        if (paragraph.isEmpty()) {
            ++i;
            continue;
        }
        const QString text = paragraph.join(QLatin1Char(' '));
        if (text.startsWith(U("发布日期："))) {
            html += QStringLiteral("<p><span style=\"color:#6B7280;\">") + mdInline(text)
                    + QStringLiteral("</span></p>");
        } else {
            html += QStringLiteral("<p>") + mdInline(text) + QStringLiteral("</p>");
        }
    }
    return html;
}

QScrollArea *makeContentScroll(QWidget *widget)
{
    QScrollArea *scroll = makeScrollPage(widget);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return scroll;
}

} // namespace

QWidget *MainWindow::buildSyncPage()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(14);

    auto *cardContent = new QWidget;
    auto *cardLayout = new QVBoxLayout(cardContent);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(12);

    auto *sourceRow = new QWidget;
    auto *sourceLayout = new QHBoxLayout(sourceRow);
    sourceLayout->setContentsMargins(0, 0, 0, 0);
    sourceLayout->setSpacing(10);

    auto *sourceLabel = new QLabel(U("源仓库"));
    sourceLabel->setObjectName(QStringLiteral("muted"));
    sourceLabel->setFixedWidth(96);
    sourceLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    sourceLayout->addWidget(sourceLabel);

    m_syncChecks.clear();
    const QStringList sourceNames = {QStringLiteral("io"), QStringLiteral("motion"),
                                     QStringLiteral("re"), QStringLiteral("mctrl")};
    for (const QString &name : sourceNames) {
        auto *check = new QCheckBox(name);
        check->setChecked(true);
        m_syncChecks.append(check);
        sourceLayout->addWidget(check);
        connect(check, &QCheckBox::toggled, this, [this](bool) { updateUpdateButtons(); });
    }
    sourceLayout->addStretch();

    auto *selectAll = makeSecondary(U("全选"));
    auto *invert = makeSecondary(U("反选"));
    auto *selectNone = makeSecondary(U("全不选"));
    selectAll->setFixedWidth(76);
    invert->setFixedWidth(76);
    selectNone->setFixedWidth(76);
    sourceLayout->addWidget(selectAll);
    sourceLayout->addWidget(invert);
    sourceLayout->addWidget(selectNone);
    cardLayout->addWidget(sourceRow);

    connect(selectAll, &QPushButton::clicked, this, [this]() {
        for (QCheckBox *check : m_syncChecks) check->setChecked(true);
    });
    connect(invert, &QPushButton::clicked, this, [this]() {
        for (QCheckBox *check : m_syncChecks) check->setChecked(!check->isChecked());
    });
    connect(selectNone, &QPushButton::clicked, this, [this]() {
        for (QCheckBox *check : m_syncChecks) check->setChecked(false);
    });

    m_syncExecute = makeDisabledPrimary(U("执行"));
    m_syncExecute->setMinimumWidth(96);
    m_syncExecute->setEnabled(true);
    m_syncPreview = makeSecondary(U("预览"));
    m_syncPreview->setFixedWidth(96);
    auto *actions = new QHBoxLayout;
    actions->addWidget(m_syncExecute);
    actions->addWidget(m_syncPreview);
    actions->addStretch();
    cardLayout->addLayout(actions);

    connect(m_syncExecute, &QPushButton::clicked, this, [this]() {
        QStringList selected;
        for (QCheckBox *check : m_syncChecks) {
            if (check->isChecked()) selected << check->text();
        }
        if (selected.isEmpty()) return;

        QStringList args;
        if (selected.size() != m_syncChecks.size()) args = selected;
        runScript(QStringLiteral("engineering\\scripts\\sync.bat"), args,
                  U("跨工程同步"), m_syncExecute, U("执行中…"), U("执行"));
    });

    connect(m_syncPreview, &QPushButton::clicked, this, [this]() { showSyncPreview(); });

    layout->addWidget(makeCard(U("跨工程同步"),
                               U("把选中源仓库的共享文件，同步到需要它们的下游仓库中"),
                               cardContent));
    layout->addWidget(labelHint(U("提示：同步会把文件更新到下游仓库，会让下游工作区变脏")));
    layout->addStretch();
    updateUpdateButtons();
    return makeScrollPage(content);
}

void MainWindow::showSyncPreview()
{
    QStringList selected;
    for (QCheckBox *check : m_syncChecks) {
        if (check->isChecked()) selected << check->text();
    }
    if (selected.isEmpty()) return;

    QStringList args;
    if (selected.size() != m_syncChecks.size()) args = selected;
    args << QStringLiteral("--dry-run");

    showScriptOutput(U("同步预览"), U("同步预览"), U("重新预览"),
                     QStringLiteral("engineering\\scripts\\sync.bat"), args);
}
QWidget *MainWindow::buildReleasePage()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(14);

    auto *cardContent = new QWidget;
    auto *cardLayout = new QVBoxLayout(cardContent);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(12);

    // 1. 主仓人工维护的发布清单
    auto *rowDoc = new QHBoxLayout;
    rowDoc->setContentsMargins(0, 0, 0, 0);
    rowDoc->addWidget(labelHint(U("人工维护")));
    rowDoc->addSpacing(10);
    auto *viewDoc = makeSecondary(U("查看主仓 RELEASE.md"));
    viewDoc->setToolTip(U("在线阅读 engineering/version/RELEASE.md"));
    connect(viewDoc, &QPushButton::clicked, this, [this]() { viewReleaseDoc(); });
    rowDoc->addWidget(viewDoc);
    rowDoc->addStretch();
    cardLayout->addLayout(rowDoc);

    // 2. 子仓人工维护的版本目录
    auto *rowVersion = new QHBoxLayout;
    rowVersion->setContentsMargins(0, 0, 0, 0);
    rowVersion->addWidget(labelHint(U("子仓")));
    rowVersion->addSpacing(10);
    m_releaseSubPick = new QComboBox;
    m_releaseSubPick->setMinimumWidth(160);
    for (const QString &repo : mainReleaseRepos()) {
        const int index = repoIndexByName(repo);
        if (index < 0) {
            m_releaseSubPick->addItem(repo, repo);
            continue;
        }
        const RepoState &state = m_repos.at(index);
        m_releaseSubPick->addItem(state.name, state.path);
    }
    rowVersion->addWidget(m_releaseSubPick);
    auto *viewVersion = makeSecondary(U("查看 VERSION.txt"));
    viewVersion->setToolTip(U("在线阅读该子仓的 VERSION.txt"));
    connect(viewVersion, &QPushButton::clicked, this, [this]() { viewSubVersionFile(); });
    rowVersion->addWidget(viewVersion);
    rowVersion->addStretch();
    cardLayout->addLayout(rowVersion);

    // 3. 生成
    auto *rowGen = new QHBoxLayout;
    rowGen->setContentsMargins(0, 0, 0, 0);
    m_releaseGenerate = new QPushButton(U("生成版本图"));
    m_releaseGenerate->setObjectName(QStringLiteral("primary"));
    m_releaseGenerate->setMinimumWidth(120);
    m_releaseGenerate->setCursor(Qt::PointingHandCursor);
    connect(m_releaseGenerate, &QPushButton::clicked, this, [this]() { generateReleaseMap(); });
    rowGen->addWidget(m_releaseGenerate);
    rowGen->addSpacing(10);
    rowGen->addWidget(labelHint(U("读主仓标签和各子仓 VERSION.txt，生成 CHANGELOG.md 与 release-map.html")));
    rowGen->addStretch();
    cardLayout->addLayout(rowGen);

    // 4/5. 查看生成结果
    auto *rowView = new QHBoxLayout;
    rowView->setContentsMargins(0, 0, 0, 0);
    auto *viewChangelog = makeSecondary(U("查看 CHANGELOG.md"));
    connect(viewChangelog, &QPushButton::clicked, this, [this]() { viewGeneratedChangelog(); });
    auto *openHtml = makeSecondary(U("打开 release-map.html"));
    connect(openHtml, &QPushButton::clicked, this, [this]() { openReleaseMap(); });
    rowView->addWidget(viewChangelog);
    rowView->addSpacing(8);
    rowView->addWidget(openHtml);
    rowView->addStretch();
    cardLayout->addLayout(rowView);

    layout->addWidget(makeCard(U("版本记录"),
                               U("标签和两份人工维护的文件都不在这里改；这个页面只负责查看和生成。"),
                               cardContent));
    layout->addStretch(1);
    return content;
}

void MainWindow::showMarkdownDialog(const QString &title, const QString &path,
                                    const QString &missingText)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::information(this, title, missingText);
        return;
    }
    const QString text = QString::fromUtf8(file.readAll());
    file.close();

    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(780, 640);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);
    layout->addWidget(labelTitle(title));

    auto *browser = new QTextBrowser;
    browser->setOpenExternalLinks(true);
    browser->setHtml(renderMainSectionHtml(text));
    layout->addWidget(browser, 1);

    auto *actions = new QHBoxLayout;
    auto *close = makeSecondary(U("关闭"));
    actions->addStretch();
    actions->addWidget(close);
    layout->addLayout(actions);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    dialog.exec();
}

void MainWindow::showPlainTextDialog(const QString &title, const QString &path,
                                     const QString &missingText)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::information(this, title, missingText);
        return;
    }
    const QString text = QString::fromUtf8(file.readAll());
    file.close();

    QDialog dialog(this);
    dialog.setWindowTitle(title);
    dialog.resize(680, 560);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(10);
    layout->addWidget(labelTitle(title));

    auto *view = new QPlainTextEdit;
    view->setReadOnly(true);
    view->setObjectName(QStringLiteral("bom"));
    view->setPlainText(text);
    layout->addWidget(view, 1);

    auto *actions = new QHBoxLayout;
    auto *close = makeSecondary(U("关闭"));
    actions->addStretch();
    actions->addWidget(close);
    layout->addLayout(actions);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    dialog.exec();
}

void MainWindow::viewReleaseDoc()
{
    const QString path = QDir(m_root).filePath(QStringLiteral("engineering/version/RELEASE.md"));
    showMarkdownDialog(U("主仓 RELEASE.md"), path, U("还没有 RELEASE.md：") + path);
}

void MainWindow::viewSubVersionFile()
{
    if (!m_releaseSubPick) return;
    const QString repo = m_releaseSubPick->currentData().toString();
    if (repo.isEmpty()) return;
    const QString path = QDir(m_root).filePath(repo + QStringLiteral("/VERSION.txt"));
    showPlainTextDialog(m_releaseSubPick->currentText() + U(" VERSION.txt"), path,
                        U("这个子仓没有 VERSION.txt：") + path);
}

void MainWindow::generateReleaseMap()
{
    runScript(QStringLiteral("engineering\\scripts\\tag_version.bat"), QStringList(),
              U("生成版本图"), m_releaseGenerate, U("生成中…"), U("生成版本图"));
}

void MainWindow::viewGeneratedChangelog()
{
    const QString path = QDir(m_root).filePath(QStringLiteral("engineering/version/CHANGELOG.md"));
    showMarkdownDialog(U("CHANGELOG.md"), path, U("还没有生成，先点「生成版本图」。"));
}

void MainWindow::openReleaseMap()
{
    const QString path = QDir(m_root).filePath(QStringLiteral("engineering/version/release-map.html"));
    if (!QFileInfo::exists(path)) {
        QMessageBox::information(this, U("版本地图"), U("还没有生成，先点「生成版本图」。"));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

int MainWindow::repoIndexByName(const QString &name) const
{
    for (int i = 0; i < m_repos.size(); ++i) {
        if (m_repos.at(i).name == name) return i;
    }
    return -1;
}

void MainWindow::syncMainDraftToLatest()
{
    for (const QString &repo : mainReleaseRepos()) {
        const int index = repoIndexByName(repo);
        if (index < 0) continue;
        const RepoState &state = m_repos.at(index);
        const QStringList tags = tagsOfRepo(mergedTags(state.tagNames, state.remoteTagNames),
                                            state.tagPrefix);
        if (tags.isEmpty()) continue;
        m_mainBindDraft.insert(repo, tags.first());
    }
}

bool MainWindow::mainIterateMode() const
{
    return m_mainModeIterate && m_mainModeIterate->isChecked();
}

void MainWindow::syncMainVersionField()
{
    if (!m_mainVersion) return;
    // 还没读到主仓 tag（启动初期 / 局部刷新）就不要动输入框，避免先写个假的 1.0.0。
    if (!m_mainRepo.tagsOk && m_mainRepo.tagNames.isEmpty()) return;
    const QStringList tags = tagsOfRepo(m_mainRepo.tagNames, m_mainRepo.tagPrefix);
    const QString latest = tags.isEmpty() ? QString()
                                          : versionOfTag(tags.first(), m_mainRepo.tagPrefix);
    const QString autoNew = latest.isEmpty() ? QStringLiteral("1.0.0") : bumpPatch(latest);

    m_mainVersionUpdating = true;
    if (mainIterateMode()) {
        if (!latest.isEmpty()) m_mainVersion->setText(latest);
        m_mainVersion->setEnabled(false);
        m_mainVersionAuto = latest;
        m_mainVersionTouched = false;
    } else {
        m_mainVersion->setEnabled(true);
        const QString cur = m_mainVersion->text().trimmed();
        if (!m_mainVersionTouched || cur.isEmpty() || cur == m_mainVersionAuto) {
            m_mainVersion->setText(autoNew);
            m_mainVersionTouched = false;
        }
        m_mainVersionAuto = autoNew;
    }
    m_mainVersionUpdating = false;
}

PrevBinding makePrevBinding(const QString &tag, const QMap<QString, QString> &tags,
                            const QMap<QString, QString> &versions)
{
    PrevBinding info;
    if (tag.isEmpty()) return info;
    info.has = true;
    info.mainTag = tag;
    const int rel = tag.indexOf(QStringLiteral("_Rel_"));
    if (rel > 0) {
        info.mainVersion = tag.left(rel).section(QLatin1Char('-'), -1);
        const QString d = tag.mid(rel + 5);
        if (d.size() >= 8) {
            info.mainDate = d.left(4) + QLatin1Char('-') + d.mid(4, 2)
                            + QLatin1Char('-') + d.mid(6, 2);
        }
    }
    else {
        // 月度结算标签没有后缀：ICB3000M_WM-2.1.0
        info.mainVersion = tag.section(QLatin1Char('-'), -1);
    }
    for (auto it = tags.constBegin(); it != tags.constEnd(); ++it) {
        info.tagByRepo.insert(it.key(), it.value());
        info.versionByRepo.insert(it.key(), versions.value(it.key()));
    }
    return info;
}

void MainWindow::refreshPreviousBindingCache()
{
    const QStringList tags = tagsOfRepo(m_mainRepo.tagNames, m_mainRepo.tagPrefix);
    const QString latest = tags.isEmpty() ? QString() : tags.first();
    if (latest == m_prevCacheTag) return;

    m_prevCacheTag = latest;
    m_prevTagByRepo.clear();
    m_prevVersionByRepo.clear();
    m_prevUnresolved.clear();
    if (latest.isEmpty()) return;

    // 上一版绑定的权威来源是最新主仓 tag 的 gitlink。
    for (const QString &repo : mainReleaseRepos()) {
        const int index = repoIndexByName(repo);
        if (index < 0) { m_prevUnresolved << repo; continue; }
        const RepoState &state = m_repos.at(index);
        const GitRun tree = runGit(m_root, QString(),
                                   {QStringLiteral("ls-tree"), latest,
                                    QStringLiteral("--"), state.path});
        QString sha;
        static const QRegularExpression gitlink(
            QStringLiteral("^160000 commit ([0-9a-f]{40})"));
        for (const QString &line : tree.out.split(QLatin1Char('\n'))) {
            const auto match = gitlink.match(line);
            if (match.hasMatch()) { sha = match.captured(1); break; }
        }
        if (sha.isEmpty()) { m_prevUnresolved << repo; continue; }
        const GitRun desc = runGit(m_root, state.path,
                                   {QStringLiteral("describe"), QStringLiteral("--tags"),
                                    QStringLiteral("--exact-match"), sha});
        const QString tag = desc.ok ? desc.out.trimmed() : QString();
        if (tag.isEmpty()) { m_prevUnresolved << repo; continue; }
        m_prevTagByRepo.insert(repo, tag);
        m_prevVersionByRepo.insert(repo, versionOfTag(tag, state.tagPrefix));
    }
}

void MainWindow::updateMainRelease()
{
    syncMainVersionField();
    if (m_mainRows.isEmpty()) return;

    const QStringList repos = mainReleaseRepos();
    refreshPreviousBindingCache();
    const PrevBinding prev =
        makePrevBinding(m_prevCacheTag, m_prevTagByRepo, m_prevVersionByRepo);

    // ---- 发布前置状态（红字 + 发布置灰）----
    QStringList blockReasons;
    QStringList blockDetails;
    const QString mainProblem = repoStateProblem(m_mainRepo);
    if (!mainProblem.isEmpty()) {
        blockReasons << U("主仓：") + mainProblem;
        blockDetails << U("主仓：") + mainProblem;
    }
    if (!m_remoteChecked) {
        blockReasons << U("远端未同步（先点刷新）");
        blockDetails << U("远端未同步：先点右上角「刷新」确认远端状态");
    } else if (!m_mainRepo.fetchOk) {
        blockReasons << U("主仓：远端未确认");
        blockDetails << U("主仓：远端未确认（点刷新重试）");
    }
    if (!m_prevCacheTag.isEmpty() && !m_prevUnresolved.isEmpty()) {
        blockReasons << U("上一版绑定无法确定");
        blockDetails << U("上一版绑定无法确定：")
                            + m_prevUnresolved.join(QStringLiteral("、"))
                            + U("（先确认 tag 已拉取）");
    }
    for (const RepoState &state : m_repos) {
        if (!state.initialized) {
            blockReasons << state.name + U(" 未初始化");
            blockDetails << state.name + U("：子仓未初始化（先一键全自动）");
        } else if (m_remoteChecked && !state.fetchOk) {
            blockReasons << state.name + U(" 远端未确认");
            blockDetails << state.name + U("：远端未确认（点刷新重试）");
        }
    }
    m_mainBlocked = !blockReasons.isEmpty();
    m_mainBlockReason = blockReasons.value(0);
    m_mainBlockDetail = blockDetails.join(QStringLiteral("\n"));

    if (m_mainStateHint) {
        if (m_mainBlocked) {
            m_mainStateHint->setText(U("无法发布：") + m_mainBlockReason);
            m_mainStateHint->setStyleSheet(QStringLiteral("color:#DC2626;"));
            m_mainStateHint->setToolTip(m_mainBlockDetail);
            m_mainStateHint->setVisible(true);
        } else if (m_mainRepo.dirty > 0) {
            m_mainStateHint->setText(U("有未提交改动（不影响发布）"));
            m_mainStateHint->setStyleSheet(QStringLiteral("color:#B45309;"));
            m_mainStateHint->setToolTip(QString());
            m_mainStateHint->setVisible(true);
        } else {
            m_mainStateHint->setVisible(false);
        }
    }

    // 选择默认跟随每个仓的最新 tag：全量刷新时由 syncMainDraftToLatest() 统一更新。
    // 想绑旧版本就在“选择”里手动改，改完一直保留到下一次全量刷新。

    for (MainBindRow &row : m_mainRows) {
        const int index = repoIndexByName(row.repo);
        if (index < 0 || !row.prevVersion || !row.latestVersion || !row.pick || !row.status) continue;
        const RepoState &state = m_repos.at(index);
        const QStringList allTags = tagsOfRepo(mergedTags(state.tagNames, state.remoteTagNames),
                                               state.tagPrefix);

        // 上一版绑定（tag）：来自最新主仓 tag 的 gitlink，查不到就明确说"无法确定"
        const QString prevTag = prev.tagByRepo.value(row.repo);
        setColored(row.prevVersion, prevTag.isEmpty() ? U("—") : prevTag,
                   prevTag.isEmpty() ? QStringLiteral("#6B7280") : QStringLiteral("#1F2937"));
        if (!prevTag.isEmpty()) {
            row.prevVersion->setToolTip(U("版本 ") + versionOfTag(prevTag, state.tagPrefix)
                                        + U("；上一版主仓 ") + prev.mainTag);
        } else if (m_prevCacheTag.isEmpty()) {
            row.prevVersion->setToolTip(U("还没有主仓 tag（首次发布）"));
        } else if (m_prevUnresolved.contains(row.repo)) {
            row.prevVersion->setToolTip(
                U("无法确定上一版绑定：该仓 tag 可能不在本地，先刷新/拉取"));
        } else {
            row.prevVersion->setToolTip(U("上一版没有绑定该仓库"));
        }

        // 最新版本（该仓最新的 tag）
        const QString latestTag = allTags.value(0);
        setColored(row.latestVersion, latestTag.isEmpty() ? U("—") : latestTag,
                   latestTag.isEmpty() ? QStringLiteral("#DC2626") : QStringLiteral("#1F2937"));
        row.latestVersion->setToolTip(
            latestTag.isEmpty()
                ? U("该仓库还没有 tag")
                : U("版本 ") + versionOfTag(latestTag, state.tagPrefix));

        // 选择（tag）
        const QString selected = m_mainBindDraft.value(row.repo);
        row.pick->setText(selected.isEmpty() ? U("选择…") : selected);
        row.pick->setToolTip(selected.isEmpty() ? U("选择该仓库本次主仓要绑定的 tag")
                                                : U("已选择 ") + selected + U("；点击可以改"));

        // 状态：tag 存在 / release tip / VERSION.txt / 是否推送
        const QString probeKey = row.repo + QLatin1Char('|') + selected + QLatin1Char('|')
                                 + state.tagPrefix + QLatin1Char('|')
                                 + allTags.join(QLatin1Char(',')) + QLatin1Char('|')
                                 + (state.remoteTagsOk ? QLatin1Char('1') : QLatin1Char('0'));
        if (m_refreshFull || row.probeKey != probeKey) {
            row.probeKey = probeKey;

            QStringList hard;
            QStringList soft;
            QString tip = state.path;
            if (!state.initialized) {
                hard << U("未初始化");
            } else if (!state.statusOk) {
                hard << U("读不到 git 状态");
            } else if (selected.isEmpty()) {
                soft << U("未选择");
            } else if (!state.tagNames.contains(selected)) {
                hard << U("tag 不存在");
            } else {
                const QString version = versionOfTag(selected, state.tagPrefix);
                if (version.isEmpty()) {
                    hard << U("tag 格式不对");
                } else {
                    tip = U("tag：") + selected + QLatin1Char('\n')
                          + U("版本：") + version + QLatin1Char('\n') + tip;

                    // 只禁止回退：不能低于上一版主仓绑定的版本；同版本流水号也不能退。
                    const QString prevTag = prev.tagByRepo.value(row.repo);
                    const QString prevVersion = versionOfTag(prevTag, state.tagPrefix);
                    if (!prevVersion.isEmpty()) {
                        if (versionGreater(prevVersion, version)) {
                            hard << U("低于上一版绑定 ") + prevVersion;
                        } else if (prevVersion == version
                                   && nnOfTag(selected, state.tagPrefix, version)
                                          < nnOfTag(prevTag, state.tagPrefix, prevVersion)) {
                            hard << U("流水号低于上一版绑定");
                        }
                    }

                    const GitRun versionFile =
                        runGit(m_root, state.path,
                               {QStringLiteral("show"),
                                selected + QStringLiteral(":VERSION.txt")});

                    if (!versionFile.ok) {
                        hard << U("读不到 VERSION.txt");
                    } else {
                        // 顺手缓存 tag 里的 VERSION.txt，生成预发布内容时直接用
                        m_tagVersionCache.insert(tagCacheKey(state.path, selected),
                                                 versionFile.out);
                        const VersionFileInfo info = parseVersionFile(versionFile.out);
                        const QString first = info.versions.isEmpty() ? QString()
                                                                     : info.versions.first();
                        if (first != version) {
                            hard << U("VERSION 首块为 ")
                                        + (first.isEmpty() ? U("空") : first);
                        }
                    }

                    if (m_remoteChecked && !state.fetchOk) {
                        soft << U("远端未确认");
                    }
                    if (m_remoteChecked && !state.remoteTagsOk) {
                        soft << U("远端 tag 未确认");
                    } else if (m_remoteChecked
                               && !state.remoteTagNames.contains(selected)) {
                        hard << U("tag 未推送");
                    }
                }
            }

            if (!hard.isEmpty()) {
                row.statusText = hard.first();
                row.statusColor = QStringLiteral("#DC2626");
            } else if (!soft.isEmpty()) {
                row.statusText = soft.first();
                row.statusColor = QStringLiteral("#6B7280");
            } else {
                row.statusText = U("正常");
                row.statusColor = QStringLiteral("#16A34A");
            }

            QStringList tipLines;
            tipLines << tip;
            tipLines << hard;
            tipLines << soft;
            row.statusTip = tipLines.join(QLatin1Char('\n'));
        }

        setStatus(row.status, row.statusText, row.statusColor);
        row.status->setToolTip(row.statusTip);
    }

    updateUpdateButtons();
}

QString MainWindow::buildMainSectionHeader(const QString &version, const QString &date) const
{
    const QStringList repos = mainReleaseRepos();
    QStringList separator;
    QStringList bound;
    for (const QString &repo : repos) {
        separator << QStringLiteral("---");
        bound << m_mainBindDraft.value(repo);
    }

    QString header;
    header += QStringLiteral("---\n\n");
    header += QStringLiteral("<a id=\"v%1\"></a>\n").arg(version);
    header += U("## 主仓 ") + version + QStringLiteral("\n\n");
    header += U("发布日期：") + date + QStringLiteral("\n\n");
    header += QStringLiteral("| %1 |\n").arg(repos.join(QStringLiteral(" | ")));
    header += QStringLiteral("|%1|\n").arg(separator.join(QLatin1Char('|')));
    header += QStringLiteral("| %1 |\n").arg(bound.join(QStringLiteral(" | ")));
    return header;
}

QString MainWindow::tagVersionText(const QString &repoPath, const QString &tag, QString *error)
{
    const QString key = tagCacheKey(repoPath, tag);
    const auto cached = m_tagVersionCache.constFind(key);
    if (cached != m_tagVersionCache.constEnd()) return cached.value();

    const GitRun show = runGit(m_root, repoPath,
                               {QStringLiteral("show"), tag + QStringLiteral(":VERSION.txt")});
    if (!show.ok) {
        if (error) *error = U("读不到 ") + tag + QStringLiteral(":VERSION.txt");
        return QString();
    }
    m_tagVersionCache.insert(key, show.out);
    return show.out;
}

QVector<RepoUpdate> MainWindow::collectRepoUpdates(QString *error)
{
    QVector<RepoUpdate> updates;
    refreshPreviousBindingCache();
    if (!m_prevCacheTag.isEmpty() && !m_prevUnresolved.isEmpty()) {
        if (error) {
            *error = U("无法确定上一版绑定：")
                     + m_prevUnresolved.join(QStringLiteral("、"))
                     + U("（先确认这些仓的 tag 已在本地）");
        }
        return updates;
    }
    const PrevBinding prev =
        makePrevBinding(m_prevCacheTag, m_prevTagByRepo, m_prevVersionByRepo);

    for (const QString &repo : mainReleaseRepos()) {
        const int index = repoIndexByName(repo);
        if (index < 0) continue;
        const RepoState &state = m_repos.at(index);

        const QString tag = m_mainBindDraft.value(repo);
        if (tag.isEmpty()) {
            if (error) *error = repo + U(" 还没有选择 tag");
            return QVector<RepoUpdate>();
        }
        const QString version = versionOfTag(tag, state.tagPrefix);
        if (version.isEmpty()) {
            if (error) *error = repo + U(" 的绑定不是合法 tag：") + tag;
            return QVector<RepoUpdate>();
        }

        const QString prevTag = prev.tagByRepo.value(repo);
        if (tag == prevTag) continue;  // 和上一版一样，不算变化

        RepoUpdate update;
        update.repo = repo;
        update.tag = tag;
        update.version = version;
        update.prevTag = prevTag;
        update.prevVersion = versionOfTag(prevTag, state.tagPrefix);

        QString readError;
        const QString versionText = tagVersionText(state.path, tag, &readError);
        if (!readError.isEmpty()) {
            if (error) *error = repo + QLatin1Char(' ') + readError;
            return QVector<RepoUpdate>();
        }

        // 版本块取 (上一版, 本次]；同版本换流水号时 prevVersion == 本次版本，
        // 严格大于会把块全滤掉，所以"本次版本"必须单独放行。
        for (const VersionBlock &block : parseVersionBlocks(versionText)) {
            if (update.prevVersion.isEmpty()) {
                update.blocks.append(block);
            } else if (block.version == update.version
                       || (versionGreater(block.version, update.prevVersion)
                           && versionGreater(update.version, block.version))) {
                update.blocks.append(block);
            }
        }
        std::sort(update.blocks.begin(), update.blocks.end(),
                  [](const VersionBlock &a, const VersionBlock &b) {
                      return versionGreater(b.version, a.version);
                  });
        updates.append(update);
    }
    return updates;
}

QString MainWindow::buildMainSectionText(const QString &version, const QString &date,
                                         QString *error)
{
    if (!isValidVersion(version)) {
        if (error) *error = U("主仓版本写成 X.Y.Z");
        return QString();
    }

    QString err;
    const QVector<RepoUpdate> updates = collectRepoUpdates(&err);
    if (!err.isEmpty()) {
        if (error) *error = err;
        return QString();
    }

    QString section = buildMainSectionHeader(version, date);
    if (updates.isEmpty()) {
        section += QStringLiteral("\n") + U("本版没有子仓版本更新。") + QLatin1Char('\n');
        return normalizeBody(section);
    }

    bool first = true;
    for (const RepoUpdate &update : updates) {
        if (first) {
            section += QStringLiteral("\n");
            first = false;
        }
        section += U("### 子仓 ") + update.repo + U("：");
        if (update.prevVersion.isEmpty()) {
            section += update.version;
        } else if (update.prevVersion == update.version) {
            section += update.version + QStringLiteral(" → ") + update.version
                       + U("（流水号迭代）");
        } else {
            section += update.prevVersion + QStringLiteral(" → ") + update.version;
        }
        section += QStringLiteral("\n\n");
        for (const VersionBlock &block : update.blocks) {
            section += repoBlockText(block);
            section += QLatin1Char('\n');
        }
    }
    return normalizeBody(section);
}

void MainWindow::openMainVersionPicker(const QString &repo)
{
    const int index = repoIndexByName(repo);
    if (index < 0) return;
    const RepoState &state = m_repos.at(index);
    const QString repoPath = state.path;
    const QString prefix = state.tagPrefix;

    const QStringList allTags = tagsOfRepo(mergedTags(state.tagNames, state.remoteTagNames), prefix);
    const PrevBinding prev =
        makePrevBinding(m_prevCacheTag, m_prevTagByRepo, m_prevVersionByRepo);
    const QString prevTag = prev.tagByRepo.value(repo);
    const QStringList tags = selectableTags(allTags, prefix, prevTag);
    if (tags.isEmpty()) {
        QMessageBox::information(this, U("选择 Tag"),
                                 U("该仓库没有可选的 tag；先给子仓打 tag。"));
        return;
    }

    // tag -> 日期
    QHash<QString, QString> dateByTag;
    const GitRun refs = runGit(m_root, repoPath,
                               {QStringLiteral("for-each-ref"),
                                QStringLiteral("--format=%(refname:short)%09%(creatordate:short)"),
                                QStringLiteral("refs/tags")});
    if (refs.ok) {
        const QStringList lines = refs.out.split(QLatin1Char('\n'));
        for (QString line : lines) {
            if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
            const QStringList parts = line.split(QLatin1Char('\t'));
            if (parts.size() < 2) continue;
            const QString name = parts.at(0).trimmed();
            if (!name.isEmpty() && !dateByTag.contains(name)) {
                dateByTag.insert(name, parts.at(1).trimmed());
            }
        }
    }

    QDialog dialog(this);
    dialog.setWindowTitle(U("选择 Tag · ") + repo);
    dialog.resize(760, 470);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(8);

    layout->addWidget(labelTitle(repo + U(" 的可选 Tag")));
    const QString range = prevTag.isEmpty()
                              ? U("该仓库还没有上一版绑定，列出全部 tag；从新到旧。")
                              : U("从上一版绑定 ") + prevTag + U(" 的版本（含）到最新，从新到旧。");
    layout->addWidget(labelMuted(range));

    auto *table = new QTableWidget(tags.size(), 4);
    table->setHorizontalHeaderLabels({ U("Tag"), U("时间"), U("迭代内容"), U("勾选") });
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setWordWrap(false);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(50);
    table->horizontalHeader()->setFixedHeight(34);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    table->setColumnWidth(1, 120);
    table->setColumnWidth(2, 96);
    table->setColumnWidth(3, 70);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->setMinimumHeight(240);

    const QFont tagFont = []() {
        QFont font(QStringLiteral("Consolas"));
        font.setPointSize(8);
        return font;
    }();

    QVector<QCheckBox *> checks;
    for (int r = 0; r < tags.size(); ++r) {
        const QString tag = tags.at(r);
        const QString version = versionOfTag(tag, prefix);

        // Tag 两行：版本 + 完整 tag
        auto *tagWrap = new QWidget;
        auto *tagLayout = new QVBoxLayout(tagWrap);
        tagLayout->setContentsMargins(2, 0, 2, 0);
        tagLayout->setSpacing(0);
        auto *versionLabel = new QLabel(version);
        versionLabel->setAlignment(Qt::AlignCenter);
        versionLabel->setStyleSheet(QStringLiteral("color:#6B7280; font-size:8pt;"));
        auto *tagLabel = new QLabel(tag);
        tagLabel->setAlignment(Qt::AlignCenter);
        tagLabel->setFont(tagFont);
        tagLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        tagLayout->addWidget(versionLabel);
        tagLayout->addWidget(tagLabel);
        table->setCellWidget(r, 0, tagWrap);

        auto *dateItem = new QTableWidgetItem(dateByTag.value(tag, QStringLiteral("—")));
        dateItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(r, 1, dateItem);

        auto *view = makeSecondary(U("查看"));
        connect(view, &QPushButton::clicked, &dialog,
                [this, repo, repoPath, tag, version]() {
                    QString body;
                    QString hint;
                    QString readError;
                    const QString versionText = tagVersionText(repoPath, tag, &readError);
                    if (!readError.isEmpty()) {
                        hint = readError;
                    } else {
                        for (const VersionBlock &block : parseVersionBlocks(versionText)) {
                            if (block.version == version) {
                                body = block.body;
                                break;
                            }
                        }
                        if (body.isEmpty()) hint = U("这个 tag 的 VERSION.txt 里没有该版本正文");
                    }

                    QDialog viewer(this);
                    viewer.setWindowTitle(U("更新内容 · ") + repo);
                    viewer.resize(620, 420);
                    auto *viewerLayout = new QVBoxLayout(&viewer);
                    viewerLayout->setContentsMargins(18, 16, 18, 14);
                    viewerLayout->setSpacing(8);
                    viewerLayout->addWidget(labelTitle(repo + QStringLiteral("  ·  ") + version));
                    viewerLayout->addWidget(labelMuted(U("tag：") + tag));
                    auto *text = new QPlainTextEdit;
                    text->setObjectName(QStringLiteral("bom"));
                    text->setReadOnly(true);
                    text->setPlainText(body.isEmpty() ? hint : body);
                    viewerLayout->addWidget(text, 1);
                    auto *close = makeSecondary(U("关闭"));
                    connect(close, &QPushButton::clicked, &viewer, &QDialog::accept);
                    auto *closeRow = new QHBoxLayout;
                    closeRow->addStretch();
                    closeRow->addWidget(close);
                    viewerLayout->addLayout(closeRow);
                    viewer.exec();
                });
        table->setCellWidget(r, 2, view);

        auto *checkWrap = new QWidget;
        auto *checkLayout = new QHBoxLayout(checkWrap);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        checkLayout->setSpacing(0);
        checkLayout->addStretch();
        auto *check = new QCheckBox;
        checkLayout->addWidget(check);
        checkLayout->addStretch();
        checks.append(check);
        table->setCellWidget(r, 3, checkWrap);

        connect(check, &QCheckBox::toggled, &dialog, [&checks, r](bool on) {
            if (!on) return;
            for (int i = 0; i < checks.size(); ++i) {
                if (i == r || !checks.at(i)->isChecked()) continue;
                const QSignalBlocker blocker(checks.at(i));
                checks.at(i)->setChecked(false);
            }
        });
    }

    // 默认当前草稿；没有就选最新 tag。
    int defaultRow = 0;
    const QString draft = m_mainBindDraft.value(repo);
    const int draftRow = tags.indexOf(draft);
    if (draftRow >= 0) defaultRow = draftRow;
    checks.at(defaultRow)->setChecked(true);
    table->setCurrentItem(nullptr);

    auto *actions = new QHBoxLayout;
    auto *cancel = makeSecondary(U("取消"));
    auto *confirm = new QPushButton(U("确认"));
    confirm->setObjectName(QStringLiteral("primary"));
    confirm->setMinimumWidth(96);
    actions->addStretch();
    actions->addWidget(cancel);
    actions->addWidget(confirm);
    layout->addWidget(table, 1);
    layout->addLayout(actions);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(confirm, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted) return;

    int picked = -1;
    for (int r = 0; r < checks.size(); ++r) {
        if (checks.at(r)->isChecked()) {
            picked = r;
            break;
        }
    }
    if (picked < 0) m_mainBindDraft.remove(repo);
    else m_mainBindDraft.insert(repo, tags.at(picked));
    updateMainRelease();
}

void MainWindow::openMainSectionDialog()
{
    const QString version = m_mainVersion ? m_mainVersion->text().trimmed() : QString();
    const bool iterate = mainIterateMode();
    const QString date = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));

    // 版本 / 迭代模式变了就重建；同版本迭代保留 CHANGELOG 里已有的正文
    const bool stale = normalizeBody(m_mainSectionText).isEmpty()
                       || m_mainSectionVersion != version
                       || m_mainSectionIterate != iterate;
    if (stale) {
        QString preserved;
        bool hasSection = false;
        if (iterate) readChangelogSectionBody(version, &preserved, &hasSection);

        if (iterate && hasSection) {
            QString composed = buildMainSectionHeader(version, date);
            if (!preserved.isEmpty()) composed += QStringLiteral("\n") + preserved;
            m_mainSectionText = normalizeBody(composed);
            appendLog(U("[UI] 已载入同版本迭代内容：") + version);
        } else {
            QString error;
            const QString generated = buildMainSectionText(version, date, &error);
            if (!error.isEmpty()) {
                QMessageBox::warning(this, U("无法生成"), error);
                return;
            }
            m_mainSectionText = generated;
            appendLog(U("[UI] 已生成主仓发布内容：") + version);
        }
        m_mainSectionVersion = version;
        m_mainSectionIterate = iterate;
        updateMainSectionStatus();
    }

    QDialog dialog(this);
    dialog.setWindowTitle(iterate ? U("预发布内容（同版本迭代） · 主仓 ") + version
                                  : U("预发布内容 · 主仓 ") + version);
    dialog.resize(1240, 790);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(8);

    auto *head = new QHBoxLayout;
    head->addWidget(labelTitle(U("主仓 ") + version + U(" 预发布内容")));
    head->addStretch();
    head->addWidget(labelMuted(iterate
                                   ? U("同版本迭代：保留已有正文，左右分栏可改可看；发布时替换已有节")
                                   : U("左侧可改，右侧实时预览；完成后发布才写文件")));
    layout->addLayout(head);

    auto *editor = new QPlainTextEdit;
    editor->setObjectName(QStringLiteral("bom"));
    editor->setPlainText(m_mainSectionText);

    auto *browser = new QTextBrowser;
    browser->setObjectName(QStringLiteral("mdPreview"));
    browser->setOpenExternalLinks(false);
    browser->setHtml(renderMainSectionHtml(m_mainSectionText));

    // 右侧：预览 / 子仓更新
    auto *sideTabs = new QTabWidget;
    sideTabs->addTab(browser, U("预览"));

    auto *updatesPage = new QWidget;
    auto *updatesLayout = new QVBoxLayout(updatesPage);
    updatesLayout->setContentsMargins(0, 0, 0, 0);
    updatesLayout->setSpacing(6);
    updatesLayout->addWidget(labelHint(U("点子仓那一行展开迭代正文；右侧“复制”复制整段正文。")));

    auto *updatesInner = new QWidget;
    auto *updatesInnerLayout = new QVBoxLayout(updatesInner);
    updatesInnerLayout->setContentsMargins(0, 0, 0, 0);
    updatesInnerLayout->setSpacing(8);

    QString updatesError;
    const QVector<RepoUpdate> updates = collectRepoUpdates(&updatesError);
    if (!updatesError.isEmpty()) {
        updatesInnerLayout->addWidget(labelMuted(updatesError));
    } else if (updates.isEmpty()) {
        updatesInnerLayout->addWidget(labelMuted(U("本次没有子仓 tag 变化。")));
    } else {
        for (const RepoUpdate &update : updates) {
            QString heading = U("子仓 ") + update.repo + U("：");
            if (update.prevVersion.isEmpty()) {
                heading += update.version;
            } else if (update.prevVersion == update.version) {
                heading += update.version + QStringLiteral(" → ") + update.version
                           + U("（流水号迭代）");
            } else {
                heading += update.prevVersion + QStringLiteral(" → ") + update.version;
            }

            QStringList parts;
            for (const VersionBlock &block : update.blocks) parts << repoBlockText(block);
            const QString body = normalizeBody(parts.join(QLatin1Char('\n')));

            auto *group = new QFrame;
            group->setObjectName(QStringLiteral("subcard"));
            auto *groupLayout = new QVBoxLayout(group);
            groupLayout->setContentsMargins(10, 6, 10, 6);
            groupLayout->setSpacing(4);

            // 收起时只有这一行：标题 + 最右边的复制
            auto *headerRow = new QWidget;
            auto *headerLayout = new QHBoxLayout(headerRow);
            headerLayout->setContentsMargins(0, 0, 0, 0);
            headerLayout->setSpacing(8);

            auto *toggle = new QPushButton(U("▸ ") + heading);
            toggle->setObjectName(QStringLiteral("accordionHeader"));
            toggle->setCheckable(true);
            toggle->setCursor(Qt::PointingHandCursor);
            toggle->setToolTip(U("点这一行展开 / 收起"));
            headerLayout->addWidget(toggle, 1);

            auto *copy = makeSecondary(U("复制"));
            copy->setFixedWidth(64);
            copy->setEnabled(!body.isEmpty());
            copy->setToolTip(U("复制整段迭代正文"));
            connect(copy, &QPushButton::clicked, &dialog, [body]() {
                QApplication::clipboard()->setText(body);
            });
            headerLayout->addWidget(copy, 0);
            groupLayout->addWidget(headerRow);

            auto *detail = new QPlainTextEdit;
            detail->setObjectName(QStringLiteral("bom"));
            detail->setReadOnly(true);
            detail->setPlainText(body.isEmpty() ? U("这个 tag 里没有可显示的版本块。") : body);
            const int textLines = body.isEmpty() ? 1 : body.count(QLatin1Char('\n')) + 1;
            detail->setFixedHeight(qBound(90, textLines * 19 + 16, 260));
            // 固定高度也把纵向策略设成 Fixed，否则展开卡会被父布局当成可纵向扩展，
            // 剩余高度分给卡片后标题行会被撑高。
            detail->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
            detail->setVisible(false);
            groupLayout->addWidget(detail);

            connect(toggle, &QPushButton::toggled, &dialog,
                    [toggle, detail, heading](bool open) {
                        toggle->setText((open ? U("▾ ") : U("▸ ")) + heading);
                        detail->setVisible(open);
                    });

            updatesInnerLayout->addWidget(group);
        }
    }
    updatesInnerLayout->addStretch();
    updatesLayout->addWidget(makeContentScroll(updatesInner), 1);
    sideTabs->addTab(updatesPage, U("子仓更新"));

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(editor);
    splitter->addWidget(sideTabs);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({620, 560});
    layout->addWidget(splitter, 1);

    // 编辑框 -> 300ms 单次定时器 -> 预览；连续输入只渲染一次
    auto *previewTimer = new QTimer(&dialog);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(300);
    connect(editor, &QPlainTextEdit::textChanged, previewTimer,
            [previewTimer]() { previewTimer->start(); });
    connect(previewTimer, &QTimer::timeout, &dialog, [browser, editor]() {
        browser->setHtml(renderMainSectionHtml(editor->toPlainText()));
    });

    auto *actions = new QHBoxLayout;
    auto *regenerate = makeSecondary(U("重新生成"));
    auto *done = new QPushButton(U("完成"));
    done->setObjectName(QStringLiteral("primary"));
    done->setMinimumWidth(96);
    actions->addWidget(regenerate);
    actions->addStretch();
    actions->addWidget(done);
    layout->addLayout(actions);

    connect(regenerate, &QPushButton::clicked, &dialog, [this, editor, &dialog]() {
        const QMessageBox::StandardButton answer =
            QMessageBox::question(&dialog, U("重新生成"),
                                  U("按当前绑定重新拼接整节，编辑框里的内容会被覆盖。继续？"));
        if (answer != QMessageBox::Yes) return;

        QString error;
        const QString generated = buildMainSectionText(
            m_mainVersion ? m_mainVersion->text().trimmed() : QString(),
            QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")), &error);
        if (!error.isEmpty()) {
            QMessageBox::warning(&dialog, U("无法生成"), error);
            return;
        }
        editor->setPlainText(generated);
    });
    connect(done, &QPushButton::clicked, &dialog, &QDialog::accept);

    dialog.exec();

    // 关闭和完成都保存；发布用这份文本
    m_mainSectionText = editor->toPlainText();
    m_mainSectionVersion = version;
    m_mainSectionIterate = iterate;
    updateMainSectionStatus();
}

void MainWindow::updateMainSectionStatus()
{
    if (!m_mainSectionHint) return;
    const QString text = normalizeBody(m_mainSectionText);
    if (text.isEmpty()) {
        m_mainSectionHint->setText(U("未生成"));
        m_mainSectionHint->setStyleSheet(QString());
    } else {
        const int lines = text.split(QLatin1Char('\n')).size();
        m_mainSectionHint->setText(U("已生成 ") + QString::number(lines) + U(" 行"));
        m_mainSectionHint->setStyleSheet(QStringLiteral("color:#16A34A;"));
    }
    updateUpdateButtons();
}

bool MainWindow::readChangelogSectionBody(const QString &version, QString *body,
                                          bool *found) const
{
    if (found) *found = false;
    if (body) body->clear();

    QFile file(QDir(m_root).filePath(QStringLiteral("engineering/version/RELEASE.md")));
    if (!file.open(QIODevice::ReadOnly)) return false;
    QString text = QString::fromUtf8(file.readAll());
    file.close();
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));

    const QStringList lines = text.split(QLatin1Char('\n'));
    const QRegularExpression titleRe(QStringLiteral("^##\\s+\\S+\\s+")
                                     + QRegularExpression::escape(version)
                                     + QStringLiteral("\\s*$"));
    int start = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (titleRe.match(lines.at(i).trimmed()).hasMatch()) {
            start = i;
            break;
        }
    }
    if (start < 0) return false;
    if (found) *found = true;

    int end = lines.size();
    const QRegularExpression otherTitle(
        QStringLiteral("^##\\s+\\S+\\s+\\d+\\.\\d+\\.\\d+\\s*$"));
    for (int i = start + 1; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        if (line == QStringLiteral("---") || otherTitle.match(line).hasMatch()) {
            end = i;
            break;
        }
    }

    int bodyStart = -1;
    for (int i = start + 1; i < end; ++i) {
        if (lines.at(i).trimmed().startsWith(U("### 子仓 "))) {
            bodyStart = i;
            break;
        }
    }
    if (bodyStart < 0) return true;

    QStringList bodyLines = lines.mid(bodyStart, end - bodyStart);
    while (!bodyLines.isEmpty() && bodyLines.last().trimmed().isEmpty()) bodyLines.removeLast();
    if (body) *body = bodyLines.join(QLatin1Char('\n'));
    return true;
}

bool MainWindow::writeReleaseDoc(const QString &version, const QString &section,
                                 bool replaceExisting, QString *error) const
{
    const QString path = QDir(m_root).filePath(QStringLiteral("engineering/version/RELEASE.md"));
    QFile file(path);
    QString text;
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) *error = U("读不到 ") + path;
            return false;
        }
        text = QString::fromUtf8(file.readAll());
        file.close();
    } else {
        text = U("# 版本记录\n\n");
    }

    QString bom;
    if (text.startsWith(QChar(0xFEFF))) {
        bom = QChar(0xFEFF);
        text.remove(0, 1);
    }
    const QString nl = text.contains(QStringLiteral("\r\n")) ? QStringLiteral("\r\n")
                                                             : QStringLiteral("\n");
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    QStringList lines = text.split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) lines.removeLast();

    const QString body = normalizeBody(section);
    if (body.isEmpty()) {
        if (error) *error = U("发布内容不能为空");
        return false;
    }

    // One tab per body line: the release script strips exactly one tab.
    QStringList block;
    block << (U("## 主仓 ") + version);
    block << QString();
    for (const QString &l : body.split(QLatin1Char('\n'))) {
        block << (l.isEmpty() ? QString() : (QString(QLatin1Char('\t')) + l));
    }

    const QRegularExpression titleRe(QStringLiteral("^##\\s+\\S+\\s+")
                                     + QRegularExpression::escape(version)
                                     + QStringLiteral("\\s*$"));
    int start = -1;
    for (int i = 0; i < lines.size(); ++i) {
        if (titleRe.match(lines.at(i).trimmed()).hasMatch()) {
            start = i;
            break;
        }
    }

    if (replaceExisting && start >= 0) {
        int end = lines.size();
        for (int i = start + 1; i < lines.size(); ++i) {
            if (lines.at(i).startsWith(QStringLiteral("## "))) {
                end = i;
                break;
            }
        }
        lines = lines.mid(0, start) + block + QStringList{ QString() } + lines.mid(end);
    } else {
        int insertAt = lines.size();
        for (int i = 0; i < lines.size(); ++i) {
            if (lines.at(i).startsWith(QStringLiteral("## "))) {
                insertAt = i;
                break;
            }
        }
        lines = lines.mid(0, insertAt) + block + QStringList{ QString() } + lines.mid(insertAt);
    }

    QStringList out;
    for (const QString &l : lines) {
        if (l.trimmed().isEmpty() && !out.isEmpty() && out.last().trimmed().isEmpty()) continue;
        out << l;
    }
    while (!out.isEmpty() && out.last().trimmed().isEmpty()) out.removeLast();

    QString result = out.join(QLatin1Char('\n')) + QLatin1Char('\n');
    if (nl == QStringLiteral("\r\n")) result.replace(QStringLiteral("\n"), nl);
    if (!bom.isEmpty()) result.prepend(bom);
    return writeTextFile(path, result, error);
}

bool MainWindow::buildMainChangelog(const QString &version, const QString &date,
                                    const QString &section, bool replaceExisting,
                                    QString *out, QString *error) const
{
    const QString path = QDir(m_root).filePath(QStringLiteral("engineering/version/CHANGELOG.md"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = U("读不到 ") + path;
        return false;
    }
    QString text = QString::fromUtf8(file.readAll());
    file.close();

    QString bom;
    if (text.startsWith(QChar(0xFEFF))) {
        bom = QChar(0xFEFF);
        text.remove(0, 1);
    }
    const QString nl = text.contains(QStringLiteral("\r\n")) ? QStringLiteral("\r\n")
                                                             : QStringLiteral("\n");
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    const QStringList lines = text.split(QLatin1Char('\n'));

    const QString sectionText = normalizeBody(section);
    if (sectionText.isEmpty()) {
        if (error) *error = U("发布内容不能为空");
        return false;
    }
    const QStringList sectionLines = sectionText.split(QLatin1Char('\n'));

    QStringList merged;

    if (replaceExisting) {
        // 同版本迭代：替换已有的 ## 主仓 X 节，不动目录行和分隔线
        const QRegularExpression titleRe(QStringLiteral("^##\\s+\\S+\\s+")
                                         + QRegularExpression::escape(version)
                                         + QStringLiteral("\\s*$"));
        int start = -1;
        for (int i = 0; i < lines.size(); ++i) {
            if (titleRe.match(lines.at(i).trimmed()).hasMatch()) {
                start = i;
                break;
            }
        }
        if (start < 0) {
            if (error) {
                *error = U("engineering/version/CHANGELOG.md 里没有 ## 主仓 ") + version + U(" 这一节");
            }
            return false;
        }

        int end = lines.size();
        const QRegularExpression otherTitle(
            QStringLiteral("^##\\s+\\S+\\s+\\d+\\.\\d+\\.\\d+\\s*$"));
        for (int i = start + 1; i < lines.size(); ++i) {
            const QString line = lines.at(i).trimmed();
            if (line == QStringLiteral("---") || otherTitle.match(line).hasMatch()) {
                end = i;
                break;
            }
        }

        // 新内容从 ## 主仓 开始（保留上面已有的 --- / <a id>）
        int bodyStart = -1;
        for (int i = 0; i < sectionLines.size(); ++i) {
            if (sectionLines.at(i).trimmed().startsWith(QStringLiteral("## "))) {
                bodyStart = i;
                break;
            }
        }
        if (bodyStart < 0) {
            if (error) *error = U("发布内容里没有 ## 主仓 标题");
            return false;
        }
        QStringList replacement = sectionLines.mid(bodyStart);
        while (!replacement.isEmpty() && replacement.last().trimmed().isEmpty()) {
            replacement.removeLast();
        }
        if (!replacement.isEmpty()) replacement.append(QString());

        for (int i = 0; i < start; ++i) merged.append(lines.at(i));
        merged.append(replacement);
        for (int i = end; i < lines.size(); ++i) merged.append(lines.at(i));

        // 目录里已有这个版本的行：只更新日期，不新增行
        const QRegularExpression dirRowRe(
            QStringLiteral("^\\|\\s*\\[") + QRegularExpression::escape(version)
            + QStringLiteral("\\]\\(#v") + QRegularExpression::escape(version)
            + QStringLiteral("\\)\\s*\\|"));
        for (QString &line : merged) {
            if (!dirRowRe.match(line.trimmed()).hasMatch()) continue;
            line = QStringLiteral("| [") + version + QStringLiteral("](#v") + version
                   + QStringLiteral(") | ") + date + QStringLiteral(" |");
            break;
        }
    } else {
        int dirIndex = -1;
        for (int i = 0; i < lines.size(); ++i) {
            if (lines.at(i).trimmed() == U("## 目录")) {
                dirIndex = i;
                break;
            }
        }
        if (dirIndex < 0) {
            if (error) *error = U("engineering/version/CHANGELOG.md 里没有“## 目录”");
            return false;
        }

        static const QRegularExpression sepRe(QStringLiteral("^\\|[\\s\\-:|]+\\|$"));
        int sepIndex = -1;
        for (int i = dirIndex + 1; i < lines.size(); ++i) {
            const QString line = lines.at(i).trimmed();
            if (line.startsWith(QStringLiteral("## "))) break;
            if (sepRe.match(line).hasMatch()) {
                sepIndex = i;
                break;
            }
        }
        if (sepIndex < 0) {
            if (error) *error = U("“## 目录”下面没有表格分隔行");
            return false;
        }

        int markerIndex = -1;
        for (int i = 0; i < lines.size(); ++i) {
            if (lines.at(i).trimmed() == QStringLiteral("<!-- release:entry -->")) {
                markerIndex = i;
                break;
            }
        }
        if (markerIndex < 0) {
            if (error) *error = U("engineering/version/CHANGELOG.md 缺少 <!-- release:entry --> 标记");
            return false;
        }

        const QString dirRow = QStringLiteral("| [") + version + QStringLiteral("](#v")
                               + version + QStringLiteral(") | ") + date
                               + QStringLiteral(" |");
        QStringList delta;
        delta << QString();
        delta << sectionLines;
        delta << QString();

        for (int i = 0; i < lines.size(); ++i) {
            merged.append(lines.at(i));
            if (i == sepIndex) merged.append(dirRow);
            if (i == markerIndex) merged.append(delta);
        }
    }

    QString result = merged.join(QLatin1Char('\n'));
    if (nl == QStringLiteral("\r\n")) result.replace(QStringLiteral("\n"), nl);
    if (!bom.isEmpty()) result.prepend(bom);
    if (out) *out = result;
    return true;
}

void MainWindow::publishMainRelease()
{
    if (m_scriptRunning) return;

    const QString version = m_mainVersion ? m_mainVersion->text().trimmed() : QString();
    const QString body = normalizeBody(m_mainSectionText);
    const bool iterate = mainIterateMode();
    const bool push = m_mainPush && m_mainPush->isChecked();
    const QString date = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    refreshPreviousBindingCache();
    const PrevBinding prev =
        makePrevBinding(m_prevCacheTag, m_prevTagByRepo, m_prevVersionByRepo);

    // ---- 校验 ----
    QString error;
    if (!isValidVersion(version)) error = U("主仓版本写成 X.Y.Z");

    QStringList missing;
    for (const QString &repo : mainReleaseRepos()) {
        if (m_mainBindDraft.value(repo).isEmpty()) missing << repo;
    }
    if (error.isEmpty() && !missing.isEmpty()) {
        error = U("还没有选择版本：") + missing.join(QStringLiteral(", "));
    }
    if (error.isEmpty() && body.isEmpty()) error = U("发布内容不能为空");
    if (error.isEmpty() && (m_mainSectionVersion != version || m_mainSectionIterate != iterate)) {
        error = U("预发布内容与当前的版本 / 迭代模式不一致，先打开“查看预发布内容”刷新");
    }
    if (error.isEmpty() && !m_mainRepo.statusOk) error = U("读不到主仓 git 状态");
    if (error.isEmpty() && m_mainRepo.conflict) error = U("主仓工作区有冲突");
    if (error.isEmpty() && m_mainBlocked) error = m_mainBlockDetail;
    QHash<QString, QString> tagByRepo;
    QHash<QString, QString> versionByRepo;
    if (error.isEmpty()) {
        QStringList bad;
        for (const QString &repo : mainReleaseRepos()) {
            const QString tag = m_mainBindDraft.value(repo);
            const int index = repoIndexByName(repo);
            if (tag.isEmpty()) {
                bad << repo + U(" 没有选择 tag");
                continue;
            }
            if (index < 0) {
                bad << repo + U(" 不存在");
                continue;
            }
            const RepoState &state = m_repos.at(index);
            const QString ver = versionOfTag(tag, state.tagPrefix);
            if (ver.isEmpty()) {
                bad << repo + U(" 的绑定不是合法 tag：") + tag;
                continue;
            }
            if (!state.tagNames.contains(tag)) {
                bad << repo + U(" 的 tag 不在本地：") + tag;
                continue;
            }
            const QString prevTag = prev.tagByRepo.value(repo);
            const QString prevVer = versionOfTag(prevTag, state.tagPrefix);
            if (!prevVer.isEmpty()) {
                if (versionGreater(prevVer, ver)) {
                    bad << repo + U(" 低于上一版绑定 ") + prevVer;
                    continue;
                }
                if (prevVer == ver
                    && nnOfTag(tag, state.tagPrefix, ver)
                           < nnOfTag(prevTag, state.tagPrefix, prevVer)) {
                    bad << repo + U(" 的流水号低于上一版绑定 ");
                    continue;
                }
            }
            tagByRepo.insert(repo, tag);
            versionByRepo.insert(repo, ver);
        }
        if (!bad.isEmpty()) error = bad.join(QLatin1Char('\n'));
    }

    const int sameVersion =
        tagsForVersion(m_mainRepo.tagNames, m_mainRepo.tagPrefix, version).size();
    if (error.isEmpty()) {
        if (iterate && sameVersion == 0) error = U("该主仓版本还没有 tag，不能同版本迭代");
        else if (!iterate && sameVersion > 0) {
            error = U("该主仓版本已经有 tag；同版本迭代请勾选“同版本迭代”");
        }
    }
    if (!error.isEmpty()) {
        QMessageBox::warning(this, U("无法发布"), error);
        return;
    }

    // 同版本迭代：CHANGELOG 里已经有这个版本时替换那一节，而不是新增
    QString existingBody;
    bool hasExistingSection = false;
    readChangelogSectionBody(version, &existingBody, &hasExistingSection);
    const bool replaceExisting = iterate && hasExistingSection;

    const int nn = iterate ? sameVersion + 1 : 1;
    const QString tag = makeTag(m_mainRepo.tagPrefix, version,
                                QStringLiteral("%1").arg(nn, 2, 10, QLatin1Char('0')));

    QString modeText;
    if (replaceExisting) modeText = U("同版本迭代（替换已有节）");
    else if (iterate) modeText = U("同版本迭代（新增节）");
    else modeText = U("新版本");

    // ---- 确认 ----
    QDialog dialog(this);
    dialog.setWindowTitle(U("确认主仓发布"));
    dialog.resize(600, 470);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(8);

    layout->addWidget(labelTitle(U("主仓 ") + version));
    layout->addWidget(makeFormRow(U("主仓 tag"), new QLabel(tag)));
    layout->addWidget(makeFormRow(U("模式"), new QLabel(modeText)));
    layout->addWidget(makeFormRow(U("推送"), new QLabel(push ? U("release 和 tag") : U("否"))));

    layout->addWidget(new QLabel(U("子仓绑定")));
    auto *bindText = new QPlainTextEdit;
    bindText->setObjectName(QStringLiteral("bom"));
    bindText->setReadOnly(true);
    QStringList bindLines;
    for (const QString &repo : mainReleaseRepos()) {
        const QString prevTag = prev.tagByRepo.value(repo);
        const QString boundTag = tagByRepo.value(repo);
        bindLines << repo.leftJustified(8, QLatin1Char(' ')) + boundTag + U("    ")
                         + versionByRepo.value(repo);
        if (!prevTag.isEmpty() && prevTag != boundTag) {
            bindLines << U("        上一版 ") + prevTag;
        }
    }
    bindText->setPlainText(bindLines.join(QLatin1Char('\n')));
    layout->addWidget(bindText, 1);

    layout->addWidget(labelHint(U("确认后会写入 engineering/version/RELEASE.md，把选中的子仓 tag 绑到 gitlink 并提交，"
                                  "然后调用 engineering\\scripts\\tag_version.bat（脚本负责生成发布文件、打 tag、推送）。")));

    auto *actions = new QHBoxLayout;
    auto *cancel = makeSecondary(U("取消"));
    auto *confirm = new QPushButton(U("发布"));
    confirm->setObjectName(QStringLiteral("primary"));
    confirm->setMinimumWidth(96);
    actions->addStretch();
    actions->addWidget(cancel);
    actions->addWidget(confirm);
    layout->addLayout(actions);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(confirm, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted) return;

    // ---- RELEASE.md ----
    if (!writeReleaseDoc(version, body, replaceExisting, &error)) {
        QMessageBox::warning(this, U("写入失败"), error);
        return;
    }
    appendLog(replaceExisting
                  ? U("[UI] 已替换 engineering/version/RELEASE.md 里的主仓 ") + version
                  : U("[UI] 已写入 engineering/version/RELEASE.md：主仓 ") + version);

    // ---- bindings: write the selected tags into the gitlinks ----
    QStringList bindPaths;
    for (const QString &repo : mainReleaseRepos()) {
        const int index = repoIndexByName(repo);
        if (index < 0) {
            QMessageBox::warning(this, U("绑定失败"), U("找不到子仓：") + repo);
            return;
        }
        const RepoState &state = m_repos.at(index);
        const QString tag = tagByRepo.value(repo);
        const GitRun rev = runGit(m_root, state.path,
                                  {QStringLiteral("rev-parse"), tag + QStringLiteral("^{commit}")},
                                  15000);
        if (!rev.ok || rev.out.trimmed().isEmpty()) {
            QMessageBox::warning(this, U("绑定失败"),
                                 U("解析 ") + repo + U(" 的 tag 失败：") + tag);
            return;
        }
        const QString sha = rev.out.trimmed();
        const GitRun idx = runGit(m_root, QString(),
                                  {QStringLiteral("update-index"), QStringLiteral("--cacheinfo"),
                                   QStringLiteral("160000,%1,%2").arg(sha, state.path)},
                                  15000);
        if (!idx.ok) {
            QMessageBox::warning(this, U("绑定失败"),
                                 idx.err.isEmpty() ? U("git update-index 失败") : idx.err);
            return;
        }
        bindPaths << state.path;
    }
    appendLog(U("[UI] 已把 ") + QString::number(bindPaths.size()) + U(" 个子仓绑定写入 index"));

    // ---- commit RELEASE.md and the gitlinks only ----
    const QString docPath = QStringLiteral("engineering/version/RELEASE.md");
    const GitRun add = runGit(m_root, QString(),
                              {QStringLiteral("add"), QStringLiteral("--"), docPath}, 15000);
    if (!add.ok) {
        QMessageBox::warning(this, U("提交失败"), add.err.isEmpty() ? U("git add 失败") : add.err);
        return;
    }
    const GitRun commit =
        runGit(m_root, QString(),
               QStringList{QStringLiteral("commit"), QStringLiteral("-m"),
                           U("release: ") + version + U(" bindings and notes"),
                           QStringLiteral("--"), docPath} + bindPaths,
               30000);
    if (!commit.ok) {
        const QString detail = commit.err.isEmpty() ? commit.out.trimmed() : commit.err;
        QMessageBox::warning(this, U("提交失败"), detail.isEmpty() ? U("git commit 失败") : detail);
        return;
    }
    appendLog(U("[UI] 已提交 RELEASE.md 和子仓绑定：release: ") + version);

    // ---- 主仓脚本 ----
    QStringList args;
    if (iterate) args << QStringLiteral("-Iterate");
    if (push) args << QStringLiteral("-Push");
    runScript(QStringLiteral("engineering\\scripts\\tag_version.bat"), args, U("主仓发布 ") + tag,
              m_mainPublish, U("发布中…"), U("发布"));
}

QWidget *MainWindow::buildSubRepoForm(int index)
{
    const RepoState &state = m_repos.at(index);
    SubForm form;
    form.repoName = state.name;
    form.repoPath = state.path;

    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(16, 16, 16, 16);
    contentLayout->setSpacing(12);

    // 固定的当前状态：滚动下面的表单时一直看得见
    auto *statusContent = new QWidget;
    auto *statusLayout = new QVBoxLayout(statusContent);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(10);

    form.branch = new QLabel(QStringLiteral("—"));
    form.branch->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusLayout->addWidget(makeFormRow(U("分支"), form.branch));

    form.worktree = new QLabel(QStringLiteral("—"));
    form.worktree->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form.worktree->setWordWrap(true);
    statusLayout->addWidget(makeFormRow(U("工作区"), form.worktree));

    form.latestVersion = new QLabel(QStringLiteral("—"));
    form.latestVersion->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusLayout->addWidget(makeFormRow(U("最新版本"), form.latestVersion));

    contentLayout->addWidget(statusContent);

    // 表单区（整页滚动）
    auto *layout = contentLayout;

    auto *modeContent = new QWidget;
    auto *modeLayout = new QHBoxLayout(modeContent);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    modeLayout->setSpacing(10);
    form.modeNew = new QRadioButton(U("新版本"));
    form.version = new QLineEdit;
    form.version->setMaximumWidth(120);
    form.modeIterate = new QRadioButton(U("当前版本流水号迭代"));
    modeLayout->addWidget(form.modeNew);
    modeLayout->addWidget(form.version);
    modeLayout->addSpacing(24);
    modeLayout->addWidget(form.modeIterate);
    modeLayout->addStretch();
    layout->addWidget(makeFormRow(U("模式"), modeContent));

    form.body = new QPlainTextEdit;
    form.body->setFixedHeight(104);
    form.body->setMinimumWidth(520);
    form.body->setPlaceholderText(U("填写本次版本的更新内容"));
    layout->addWidget(makeFormRow(U("更新内容"), form.body));

    form.writePreview = new QLabel(QStringLiteral("—"));
    form.writePreview->setWordWrap(true);
    form.writePreview->setMinimumWidth(520);
    form.writePreview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form.writePreview->setStyleSheet(QStringLiteral("font-family:Consolas; color:#1F2937;"));
    layout->addWidget(makeFormRow(U("将写入"), form.writePreview));

    form.tagPreview = new QLabel(QStringLiteral("—"));
    form.tagPreview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form.tagPreview->setStyleSheet(
        QStringLiteral("font-family:Consolas; color:#1D4ED8; font-weight:600;"));
    layout->addWidget(makeFormRow(U("tag"), form.tagPreview));

    form.hint = new QLabel;
    form.hint->setWordWrap(true);
    form.hint->setObjectName(QStringLiteral("hint"));
    form.hint->setVisible(false);
    layout->addWidget(form.hint);

    auto *actions = new QHBoxLayout;
    form.publish = new QPushButton(U("发布"));
    form.publish->setObjectName(QStringLiteral("primary"));
    form.publish->setMinimumWidth(96);
    form.publish->setCursor(Qt::PointingHandCursor);
    actions->addWidget(form.publish);
    actions->addSpacing(12);
    form.push = new QCheckBox(U("自动推送到远端（develop 和 tag）"));
    actions->addWidget(form.push);
    actions->addStretch();
    layout->addLayout(actions);

    // 整页在 tab 内滚动；白卡只包到发布按钮，下面露出页面灰底
    auto *page = new QWidget;
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);
    pageLayout->addWidget(makeCard(state.name, QString(), content));
    pageLayout->addStretch();

    m_subForms.append(form);
    SubForm &f = m_subForms.last();

    connect(f.modeNew, &QRadioButton::clicked, this,
            [this, index]() { switchSubMode(index, true); });
    connect(f.modeIterate, &QRadioButton::clicked, this,
            [this, index]() { switchSubMode(index, false); });
    connect(f.version, &QLineEdit::textChanged, this, [this, index](const QString &) {
        if (!m_subForms.at(index).updating) updateSubRelease(index);
    });
    connect(f.body, &QPlainTextEdit::textChanged, this, [this, index]() {
        if (!m_subForms.at(index).updating) updateSubRelease(index);
    });
    connect(f.publish, &QPushButton::clicked, this,
            [this, index]() { publishSubRepo(index); });

    resetSubForm(index);
    return makeScrollPage(page);
}

void MainWindow::updateAllSubReleases()
{
    for (int i = 0; i < m_subForms.size(); ++i) updateSubRelease(i);
}

void MainWindow::resetSubForm(int index)
{
    if (index < 0 || index >= m_subForms.size()) return;
    SubForm &f = m_subForms[index];
    const RepoState &state = m_repos.at(index);

    QFile file(QDir(m_root).filePath(state.path + QStringLiteral("/VERSION.txt")));
    QString text;
    if (file.open(QIODevice::ReadOnly)) text = QString::fromUtf8(file.readAll());
    const VersionFileInfo info = parseVersionFile(text);
    const QString latest = info.versions.isEmpty() ? QString() : info.versions.first();
    const bool tagged = !latest.isEmpty()
                        && !tagsForVersion(mergedTags(state.tagNames, state.remoteTagNames),
                                           state.tagPrefix, latest).isEmpty();

    f.updating = true;
    f.baseVersion = latest;
    f.baseBody = info.firstBody;
    f.baseTagged = tagged;
    f.iteratePossible = tagged;
    f.iterateMode = false;
    f.newDraftReady = true;
    f.newVersion = tagged ? bumpPatch(latest) : latest;
    f.newBody = (f.newVersion == latest) ? info.firstBody : QString();
    f.iterateDraftReady = true;
    f.iterateBody = info.firstBody;
    f.modeNew->setChecked(true);
    f.modeIterate->setChecked(false);
    f.version->setEnabled(true);
    f.version->setText(f.newVersion);
    f.body->setPlainText(f.newBody);
    f.updating = false;
    updateSubRelease(index);
}

void MainWindow::switchSubMode(int index, bool toNew)
{
    if (index < 0 || index >= m_subForms.size()) return;
    SubForm &f = m_subForms[index];
    if (f.updating) return;
    if (f.iterateMode == !toNew) return;

    if (f.iterateMode) {
        f.iterateBody = normalizeBody(f.body->toPlainText());
        f.iterateDraftReady = true;
    } else {
        f.newVersion = f.version->text().trimmed();
        f.newBody = normalizeBody(f.body->toPlainText());
        f.newDraftReady = true;
    }

    f.updating = true;
    f.iterateMode = toNew ? false : true;
    if (toNew) {
        if (!f.newDraftReady) {
            f.newVersion = f.baseTagged ? bumpPatch(f.baseVersion) : f.baseVersion;
            f.newBody = (f.newVersion == f.baseVersion) ? f.baseBody : QString();
            f.newDraftReady = true;
        }
        f.modeNew->setChecked(true);
        f.version->setEnabled(true);
        f.version->setText(f.newVersion);
        f.body->setPlainText(f.newBody);
    } else {
        if (!f.iterateDraftReady) {
            f.iterateBody = f.baseBody;
            f.iterateDraftReady = true;
        }
        f.modeIterate->setChecked(true);
        f.version->setEnabled(false);
        f.version->setText(f.baseVersion);
        f.body->setPlainText(f.iterateBody);
    }
    f.updating = false;
    updateSubRelease(index);
}

void MainWindow::updateSubRelease(int index)
{
    if (index < 0 || index >= m_subForms.size()) return;
    SubForm &f = m_subForms[index];
    if (f.updating) return;
    const RepoState &state = m_repos.at(index);

    // Script-blocking states are red here, not the overview's orange.
    QString branchText = state.branch;
    QString branchColor = QStringLiteral("#1F2937");
    if (!state.statusOk) {
        branchText = U("读取失败");
        branchColor = QStringLiteral("#DC2626");
    } else if (state.branch == QStringLiteral("(detached)")) {
        branchText = U("detached");
        branchColor = QStringLiteral("#DC2626");
    } else if (state.branch.isEmpty()) {
        branchText = U("—");
        branchColor = QStringLiteral("#6B7280");
    } else if (state.branch == QStringLiteral("develop")) {
        branchColor = QStringLiteral("#16A34A");
    } else {
        branchColor = QStringLiteral("#DC2626");
    }
    setColored(f.branch, branchText, branchColor);

    QString worktree;
    QString worktreeColor;
    QString worktreeTip;
    worktreeText(state, &worktree, &worktreeColor, &worktreeTip);
    if (!state.statusOk || state.conflict || state.dirty > 0) {
        worktreeColor = QStringLiteral("#DC2626");
    }
    setColored(f.worktree, worktree, worktreeColor);
    f.worktree->setToolTip(worktreeTip);

    QFile file(QDir(m_root).filePath(state.path + QStringLiteral("/VERSION.txt")));
    QString text;
    if (file.open(QIODevice::ReadOnly)) text = QString::fromUtf8(file.readAll());
    const VersionFileInfo info = parseVersionFile(text);
    const QString latest = info.versions.isEmpty() ? QString() : info.versions.first();

    if (latest != f.baseVersion) {
        resetSubForm(index);
        return;
    }

    setColored(f.latestVersion, latest.isEmpty() ? U("—") : latest,
               latest.isEmpty() ? QStringLiteral("#DC2626") : QStringLiteral("#1F2937"));

    const QStringList allTags = mergedTags(state.tagNames, state.remoteTagNames);
    const bool taggedLatest =
        !latest.isEmpty() && !tagsForVersion(allTags, state.tagPrefix, latest).isEmpty();
    // Tag state arrives with the async refresh, after the form was first initialised.
    // Re-seed the form when it changes so 2.0.0 -> 2.0.1 actually happens.
    if (taggedLatest != f.baseTagged) {
        resetSubForm(index);
        return;
    }
    f.iteratePossible = taggedLatest;

    const bool iterate = f.iterateMode;
    const QString version = f.version->text().trimmed();
    const QString body = normalizeBody(f.body->toPlainText());

    QString error;
    if (!state.statusOk) {
        error = U("读不到 git 状态");
    } else if (state.conflict) {
        error = U("工作区有冲突");
    } else if (state.branch != QStringLiteral("develop")) {
        error = U("先切回 develop");
    } else if (latest.isEmpty()) {
        error = U("VERSION.txt 里没有版本块");
    } else if (!isValidVersion(version)) {
        error = U("版本号写成 X.Y.Z");
    }

    if (error.isEmpty()) {
        const int tagged = tagsForVersion(allTags, state.tagPrefix, version).size();
        if (iterate) {
            if (version != latest) error = U("同版本迭代只能针对最新版本");
            else if (tagged == 0) error = U("该版本还没有 tag，不能迭代");
        } else if (version != latest && versionGreater(latest, version)) {
            error = U("新版本不能低于最新版本");
        } else if (tagged > 0) {
            error = U("该版本已经有 tag");
        } else if (version != latest && info.versions.contains(version)) {
            error = U("该版本在 VERSION.txt 里已存在");
        }
    }
    QString tagName;
    if (error.isEmpty()) {
        const int count = tagsForVersion(allTags, state.tagPrefix, version).size();
        const int nn = iterate ? count + 1 : 1;
        tagName = makeTag(state.tagPrefix, version,
                          QStringLiteral("%1").arg(nn, 2, 10, QLatin1Char('0')));
    }
    if (error.isEmpty() && body.isEmpty()) error = U("更新内容不能为空");
    if (error.isEmpty() && bodyHasBoundary(body)) error = U("更新内容不能有以 >>>>> 开头的行");

    QString preview;
    if (error.isEmpty()) {
        const bool needWrite = (version != latest) || (body != normalizeBody(info.firstBody));
        preview = needWrite ? QStringLiteral(">>>>> ") + version + QLatin1Char('\n') + body
                            : U("与当前文件一致，无需写入");
    }

    const QString unpushed = state.remoteTagsOk
                                 ? newestUnpushedTag(state.tagNames, state.remoteTagNames,
                                                     state.tagPrefix)
                                 : QString();

    QString hint;
    bool hintError = false;
    if (!unpushed.isEmpty()) {
        f.action = SubActionPush;
        f.pushTag = unpushed;
        hint = U("本地有未推送的 tag：") + unpushed;
        f.tagPreview->setText(unpushed);
    } else {
        f.pushTag.clear();
        f.action = error.isEmpty() ? SubActionTag : SubActionNone;
        f.tagPreview->setText(tagName.isEmpty() ? QStringLiteral("—") : tagName);
        if (!error.isEmpty()) {
            hint = error;
            hintError = true;
        } else if (!state.remoteTagsOk) {
            hint = U("远端 tag 未确认，发布前会再检查一次");
        }
    }

    QString problem;
    if (f.action == SubActionPush) {
        if (!state.initialized) problem = U("未初始化");
    } else {
        problem = repoStateProblem(state);
    }
    if (problem.isEmpty()) {
        if (!m_remoteChecked) problem = U("远端未同步，先点刷新");
        else if (!state.fetchOk) problem = U("远端未确认，点刷新重试");
    }
    f.blocked = !problem.isEmpty();
    f.blockReason = problem;
    if (f.blocked) {
        hint = problem;
        hintError = true;
    } else if (state.dirty > 0 && hint.isEmpty()) {
        hint = U("有未提交改动（不影响发布）");
    }

    f.writePreview->setText(preview.isEmpty() ? QStringLiteral("—") : preview);
    f.hint->setText(hint);
    f.hint->setStyleSheet(hintError ? QStringLiteral("color:#DC2626;") : QString());
    f.hint->setVisible(!hint.isEmpty());
    updateUpdateButtons();
}

void MainWindow::publishSubRepo(int index)
{
    if (m_scriptRunning || index < 0 || index >= m_subForms.size()) return;
    SubForm &f = m_subForms[index];
    const RepoState &state = m_repos.at(index);
    if (f.action == SubActionNone) return;

    QString problem;
    if (f.action == SubActionPush) {
        if (!state.initialized) problem = U("未初始化");
    } else {
        problem = repoStateProblem(state);
    }
    if (problem.isEmpty()) {
        if (!m_remoteChecked) problem = U("远端未同步，先点刷新");
        else if (!state.fetchOk) problem = U("远端未确认，点刷新重试");
    }
    if (!problem.isEmpty()) {
        QMessageBox::warning(this, U("无法发布"), problem);
        return;
    }

    if (f.action == SubActionPush) {
        if (f.pushTag.isEmpty()) return;
        QStringList args;
        args << QStringLiteral("-C") << state.path << QStringLiteral("push")
             << QStringLiteral("--atomic")
             << QStringLiteral("origin") << QStringLiteral("develop")
             << QStringLiteral("refs/tags/") + f.pushTag;
        runScript(QStringLiteral("git"), args, U("推送 ") + state.name, f.publish,
                  U("推送中…"), U("推送"));
        return;
    }

    const bool iterate = f.iterateMode;
    const QString version = f.version->text().trimmed();
    const QString body = normalizeBody(f.body->toPlainText());
    const int count = tagsForVersion(mergedTags(state.tagNames, state.remoteTagNames),
                                     state.tagPrefix, version).size();
    const int nn = iterate ? count + 1 : 1;
    const QString tag = makeTag(state.tagPrefix, version,
                                QStringLiteral("%1").arg(nn, 2, 10, QLatin1Char('0')));
    const bool needWrite = (version != f.baseVersion)
                           || (body != normalizeBody(f.baseBody));

    QDialog dialog(this);
    dialog.setWindowTitle(U("确认发布"));
    dialog.resize(560, 440);
    auto *dialogLayout = new QVBoxLayout(&dialog);
    dialogLayout->setContentsMargins(18, 16, 18, 14);
    dialogLayout->setSpacing(8);

    dialogLayout->addWidget(labelTitle(state.name + QStringLiteral("  ·  ") + version));
    dialogLayout->addWidget(makeFormRow(U("仓库"), new QLabel(state.path)));
    dialogLayout->addWidget(makeFormRow(U("版本"), new QLabel(version)));
    dialogLayout->addWidget(makeFormRow(U("tag"), new QLabel(tag)));
    dialogLayout->addWidget(new QLabel(U("更新内容")));
    auto *bodyText = new QPlainTextEdit;
    bodyText->setReadOnly(true);
    bodyText->setPlainText(body);
    dialogLayout->addWidget(bodyText, 1);
    if (!needWrite) {
        dialogLayout->addWidget(labelHint(U("VERSION.txt 与当前内容一致，不会产生新提交。")));
    }

    auto *actions = new QHBoxLayout;
    auto *cancel = makeSecondary(U("取消"));
    auto *confirm = new QPushButton(U("发布"));
    confirm->setObjectName(QStringLiteral("primary"));
    confirm->setMinimumWidth(96);
    actions->addStretch();
    actions->addWidget(cancel);
    actions->addWidget(confirm);
    dialogLayout->addLayout(actions);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(confirm, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted) return;

    if (needWrite) {
        QString error;
        if (!writeSubVersionFile(state.path, version, body, &error)) {
            QMessageBox::warning(this, U("写入失败"), error);
            return;
        }
        if (!commitSubVersion(state.path, version, &error)) {
            QMessageBox::warning(this, U("提交失败"), error);
            return;
        }
    }

    // 子仓没有发布脚本：VERSION.txt 写完并提交后，这里直接打 annotated tag。
    // tag 说明用更新内容；版本事实来源仍然是子仓的 VERSION.txt。
    const GitRun tagged = runGit(m_root, state.path,
                                 {QStringLiteral("tag"), QStringLiteral("-a"), tag,
                                  QStringLiteral("-m"), body.isEmpty() ? version : body},
                                 30000);
    if (!tagged.ok) {
        const QString detail = tagged.err.isEmpty() ? tagged.out.trimmed() : tagged.err;
        QMessageBox::warning(this, U("打 tag 失败"), detail.isEmpty() ? tag : detail);
        return;
    }
    appendLog(U("[UI] 已打 tag：") + state.path + QLatin1Char(' ') + tag);

    if (!f.push->isChecked()) {
        appendLog(U("[UI] tag 只在本地，别忘了推送：git -C ") + state.path
                  + U(" push origin develop refs/tags/") + tag);
        refreshStatus(true, false);
        return;
    }

    QStringList args;
    args << QStringLiteral("-C") << state.path << QStringLiteral("push")
         << QStringLiteral("--atomic")
         << QStringLiteral("origin") << QStringLiteral("develop")
         << QStringLiteral("refs/tags/") + tag;
    runScript(QStringLiteral("git"), args, U("发布 ") + state.name, f.publish,
              U("发布中…"), U("发布"));
}

bool MainWindow::writeSubVersionFile(const QString &repoPath, const QString &version,
                                     const QString &body, QString *error)
{
    const QString path = QDir(m_root).filePath(repoPath + QStringLiteral("/VERSION.txt"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = U("读不到 ") + path;
        return false;
    }
    const QString text = QString::fromUtf8(file.readAll());
    file.close();

    const VersionFileInfo info = parseVersionFile(text);
    if (!info.ok || info.versions.isEmpty()) {
        if (error) *error = U("VERSION.txt 里没有版本块");
        return false;
    }

    const QString latest = info.versions.first();
    const QString nl = text.contains(QStringLiteral("\r\n")) ? QStringLiteral("\r\n")
                                                             : QStringLiteral("\n");
    QString bodyText = body;
    if (nl == QStringLiteral("\r\n")) bodyText.replace(QLatin1Char('\n'), nl);

    QString out;
    if (version == latest) {
        // Rewrite the top block; keep everything from the second block down.
        QString head = text.left(info.firstBoundaryLineEnd);
        if (!head.endsWith(QLatin1Char('\n'))) head += nl;
        const QString tail = text.mid(info.secondBoundaryStart);
        out = head + bodyText + nl;
        if (!tail.isEmpty()) out += nl + tail;
    } else {
        // New version goes above the existing blocks.
        QString existing = text;
        QString bom;
        if (existing.startsWith(QChar(0xFEFF))) {
            bom = QChar(0xFEFF);
            existing.remove(0, 1);
        }
        out = bom + QStringLiteral(">>>>> ") + version + nl + bodyText + nl + nl + existing;
    }

    QSaveFile save(path);
    if (!save.open(QIODevice::WriteOnly)) {
        if (error) *error = U("写不了 ") + path;
        return false;
    }
    save.write(out.toUtf8());
    if (!save.commit()) {
        if (error) *error = U("写入失败 ") + path;
        return false;
    }

    QFile check(path);
    if (!check.open(QIODevice::ReadOnly)) {
        if (error) *error = U("写入后读不回来");
        return false;
    }
    const VersionFileInfo after = parseVersionFile(QString::fromUtf8(check.readAll()));
    check.close();
    if (after.versions.isEmpty() || after.versions.first() != version
        || normalizeBody(after.firstBody) != normalizeBody(body)) {
        if (error) *error = U("写入后校验失败");
        return false;
    }
    return true;
}

bool MainWindow::commitSubVersion(const QString &repoPath, const QString &version,
                                  QString *error)
{
    QProcess add;
    add.setWorkingDirectory(m_root);
    add.start(QStringLiteral("git"),
              QStringList() << QStringLiteral("-C") << repoPath << QStringLiteral("add")
                            << QStringLiteral("--") << QStringLiteral("VERSION.txt"));
    if (!add.waitForFinished(30000)) {
        add.kill();
        if (error) *error = U("git add 超时");
        return false;
    }
    if (add.exitStatus() != QProcess::NormalExit || add.exitCode() != 0) {
        const QString out = QString::fromUtf8(add.readAllStandardError()).trimmed();
        if (error) *error = out.isEmpty() ? U("git add 失败") : out;
        return false;
    }

    QProcess commit;
    commit.setWorkingDirectory(m_root);
    commit.start(QStringLiteral("git"),
                 QStringList() << QStringLiteral("-C") << repoPath
                               << QStringLiteral("commit") << QStringLiteral("-m")
                               << (QStringLiteral("release: ") + version)
                               << QStringLiteral("--") << QStringLiteral("VERSION.txt"));
    if (!commit.waitForFinished(30000)) {
        commit.kill();
        if (error) *error = U("git commit 超时");
        return false;
    }
    if (commit.exitStatus() != QProcess::NormalExit || commit.exitCode() != 0) {
        QString out = QString::fromUtf8(commit.readAllStandardError()).trimmed();
        if (out.isEmpty()) out = QString::fromUtf8(commit.readAllStandardOutput()).trimmed();
        if (error) *error = out.isEmpty() ? U("git commit 失败") : out;
        return false;
    }
    appendLog(U("[UI] 已提交 VERSION.txt：") + repoPath + QLatin1Char(' ') + version);
    return true;
}