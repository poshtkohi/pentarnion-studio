#include "ide/desktop/about_dialog.hpp"
#include "ide/desktop/pane_hover.hpp"
#include "ide/desktop/studio_icons.hpp"
#include "ide/studio_branding.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDate>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QList>
#include <QPair>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QRadialGradient>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QScreen>
#include <QResizeEvent>
#include <QSplitter>
#include <QSysInfo>
#include <QTabBar>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVariant>
#include <algorithm>
#include <array>
#include <cmath>

namespace penta::desktop {
namespace {
QString tr(const char* value){return QCoreApplication::translate("PentarnionAbout",value);}
class AboutArtwork final:public QWidget {
public:
    explicit AboutArtwork(QWidget* parent):QWidget(parent) {
        setObjectName(QStringLiteral("aboutAlgebraArtwork"));setMinimumWidth(96);setMaximumWidth(190);
        setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Expanding);
        setAccessibleName(tr("Five algebra basis labels joined by a graph and four quaternion-inspired orbits"));
        for(std::size_t i=0;i<m_orbits.size();++i){QPainterPath orbit;orbit.addEllipse(QRectF(-97,-31,194,62));QTransform rotation;rotation.rotate(static_cast<int>(i)*43-30);m_orbits[i]=rotation.map(orbit);}
        const std::array<QString,5> labels{{QStringLiteral("1"),QStringLiteral("i"),QStringLiteral("j"),QStringLiteral("k"),QString::fromUtf8("ℓ")}};
        std::array<QPointF,5> vertices;constexpr double pi=3.14159265358979323846;
        for(std::size_t i=0;i<vertices.size();++i){const double angle=-pi/2+2*pi*i/vertices.size();vertices[i]={72*std::cos(angle),65*std::sin(angle)};m_nodes.addEllipse(vertices[i],4,4);}
        for(std::size_t i=0;i<vertices.size();++i)for(std::size_t j=i+1;j<vertices.size();++j){m_edges.moveTo(vertices[i]);m_edges.lineTo(vertices[j]);}
        QFont basis=font();basis.setPointSize(14);basis.setItalic(true);
        for(std::size_t i=0;i<labels.size();++i){
            QPainterPath glyph;glyph.addText(QPointF{},basis,labels[i]);const auto center=vertices[i]*1.19-glyph.boundingRect().center();
            QTransform placement;placement.translate(center.x(),center.y());m_labels[i]=placement.map(glyph);
        }
        const auto include=[this](const QPainterPath& path,qreal penWidth){
            QPainterPathStroker stroke;stroke.setWidth(penWidth);const auto bounds=penWidth>0?stroke.createStroke(path).boundingRect().united(path.boundingRect()):path.boundingRect();
            m_bounds=m_bounds.isEmpty()?bounds:m_bounds.united(bounds);
        };
        for(const auto& orbit:m_orbits)include(orbit,1.1);
        include(m_edges,.9);include(m_nodes,1);
        for(const auto& glyph:m_labels)include(glyph,0);
        placeArtwork();
    }
    QSize sizeHint()const override{return {180,146};}
protected:
    void resizeEvent(QResizeEvent* event)override{QWidget::resizeEvent(event);placeArtwork();}
    void paintEvent(QPaintEvent*)override {
        if(m_available.isEmpty())return;
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.setWorldTransform(m_fit);
        painter.setBrush(Qt::NoBrush);
        for(std::size_t i=0;i<m_orbits.size();++i){painter.setPen(QPen(QColor(123,236,241,100+static_cast<int>(i)*20),1.1));painter.drawPath(m_orbits[i]);}
        painter.setPen(QPen(QColor(122,219,237,110),.9));painter.drawPath(m_edges);
        painter.setPen(QPen(QColor(211,254,255),1));painter.setBrush(QColor(57,195,208));painter.drawPath(m_nodes);
        painter.setPen(Qt::NoPen);painter.setBrush(QColor(223,254,255));for(const auto& glyph:m_labels)painter.drawPath(glyph);
    }
private:
    void placeArtwork(){
        m_available=QRectF(rect()).adjusted(8,8,-8,-8);m_fit=QTransform{};setProperty("artworkAvailableRect",m_available);
        if(m_available.isEmpty()||m_bounds.isEmpty()){setProperty("artworkPaintBounds",QRectF{});setProperty("artworkLabelBounds",QVariantList{});return;}
        const auto scale=std::min(m_available.width()/m_bounds.width(),m_available.height()/m_bounds.height());
        m_fit.translate(m_available.center().x(),m_available.center().y());m_fit.scale(scale,scale);m_fit.translate(-m_bounds.center().x(),-m_bounds.center().y());
        // These describe the actual paths painted above, including stroke
        // widths and glyph outlines, and support accessibility/geometry checks.
        setProperty("artworkPaintBounds",m_fit.mapRect(m_bounds));
        QVariantList labels;for(const auto& glyph:m_labels)labels.append(m_fit.mapRect(glyph.boundingRect()));setProperty("artworkLabelBounds",labels);
        update();
    }
    std::array<QPainterPath,4> m_orbits;
    std::array<QPainterPath,5> m_labels;
    QPainterPath m_edges,m_nodes;
    QRectF m_bounds,m_available;
    QTransform m_fit;
};
class AboutBanner final:public QWidget {
public:
    AboutBanner(const QString& version,QWidget* parent):QWidget(parent) {
        setObjectName(QStringLiteral("aboutBanner"));setFixedHeight(170);
        auto* row=new QHBoxLayout(this);row->setContentsMargins(18,12,18,12);row->setSpacing(16);
        auto* logo=new QLabel(this);logo->setObjectName(QStringLiteral("aboutStudioLogo"));logo->setFixedSize(72,72);
        logo->setAccessibleName(tr("Pentarnion Studio logo: four quaternion-inspired arcs and five Pentarnion-inspired vertices"));
        logo->setPixmap(studioApplicationIcon().pixmap(72,72));row->addWidget(logo);
        auto* details=new QVBoxLayout;details->setSpacing(3);details->addStretch();
        auto* name=new QLabel(QStringLiteral("Pentarnion Studio"),this);name->setObjectName(QStringLiteral("aboutStudioName"));
        auto font=name->font();font.setPointSize(25);font.setBold(true);name->setFont(font);name->setWordWrap(true);name->setStyleSheet(QStringLiteral("color:white;"));details->addWidget(name);
        auto* tagline=new QLabel(tr("Algebra, scientific computing and visualization"),this);
        tagline->setObjectName(QStringLiteral("aboutTagline"));tagline->setWordWrap(true);tagline->setStyleSheet(QStringLiteral("color:#dbeef2;"));details->addWidget(tagline);
        auto* number=new QLabel(tr("Version %1").arg(version),this);number->setObjectName(QStringLiteral("aboutVersion"));
        number->setStyleSheet(QStringLiteral("color:#dbeef2;"));number->setTextInteractionFlags(Qt::TextSelectableByMouse);details->addWidget(number);
        details->addStretch();row->addLayout(details,1);m_artwork=new AboutArtwork(this);row->addWidget(m_artwork);
    }
protected:
    void paintEvent(QPaintEvent*)override {
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        QLinearGradient background(0,0,width(),height());
        background.setColorAt(0,QColor(QStringLiteral("#102d43")));background.setColorAt(.65,QColor(QStringLiteral("#105d71")));background.setColorAt(1,QColor(QStringLiteral("#132746")));
        painter.fillRect(rect(),background);
        // The child fits every orbit, node and glyph into its own safe inset.
        // The glow belongs to the background and may extend beyond that inset.
        const QPointF center=m_artwork->geometry().center();
        QRadialGradient glow(center,150);glow.setColorAt(0,QColor(67,223,221,110));glow.setColorAt(1,QColor(20,75,110,0));painter.fillRect(rect(),glow);
    }
private:AboutArtwork* m_artwork=nullptr;
};
QTreeWidget* tree(QWidget* parent,const QString& name,const QStringList& headings) {
    auto* widget=new QTreeWidget(parent);widget->setObjectName(name);widget->setHeaderLabels(headings);
    widget->setRootIsDecorated(false);widget->setAlternatingRowColors(true);widget->setWordWrap(true);
    widget->setFrameShape(QFrame::NoFrame);widget->header()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    widget->header()->setStretchLastSection(true);return widget;
}
void detail(QTreeWidget* target,const QString& key,const QString& value) {
    auto* row=new QTreeWidgetItem(target,{key,value});row->setToolTip(1,value);
}
QLabel* label(QWidget* parent,const QString& name,const QString& text,bool bold=false) {
    auto* result=new QLabel(text,parent);result->setObjectName(name);result->setWordWrap(true);
    result->setTextFormat(Qt::PlainText);result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if(bold){auto font=result->font();font.setBold(true);result->setFont(font);}return result;
}
QLabel* emailLabel(QWidget* parent,const QString& name,const QString& address){
    auto* result=label(parent,name,{});result->setTextFormat(Qt::RichText);
    result->setText(QStringLiteral("<a href=\"mailto:%1\" style=\"color:#0067b8;text-decoration:underline\">%1</a>").arg(address.toHtmlEscaped()));
    result->setTextInteractionFlags(Qt::TextBrowserInteraction);result->setOpenExternalLinks(true);result->setToolTip(tr("Send email to %1").arg(address));return result;
}
QWidget* authors(QWidget* parent) {
    auto* scroll=new QScrollArea(parent);scroll->setObjectName(QStringLiteral("aboutAuthors"));scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);auto* page=new QWidget(scroll);auto* layout=new QVBoxLayout(page);
    layout->setContentsMargins(15,15,15,10);layout->setSpacing(7);
    layout->addWidget(label(page,QStringLiteral("aboutAuthor1Name"),QStringLiteral("Dr Alireza Poshtkohi"),true));
    layout->addWidget(label(page,QStringLiteral("aboutAuthor1Affiliation"),QStringLiteral("School of Physics, Engineering and Computer Science, University of Hertfordshire, Hatfield, Hertfordshire, United Kingdom")));
    layout->addWidget(emailLabel(page,QStringLiteral("aboutAuthor1Email"),QStringLiteral("a.poshtkohi@herts.ac.uk")));
    layout->addSpacing(14);
    layout->addWidget(label(page,QStringLiteral("aboutAuthor2Name"),QString::fromUtf8("Dr Susanne Pumplün"),true));
    layout->addWidget(label(page,QStringLiteral("aboutAuthor2Affiliation"),QStringLiteral("School of Mathematical Sciences, University of Nottingham, Nottingham NG7 2RD, United Kingdom")));
    layout->addWidget(emailLabel(page,QStringLiteral("aboutAuthor2Email"),QStringLiteral("Susanne.Pumpluen@nottingham.ac.uk")));
    layout->addStretch();scroll->setWidget(page);return scroll;
}
QStringList defaultLicenseRoots() {
    const QDir binary(QCoreApplication::applicationDirPath());
#ifdef PENTA_DISTRIBUTION_BUILD
    return {binary.filePath(QStringLiteral("../licenses")),binary.filePath(QStringLiteral("licenses")),
            binary.filePath(QStringLiteral("../THIRD_PARTY_NOTICES"))};
#else
    return {binary.filePath(QStringLiteral("../THIRD_PARTY_NOTICES")),binary.filePath(QStringLiteral("../share/pentarnion/licenses")),binary.filePath(QStringLiteral("../share/pentarnion/third-party/licenses")),
            binary.filePath(QStringLiteral("../share/pentarnion/third-party/runtime-licenses")),binary.filePath(QStringLiteral("../share/licenses")),
            binary.filePath(QStringLiteral("../../../third-party/licenses"))};
#endif
}
QWidget* notices(QWidget* parent,const QStringList& roots) {
    auto* page=new QWidget(parent);auto* outer=new QVBoxLayout(page);outer->setContentsMargins(10,10,10,8);outer->setSpacing(7);
    outer->addWidget(label(page,QStringLiteral("aboutLicensesScope"),tr("Third-party notices supplied with this installation")));
    outer->addWidget(label(page,QStringLiteral("aboutQtNotice"),tr("Built with Qt 6. Qt and the other open-source libraries retain their respective licences.")));
    auto* splitter=new QSplitter(Qt::Horizontal,page);outer->addWidget(splitter,1);
    auto* list=tree(splitter,QStringLiteral("aboutLicenseFiles"),{tr("Notice")});
    auto* text=new QPlainTextEdit(splitter);text->setObjectName(QStringLiteral("aboutLicenseText"));text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::WidgetWidth);splitter->setStretchFactor(1,1);splitter->setSizes({210,400});
    QStringList found;int visited=0;
    for(const auto& location:roots){
        const QDir root(location);const auto canonical=root.canonicalPath();if(canonical.isEmpty())continue;
        QDirIterator iterator(canonical,QDir::Files|QDir::Readable,QDirIterator::Subdirectories);
        while(iterator.hasNext()&&found.size()<512&&visited++<8192){
            const auto path=iterator.next();const QFileInfo info(path);const auto resolved=info.canonicalFilePath();
            if(!resolved.startsWith(canonical+QLatin1Char('/'),Qt::CaseInsensitive)||info.isSymLink()||info.size()>2*1024*1024)continue;
            const auto name=info.fileName().toLower();
            if(!name.contains(QStringLiteral("license"))&&!name.contains(QStringLiteral("licence"))&&!name.contains(QStringLiteral("copying"))&&!name.contains(QStringLiteral("notice"))&&info.suffix().compare(QStringLiteral("txt"),Qt::CaseInsensitive)!=0)continue;
            if(found.contains(resolved))continue;
            found.push_back(resolved);
            auto* item=new QTreeWidgetItem(list,{root.relativeFilePath(path)});item->setData(0,Qt::UserRole,resolved);item->setToolTip(0,resolved);
        }
    }
    QObject::connect(list,&QTreeWidget::currentItemChanged,page,[text](QTreeWidgetItem* item){
        if(!item)return;
        QFile file(item->data(0,Qt::UserRole).toString());
        if(!file.open(QIODevice::ReadOnly)||file.size()>2*1024*1024){text->setPlainText(tr("This notice could not be read."));return;}
        text->setPlainText(QString::fromUtf8(file.readAll()));
    });
    if(list->topLevelItemCount())list->setCurrentItem(list->topLevelItem(0));
    else text->setPlainText(tr("No notice files were found in the supplied installation paths."));
    return page;
}
}
QString studioCopyrightText(int year) {
    if(!year)year=QDate::currentDate().year();
    year=std::max(2026,year);
    const auto years=year==2026?QStringLiteral("2026"):QString::fromUtf8("2026–%1").arg(year);
    return QString::fromUtf8("© %1 Alireza Poshtkohi and Susanne Pumplün").arg(years);
}
AboutDialog::AboutDialog(QWidget* parent,const AboutOptions& options):QDialog(parent) {
    auto reading=QFontDatabase::systemFont(QFontDatabase::GeneralFont);const auto families=QFontDatabase::families();
    for(const auto* family:{"Segoe UI","Noto Sans","DejaVu Sans","Arial"})if(families.contains(QString::fromLatin1(family),Qt::CaseInsensitive)){reading.setFamily(QString::fromLatin1(family));break;}
    reading.setStyleHint(QFont::SansSerif);reading.setStretch(QFont::Unstretched);reading.setWeight(QFont::Normal);reading.setPointSizeF(10);setFont(reading);
    setObjectName(QStringLiteral("aboutStudio"));
    setWindowIcon(studioApplicationIcon());setWindowFlag(Qt::WindowContextHelpButtonHint,false);
    auto version=options.version;if(version.isEmpty())version=QCoreApplication::applicationVersion();if(version.isEmpty())version=QStringLiteral("0.2");
    setWindowTitle(tr("About Pentarnion Studio v%1").arg(version));
    auto* outer=new QVBoxLayout(this);outer->setContentsMargins(12,12,12,10);outer->setSpacing(9);outer->addWidget(new AboutBanner(version,this));
    auto* tabs=new QTabWidget(this);tabs->setObjectName(QStringLiteral("aboutTabs"));outer->addWidget(tabs,1);
    tabs->tabBar()->setProperty("studioPaneHoverSelectedTabOnly",true);tabs->tabBar()->setProperty("studioPanePersistentSelectedTab",true);
    registerStudioPaneHover(tabs,tabs->tabBar());
    tabs->addTab(authors(tabs),studioCommandIcon(QStringLiteral("Project")),tr("Authors"));
    auto* overview=tree(tabs,QStringLiteral("aboutOverview"),{tr("Area"),tr("Tools")});
    detail(overview,tr("Algebra"),tr("Pentarnions · native algebra families · multiplication tables"));
    detail(overview,tr("Language"),tr("Editor · interpreter · cached native callbacks"));
    detail(overview,tr("Scientific computing"),tr("ODEs · PDEs · matrices · optimization · chaos"));
    detail(overview,tr("Figures"),tr("2D/3D · layouts · LaTeX · image/vector export"));
    detail(overview,tr("Desktop"),tr("Classic/Ribbon · Command Window · Help · Copilot · workbooks"));
    tabs->addTab(overview,studioCommandIcon(QStringLiteral("About")),tr("Overview"));
    auto* libraries=tree(tabs,QStringLiteral("aboutLibraries"),{tr("Component"),tr("Role")});
    for(const auto& row:QList<QPair<QString,QString>>{
        {QStringLiteral("Qt %1").arg(QString::fromLatin1(qVersion())),tr("Desktop interface")},{QStringLiteral("VTK"),tr("Scientific scenes")},
        {QStringLiteral("SUNDIALS"),tr("ODE / DAE integration")},{QStringLiteral("OpenBLAS / Eigen"),tr("Linear algebra")},
        {QStringLiteral("Ceres / NLopt / pagmo"),tr("Fitting and optimization")},{QStringLiteral("MFEM / PETSc"),tr("Finite elements and numerical solvers")},
        {QStringLiteral("XLSXIO / Expat / minizip / zlib"),tr("Native workbook data")},{QStringLiteral("JKQTMathText"),tr("Mathematical labels")}})
        detail(libraries,row.first,row.second);
    tabs->addTab(libraries,studioCommandIcon(QStringLiteral("Build")),tr("Libraries"));
    tabs->addTab(notices(tabs,options.licenseRoots.isEmpty()?defaultLicenseRoots():options.licenseRoots),studioCommandIcon(QStringLiteral("Help")),tr("Licences"));
    auto* build=tree(tabs,QStringLiteral("aboutBuildInformation"),{tr("Detail"),tr("Value")});
    detail(build,tr("Studio"),version);detail(build,tr("Qt"),QString::fromLatin1(qVersion()));
    detail(build,tr("Platform"),QSysInfo::prettyProductName());detail(build,tr("Architecture"),QSysInfo::currentCpuArchitecture());
    detail(build,tr("Qt platform"),QGuiApplication::platformName());detail(build,tr("Pointer size"),tr("%1 bits").arg(sizeof(void*)*8));
#if defined(__clang__)
    detail(build,tr("Compiler"),QStringLiteral("Clang ")+QString::fromLatin1(__clang_version__));
#elif defined(__GNUC__)
    detail(build,tr("Compiler"),QStringLiteral("GCC ")+QString::fromLatin1(__VERSION__));
#elif defined(_MSC_VER)
    detail(build,tr("Compiler"),QStringLiteral("MSVC %1").arg(_MSC_VER));
#endif
    tabs->addTab(build,studioCommandIcon(QStringLiteral("View")),tr("Build"));
    auto* footer=new QHBoxLayout;
    auto* copyright=label(this,QStringLiteral("aboutCopyright"),studioCopyrightText());
    copyright->setWordWrap(true);copyright->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);footer->addWidget(copyright,1);
    auto* website=new QLabel(QStringLiteral("<a href=\"https://pentarnion.com\">pentarnion.com</a>"),this);
    website->setObjectName(QStringLiteral("aboutWebsite"));website->setTextFormat(Qt::RichText);
    website->setTextInteractionFlags(Qt::TextBrowserInteraction);website->setOpenExternalLinks(true);
    website->setToolTip(tr("Open Pentarnion website"));website->setAccessibleName(tr("Pentarnion website"));
    website->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Preferred);footer->addSpacing(10);footer->addWidget(website);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,this);buttons->setObjectName(QStringLiteral("aboutButtons"));
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);footer->addWidget(buttons);outer->addLayout(footer);applyStudioButtonIcons(this);
    resize(720,555);if(auto* target=screen())resize(size().boundedTo(target->availableGeometry().size()-QSize(40,60)));
}
}
