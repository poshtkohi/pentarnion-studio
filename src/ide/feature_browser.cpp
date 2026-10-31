#include "feature_browser.hpp"
#include "language/language_version.hpp"
#include "runtime/module_inventory.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace penta {
namespace {
QString pathText(const std::filesystem::path& path){const auto text=path.u8string();return QString::fromUtf8(reinterpret_cast<const char*>(text.data()),static_cast<qsizetype>(text.size()));}
bool inside(const QString& root,const QString& path){const auto relative=QDir(root).relativeFilePath(path);return !relative.startsWith(QStringLiteral("../"))&&relative!=QStringLiteral("..")&&!QDir::isAbsolutePath(relative);}
}
FeatureBrowser::FeatureBrowser(const QString& referenceRoot,std::function<bool()> busy,QWidget* parent):QWidget(parent),root_(QFileInfo(referenceRoot).canonicalFilePath()),busy_(std::move(busy)){
    setObjectName(QStringLiteral("featureBrowser"));auto* layout=new QVBoxLayout(this);auto* tabs=new QTabWidget(this);layout->addWidget(tabs);
    auto* rulesPage=new QWidget(tabs);auto* ruleLayout=new QVBoxLayout(rulesPage);version_=new QLabel(rulesPage);version_->setObjectName(QStringLiteral("loadedSpecificationInfo"));version_->setWordWrap(true);ruleLayout->addWidget(version_);
    auto* row=new QHBoxLayout;filter_=new QLineEdit(rulesPage);filter_->setObjectName(QStringLiteral("featureRuleFilter"));filter_->setPlaceholderText(tr("Filter features, rules or filenames"));row->addWidget(filter_);auto* reload=new QPushButton(tr("Reload References"),rulesPage);row->addWidget(reload);ruleLayout->addLayout(row);
    auto* splitter=new QSplitter(Qt::Vertical,rulesPage);rules_=new QTreeWidget(splitter);rules_->setObjectName(QStringLiteral("featureRulesTree"));rules_->setHeaderLabels({tr("Rule"),tr("Reference")});rules_->setRootIsDecorated(false);rules_->setAlternatingRowColors(true);rules_->header()->setSectionResizeMode(0,QHeaderView::Stretch);rules_->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    text_=new QPlainTextEdit(splitter);text_->setObjectName(QStringLiteral("featureRuleText"));text_->setReadOnly(true);ruleLayout->addWidget(splitter);tabs->addTab(rulesPage,tr("Feature Rules"));
    auto* modulePage=new QWidget(tabs);auto* moduleLayout=new QVBoxLayout(modulePage);
    auto* explanation=new QLabel(tr("Modules load when first used. Inspecting this list does not load them. Load Selected prepares a module for this session; numerical calls check their required capabilities."),modulePage);explanation->setWordWrap(true);moduleLayout->addWidget(explanation);
    modules_=new QTreeWidget(modulePage);modules_->setObjectName(QStringLiteral("nativeModulesTree"));modules_->setHeaderLabels({tr("Module"),tr("State"),tr("ABI"),tr("Path")});modules_->setRootIsDecorated(false);modules_->header()->setSectionResizeMode(3,QHeaderView::Stretch);moduleLayout->addWidget(modules_);
    auto* buttons=new QHBoxLayout;auto* refresh=new QPushButton(tr("Refresh"),modulePage);refresh->setObjectName(QStringLiteral("refreshModules"));auto* load=new QPushButton(tr("Load Selected"),modulePage);load->setObjectName(QStringLiteral("loadSelectedModule"));buttons->addWidget(refresh);buttons->addWidget(load);buttons->addStretch();moduleLayout->addLayout(buttons);
    moduleStatus_=new QLabel(modulePage);moduleStatus_->setObjectName(QStringLiteral("moduleManagementStatus"));moduleStatus_->setWordWrap(true);moduleLayout->addWidget(moduleStatus_);
    auto* threading=new QLabel(tr("Numerical thread limits are chosen per call. A value of 0 selects the available CPUs automatically; nested native calls share Studio's CPU budget. Modules remain resident until Studio exits."),modulePage);threading->setWordWrap(true);moduleLayout->addWidget(threading);tabs->addTab(modulePage,tr("Native Modules"));
    connect(rules_,&QTreeWidget::currentItemChanged,this,[this]{showRule();});connect(reload,&QPushButton::clicked,this,&FeatureBrowser::loadRules);
    connect(filter_,&QLineEdit::textChanged,this,[this](const QString& query){for(int i=0;i<rules_->topLevelItemCount();++i){auto* item=rules_->topLevelItem(i);item->setHidden(!(item->text(0)+QLatin1Char(' ')+item->text(1)).contains(query,Qt::CaseInsensitive));}});
    connect(refresh,&QPushButton::clicked,this,&FeatureBrowser::refreshModules);connect(load,&QPushButton::clicked,this,&FeatureBrowser::loadSelectedModule);
    loadRules();refreshModules();
}
int FeatureBrowser::ruleCount()const{return rules_->topLevelItemCount();}
void FeatureBrowser::loadRules(){
    rules_->clear();text_->clear();QStringList paths;
    if(!root_.isEmpty()){paths<<QDir(root_).filePath(QStringLiteral("language.txt"));QDirIterator files(QDir(root_).filePath(QStringLiteral("specs")),{QStringLiteral("*.txt")},QDir::Files,QDirIterator::Subdirectories);while(files.hasNext()&&paths.size()<4096)paths<<files.next();}
    paths.sort(Qt::CaseInsensitive);qsizetype bytes=0;QByteArray languageHash;
    for(const auto& path:paths){const auto canonical=QFileInfo(path).canonicalFilePath();if(canonical.isEmpty()||!inside(root_,canonical))continue;QFile file(canonical);if(!file.open(QIODevice::ReadOnly)||file.size()>512*1024||file.size()>16*1024*1024-bytes)continue;const auto data=file.readAll();bytes+=data.size();if(QFileInfo(path).fileName()==QStringLiteral("language.txt"))languageHash=QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex();
        const auto summary=QString::fromUtf8(data).section(QLatin1Char('\n'),0,0).trimmed();auto* item=new QTreeWidgetItem(rules_,{summary.isEmpty()?QFileInfo(path).fileName():summary,QDir(root_).relativeFilePath(canonical)});item->setData(0,Qt::UserRole,canonical);item->setData(0,Qt::UserRole+1,data);item->setToolTip(1,canonical);
    }
    const auto version=language::version();version_->setText(tr("Language engine: %1\nReference root: %2\nlanguage.txt SHA256: %3\n%4 reference files. These text files document the compiled language rules.").arg(QString::fromUtf8(version.data(),static_cast<int>(version.size())),root_.isEmpty()?tr("References unavailable"):root_,languageHash.isEmpty()?tr("Unavailable"):QString::fromLatin1(languageHash)).arg(ruleCount()));
    if(ruleCount())rules_->setCurrentItem(rules_->topLevelItem(0));
}
void FeatureBrowser::showRule(){const auto* item=rules_->currentItem();text_->setPlainText(item?QString::fromUtf8(item->data(0,Qt::UserRole+1).toByteArray()):QString());}
void FeatureBrowser::refreshModules(){
    const auto selected=modules_->currentItem()?modules_->currentItem()->text(0):QString();modules_->clear();
    try{for(const auto& module:runtime::moduleInventory()){auto* item=new QTreeWidgetItem(modules_,{QString::fromStdString(module.name),QString::fromStdString(module.status),module.abi?QStringLiteral("%1.%2").arg(module.abi>>16).arg(module.abi&65535):QStringLiteral("—"),pathText(module.path)});item->setToolTip(3,pathText(module.path));if(item->text(0)==selected)modules_->setCurrentItem(item);}if(!modules_->currentItem()&&modules_->topLevelItemCount())modules_->setCurrentItem(modules_->topLevelItem(0));}
    catch(const std::exception& error){moduleStatus_->setText(QString::fromUtf8(error.what()));}
}
void FeatureBrowser::loadSelectedModule(){
    if(busy_&&busy_()){moduleStatus_->setText(tr("Wait for the current calculation to finish before preparing a module."));return;}
    const auto* selected=modules_->currentItem();if(!selected)return;
    try{const auto module=runtime::loadModule(selected->text(0).toStdString());moduleStatus_->setText(tr("%1 is loaded. Numerical calls validate their operation-specific API before use.").arg(QString::fromStdString(module.name)));refreshModules();}
    catch(const std::exception& error){moduleStatus_->setText(QString::fromUtf8(error.what()));}
}
}
