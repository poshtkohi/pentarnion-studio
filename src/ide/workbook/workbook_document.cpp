#include "workbook_document.hpp"
#include "workbook_data.hpp"

#include <QAbstractTableModel>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QColor>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPointer>
#include <QRunnable>
#include <QSaveFile>
#include <QSet>
#include <QSignalBlocker>
#include <QTableView>
#include <QThreadPool>
#include <QTimer>
#include <QToolBar>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>
#include <functional>
#include <optional>
#include <stdexcept>

namespace penta::workbook {
namespace {
bool fail(QString* error,const QString& message){if(error)*error=message;return false;}
QByteArray readBytes(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error(file.errorString().toUtf8().constData());auto bytes=file.read(MaximumInputBytes+1);if(bytes.size()>MaximumInputBytes)throw std::runtime_error("The file exceeds the 16 MiB editor limit.");if(file.error()!=QFile::NoError)throw std::runtime_error(file.errorString().toUtf8().constData());return bytes;}
QByteArray digest(const QByteArray& bytes){return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);}
QThreadPool* loaderPool(){static QThreadPool* pool=[](){auto* result=new QThreadPool(QCoreApplication::instance());result->setMaxThreadCount(2);result->setExpiryTimeout(1000);return result;}();return pool;}
QString canonical(const QString& path){QFileInfo info(path);return info.canonicalFilePath().isEmpty()?info.absoluteFilePath():info.canonicalFilePath();}
bool atomicallyWrite(const QString& path,const QByteArray& bytes,bool overwrite,const std::optional<QByteArray>& expected,QString* error){
    const QFileInfo info(path);if(info.isSymLink())return fail(error,QStringLiteral("Save through a symbolic link is not supported. Choose Save As."));
    if(info.exists()&&!overwrite)return fail(error,QStringLiteral("The destination already exists. Confirm replacement or choose another name."));
    std::optional<QByteArray> baseline=expected;
    if(!baseline&&info.exists()){try{baseline=digest(readBytes(path));}catch(const std::exception& exception){return fail(error,QString::fromUtf8(exception.what()));}}
    const auto unchanged=[&]{if(!baseline)return !QFileInfo::exists(path);try{return QFileInfo::exists(path)&&digest(readBytes(path))==*baseline;}catch(...){return false;}};
    if(!unchanged())return fail(error,QStringLiteral("The workbook changed on disk. Reopen it or use Save As to preserve both versions."));
    QSaveFile file(path);file.setDirectWriteFallback(false);if(!file.open(QIODevice::WriteOnly))return fail(error,file.errorString());
    if(file.write(bytes)!=bytes.size()){file.cancelWriting();return fail(error,file.errorString());}
    if(!unchanged()){file.cancelWriting();return fail(error,QStringLiteral("The workbook changed during saving. No replacement was committed."));}
    if(!file.commit())return fail(error,file.errorString());
    return true;
}
class Table final:public QTableView {
public:
    explicit Table(QWidget* parent):QTableView(parent){}
    std::function<void()> copyCommand,pasteCommand,clearCommand,undoCommand,redoCommand;
protected:
    void keyPressEvent(QKeyEvent* event)override{
        const auto call=[&](const auto& fn){if(fn)fn();event->accept();};
        if(event->matches(QKeySequence::Copy))call(copyCommand);
        else if(event->matches(QKeySequence::Paste))call(pasteCommand);
        else if(event->matches(QKeySequence::Undo))call(undoCommand);
        else if(event->matches(QKeySequence::Redo))call(redoCommand);
        else if(event->key()==Qt::Key_Delete||event->key()==Qt::Key_Backspace)call(clearCommand);
        else QTableView::keyPressEvent(event);
    }
    void contextMenuEvent(QContextMenuEvent* event)override{
        QMenu menu(this);menu.addAction(QStringLiteral("Copy"),this,[this]{if(copyCommand)copyCommand();});menu.addAction(QStringLiteral("Paste"),this,[this]{if(pasteCommand)pasteCommand();});menu.addAction(QStringLiteral("Clear cells"),this,[this]{if(clearCommand)clearCommand();});menu.addSeparator();menu.addAction(QStringLiteral("Select all"),this,&QTableView::selectAll);menu.exec(event->globalPos());
    }
};
} // namespace

