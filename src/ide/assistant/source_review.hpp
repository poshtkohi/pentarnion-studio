#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <atomic>
#include <memory>
namespace penta::assistant {
struct SourceSnapshot {
    QString text,path,selection,projectRoot;
    QByteArray identity; // Host document/revision/project identity, never a pointer dereferenced here.
    QString algebraId=QStringLiteral("A_gamma_v1"),languageVersion=QStringLiteral("0.2");
    double gamma=1;
    int selectionStart=0,selectionEnd=0; // UTF-16 editor positions.
    bool valid()const{return !identity.isEmpty();}
};
bool sourceSnapshotMatches(const SourceSnapshot& expected,const SourceSnapshot& current);
struct SourceDependency {QString path;QByteArray hash;qint64 bytes=0;QDateTime modified,created;};
struct SourceValidation {bool valid=false;QString message,candidate;QList<SourceDependency> dependencies;};
// Worker-only checked AST analysis. No evaluation/JIT/solver is invoked. The
// candidate can reuse original imports, but cannot introduce new file reads.
SourceValidation validateSourceSuggestion(const SourceSnapshot&,QString candidate,
    const std::shared_ptr<std::atomic_bool>& cancelled={});
bool sourceDependenciesMatch(const SourceValidation&,bool hashContents,
                             QString* error=nullptr);
}
