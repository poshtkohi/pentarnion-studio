#include "project_service.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <filesystem>

using penta::Project;
using penta::ProjectService;

namespace {

QByteArray contents(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

bool put(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

QJsonObject metadata(const QString& path)
{
    return QJsonDocument::fromJson(contents(path)).object();
}

bool putMetadata(const QString& path, const QJsonObject& object)
{
    return put(path, QJsonDocument(object).toJson());
}

std::filesystem::path fsPath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toUtf8().constData());
#endif
}

} // namespace

class ProjectTests : public QObject {
    Q_OBJECT

private slots:
    void languageUpgradeIsExplicitAndPreservesSources()
    {
        QTemporaryDir temporary;
        Project project;
        QString error;
        QVERIFY(ProjectService::create(temporary.path(), "Versions", project, &error));
        QCOMPARE(project.languageVersion, QStringLiteral("0.2"));
        auto object = metadata(project.manifestPath);
        object["language_version"] = "0.1";
        object["custom_metadata"] = "preserved";
        QVERIFY(putMetadata(project.manifestPath, object));
        const QByteArray source = contents(QDir(project.rootPath).filePath("main.p"));
        const QByteArray before = contents(project.manifestPath);
        QVERIFY(ProjectService::load(project.manifestPath, project, &error));
        QCOMPARE(project.languageVersion, QStringLiteral("0.1"));
        QCOMPARE(contents(project.manifestPath), before);
        QVERIFY(ProjectService::upgradeLanguageVersion(project, "0.2", &error));
        QCOMPARE(metadata(project.manifestPath)["language_version"].toString(), QStringLiteral("0.2"));
        QCOMPARE(metadata(project.manifestPath)["custom_metadata"].toString(), QStringLiteral("preserved"));
        QCOMPARE(contents(QDir(project.rootPath).filePath("main.p")), source);
        Project reopened;
        QVERIFY(ProjectService::load(project.manifestPath, reopened, &error));
        QCOMPARE(reopened.languageVersion, QStringLiteral("0.2"));
        QVERIFY(!ProjectService::upgradeLanguageVersion(project, "0.3", &error));
    }

    void languageUpgradeRejectsExternallyModifiedMetadata()
    {
        QTemporaryDir temporary;
        Project project;
        QString error;
        QVERIFY(ProjectService::create(temporary.path(), "Conflict", project, &error));
        auto object = metadata(project.manifestPath);
        object["language_version"] = "0.1";
        QVERIFY(putMetadata(project.manifestPath, object));
        QVERIFY(ProjectService::load(project.manifestPath, project, &error));
        object["external"] = true;
        QVERIFY(putMetadata(project.manifestPath, object));
        const QByteArray before = contents(project.manifestPath);
        QVERIFY(!ProjectService::upgradeLanguageVersion(project, "0.2", &error));
        QCOMPARE(contents(project.manifestPath), before);
        QCOMPARE(project.languageVersion, QStringLiteral("0.1"));
    }

    void createNestedRenameAndReopen()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        Project project;
        QString error;
        QVERIFY2(ProjectService::create(temporary.path(), QString::fromUtf8("Research Ω space"), project, &error), qPrintable(error));
        QCOMPARE(project.name, QString::fromUtf8("Research Ω space"));
        QCOMPARE(project.sources, QStringList{"main.p"});
        QVERIFY(!project.manifestDigest.isEmpty());
        const QString text = QString::fromUtf8("% Unicode α 😀\npentarnion q = j;\n");
        QVERIFY2(ProjectService::addSource(project, "models/nested/test.p", text, &error), qPrintable(error));
        const QString oldFile = QDir(project.rootPath).filePath("models/nested/test.p");
        QCOMPARE(contents(oldFile), text.toUtf8());
        QVERIFY2(ProjectService::renameSource(project, "models/nested/test.p", "models/renamed.p", &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(oldFile));
        QCOMPARE(contents(QDir(project.rootPath).filePath("models/renamed.p")), text.toUtf8());
        Project reopened;
        QVERIFY2(ProjectService::load(project.manifestPath, reopened, &error), qPrintable(error));
        QCOMPARE(reopened.sources, QStringList({"main.p", "models/renamed.p"}));
        QCOMPARE(reopened.name, project.name);
        QCOMPARE(reopened.algebraId, QString("A_gamma_v1"));
        QCOMPARE(reopened.gamma, 1.0);
        QCOMPARE(metadata(project.manifestPath).value("schema_version").toInt(), 1);
        QVERIFY(!contents(project.manifestPath).contains(project.rootPath.toUtf8()));
    }

