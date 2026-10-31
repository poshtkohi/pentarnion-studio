#include "execution_coordinator.hpp"

#include <QMetaObject>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QCryptographicHash>

#include <exception>
#include <utility>

namespace penta {

ExecutionCoordinator::ExecutionCoordinator(QObject* parent)
    : QObject(parent), m_worker([this](std::stop_token stop) { workerLoop(stop); }) {}

ExecutionCoordinator::~ExecutionCoordinator() {
    cancel();
    m_worker.request_stop();
    m_condition.notify_all();
    // Core analysis/evaluation are cancellable and resource bounded. Joining
    // prevents any callback from surviving its owning window.
    if (m_worker.joinable()) m_worker.join();
}

quint64 ExecutionCoordinator::submit(WorkKind kind, QVector<SourceSnapshot> sources) {
    std::lock_guard lock(m_mutex);
    if (m_activeCancellation) m_activeCancellation->store(true);
    if (m_pending) m_pending->cancellation->store(true);
    Request request;
    request.id = ++m_nextRequest;
    request.generation = m_generation;
    request.kind = kind;
    request.sources = std::move(sources);
    request.cancellation = std::make_shared<std::atomic_bool>(false);
    const quint64 id = request.id;
    m_pending = std::move(request);
    m_condition.notify_all();
    return id;
}

quint64 ExecutionCoordinator::resetWorkspace() {
    std::lock_guard lock(m_mutex);
    if (m_activeCancellation) m_activeCancellation->store(true);
    if (m_pending) m_pending->cancellation->store(true);
    Request request;
    request.id = ++m_nextRequest;
    request.generation = ++m_generation;
    request.kind = WorkKind::ResetWorkspace;
    request.cancellation = std::make_shared<std::atomic_bool>(false);
    const quint64 id = request.id;
    m_pending = std::move(request);
    m_condition.notify_all();
    return id;
}

void ExecutionCoordinator::cancel() {
    std::lock_guard lock(m_mutex);
    if (m_activeCancellation) m_activeCancellation->store(true);
    if (m_pending) m_pending->cancellation->store(true);
    m_condition.notify_all();
}

void ExecutionCoordinator::workerLoop(std::stop_token stop) {
    runtime::InterpreterSession session;
    quint64 sessionGeneration = 0;
    while (!stop.stop_requested()) {
        Request request;
        {
            std::unique_lock lock(m_mutex);
            if (!m_condition.wait(lock, stop, [this] { return m_pending.has_value(); })) return;
            if (stop.stop_requested()) return;
            request = std::move(*m_pending);
            m_pending.reset();
            m_activeCancellation = request.cancellation;
        }
        WorkResult result;
        result.requestId = request.id;
        result.kind = request.kind;
        try {
            if (request.generation != sessionGeneration || request.kind == WorkKind::ResetWorkspace) {
                session.reset();
                sessionGeneration = request.generation;
            }
            for (auto& source : request.sources) {
                if (request.cancellation->load() || stop.stop_requested()) { result.cancelled = true; break; }
                language::AnalysisOptions options;
                options.algebraId = source.algebraId.toStdString();
                options.languageVersion = source.languageVersion.toStdString();
                options.gamma = source.gamma;
                options.cancellation = request.cancellation;
                FileWorkResult file;
                file.snapshot = std::move(source);
                options.sourceIdentity=file.snapshot.path.toUtf8().toStdString();
                const QString sourceDirectory=!file.snapshot.command&&!file.snapshot.path.isEmpty()
                    ? QFileInfo(file.snapshot.path).absolutePath()
                    : !file.snapshot.projectPath.isEmpty()?QFileInfo(file.snapshot.projectPath).absolutePath()
                    : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
#ifdef _WIN32
                options.sourceDirectory=std::filesystem::path(sourceDirectory.toStdWString());
#else
                options.sourceDirectory=std::filesystem::path(sourceDirectory.toUtf8().constData());
#endif
                const std::string_view text(file.snapshot.source.constData(),
                                            static_cast<std::size_t>(file.snapshot.source.size()));
                if (request.kind == WorkKind::RunFile || request.kind == WorkKind::RunCommand) {
                    runtime::ExecutionOptions execution;
                    execution.cancellation = request.cancellation;
                    std::vector<runtime::FigureAction> pendingFigures;
                    std::size_t pendingPoints=0;
                    auto lastPublish=std::chrono::steady_clock::time_point{};
                    auto publishFigures=[&]{
                        if(pendingFigures.empty())return;
                        FigureProgress progress;progress.requestId=request.id;progress.snapshot=file.snapshot;progress.actions=std::move(pendingFigures);
                        pendingFigures.clear();pendingPoints=0;lastPublish=std::chrono::steady_clock::now();
                        QMetaObject::invokeMethod(this,[this,progress=std::move(progress)]{emit figureProgress(progress);},Qt::QueuedConnection);
                    };
                    execution.figureProgress=[&](const std::vector<runtime::FigureAction>& actions){
                        for(const auto& action:actions){for(const auto& curve:action.curves)pendingPoints+=curve.points.size();if(action.mesh)pendingPoints+=action.mesh->points.size();}
                        pendingFigures.insert(pendingFigures.end(),actions.begin(),actions.end());
                        if(pendingFigures.size()>=128||pendingPoints>=200000||std::chrono::steady_clock::now()-lastPublish>=std::chrono::milliseconds(100))publishFigures();
                    };
                    // Native artifacts are owned by the project/source location,
                    // never by the process working directory or saved .p text.
                    const QString cacheBase = !file.snapshot.projectPath.isEmpty()
                        ? QFileInfo(file.snapshot.projectPath).absolutePath()
                        : !file.snapshot.command&&!file.snapshot.path.isEmpty()
                            ? QFileInfo(file.snapshot.path).absolutePath()
                            : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
                    const QString cachePath = QDir(cacheBase).filePath(QStringLiteral(".penta-cache/odes"));
#ifdef _WIN32
                    execution.odeCacheDirectory = std::filesystem::path(cachePath.toStdWString());
                    execution.dataDirectory = std::filesystem::path(cacheBase.toStdWString());
#else
                    execution.odeCacheDirectory = std::filesystem::path(cachePath.toUtf8().constData());
                    execution.dataDirectory = std::filesystem::path(cacheBase.toUtf8().constData());
#endif
                    file.execution = request.kind == WorkKind::RunCommand
                        ? session.submit(text, options, execution)
                        : session.runFile(text, options, execution);
                    publishFigures();
                    file.analysis = file.execution->analysis;
                    result.cancelled = file.execution->status == runtime::ExecutionStatus::Cancelled;
                } else {
                    file.analysis = language::analyse(text, options);
                    result.cancelled = file.analysis.cancelled;
                }
                for(const auto& dependency:file.analysis.dependencies){
                    if(!dependency.source)continue;
                    ImportedSourceSnapshot imported;
                    imported.path=QString::fromUtf8(dependency.identity.data(),static_cast<qsizetype>(dependency.identity.size()));
                    const QByteArray bytes(dependency.source->data(),static_cast<qsizetype>(dependency.source->size()));
                    imported.diskHash=QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
                    QString editorText=QString::fromUtf8(bytes);
                    if(!editorText.isEmpty()&&editorText.front()==QChar(0xfeff))editorText.remove(0,1);
                    editorText.replace(QStringLiteral("\r\n"),QStringLiteral("\n"));
                    editorText.replace(QChar('\r'),QChar('\n'));
                    imported.editorHash=QCryptographicHash::hash(editorText.toUtf8(),QCryptographicHash::Sha256);
                    file.snapshot.imports.append(std::move(imported));
                }
                result.files.append(std::move(file));
                if (result.cancelled) break;
            }
            if (request.kind == WorkKind::CheckFile || request.kind == WorkKind::CheckProject || result.files.isEmpty())
                result.cancelled = result.cancelled || request.cancellation->load();
        } catch (const std::exception& error) {
            result.internalError = QString::fromUtf8(error.what());
        } catch (...) {
            result.internalError = QStringLiteral("Unexpected worker failure.");
        }
        {
            std::lock_guard lock(m_mutex);
            m_activeCancellation.reset();
        }
        if (!stop.stop_requested()) {
            QMetaObject::invokeMethod(this, [this, result = std::move(result)] {
                emit finished(result);
            }, Qt::QueuedConnection);
        }
    }
}

} // namespace penta
