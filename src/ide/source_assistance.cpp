#include "source_assistance.hpp"
#include "editor_widget.hpp"
#include "runtime/help_catalog.hpp"
#include <QAbstractItemView>
#include <QCompleter>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QStandardItemModel>
#include <QTextBlock>
#include <QTextCursor>
#include <QToolTip>
#include <algorithm>
#include <limits>

namespace penta {
namespace {
QString typeText(language::Type type,const std::shared_ptr<const language::ValueSchema>& schema={}) {
    QString out=QString::fromUtf8(language::typeName(type).data(),static_cast<int>(language::typeName(type).size()));
    if(schema&&schema->algebra)out+=QStringLiteral("<%1>").arg(QString::fromStdString(schema->algebra->name()));
    if(schema&&!schema->shape.empty()){QStringList dimensions;for(const auto n:schema->shape)dimensions<<QString::number(n);out+=QStringLiteral("[%1]").arg(dimensions.join(QLatin1Char('x')));}
    return out;
}
QString functionText(const language::FunctionDefinition& function) {
    QStringList parameters;
    for(std::size_t n=0;n<function.parameters.size();++n){const auto& p=function.parameters[n];parameters<<typeText(p.type,n<function.parameterSchemas.size()?function.parameterSchemas[n]:p.schema)+QLatin1Char(' ')+QString::fromStdString(p.name);}
    return typeText(function.result,function.resultSchema)+QLatin1Char(' ')+QString::fromStdString(function.name)+QLatin1Char('(')+parameters.join(QStringLiteral(", "))+QLatin1Char(')');
}
bool contains(language::SourceSpan scope,std::size_t at) {return at>=scope.offset&&at-scope.offset<=scope.length;}
}

SourceAssistance::SourceAssistance(EditorWidget* editor):QObject(editor),editor_(editor),completer_(new QCompleter(this)),model_(new QStandardItemModel(this)) {
    setObjectName(QStringLiteral("sourceAssistance"));
    for(const auto& entry:runtime::help::entries()){
        const QString names=QString::fromUtf8(entry.names.data(),static_cast<int>(entry.names.size()));
        for(const auto& name:names.split(QLatin1Char('|'),Qt::SkipEmptyParts))builtins_.push_back({name,QString::fromUtf8(entry.usage.data(),static_cast<int>(entry.usage.size())),QString::fromUtf8(entry.summary.data(),static_cast<int>(entry.summary.size()))+QLatin1Char('\n')+QString::fromUtf8(entry.details.data(),static_cast<int>(entry.details.size())),{},{},true,false});
    }
    for(const auto* word:{"if","else","for","while","break","continue","return","function","native","import","as","ode","use_algebra","true","false","end"})builtins_.push_back({QString::fromLatin1(word),QString::fromLatin1(word),tr("Language syntax; use Help for its specification."),{},{},true,false});
    completer_->setModel(model_);completer_->setWidget(editor_);completer_->setCaseSensitivity(Qt::CaseInsensitive);
    completer_->setCompletionMode(QCompleter::PopupCompletion);completer_->setFilterMode(Qt::MatchStartsWith);
    completer_->popup()->setObjectName(QStringLiteral("sourceCompletionPopup"));
    connect(completer_,qOverload<const QString&>(&QCompleter::activated),this,&SourceAssistance::insertCompletion);
    editor_->installEventFilter(this);editor_->viewport()->installEventFilter(this);
    connect(editor_,&QPlainTextEdit::textChanged,this,[this]{checked_=false;completer_->popup()->hide();});
    rebuildModel();
}
bool SourceAssistance::hasCurrentAnalysis()const {return checked_&&source_==editor_->toPlainText().toUtf8();}
QString SourceAssistance::prefixAt(int position)const {
    auto cursor=editor_->textCursor();cursor.setPosition(std::clamp(position,0,editor_->document()->characterCount()-1));
    const auto left=cursor.block().text().left(cursor.positionInBlock());
    static const QRegularExpression pattern(QStringLiteral("[A-Za-z_][A-Za-z_0-9]*(?:\\.[A-Za-z_][A-Za-z_0-9]*)*\\.?$"));
    const auto match=pattern.match(left);return match.hasMatch()?match.captured():QString();
}
void SourceAssistance::addSchemaMembers(const QString& prefix,const std::shared_ptr<const language::ValueSchema>& schema,language::SourceSpan scope,std::size_t depth){
    if(!schema||depth>16||items_.size()>50000)return;
    if(schema->algebra&&schema->fields.empty())for(const auto& label:schema->algebra->basis())items_.push_back({prefix+QLatin1Char('.')+QString::fromStdString(label),tr("Basis element of %1").arg(QString::fromStdString(schema->algebra->name())),tr("Immutable algebra basis; all coefficients are retained."),{},scope,false,false});
    for(const auto& field:schema->fields){
        const auto name=prefix+QLatin1Char('.')+QString::fromStdString(field.name);
        const auto function=field.schema?field.schema->function:nullptr;
        items_.push_back({name,function?functionText(*function):typeText(field.type,field.schema),tr("Typed record or imported module member."),function?language::SourceSpan{function->span.offset,1}:language::SourceSpan{},scope,false,function&&function->origin&&function->origin->source,function?function->origin:nullptr});
        if(field.type==language::Type::Record||field.type==language::Type::Algebra)addSchemaMembers(name,field.schema,scope,depth+1);
    }
}
void SourceAssistance::collectStatements(const std::vector<language::Statement>& statements,language::SourceSpan scope,std::size_t depth){
    if(depth>64)return;
    using Kind=language::StatementKind;
    for(const auto& statement:statements){
        if(items_.size()>50000)return;
        if(statement.kind==Kind::Declaration||statement.kind==Kind::FunctionDefinition||statement.kind==Kind::OdeDefinition||statement.kind==Kind::Import){
            const auto type=statement.function?language::Type::Function:statement.ode?language::Type::OdeModel:statement.kind==Kind::Import?language::Type::Record:statement.declaredType;
            auto schema=statement.declaredSchema?statement.declaredSchema:statement.expression?statement.expression->schema:nullptr;
            items_.push_back({QString::fromStdString(statement.name),statement.function?functionText(*statement.function):typeText(type,schema),tr("Declared in the checked source snapshot."),statement.nameSpan,scope,false,true});
            if(type==language::Type::Record||type==language::Type::Algebra){const auto first=std::max(scope.offset,statement.nameSpan.offset);addSchemaMembers(QString::fromStdString(statement.name),schema,{first,scope.offset+scope.length-first});}
        }
        if(statement.function){
            const auto functionScope=statement.function->span;
            for(std::size_t n=0;n<statement.function->parameters.size();++n){const auto& p=statement.function->parameters[n];const auto schema=n<statement.function->parameterSchemas.size()?statement.function->parameterSchemas[n]:p.schema;items_.push_back({QString::fromStdString(p.name),typeText(p.type,schema),tr("Function parameter."),p.span,functionScope,false,true});}
            collectStatements(statement.function->body,functionScope,depth+1);
        }
        if(statement.ode){for(const auto* variables:{&statement.ode->states,&statement.ode->parameters})for(const auto& p:*variables)items_.push_back({QString::fromStdString(p.name),typeText(p.type,p.schema),tr("ODE state or runtime parameter."),p.span,statement.ode->span,false,true});}
        auto collectBlock=[&](const auto& body){if(body.empty())return;const auto first=body.front().span.offset;const auto last=body.back().span;collectStatements(body,{first,last.offset+last.length-first},depth+1);};
        collectBlock(statement.body);collectBlock(statement.alternative);
    }
}
void SourceAssistance::updateAnalysis(const language::AnalysisResult& result,const QByteArray& source,const QString& path){
    if(source!=editor_->toPlainText().toUtf8())return;
    source_=source;path_=path;checked_=result.ok();items_.clear();
    if(checked_){collectStatements(result.program.statements,{0,static_cast<std::size_t>(source.size())});for(const auto& symbol:result.symbols)if(symbol.type==language::Type::Record||symbol.type==language::Type::Algebra){const auto first=std::min(symbol.declarationSpan.offset,static_cast<std::size_t>(source.size()));addSchemaMembers(QString::fromStdString(symbol.name),symbol.schema,{first,static_cast<std::size_t>(source.size())-first});}}
    rebuildModel();const auto pending=pending_;pending_=Pending::None;
    if(!checked_){if(pending==Pending::Inspect||pending==Pending::Definition)emit inspectionRequested(tr("The source has static diagnostics. Fix them and Check again for verified symbol information."));return;}
    if(pending==Pending::Complete)showCompletion();else if(pending==Pending::Inspect)showInspection();else if(pending==Pending::Definition)jumpToDefinition();
}
QStringList SourceAssistance::completions(const QString& prefix)const {
    QStringList names;const auto at=static_cast<std::size_t>(editor_->toPlainText().left(editor_->textCursor().position()).toUtf8().size());
    for(const auto& item:builtins_)if(item.name.startsWith(prefix,Qt::CaseInsensitive))names<<item.name;
    if(hasCurrentAnalysis())for(const auto& item:items_)if(contains(item.scope,at)&&(item.origin||item.declaration.offset<=at)&&item.name.startsWith(prefix,Qt::CaseInsensitive))names<<item.name;
    names.removeDuplicates();names.sort(Qt::CaseInsensitive);return names;
}
void SourceAssistance::rebuildModel(){
    model_->clear();const auto names=completions({});
    for(const auto& name:names){auto* item=new QStandardItem(name);const auto* entry=static_cast<const Item*>(nullptr);for(const auto& candidate:builtins_)if(candidate.name==name){entry=&candidate;break;}if(entry)item->setToolTip(entry->signature+QLatin1Char('\n')+entry->details);model_->appendRow(item);}
}
const SourceAssistance::Item* SourceAssistance::symbolAt(int position)const {
    auto word=editor_->textCursor();word.setPosition(std::clamp(position,0,editor_->document()->characterCount()-1));word.select(QTextCursor::WordUnderCursor);
    const auto name=prefixAt(word.selectionEnd());if(name.isEmpty())return nullptr;
    const auto bytes=static_cast<std::size_t>(editor_->toPlainText().left(position).toUtf8().size());const Item* found=nullptr;
    if(hasCurrentAnalysis())for(const auto& item:items_)if(item.name==name&&contains(item.scope,bytes)&&(item.origin||item.declaration.offset<=bytes)&&(!found||item.scope.length<found->scope.length||(item.scope.length==found->scope.length&&item.declaration.offset>found->declaration.offset)))found=&item;
    if(found)return found;
    for(const auto& item:builtins_)if(item.name==name)return &item;
    return nullptr;
}
QString SourceAssistance::inspectionAt(int position)const {
    const auto* item=symbolAt(position);if(!item)return tr("No checked symbol or builtin at this position. Check the current source for symbol types and declaration locations.");
    auto text=item->name+QLatin1Char('\n')+item->signature+QLatin1Char('\n')+item->details;
    if(item->navigable){const auto bytes=item->origin&&item->origin->source?std::string_view(*item->origin->source):std::string_view(source_.constData(),static_cast<std::size_t>(source_.size()));const auto location=language::sourceLocation(bytes,item->declaration.offset);text+=tr("\n%1:%2:%3\nF12: go to declaration").arg(item->origin?QString::fromStdString(item->origin->identity):path_.isEmpty()?tr("Current source"):path_).arg(location.line).arg(location.column);}
    return text;
}
void SourceAssistance::showCompletion(){
    const auto prefix=prefixAt(editor_->textCursor().position());
    if(!hasCurrentAnalysis()){pending_=Pending::Complete;emit analysisRequested();}
    rebuildModel();completer_->setCompletionPrefix(prefix);if(!completer_->completionCount())return;
    auto rectangle=editor_->cursorRect();rectangle.setWidth(std::max(280,completer_->popup()->sizeHintForColumn(0)+28));completer_->complete(rectangle);completer_->popup()->setCurrentIndex(completer_->completionModel()->index(0,0));
}
void SourceAssistance::showInspection(){if(!hasCurrentAnalysis()){pending_=Pending::Inspect;emit analysisRequested();}emit inspectionRequested(inspectionAt(editor_->textCursor().position()));}
void SourceAssistance::jumpToDefinition(){
    if(!hasCurrentAnalysis()){pending_=Pending::Definition;emit analysisRequested();return;}
    const auto* item=symbolAt(editor_->textCursor().position());if(!item||!item->navigable){emit inspectionRequested(inspectionAt(editor_->textCursor().position()));return;}
    if(item->origin&&item->origin->source){emit externalDefinitionRequested(QString::fromStdString(item->origin->identity),QByteArray(item->origin->source->data(),static_cast<qsizetype>(item->origin->source->size())),item->declaration.offset,item->declaration.length);return;}
    const auto start=QString::fromUtf8(source_.constData(),static_cast<qsizetype>(item->declaration.offset)).size();
    const auto length=QString::fromUtf8(source_.constData()+item->declaration.offset,static_cast<qsizetype>(item->declaration.length)).size();
    auto cursor=editor_->textCursor();cursor.setPosition(static_cast<int>(start));cursor.setPosition(static_cast<int>(start+length),QTextCursor::KeepAnchor);editor_->setTextCursor(cursor);editor_->ensureCursorVisible();editor_->setFocus();
}
void SourceAssistance::insertCompletion(const QString& text){
    auto cursor=editor_->textCursor();const auto at=cursor.position();const auto prefix=prefixAt(at);const auto right=cursor.block().text().mid(cursor.positionInBlock());
    static const QRegularExpression suffix(QStringLiteral("^[A-Za-z_0-9]*"));const auto tail=suffix.match(right).capturedLength();
    cursor.setPosition(at-prefix.size());cursor.setPosition(at+tail,QTextCursor::KeepAnchor);cursor.insertText(text);editor_->setTextCursor(cursor);
}
bool SourceAssistance::eventFilter(QObject* watched,QEvent* event){
    if(watched==editor_&&event->type()==QEvent::KeyPress){auto* key=static_cast<QKeyEvent*>(event);if(completer_->popup()->isVisible()){if(key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter||key->key()==Qt::Key_Tab){const auto index=completer_->popup()->currentIndex();if(index.isValid()){const auto text=index.data(Qt::DisplayRole).toString();completer_->popup()->hide();insertCompletion(text);return true;}}if(key->key()==Qt::Key_Escape){completer_->popup()->hide();return true;}}if(key->key()==Qt::Key_Space&&key->modifiers()==Qt::ControlModifier){showCompletion();return true;}if(key->key()==Qt::Key_F12&&key->modifiers()==Qt::NoModifier){jumpToDefinition();return true;}if(key->key()==Qt::Key_I&&key->modifiers()==(Qt::ControlModifier|Qt::ShiftModifier)){showInspection();return true;}}
    if(watched==editor_->viewport()&&event->type()==QEvent::ToolTip){auto* help=static_cast<QHelpEvent*>(event);auto cursor=editor_->cursorForPosition(help->pos());cursor.select(QTextCursor::WordUnderCursor);const auto text=inspectionAt(cursor.selectionEnd());if(symbolAt(cursor.selectionEnd()))QToolTip::showText(help->globalPos(),QStringLiteral("<pre>%1</pre>").arg(text.toHtmlEscaped()),editor_);else QToolTip::hideText();return true;}
    return QObject::eventFilter(watched,event);
}
}
