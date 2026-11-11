#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

struct RefreshJob
{
    QString key;
    QString kind;
    QString path;
    QStringList args;
    int phase = 1;       // 1 = normal; 0 = runs first (barrier phase, e.g. fetch)
    int timeoutMs = 6000;
    QString program = QStringLiteral("git");  // "cmd" for helper scripts
};

struct RefreshResult
{
    bool ok = false;
    QString out;
    QString err;
};

// A typedef avoids the comma inside Q_DECLARE_METATYPE(...).
using RefreshResultMap = QHash<QString, RefreshResult>;

Q_DECLARE_METATYPE(RefreshJob)
Q_DECLARE_METATYPE(QVector<RefreshJob>)
Q_DECLARE_METATYPE(RefreshResult)
Q_DECLARE_METATYPE(RefreshResultMap)

class QProcess;

// Runs the overview's git commands in a worker thread. The UI thread never creates a
// QProcess for the periodic refresh and never handles its output callbacks.
class RefreshWorker : public QObject
{
    Q_OBJECT

public:
    explicit RefreshWorker(QObject *parent = nullptr);

public slots:
    void start(int generation, const QVector<RefreshJob> &jobs, const QString &root,
               int maxConcurrent);
    void stop();

signals:
    void finished(int generation, const RefreshResultMap &results);

private:
    void pump();
    void onProcessFinished(QProcess *process);

    QVector<QProcess *> m_processes;
    QVector<RefreshJob> m_queue;
    RefreshResultMap m_results;
    QString m_root;
    int m_generation = 0;
    int m_active = 0;
    int m_maxConcurrent = 4;
};
