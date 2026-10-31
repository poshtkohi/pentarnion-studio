#pragma once
#include "language/frontend.hpp"
#include <QObject>
#include <QByteArray>
#include <QStringList>
#include <QVector>

class QCompleter;
class QStandardItemModel;
namespace penta {
class EditorWidget;
// View-only assistance consumes immutable checked snapshots. It never runs code.
class SourceAssistance final : public QObject {
    Q_OBJECT
public:
    explicit SourceAssistance(EditorWidget* editor);
    void updateAnalysis(const language::AnalysisResult&,const QByteArray& source,const QString& path);
    QStringList completions(const QString& prefix) const;
    QString inspectionAt(int position) const;
    bool hasCurrentAnalysis() const;
    void showCompletion();
    void showInspection();
    void jumpToDefinition();
signals:
    void analysisRequested();
    void inspectionRequested(const QString& text);
    void externalDefinitionRequested(const QString& path,const QByteArray& source,quint64 offset,quint64 length);
protected:
    bool eventFilter(QObject*,QEvent*) override;
private:
    struct Item {
        QString name,signature,details;
        language::SourceSpan declaration{},scope{};
        bool builtin=false,navigable=false;
        std::shared_ptr<const language::SourceOrigin> origin{};
    };
    enum class Pending {None,Complete,Inspect,Definition};
    QString prefixAt(int position) const;
    const Item* symbolAt(int position) const;
    void rebuildModel();
    void addSchemaMembers(const QString&,const std::shared_ptr<const language::ValueSchema>&,
                          language::SourceSpan,std::size_t depth=0);
    void collectStatements(const std::vector<language::Statement>&,language::SourceSpan,std::size_t depth=0);
    void insertCompletion(const QString&);
    EditorWidget* editor_;
    QCompleter* completer_;
    QStandardItemModel* model_;
    QVector<Item> builtins_,items_;
    QByteArray source_;
    QString path_;
    bool checked_=false;
    Pending pending_=Pending::None;
};
}