struct WorkbookDocument::Private {
    struct Job {std::atomic_bool cancelled{false},done{false};WorkbookData data;QByteArray hash;QString error,path;};
    struct Edit {quint64 key=0;Cell before,after;};
    struct Model final:QAbstractTableModel {
        explicit Model(Private& value):QAbstractTableModel(value.owner),d(value){}
        int rowCount(const QModelIndex& parent={})const override{if(parent.isValid()||d.data.sheets.isEmpty())return 0;return std::min(std::max(100,d.displayRows[d.sheet]+20),MaximumRows);}
        int columnCount(const QModelIndex& parent={})const override{if(parent.isValid()||d.data.sheets.isEmpty())return 0;return std::min(std::max(26,d.displayColumns[d.sheet]+5),MaximumColumns);}
        QVariant data(const QModelIndex& index,int role)const override{
            if(!index.isValid()||index.row()<0||index.column()<0||index.row()>=rowCount()||index.column()>=columnCount())return {};
            const auto value=d.value(d.sheet,cellKey(index.row(),index.column()));
            if(role==Qt::DisplayRole)return cellDisplay(value);
            if(role==Qt::EditRole)return cellEditText(value);
            if(role==Qt::ToolTipRole&&value.kind==CellKind::Formula)return QStringLiteral("%1\nStored/cached value: %2\nStudio does not recalculate spreadsheet formulas.").arg(cellEditText(value),value.cached);
            if(role==Qt::TextAlignmentRole&&(value.kind==CellKind::Number||value.kind==CellKind::Boolean))return int(Qt::AlignRight|Qt::AlignVCenter);
            if(role==Qt::BackgroundRole&&value.kind==CellKind::Formula)return QColor(255,250,228);
            return {};
        }
        bool setData(const QModelIndex& index,const QVariant& value,int role)override{if(role!=Qt::EditRole||!index.isValid())return false;QString message;const bool ok=d.owner->setCell(index.row(),index.column(),value.toString(),&message);if(!ok)emit d.owner->error(message);return ok;}
        QVariant headerData(int section,Qt::Orientation orientation,int role)const override{if(role!=Qt::DisplayRole)return {};return orientation==Qt::Vertical?QVariant(section+1):QVariant(cellAddress(0,section).chopped(1));}
        Qt::ItemFlags flags(const QModelIndex& index)const override{if(!index.isValid())return Qt::NoItemFlags;auto flags=Qt::ItemIsEnabled|Qt::ItemIsSelectable;if(!d.loading&&!formulaRangeProtected(d.data.sheets[d.sheet],index.row(),index.column()))flags|=Qt::ItemIsEditable;return flags;}
        void reset(const std::function<void()>& mutation){beginResetModel();mutation();endResetModel();}
        Private& d;
    };
    struct EditCommand final:QUndoCommand {
        EditCommand(Private& document,int index,QVector<Edit> values,const QString& title):QUndoCommand(title),d(document),sheet(index),edits(std::move(values)){}
        void redo()override{d.apply(sheet,edits,true);}
        void undo()override{d.apply(sheet,edits,false);}
        Private& d;int sheet;QVector<Edit> edits;
    };

