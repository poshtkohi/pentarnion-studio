#include "ide/assistant/source_review.hpp"
#include "language/frontend.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <map>
#include <filesystem>
namespace penta::assistant {
namespace {
QString pathText(const std::filesystem::path& path){return QString::fromStdU16String(path.u16string());}
std::filesystem::path nativePath(const QString& path){return std::filesystem::path(path.toStdU16String());}
bool stamp(const QString& path,SourceDependency* result){
    QFile file(path);const QFileInfo before(path);if(!before.isFile()||before.size()>4*1024*1024||!file.open(QIODevice::ReadOnly))return false;
    const auto data=file.read(4*1024*1024+1);const QFileInfo after(path);if(data.size()>4*1024*1024||before.size()!=after.size()||before.lastModified()!=after.lastModified())return false;
    *result={after.canonicalFilePath(),QCryptographicHash::hash(data,QCryptographicHash::Sha256),after.size(),after.lastModified(),after.birthTime()};return true;
}
}
bool sourceSnapshotMatches(const SourceSnapshot& expected,const SourceSnapshot& current){
    return expected.valid()&&current.valid()&&expected.identity==current.identity&&expected.path==current.path&&expected.projectRoot==current.projectRoot&&expected.text==current.text&&expected.algebraId==current.algebraId&&expected.gamma==current.gamma&&expected.languageVersion==current.languageVersion;
}
bool sourceDependenciesMatch(const SourceValidation& validation,bool hashContents,QString* error){
    for(const auto& expected:validation.dependencies){
        const QFileInfo file(expected.path);if(!file.isFile()||file.canonicalFilePath()!=expected.path||file.size()!=expected.bytes||file.lastModified()!=expected.modified||file.birthTime()!=expected.created){if(error)*error=QStringLiteral("An imported source changed; validate the proposal again.");return false;}
        if(hashContents){SourceDependency actual;if(!stamp(expected.path,&actual)||actual.hash!=expected.hash){if(error)*error=QStringLiteral("An imported source changed; validate the proposal again.");return false;}}
    }
    return true;
}
SourceValidation validateSourceSuggestion(const SourceSnapshot& snapshot,QString candidate,const std::shared_ptr<std::atomic_bool>& cancellation){
    SourceValidation result;result.candidate=std::move(candidate);
    if(!snapshot.valid()){result.message=QStringLiteral("Select a source document before applying a suggestion.");return result;}
    const auto original=snapshot.text.toUtf8(),bytes=result.candidate.toUtf8();
    if(original.size()>4*1024*1024||bytes.size()>4*1024*1024){result.message=QStringLiteral("Source exceeds the 4 MiB review limit.");return result;}
    try {
        language::AnalysisOptions options;options.algebraId=snapshot.algebraId.toStdString();options.gamma=snapshot.gamma;options.languageVersion=snapshot.languageVersion.toStdString();options.cancellation=cancellation;options.maximumDiagnostics=32;
        options.sourceDirectory=nativePath(snapshot.path.isEmpty()?snapshot.projectRoot:QFileInfo(snapshot.path).absolutePath());options.sourceIdentity=snapshot.path.toStdString();
        const auto baseline=language::analyse(std::string_view(original.constData(),static_cast<std::size_t>(original.size())),options);
        if(baseline.cancelled||(cancellation&&cancellation->load())){result.message=QStringLiteral("Validation cancelled.");return result;}
        std::map<QString,language::ModuleSource> permitted;
        for(const auto& dependency:baseline.dependencies){
            if(!dependency.source)continue;
            const auto path=QFileInfo(QString::fromStdString(dependency.identity)).canonicalFilePath();
            SourceDependency identity;if(path.isEmpty()||!stamp(path,&identity)||identity.hash!=QCryptographicHash::hash(QByteArray::fromStdString(*dependency.source),QCryptographicHash::Sha256)){result.message=QStringLiteral("An original import changed while capturing validation context.");return result;}
            permitted.emplace(path,language::ModuleSource{dependency.identity,*dependency.source});result.dependencies.append(identity);
        }
        options.importBudget=std::make_shared<language::ImportBudget>();
        options.moduleResolver=[permitted=std::move(permitted)](const std::string& requested,const std::filesystem::path& directory)->std::optional<language::ModuleSource>{
            auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(requested.data()),requested.size()));if(path.is_relative())path=directory/path;
            const auto identity=QFileInfo(pathText(path)).canonicalFilePath();const auto found=permitted.find(identity);if(found==permitted.end())return std::nullopt;return found->second;
        };
        const auto analysis=language::analyse(std::string_view(bytes.constData(),static_cast<std::size_t>(bytes.size())),options);
        if(analysis.cancelled||(cancellation&&cancellation->load())){result.message=QStringLiteral("Validation cancelled.");return result;}
        if(!analysis.ok()){
            QStringList errors;for(const auto& diagnostic:analysis.diagnostics){const auto location=language::sourceLocation(std::string_view(bytes.constData(),static_cast<std::size_t>(bytes.size())),diagnostic.span.offset);errors.append(QStringLiteral("%1:%2 · %3: %4").arg(location.line).arg(location.column).arg(QString::fromStdString(diagnostic.code),QString::fromStdString(diagnostic.message)));if(errors.size()==8)break;}
            result.message=errors.join(QLatin1Char('\n'));return result;
        }
        if(!sourceDependenciesMatch(result,true,&result.message))return result;
        result.valid=true;result.message=QStringLiteral("Static syntax/type validation passed. No code was executed. Review numerical and mathematical assumptions before Run.");
    }catch(const std::exception& error){result.message=QStringLiteral("Validation failed: ")+QString::fromUtf8(error.what()).left(2048);}
    return result;
}
}
