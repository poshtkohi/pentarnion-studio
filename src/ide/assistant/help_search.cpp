// Adapted from GTG Cell Studio desktop components; scientific engine remains separate.
#include "ide/assistant/help_search.hpp"
#include "ide/assistant/offline_help.hpp"
#include "ide/desktop/studio_icons.hpp"
#include <QAbstractItemView>
#include <QAction>
#include <QCompleter>
#include <QStandardItemModel>
#include <QListView>
#include <QFontDatabase>
#include <QTimer>
namespace penta::assistant {
using penta::desktop::studioCommandIcon;
using penta::desktop::applyStudioButtonIcons;
HelpSearchBox::HelpSearchBox(QWidget* parent):QLineEdit(parent),suggestions_(new QCompleter(this)),model_(new QStandardItemModel(this)){
    setObjectName("pentaHelpSearchBox");setAccessibleName("Search Pentarnion Help");
    setPlaceholderText("Search Pentarnion Help");setToolTip("Search local Pentarnion documentation (Ctrl+Shift+Space)");
    setClearButtonEnabled(true);setMaxLength(256);setFixedHeight(22);setMinimumWidth(120);setMaximumWidth(320);setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);
    auto reading=QFontDatabase::systemFont(QFontDatabase::GeneralFont);reading.setStyleHint(QFont::SansSerif);reading.setStretch(QFont::Unstretched);reading.setPointSize(9);setFont(reading);
    setStyleSheet("QLineEdit {background:white;color:#22384a;border:1px solid #b7c8d6;border-radius:2px;padding:1px 4px;}QLineEdit:enabled:hover {border-color:#159b9b;}");
    auto* find=addAction(studioCommandIcon("Find","editFind"),QLineEdit::LeadingPosition);find->setToolTip("Search Pentarnion Help");
    suggestionsTimer_=new QTimer(this);suggestionsTimer_->setSingleShot(true);suggestionsTimer_->setInterval(120);
    const auto submit=[this]{if(completionHandled_)return;suggestionsTimer_->stop();suggestions_->popup()->hide();if(searchRequested)searchRequested(text().trimmed());};
    connect(find,&QAction::triggered,this,submit);connect(this,&QLineEdit::returnPressed,this,submit);
    suggestions_->setModel(model_);suggestions_->setCompletionMode(QCompleter::UnfilteredPopupCompletion);suggestions_->setMaxVisibleItems(8);suggestions_->setCaseSensitivity(Qt::CaseInsensitive);setCompleter(suggestions_);
    suggestions_->popup()->setObjectName("pentaHelpSearchSuggestions");if(auto* list=qobject_cast<QListView*>(suggestions_->popup()))list->setUniformItemSizes(true);
    suggestions_->popup()->setFont(reading);suggestions_->popup()->setStyleSheet(QStringLiteral("QListView {background:white;color:#234054;border:1px solid #b7c8d6;}QListView::item {padding:5px 7px;}QListView::item:selected {background:#e5f1fb;color:#145579;}"));
    connect(this,&QLineEdit::textEdited,suggestionsTimer_,qOverload<>(&QTimer::start));connect(suggestionsTimer_,&QTimer::timeout,this,[this]{updateSuggestions(text());});
    connect(suggestions_,qOverload<const QModelIndex&>(&QCompleter::activated),this,[this](const QModelIndex& index){
        const auto id=index.data(Qt::UserRole).toString();if(id.isEmpty())return;completionHandled_=true;
        suggestionsTimer_->stop();
        if(topicRequested)topicRequested(id);
        QTimer::singleShot(0,this,[this]{completionHandled_=false;});
    });
}
QSize HelpSearchBox::sizeHint()const{return {280,22};}
QSize HelpSearchBox::minimumSizeHint()const{return {120,22};}
void HelpSearchBox::updateSuggestions(const QString& text){
    model_->clear();const auto query=text.trimmed();if(query.size()<2){suggestions_->popup()->hide();return;}
    for(const auto& result:HelpDialog::searchReferences(query,8)){
        auto* item=new QStandardItem(result.reference.title);item->setData(result.reference.id,Qt::UserRole);item->setToolTip(result.snippet);item->setEditable(false);model_->appendRow(item);
    }
    if(!model_->rowCount()){suggestions_->popup()->hide();return;}
    suggestions_->setCompletionPrefix({});suggestions_->complete();
}
}