    WorkbookDocument* owner;WorkbookData data;QVector<CellMap> changes;QVector<int> displayRows,displayColumns;QString path;
    QByteArray diskHash;int sheet=0;bool loading=false;qsizetype characters=0;int cellCount=0;
    QUndoStack history;QToolBar* toolbar=nullptr;Table* table=nullptr;Model* model=nullptr;QComboBox* sheets=nullptr;
    QLineEdit* formula=nullptr;QLabel* status=nullptr;QLabel* address=nullptr;QTimer* timer=nullptr;
    std::shared_ptr<Job> job;
    explicit Private(WorkbookDocument* widget):owner(widget){history.setUndoLimit(16);}
    ~Private(){if(job)job->cancelled.store(true);if(timer)timer->stop();QObject::disconnect(&history,nullptr,owner,nullptr);history.clear();}
    Cell value(int index,quint64 key)const{const auto changed=changes[index].constFind(key);return changed==changes[index].cend()?data.sheets[index].cells.value(key):*changed;}
    static qsizetype size(const Cell& cell){return cell.text.size()+cell.cached.size();}
    static bool nonempty(const Cell& cell){return cell.kind!=CellKind::Text||!cell.text.isEmpty();}
    void count(){characters=0;cellCount=0;for(const auto& s:data.sheets)for(auto it=s.cells.cbegin();it!=s.cells.cend();++it){characters+=size(it.value());++cellCount;}}
    void updateStatus(){
        if(loading){status->setText(QStringLiteral("Loading workbook…"));return;}
        status->setText(QStringLiteral("%1%2 · %3 · Editable cells · Formulas are stored/preserved, not recalculated").arg(path.isEmpty()?QStringLiteral("Untitled workbook"):QFileInfo(path).fileName(),history.isClean()&&!pendingFormula()?QString():QStringLiteral(" *"),data.sheets.isEmpty()?QString():data.sheets[sheet].name));
    }
    bool pendingFormula()const{
        const auto current=table->currentIndex();
        return !loading&&current.isValid()&&!formula->isReadOnly()&&formula->text()!=cellEditText(value(sheet,cellKey(current.row(),current.column())));
    }
    bool commitFormula(QString* error){
        if(!pendingFormula())return true;
        const auto current=table->currentIndex();
        return owner->setCell(current.row(),current.column(),formula->text(),error);
    }
    void updateFormula(){const auto current=table->currentIndex();if(!current.isValid()){address->clear();formula->clear();return;}address->setText(cellAddress(current.row(),current.column()));QSignalBlocker blocker(formula);formula->setText(cellEditText(value(sheet,cellKey(current.row(),current.column()))));formula->setReadOnly(formulaRangeProtected(data.sheets[sheet],current.row(),current.column()));}
    void refreshExtents(int index){
        // Recompute once per committed edit/undo batch, never from paint-time
        // rowCount/columnCount queries. Model resets enclose every shape change.
        int rows=data.sheets[index].rows,columns=data.sheets[index].columns;
        for(auto it=changes[index].cbegin();it!=changes[index].cend();++it){
            rows=std::max(rows,cellRow(it.key())+1);
            columns=std::max(columns,cellColumn(it.key())+1);
        }
        displayRows[index]=rows;displayColumns[index]=columns;
    }
    void apply(int index,const QVector<Edit>& edits,bool forward){
        const QModelIndex previous=table->currentIndex();model->reset([&]{sheet=index;for(const auto& edit:edits){const auto before=value(index,edit.key),after=forward?edit.after:edit.before;characters+=size(after)-size(before);if(!data.sheets[index].cells.contains(edit.key))cellCount+=int(nonempty(after))-int(nonempty(before));if(after==data.sheets[index].cells.value(edit.key))changes[index].remove(edit.key);else changes[index].insert(edit.key,after);}refreshExtents(index);});
        {QSignalBlocker blocker(sheets);sheets->setCurrentIndex(sheet);}if(previous.isValid())table->setCurrentIndex(model->index(previous.row(),previous.column()));updateStatus();updateFormula();
    }
    bool push(QVector<Edit> edits,const QString& title,QString* error){
        if(loading)return fail(error,QStringLiteral("Wait for the workbook to finish loading."));
        if(edits.isEmpty())return true;
        qsizetype projected=characters,budget=0;int count=cellCount;
        for(const auto& edit:edits){projected+=size(edit.after)-size(edit.before);budget+=size(edit.after)+size(edit.before);if(!data.sheets[sheet].cells.contains(edit.key))count+=int(nonempty(edit.after))-int(nonempty(edit.before));}
        if(projected>MaximumTextCharacters||count>MaximumCells||budget>MaximumEditCharacters)return fail(error,QStringLiteral("The edit exceeds the bounded cell, text or undo-operation limit."));
        history.push(new EditCommand(*this,sheet,std::move(edits),title));return true;
    }
    void reset(WorkbookData result,const QString& filename,const QByteArray& hash){
        model->reset([&]{data=std::move(result);changes.clear();changes.resize(data.sheets.size());displayRows.resize(data.sheets.size());displayColumns.resize(data.sheets.size());for(int index=0;index<data.sheets.size();++index)refreshExtents(index);sheet=0;path=filename;diskHash=hash;});history.clear();history.setClean();count();
        {QSignalBlocker blocker(sheets);sheets->clear();for(const auto& s:data.sheets)sheets->addItem(s.name);sheets->setCurrentIndex(0);}sheets->setVisible(data.sheets.size()>1);table->setCurrentIndex(model->index(0,0));updateStatus();updateFormula();emit owner->pathChanged(path);emit owner->modifiedChanged(false);
    }
    void collect(){
        if(!job||!job->done.load(std::memory_order_acquire))return;
        timer->stop();auto completed=std::move(job);loading=false;toolbar->setEnabled(true);table->setEnabled(true);sheets->setEnabled(true);formula->setEnabled(true);
        if(!completed->error.isEmpty()){updateStatus();status->setText(completed->error);emit owner->error(completed->error);emit owner->loadingFinished(false);return;}
        reset(std::move(completed->data),completed->path,completed->hash);emit owner->loadingFinished(true);
    }
};

