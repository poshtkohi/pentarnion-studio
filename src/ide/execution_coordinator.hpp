#pragma once

#include "runtime/interpreter.hpp"

#include <QByteArray>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace penta {

enum class WorkKind { CheckFile, CheckProject, RunFile, RunCommand, ResetWorkspace };

struct ImportedSourceSnapshot {
    QString path;
    QByteArray diskHash;
    QByteArray editorHash;
};

struct SourceSnapshot {
    quint64 documentId = 0; // Zero denotes a source not open in an editor.
    quint64 revision = 0;
    QString path;
    QByteArray source;
    QByteArray diskHash;
    QString projectPath;
    QString algebraId = QStringLiteral("A_gamma_v1");
    QString languageVersion = QStringLiteral("0.2");
    double gamma = 1.0;
    bool command = false;
    QVector<ImportedSourceSnapshot> imports;
    std::shared_ptr<const std::string> sourceOwner{}; // Lifetime for zero-copy retained callee text.
    bool retainedFrame=false;
};

struct FileWorkResult {
    SourceSnapshot snapshot;
    language::AnalysisResult analysis;
    std::optional<runtime::ExecutionResult> execution;
};

struct WorkResult {
    quint64 requestId = 0;
    WorkKind kind = WorkKind::CheckFile;
    QVector<FileWorkResult> files;
    bool cancelled = false;
    QString internalError;
};
struct FigureProgress {
    quint64 requestId=0;
    SourceSnapshot snapshot;
    std::vector<runtime::FigureAction> actions;
};

// Exactly one worker and one replaceable pending request. Inputs and outputs
// contain values only; the worker never reads documents or touches widgets.
class ExecutionCoordinator final : public QObject {
    Q_OBJECT
public:
    explicit ExecutionCoordinator(QObject* parent = nullptr);
    ~ExecutionCoordinator() override;
    quint64 submit(WorkKind kind, QVector<SourceSnapshot> sources);
    quint64 resetWorkspace();
    void cancel();

signals:
    void finished(const penta::WorkResult& result);
    void figureProgress(const penta::FigureProgress& result);

private:
    struct Request {
        quint64 id = 0;
        quint64 generation = 0;
        WorkKind kind = WorkKind::CheckFile;
        QVector<SourceSnapshot> sources;
        std::shared_ptr<std::atomic_bool> cancellation;
    };
    void workerLoop(std::stop_token stop);
    std::mutex m_mutex;
    std::condition_variable_any m_condition;
    std::optional<Request> m_pending;
    std::shared_ptr<std::atomic_bool> m_activeCancellation;
    quint64 m_nextRequest = 0;
    quint64 m_generation = 0;
    std::jthread m_worker;
};

} // namespace penta

Q_DECLARE_METATYPE(penta::SourceSnapshot)
Q_DECLARE_METATYPE(penta::WorkResult)
Q_DECLARE_METATYPE(penta::FigureProgress)