    void existingProjectIsNeverOverwritten()
    {
        QTemporaryDir temporary;
        Project project;
        QString error;
        QVERIFY(ProjectService::create(temporary.path(), "existing", project, &error));
        const QByteArray original = contents(project.manifestPath);
        Project sentinel;
        sentinel.name = "unchanged";
        QVERIFY(!ProjectService::create(temporary.path(), "existing", sentinel, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(sentinel.name, QString("unchanged"));
        QCOMPARE(contents(project.manifestPath), original);
        QVERIFY(!ProjectService::create(temporary.path(), "EXISTING", sentinel, &error));
        QVERIFY(QDir(temporary.path()).mkdir("empty"));
        QVERIFY(!ProjectService::create(temporary.path(), "empty", sentinel, &error));
    }

    void invalidMetadata_data()
    {
        QTest::addColumn<QString>("kind");
        for (const auto* name : {"schema", "language", "algebra", "gamma-string", "gamma-domain", "sources-string", "source-not-string", "duplicate", "missing", "traversal", "absolute", "malformed"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }

    void invalidMetadata()
    {
        QFETCH(QString, kind);
        QTemporaryDir temporary;
        Project project;
        QString error;
        QVERIFY(ProjectService::create(temporary.path(), "project", project, &error));
        auto object = metadata(project.manifestPath);
        if (kind == "schema") object["schema_version"] = 999;
        if (kind == "language") object["language_version"] = "2.0";
        if (kind == "algebra") object["algebra"] = QJsonObject{{"id", "other"}, {"gamma", 1}};
        if (kind == "gamma-string") object["algebra"] = QJsonObject{{"id", "A_gamma_v1"}, {"gamma", "1"}};
        if (kind == "gamma-domain") object["algebra"] = QJsonObject{{"id", "A_gamma_v1"}, {"gamma", 1.1}};
        if (kind == "sources-string") object["sources"] = "main.p";
        if (kind == "source-not-string") object["sources"] = QJsonArray{42};
        if (kind == "duplicate") object["sources"] = QJsonArray{"main.p", "main.p"};
        if (kind == "missing") object["sources"] = QJsonArray{"missing.p"};
        if (kind == "traversal") object["sources"] = QJsonArray{"../outside.p"};
        if (kind == "absolute") object["sources"] = QJsonArray{QDir(project.rootPath).filePath("main.p")};
        QVERIFY(putMetadata(project.manifestPath, object));
        if (kind == "malformed") QVERIFY(put(project.manifestPath, "{ not JSON"));
        const QByteArray diskBefore = contents(project.manifestPath);
        Project out = project;
        out.name = "current project";
        QVERIFY(!ProjectService::load(project.manifestPath, out, &error));
        QVERIFY2(!error.isEmpty(), qPrintable(kind));
        QCOMPARE(out.name, QString("current project"));
        QCOMPARE(contents(project.manifestPath), diskBefore);
        QVERIFY(QFileInfo::exists(QDir(project.rootPath).filePath("main.p")));
    }

    void invalidSourcePaths_data()
    {
        QTest::addColumn<QString>("relative");
        for (const auto* value : {"../escape.p", "a/../../escape.p", "/absolute.p", "C:/absolute.p", "a\\b.p", "a//b.p", "./a.p", "a/./b.p", "CON.p", "aux.p", "x/LPT9.p", "CONIN$.p", "trailing ./b.p", "x./b.p", "bad?.p", "a.txt", "a/"})
            QTest::newRow(value) << QString::fromLatin1(value);
    }

    void invalidSourcePaths()
    {
        QFETCH(QString, relative);
        QTemporaryDir temporary;
        Project project;
        QString error;
        QVERIFY(ProjectService::create(temporary.path(), "project", project, &error));
        const QByteArray manifestBefore = contents(project.manifestPath);
        QString absolute = "unchanged";
        QVERIFY(!ProjectService::resolveSource(project, relative, absolute, &error, false));
        QCOMPARE(absolute, QString("unchanged"));
        QVERIFY(!ProjectService::addSource(project, relative, "source", &error));
        QCOMPARE(contents(project.manifestPath), manifestBefore);
        QCOMPARE(project.sources, QStringList{"main.p"});
    }

    void includeExistingNeverChangesBytes()
    {
        QTemporaryDir temporary;
        Project project;
        QString error;
        QVERIFY(ProjectService::create(temporary.path(), "project", project, &error));
        const QString path = QDir(project.rootPath).filePath("existing.p");
        const QByteArray bytes = QByteArray::fromHex("efbbbf") + "% keep CRLF\r\n";
        QVERIFY(put(path, bytes));
        QVERIFY(!ProjectService::addSource(project, "existing.p", "would overwrite", &error));
        QCOMPARE(contents(path), bytes);
        QVERIFY2(ProjectService::includeSource(project, "existing.p", &error), qPrintable(error));
        QCOMPARE(contents(path), bytes);
        QVERIFY(project.sources.contains("existing.p"));
        QVERIFY(ProjectService::includeSource(project, "existing.p", &error));
        QCOMPARE(project.sources.count("existing.p"), 1);
    }

    void renameCollisionPreservesBothFiles()
    {
        QTemporaryDir temporary;
        Project project;
        QString error;
        QVERIFY(ProjectService::create(temporary.path(), "project", project, &error));
        QVERIFY(ProjectService::addSource(project, "second.p", "second content", &error));
        const QString mainPath = QDir(project.rootPath).filePath("main.p");
        const QByteArray mainBefore = contents(mainPath);
        const QByteArray manifestBefore = contents(project.manifestPath);
        QVERIFY(!ProjectService::renameSource(project, "main.p", "second.p", &error));
        QCOMPARE(contents(mainPath), mainBefore);
        QCOMPARE(contents(QDir(project.rootPath).filePath("second.p")), QByteArray("second content"));
        QCOMPARE(contents(project.manifestPath), manifestBefore);
    }

    void caseCollisionsAreRejectedOnBothPlatforms()
    {
        QTemporaryDir temporary;
        Project project;
        QString error;
        QVERIFY(ProjectService::create(temporary.path(), "project", project, &error));
        QVERIFY(!ProjectService::addSource(project, "MAIN.p", "other", &error));
        QVERIFY(ProjectService::createFolder(project, "Models", &error));
        QVERIFY(!ProjectService::addSource(project, "models/example.p", "other", &error));
        auto object = metadata(project.manifestPath);
        object["sources"] = QJsonArray{"MAIN.p"};
        QVERIFY(putMetadata(project.manifestPath, object));
        Project out;
        QVERIFY(!ProjectService::load(project.manifestPath, out, &error));
    }

    void externalManifestChangeRollsBackRename()
    {
        QTemporaryDir temporary;
        Project project;
        QString error;
        QVERIFY(ProjectService::create(temporary.path(), "project", project, &error));
        const QString oldPath = QDir(project.rootPath).filePath("main.p");
        const QByteArray original = contents(oldPath);
        auto changed = metadata(project.manifestPath);
        changed["name"] = "externally edited";
        QVERIFY(putMetadata(project.manifestPath, changed));
        const QByteArray changedBytes = contents(project.manifestPath);
        QVERIFY(!ProjectService::renameSource(project, "main.p", "new/folder/renamed.p", &error));
        QVERIFY(error.contains("changed outside"));
        QCOMPARE(contents(oldPath), original);
        QVERIFY(!QFileInfo::exists(QDir(project.rootPath).filePath("new")));
        QCOMPARE(contents(project.manifestPath), changedBytes);
        QCOMPARE(project.sources, QStringList{"main.p"});
    }

    void externalManifestChangeRollsBackNewSource()
    {
        QTemporaryDir temporary;
        Project project;
        QString error;
        QVERIFY(ProjectService::create(temporary.path(), "project", project, &error));
        auto changed = metadata(project.manifestPath);
        changed["name"] = "externally edited";
        QVERIFY(putMetadata(project.manifestPath, changed));
        QVERIFY(!ProjectService::addSource(project, "new/nested.p", "unsaved", &error));
        QVERIFY(!QFileInfo::exists(QDir(project.rootPath).filePath("new")));
        QCOMPARE(project.sources, QStringList{"main.p"});
        QCOMPARE(metadata(project.manifestPath).value("name").toString(), QString("externally edited"));
    }

    void defaultRemovalKeepsSourceAndReopenWorks()
    {
        QTemporaryDir temporary;
        Project project;
        QString error;
        QVERIFY(ProjectService::create(temporary.path(), "project", project, &error));
        const QString source = QDir(project.rootPath).filePath("main.p");
        const QByteArray original = contents(source);
        QVERIFY(ProjectService::removeSource(project, "main.p", false, &error));
        QCOMPARE(contents(source), original);
        QVERIFY(project.sources.isEmpty());
        Project reopened;
        QVERIFY2(ProjectService::load(project.manifestPath, reopened, &error), qPrintable(error));
        QVERIFY(reopened.sources.isEmpty());
    }

    void missingSourceCanBeRemovedFromMetadata()
    {
        QTemporaryDir temporary;
        Project project;
        QString error;
        QVERIFY(ProjectService::create(temporary.path(), "project", project, &error));
        QVERIFY(QFile::remove(QDir(project.rootPath).filePath("main.p")));
        QVERIFY(ProjectService::removeSource(project, "main.p", false, &error));
        Project reopened;
        QVERIFY(ProjectService::load(project.manifestPath, reopened, &error));
        QVERIFY(reopened.sources.isEmpty());
    }

    void unknownMetadataIsPreservedWithoutExecution()
    {
        QTemporaryDir temporary;
        Project project;
        QString error;
        QVERIFY(ProjectService::create(temporary.path(), "project", project, &error));
        auto object = metadata(project.manifestPath);
        object["research_note"] = "retain this metadata";
        auto algebra = object.value("algebra").toObject();
        algebra["research_note"] = "retain this too";
        object["algebra"] = algebra;
        QVERIFY(putMetadata(project.manifestPath, object));
        QVERIFY(ProjectService::load(project.manifestPath, project, &error));
        QVERIFY(ProjectService::addSource(project, "second.p", "% source\n", &error));
        const auto saved = metadata(project.manifestPath);
        QCOMPARE(saved.value("research_note").toString(), QString("retain this metadata"));
        QCOMPARE(saved.value("algebra").toObject().value("research_note").toString(), QString("retain this too"));
        QVERIFY(!ProjectService::createFolder(project, "", &error));
    }

    void explicitDeletionRemovesOnlySelectedFile()
    {
        QTemporaryDir temporary;
        Project project;
        QString error;
        QVERIFY(ProjectService::create(temporary.path(), "project", project, &error));
        QVERIFY(ProjectService::addSource(project, "folder/second.p", "keep", &error));
        QVERIFY(ProjectService::removeSource(project, "main.p", true, &error));
        QVERIFY(!QFileInfo::exists(QDir(project.rootPath).filePath("main.p")));
        QCOMPARE(contents(QDir(project.rootPath).filePath("folder/second.p")), QByteArray("keep"));
        QCOMPARE(project.sources, QStringList{"folder/second.p"});
        QVERIFY(QFileInfo::exists(project.manifestPath));
    }

    void deletionRollsBackOnMetadataConflict()
    {
        QTemporaryDir temporary;
        Project project;
        QString error;
        QVERIFY(ProjectService::create(temporary.path(), "project", project, &error));
        const QString source = QDir(project.rootPath).filePath("main.p");
        const QByteArray original = contents(source);
        QVERIFY(put(project.manifestPath, contents(project.manifestPath) + "\n"));
        QVERIFY(!ProjectService::removeSource(project, "main.p", true, &error));
        QCOMPARE(contents(source), original);
        QCOMPARE(project.sources, QStringList{"main.p"});
        QCOMPARE(QDir(project.rootPath).entryList({"*.penta-delete-*"}, QDir::Files).size(), 0);
    }

    void symlinkOutsideRootIsRejected()
    {
        QTemporaryDir temporary;
        Project project;
        QString error;
        QVERIFY(ProjectService::create(temporary.path(), "project", project, &error));
        const QString outside = QDir(temporary.path()).filePath("outside");
        QVERIFY(QDir().mkdir(outside));
        QVERIFY(put(QDir(outside).filePath("secret.p"), "do not change"));
        const QString link = QDir(project.rootPath).filePath("escape");
        std::error_code ec;
        std::filesystem::create_directory_symlink(fsPath(outside), fsPath(link), ec);
        if (ec) {
#ifdef Q_OS_WIN
            // NTFS junctions exercise the same escape boundary without requiring
            // the administrator/developer-mode privilege of a directory symlink.
            QProcess command;
            command.setProgram("cmd.exe");
            command.setNativeArguments(QStringLiteral("/d /c mklink /J \"%1\" \"%2\"")
                .arg(QDir::toNativeSeparators(link), QDir::toNativeSeparators(outside)));
            command.start();
            if (!command.waitForFinished(10000) || command.exitCode() != 0)
                QSKIP("This environment cannot create either directory symlinks or NTFS junctions.");
#else
            QSKIP("This environment cannot create directory symlinks.");
#endif
        }
        QString result;
        QVERIFY(!ProjectService::resolveSource(project, "escape/secret.p", result, &error));
        QVERIFY(!ProjectService::addSource(project, "escape/new.p", "bad", &error));
        QVERIFY(!ProjectService::createFolder(project, "escape/newfolder", &error));
        QCOMPARE(contents(QDir(outside).filePath("secret.p")), QByteArray("do not change"));
        QVERIFY(!QFileInfo::exists(QDir(outside).filePath("new.p")));
        // Remove only the link. POSIX rmdir does not unlink directory symlinks;
        // Windows directory links/junctions use RemoveDirectory via QDir.
#ifdef Q_OS_WIN
        QVERIFY(QDir().rmdir(link));
#else
        QVERIFY(QFile::remove(link));
#endif
    }

    void canonicalAliasesCannotBeIncludedTwice()
    {
        QTemporaryDir temporary;
        Project project;
        QString error;
        QVERIFY(ProjectService::create(temporary.path(), "project", project, &error));
        const QString source = QDir(project.rootPath).filePath("main.p");
        const QString link = QDir(project.rootPath).filePath("alias.p");
        std::error_code ec;
        std::filesystem::create_symlink(fsPath(source), fsPath(link), ec);
        if (ec) QSKIP("This environment cannot create symlinks; requires OS permission/developer mode.");
        QVERIFY(!ProjectService::includeSource(project, "alias.p", &error));
        QVERIFY(error.contains("canonical"));
        QCOMPARE(project.sources, QStringList{"main.p"});
    }
};

QTEST_GUILESS_MAIN(ProjectTests)
#include "project_tests.moc"