WorkbookDocument::WorkbookDocument(QWidget* parent):QWidget(parent),d(std::make_unique<Private>(this)){
    setObjectName(QStringLiteral("workbookDocument"));setProperty("pentarnionWorkbookDocument",true);
    auto* layout=new QVBoxLayout(this);layout->setContentsMargins(4,4,4,4);layout->setSpacing(3);
    auto* toolbar=new QToolBar(this);d->toolbar=toolbar;toolbar->setObjectName(QStringLiteral("workbookToolbar"));toolbar->setIconSize({16,16});
    auto* saveAction=toolbar->addAction(QStringLiteral("Save"));saveAction->setObjectName(QStringLiteral("workbookSave"));connect(saveAction,&QAction::triggered,this,[this]{QString message;if(d->path.isEmpty()){const auto selected=QFileDialog::getSaveFileName(this,tr("Save workbook"),{},tr("Excel workbook (*.xlsx);;CSV (*.csv);;TSV (*.tsv)"));if(selected.isEmpty())return;if(!saveAs(selected,&message,true))emit error(message);}else if(!save(&message))emit error(message);});
    auto* saveAsAction=toolbar->addAction(QStringLiteral("Save As…"));saveAsAction->setObjectName(QStringLiteral("workbookSaveAs"));connect(saveAsAction,&QAction::triggered,this,[this]{const auto selected=QFileDialog::getSaveFileName(this,tr("Save workbook as"),d->path,tr("Excel workbook (*.xlsx);;CSV (*.csv);;TSV (*.tsv)"));if(selected.isEmpty())return;QString message;if(!saveAs(selected,&message,true))emit error(message);});
    toolbar->addSeparator();toolbar->addAction(d->history.createUndoAction(this,tr("Undo")));toolbar->addAction(d->history.createRedoAction(this,tr("Redo")));toolbar->addAction(QStringLiteral("Copy"),this,&WorkbookDocument::copy);toolbar->addAction(QStringLiteral("Paste"),this,&WorkbookDocument::paste);toolbar->addAction(QStringLiteral("Clear cells"),this,&WorkbookDocument::clearSelection);
    auto* exportAction=toolbar->addAction(QStringLiteral("Export sheet…"));connect(exportAction,&QAction::triggered,this,[this]{const auto selected=QFileDialog::getSaveFileName(this,tr("Export active worksheet"),{},tr("CSV (*.csv);;TSV (*.tsv)"));if(selected.isEmpty())return;QString message;if(!exportActiveSheet(selected,&message,true))emit error(message);});
    d->sheets=new QComboBox(this);d->sheets->setObjectName(QStringLiteral("workbookSheet"));d->sheets->setAccessibleName(tr("Worksheet"));toolbar->addWidget(d->sheets);layout->addWidget(toolbar);
    auto* formulaRow=new QWidget(this);auto* formulaLayout=new QHBoxLayout(formulaRow);formulaLayout->setContentsMargins(0,0,0,0);d->address=new QLabel(formulaRow);d->address->setMinimumWidth(56);d->formula=new QLineEdit(formulaRow);d->formula->setObjectName(QStringLiteral("workbookFormula"));d->formula->setAccessibleName(tr("Cell value or stored formula"));formulaLayout->addWidget(d->address);formulaLayout->addWidget(d->formula,1);layout->addWidget(formulaRow);
    d->table=new Table(this);d->table->setObjectName(QStringLiteral("workbookTable"));d->table->setAlternatingRowColors(true);d->table->setSelectionMode(QAbstractItemView::ExtendedSelection);d->table->setSelectionBehavior(QAbstractItemView::SelectItems);d->table->setSortingEnabled(false);d->table->setWordWrap(false);d->table->horizontalHeader()->setDefaultSectionSize(110);d->table->verticalHeader()->setDefaultSectionSize(22);d->model=new Private::Model(*d);d->table->setModel(d->model);layout->addWidget(d->table,1);
    d->table->copyCommand=[this]{copy();};d->table->pasteCommand=[this]{paste();};d->table->clearCommand=[this]{clearSelection();};d->table->undoCommand=[this]{undo();};d->table->redoCommand=[this]{redo();};
    d->status=new QLabel(this);d->status->setObjectName(QStringLiteral("workbookStatus"));d->status->setTextFormat(Qt::PlainText);d->status->setWordWrap(true);layout->addWidget(d->status);
    d->timer=new QTimer(this);d->timer->setInterval(30);connect(d->timer,&QTimer::timeout,this,[this]{d->collect();});
    connect(d->sheets,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){selectSheet(index);});connect(d->table->selectionModel(),&QItemSelectionModel::currentChanged,this,[this]{d->updateFormula();});
    connect(d->formula,&QLineEdit::editingFinished,this,[this]{QString message;if(!d->commitFormula(&message))emit error(message);});
    connect(d->formula,&QLineEdit::textEdited,this,[this]{d->updateStatus();emit modifiedChanged(isModified());emit undoStateChanged(canUndo(),canRedo());});
    connect(&d->history,&QUndoStack::cleanChanged,this,[this](bool){d->updateStatus();emit modifiedChanged(isModified());});
    connect(&d->history,&QUndoStack::canUndoChanged,this,[this]{emit undoStateChanged(canUndo(),canRedo());});
    connect(&d->history,&QUndoStack::canRedoChanged,this,[this]{emit undoStateChanged(canUndo(),canRedo());});newWorkbook();
}
WorkbookDocument::~WorkbookDocument(){
    // Widget/model destruction can emit selection signals after derived members
    // are gone. Disconnect them while the document implementation is still live.
    for(auto* child:findChildren<QObject*>())QObject::disconnect(child,nullptr,this,nullptr);
    d->table->setModel(nullptr);
}
bool WorkbookDocument::supportsPath(const QString& path){return QStringList{"csv","tsv","xlsx"}.contains(QFileInfo(path).suffix().toLower());}
void WorkbookDocument::newWorkbook(){if(d->job)d->job->cancelled.store(true);d->job.reset();d->timer->stop();d->loading=false;d->toolbar->setEnabled(true);d->table->setEnabled(true);d->sheets->setEnabled(true);d->formula->setEnabled(true);WorkbookData data;data.sheets.push_back(Sheet{QStringLiteral("Sheet1"),{},{},{},0,0});d->reset(std::move(data),{},{});}
QString WorkbookDocument::path()const{return d->path;}
bool WorkbookDocument::isLoading()const{return d->loading;}
bool WorkbookDocument::isModified()const{return !d->history.isClean()||d->pendingFormula();}
QString WorkbookDocument::statusText()const{return d->status->text();}
QTableView* WorkbookDocument::table()const{return d->table;}
QStringList WorkbookDocument::sheetNames()const{QStringList names;for(const auto& sheet:d->data.sheets)names.push_back(sheet.name);return names;}
int WorkbookDocument::activeSheet()const{return d->sheet;}
bool WorkbookDocument::selectSheet(int index){
    if(d->loading||index<0||index>=d->data.sheets.size())return false;
    if(index==d->sheet)return true;
    QString message;
    if(!d->commitFormula(&message)){
        // A combo-box change reaches us after its visible index changed. Keep
        // the original sheet and pending text together when validation fails.
        {QSignalBlocker blocker(d->sheets);d->sheets->setCurrentIndex(d->sheet);}
        emit error(message);
        return false;
    }
    d->model->reset([&]{d->sheet=index;});
    {QSignalBlocker blocker(d->sheets);d->sheets->setCurrentIndex(index);}
    d->table->setCurrentIndex(d->model->index(0,0));
    d->updateStatus();d->updateFormula();
    return true;
}
bool WorkbookDocument::openFile(const QString& path,QString* error){
    if(isModified())return fail(error,tr("Save or discard the current workbook edits before opening another file."));
    if(!supportsPath(path))return fail(error,tr("Editable documents support CSV, TSV and XLSX."));
    QFileInfo info(path);
    if(!info.isFile()||!info.isReadable())return fail(error,tr("The selected workbook is not a readable file."));
    if(info.size()>MaximumInputBytes)return fail(error,tr("The workbook exceeds the 16 MiB input limit."));
    if(d->job)d->job->cancelled.store(true);
    d->job=std::make_shared<Private::Job>();d->job->path=canonical(path);d->loading=true;d->toolbar->setEnabled(false);d->table->setEnabled(false);d->sheets->setEnabled(false);d->formula->setEnabled(false);d->updateStatus();const auto job=d->job;const auto format=info.suffix().toLower();
    loaderPool()->start(QRunnable::create([job,format]{try{const auto bytes=readBytes(job->path);job->hash=digest(bytes);job->data=readWorkbook(bytes,format,job->cancelled);}catch(const std::exception& exception){job->error=QString::fromUtf8(exception.what());}catch(...){job->error=QStringLiteral("The workbook could not be loaded.");}job->done.store(true,std::memory_order_release);}));d->timer->start();return true;
}
bool WorkbookDocument::setCell(int row,int column,const QString& text,QString* error){
    if(row<0||column<0||row>=MaximumRows||column>=MaximumColumns)return fail(error,tr("The cell is outside Excel row/column bounds."));
    if(d->loading)return fail(error,tr("Wait for workbook loading to finish."));
    if(formulaRangeProtected(d->data.sheets[d->sheet],row,column))return fail(error,tr("Editing shared, array or data-table formula ranges is not supported. Other cells remain editable."));
    const auto key=cellKey(row,column);const auto before=d->value(d->sheet,key);if(cellEditText(before)==text)return true;
    try{const auto after=enteredCell(text,d->data.format=="xlsx"||QFileInfo(d->path).suffix().compare("xlsx",Qt::CaseInsensitive)==0);if(before==after)return true;return d->push({Private::Edit{key,before,after}},tr("Edit cell"),error);}catch(const std::exception& exception){return fail(error,QString::fromUtf8(exception.what()));}
}
QString WorkbookDocument::cellText(int row,int column)const{if(row<0||column<0||row>=MaximumRows||column>=MaximumColumns)return {};return cellEditText(d->value(d->sheet,cellKey(row,column)));}
bool WorkbookDocument::pasteText(const QString& text,int row,int column,QString* error){
    if(d->loading)return fail(error,tr("Wait for workbook loading to finish."));
    if(text.size()>MaximumEditCharacters)return fail(error,tr("Paste exceeds the 2 Mi-character operation limit."));
    try{const auto lines=parseDelimited(text,'\t');QVector<Private::Edit> edits;for(int r=0;r<lines.size();++r)for(int c=0;c<lines[r].size();++c){const int targetRow=row+r,targetColumn=column+c;if(targetRow<0||targetColumn<0||targetRow>=MaximumRows||targetColumn>=MaximumColumns)return fail(error,tr("Paste exceeds Excel row/column bounds."));if(formulaRangeProtected(d->data.sheets[d->sheet],targetRow,targetColumn))return fail(error,tr("Paste intersects a protected shared/array formula range."));const auto key=cellKey(targetRow,targetColumn);const auto before=d->value(d->sheet,key);if(cellEditText(before)==lines[r][c])continue;const auto after=enteredCell(lines[r][c],d->data.format=="xlsx"||QFileInfo(d->path).suffix().compare("xlsx",Qt::CaseInsensitive)==0);if(before!=after)edits.push_back({key,before,after});}return d->push(std::move(edits),tr("Paste cells"),error);}catch(const std::exception& exception){return fail(error,QString::fromUtf8(exception.what()));}
}
bool WorkbookDocument::canUndo()const{return !d->loading&&(d->formula->hasFocus()&&d->pendingFormula()?d->formula->isUndoAvailable():d->history.canUndo());}
bool WorkbookDocument::canRedo()const{return !d->loading&&(d->formula->hasFocus()&&d->pendingFormula()?d->formula->isRedoAvailable():d->history.canRedo());}
void WorkbookDocument::undo(){if(!d->loading){if(d->formula->hasFocus()&&d->pendingFormula())d->formula->undo();else d->history.undo();}}
void WorkbookDocument::redo(){if(!d->loading){if(d->formula->hasFocus()&&d->pendingFormula())d->formula->redo();else d->history.redo();}}
void WorkbookDocument::selectAll(){if(!d->loading)d->table->selectAll();}
void WorkbookDocument::cut(){
    if(d->loading)return;
    if(auto* editor=qobject_cast<QLineEdit*>(QApplication::focusWidget());
       editor&&(editor==d->formula||d->table->isAncestorOf(editor))){
        editor->cut();
        return;
    }
    if(copySelectedCells())clearSelection();
}
void WorkbookDocument::copy(){
    if(d->formula->hasFocus()&&d->formula->hasSelectedText()){d->formula->copy();return;}
    copySelectedCells();
}
bool WorkbookDocument::copySelectedCells(){
    if(d->loading||!d->table->selectionModel())return false;
    const auto selection=d->table->selectionModel()->selection();
    if(selection.isEmpty())return false;
    int firstRow=MaximumRows,lastRow=-1,firstColumn=MaximumColumns,lastColumn=-1;for(const auto& r:selection){firstRow=std::min(firstRow,r.top());lastRow=std::max(lastRow,r.bottom());firstColumn=std::min(firstColumn,r.left());lastColumn=std::max(lastColumn,r.right());}
    if(qint64(lastRow-firstRow+1)*(lastColumn-firstColumn+1)>MaximumCells){emit error(tr("Choose at most 100,000 cells to copy."));return false;}
    QVector<QStringList> rows;QString html=QStringLiteral("<html><body><table>");qsizetype characters=0;
    for(int r=firstRow;r<=lastRow;++r){QStringList line;html+="<tr>";for(int c=firstColumn;c<=lastColumn;++c){const auto index=d->model->index(r,c);const auto value=d->table->selectionModel()->isSelected(index)?cellDisplay(d->value(d->sheet,cellKey(r,c))):QString();characters+=value.size();if(characters>MaximumEditCharacters){emit error(tr("The copied text exceeds the clipboard limit."));return false;}line.push_back(value);html+="<td style=\"white-space:pre-wrap\">"+value.toHtmlEscaped()+"</td>";}html+="</tr>";rows.push_back(std::move(line));}html+="</table></body></html>";auto* mime=new QMimeData;mime->setText(encodeDelimited(rows,'\t',"\n",false));mime->setHtml(html);QApplication::clipboard()->setMimeData(mime);
    return true;
}
void WorkbookDocument::paste(){if(d->formula->hasFocus()){d->formula->paste();return;}const auto index=d->table->currentIndex();QString message;if(!pasteText(QApplication::clipboard()->text(),index.isValid()?index.row():0,index.isValid()?index.column():0,&message))emit error(message);}
void WorkbookDocument::clearSelection(){
    if(d->loading||!d->table->selectionModel())return;
    QSet<quint64> keys;for(const auto& range:d->table->selectionModel()->selection()){if(qint64(range.height())*range.width()>MaximumCells){emit error(tr("Choose at most 100,000 cells to clear."));return;}for(int r=range.top();r<=range.bottom();++r)for(int c=range.left();c<=range.right();++c){if(formulaRangeProtected(d->data.sheets[d->sheet],r,c)){emit error(tr("The selection intersects a protected shared/array formula range."));return;}keys.insert(cellKey(r,c));if(keys.size()>MaximumCells){emit error(tr("Choose at most 100,000 cells to clear."));return;}}}
    QVector<Private::Edit> edits;for(const auto key:keys){const auto before=d->value(d->sheet,key);if(Private::nonempty(before))edits.push_back({key,before,Cell{}});}QString message;if(!d->push(std::move(edits),tr("Clear cells"),&message))emit error(message);
}
bool WorkbookDocument::save(QString* error){if(d->path.isEmpty())return fail(error,tr("Choose Save As for this new workbook."));return saveAs(d->path,error,true);}
bool WorkbookDocument::saveAs(const QString& path,QString* error,bool overwrite){
    if(d->loading)return fail(error,tr("Wait for workbook loading to finish."));
    if(!d->commitFormula(error))return false;
    if(!supportsPath(path))return fail(error,tr("Choose an XLSX, CSV or TSV destination."));
    const auto format=QFileInfo(path).suffix().toLower();
    if(d->data.format=="xlsx"&&format!="xlsx")return fail(error,tr("Use Export Active Sheet for CSV/TSV so the original workbook remains intact."));
    const auto target=QFileInfo(path).absoluteFilePath();const bool same=!d->path.isEmpty()&&canonical(target)==canonical(d->path);
    try{const auto bytes=writeWorkbook(d->data,d->changes,format);if(bytes.size()>MaximumInputBytes)return fail(error,tr("The saved workbook would exceed the 16 MiB editor limit."));if(!atomicallyWrite(target,bytes,overwrite,same?std::optional<QByteArray>(d->diskHash):std::nullopt,error))return false;d->path=target;d->diskHash=digest(bytes);d->history.setClean();d->updateStatus();emit pathChanged(d->path);emit modifiedChanged(false);return true;}catch(const std::exception& exception){return fail(error,QString::fromUtf8(exception.what()));}
}
bool WorkbookDocument::exportActiveSheet(const QString& path,QString* error,bool overwrite)const{
    if(d->loading)return fail(error,tr("Wait for workbook loading to finish."));
    if(d->pendingFormula())return fail(error,tr("Finish editing the current cell before exporting."));
    const auto format=QFileInfo(path).suffix().toLower();
    if(format!="csv"&&format!="tsv")return fail(error,tr("Choose CSV or TSV for worksheet export."));
    if(!d->path.isEmpty()&&canonical(path)==canonical(d->path))return fail(error,tr("Export to a different path to preserve the workbook."));
    try{WorkbookData data;data.format=format;data.sheets.push_back(d->data.sheets[d->sheet]);data.newline=d->data.newline;data.trailingNewline=true;const auto bytes=writeWorkbook(data,{d->changes[d->sheet]},format);return atomicallyWrite(path,bytes,overwrite,std::nullopt,error);}catch(const std::exception& exception){return fail(error,QString::fromUtf8(exception.what()));}
}
} // namespace penta::workbook
