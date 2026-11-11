#include "refreshworker.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <QVariant>

RefreshWorker::RefreshWorker(QObject *parent)
    : QObject(parent)
{
}

void RefreshWorker::start(int generation, const QVector<RefreshJob> &jobs,
                          const QString &root, int maxConcurrent)
{
    for (QProcess *process : m_processes) {
        if (!process) continue;
        process->disconnect(this);
        if (process->state() != QProcess::NotRunning) process->kill();
        process->deleteLater();
    }
    m_processes.clear();
    m_queue = jobs;
    m_results.clear();
    m_root = root;
    m_generation = generation;
    m_maxConcurrent = qMax(1, maxConcurrent);
    m_active = 0;
    pump();
}

void RefreshWorker::pump()
{
    while (m_active < m_maxConcurrent && !m_queue.isEmpty()) {
        // Run the lowest phase first (phase 0 = fetch); the rest only start once
        // every phase-0 job has finished, so tag reads see the fresh refs.
        int phase = m_queue.first().phase;
        for (const RefreshJob &job : m_queue) phase = qMin(phase, job.phase);
        int index = 0;
        while (index < m_queue.size() && m_queue.at(index).phase != phase) ++index;
        const RefreshJob job = m_queue.takeAt(index);

        auto *process = new QProcess(this);
        process->setWorkingDirectory(m_root);
        {
            // Fail fast instead of hanging on SSH prompts. Do not override
            // GIT_SSH_COMMAND: let git use its own ssh, otherwise a GUI-launched
            // process may resolve a different ssh.exe with different keys/agent.
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
            process->setProcessEnvironment(env);
        }
        process->setProperty("key", job.key);
        process->setProperty("kind", job.kind);
        process->setProperty("done", false);

        connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, process](int, QProcess::ExitStatus) { onProcessFinished(process); });
        connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
            if (process->state() == QProcess::NotRunning) onProcessFinished(process);
        });

        QTimer::singleShot(job.timeoutMs, process, [process]() {
            if (process->state() != QProcess::NotRunning) process->kill();
        });

        QStringList full;
        if (job.program == QStringLiteral("git")) {
            full << QStringLiteral("-C") << job.path << job.args;
        } else {
            full = job.args;
        }
        process->start(job.program, full);

        m_processes.append(process);
        ++m_active;
    }

    if (m_active == 0 && m_queue.isEmpty()) {
        emit finished(m_generation, m_results);
    }
}

void RefreshWorker::onProcessFinished(QProcess *process)
{
    if (!process || process->property("done").toBool()) return;
    process->setProperty("done", true);

    const QString key = process->property("key").toString();
    const QString kind = process->property("kind").toString();

    RefreshResult result;
    result.ok = process->exitStatus() == QProcess::NormalExit && process->exitCode() == 0;
    result.out = QString::fromUtf8(process->readAllStandardOutput());
    result.err = QString::fromUtf8(process->readAllStandardError());

    m_results.insert(key + QLatin1Char('/') + kind, result);
    m_processes.removeOne(process);
    process->deleteLater();
    if (m_active > 0) --m_active;

    if (m_active == 0 && m_queue.isEmpty()) emit finished(m_generation, m_results);
    else pump();
}

void RefreshWorker::stop()
{
    for (QProcess *process : m_processes) {
        if (!process) continue;
        process->disconnect(this);
        if (process->state() != QProcess::NotRunning) process->kill();
        process->deleteLater();
    }
    m_processes.clear();
    m_queue.clear();
    m_active = 0;
}
