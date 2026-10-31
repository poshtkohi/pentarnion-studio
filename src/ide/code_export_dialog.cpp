#include "code_export_dialog.hpp"
#include "export/code_export.hpp"
#include "export/distribution_resources.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QVBoxLayout>
namespace penta::ide {
namespace {
QString text(const std::string& value){return QString::fromUtf8(value.data(),static_cast<qsizetype>(value.size()));}
QString pathText(const std::filesystem::path& value){const auto encoded=value.u8string();return QString::fromUtf8(reinterpret_cast<const char*>(encoded.data()),static_cast<qsizetype>(encoded.size()));}
}
CodeExportOutcome showCodeExportDialog(QWidget* parent,const std::string& source,
    const language::AnalysisOptions& analysisOptions,const std::filesystem::path& sourcePath){
    CodeExportOutcome outcome;QDialog dialog(parent);dialog.setWindowTitle(QObject::tr("Export checked code"));dialog.resize(660,560);
    auto* layout=new QVBoxLayout(&dialog);auto* description=new QLabel(QObject::tr("Export the supported checked subset with a target runtime, source map and compatibility report. Unsupported constructs leave the destination untouched."),&dialog);description->setWordWrap(true);layout->addWidget(description);
    auto* form=new QFormLayout;auto* target=new QComboBox(&dialog);target->addItems({QObject::tr("Python (.py)"),QObject::tr("MATLAB (.m)")});form->addRow(QObject::tr("Target"),target);
    auto* adapter=new QCheckBox(QObject::tr("Allow explicit SciPy BDF adapter (different integrator from CVODE)"),&dialog);adapter->setToolTip(QObject::tr("Analytic coordinate Jacobians, localized events and simultaneous AD2 trajectory sensitivities. With the scientific option: regular mass, hybrid resets and continuous adjoints. Target adaptive trajectories differ; unsupported solver options fail export."));form->addRow(adapter);
    auto* scientific=new QCheckBox(QObject::tr("Allow explicit scientific solver adapters"),&dialog);scientific->setToolTip(QObject::tr("Maps supported fitting, optimization, graphs, 1D PDEs and analytical mechanics. Advanced ODE mappings also require the BDF option. Target algorithms, scheduling and stopping policies differ; review the compatibility report."));form->addRow(scientific);
    const auto initial=pathText(sourcePath.parent_path());auto* directory=new QLineEdit(initial,&dialog);auto* choose=new QPushButton(QObject::tr("Choose parent folder…"),&dialog);form->addRow(QObject::tr("Parent folder"),directory);form->addRow(choose);
    auto* name=new QLineEdit(QFileInfo(pathText(sourcePath)).completeBaseName()+QStringLiteral("-python-export"),&dialog);form->addRow(QObject::tr("New folder name"),name);layout->addLayout(form);
    auto* report=new QTextEdit(&dialog);report->setReadOnly(true);report->setPlainText(QObject::tr("Python requires NumPy; figure adapters use Matplotlib. Explicit ODE adapters use SciPy BDF or MATLAB ode15s BDF and a bundled analytic AD Jacobian. Native-cache/MEX reuse is unsupported. Target arithmetic, derivative and solver differences are described in the report."));layout->addWidget(report);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Cancel,&dialog);auto* save=buttons->addButton(QObject::tr("Export to new folder"),QDialogButtonBox::AcceptRole);layout->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(choose,&QPushButton::clicked,&dialog,[&]{const auto selected=QFileDialog::getExistingDirectory(&dialog,QObject::tr("Choose export parent folder"),directory->text());if(!selected.isEmpty())directory->setText(selected);});
    QObject::connect(target,&QComboBox::currentIndexChanged,&dialog,[&]{adapter->setText(target->currentIndex()==0?QObject::tr("Allow explicit SciPy BDF adapter (different integrator from CVODE)"):QObject::tr("Allow explicit MATLAB ode15s BDF adapter (different integrator from CVODE)"));const auto base=QFileInfo(pathText(sourcePath)).completeBaseName();name->setText(base+(target->currentIndex()==0?QStringLiteral("-python-export"):QStringLiteral("-matlab-export")));});
    QObject::connect(save,&QPushButton::clicked,&dialog,[&]{
        const auto fail=[&](const QString& message){report->setPlainText(message);};
        const auto leaf=name->text().trimmed();if(leaf.isEmpty()||leaf==QStringLiteral(".")||leaf==QStringLiteral("..")||QFileInfo(leaf).fileName()!=leaf||leaf.contains('/')||leaf.contains('\\')){fail(QObject::tr("Choose a plain new folder name."));return;}
        const QFileInfo parentInfo(directory->text());if(!parentInfo.isDir()){fail(QObject::tr("The selected parent folder does not exist."));return;}const QDir folder(parentInfo.canonicalFilePath());const auto destination=folder.absoluteFilePath(leaf);
        if(QFileInfo::exists(destination)||QFileInfo(destination).isSymLink()){fail(QObject::tr("That destination already exists. Choose a new folder name; existing files will not be overwritten."));return;}
        auto settings=analysisOptions;if(!sourcePath.empty())settings.sourceDirectory=sourcePath.parent_path();const auto checked=language::analyse(source,settings);
        exporting::Options options;options.sourceName=sourcePath.empty()?"editor.p":pathText(sourcePath).toUtf8().toStdString();options.allowPythonBdfAdapter=target->currentIndex()==0&&adapter->isChecked();options.allowMatlabOde15sAdapter=target->currentIndex()==1&&adapter->isChecked();options.allowScientificAdapters=scientific->isChecked();
        std::string resourceError;if(!exporting::loadCompiledRuntimeFiles(options,target->currentIndex()==0?exporting::Target::Python:exporting::Target::Matlab,resourceError)){fail(text(resourceError));return;}
        const auto result=exporting::exportProgram(checked,target->currentIndex()==0?exporting::Target::Python:exporting::Target::Matlab,options);
        if(!result.ok()){QString message=QObject::tr("Export was not written:\n");for(const auto& diagnostic:result.diagnostics){const auto at=language::sourceLocation(source,diagnostic.span.offset);message+=QStringLiteral("%1:%2: %3: %4\n").arg(at.line).arg(at.column).arg(text(diagnostic.code),text(diagnostic.message));}fail(message);return;}
        QTemporaryDir staging(folder.absoluteFilePath(QStringLiteral(".penta-export-XXXXXX")));if(!staging.isValid()||QFileInfo(staging.path()).absolutePath()!=folder.absolutePath()){fail(QObject::tr("Cannot create an export staging folder in the selected parent."));return;}
        for(const auto& file:result.files){const auto fileName=text(file.name);if(QFileInfo(fileName).fileName()!=fileName){fail(QObject::tr("Unsafe generated file name; export cancelled."));return;}QSaveFile output(QDir(staging.path()).filePath(fileName));if(!output.open(QIODevice::WriteOnly)||output.write(file.contents.data(),static_cast<qint64>(file.contents.size()))!=static_cast<qint64>(file.contents.size())||!output.commit()){fail(QObject::tr("Could not save the complete export bundle."));return;}}
        if(QFileInfo::exists(destination)||!QDir().rename(staging.path(),destination)){fail(QObject::tr("The destination appeared or the final folder move failed. No existing files were overwritten."));return;}
        staging.setAutoRemove(false);outcome.written=true;outcome.directory=destination;outcome.summary=QObject::tr("Exported the checked supported subset to %1. Review compatibility.json before running it.").arg(destination);dialog.accept();
    });
    dialog.exec();return outcome;
}
}
