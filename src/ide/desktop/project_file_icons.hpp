// Adapted from GTG Cell Studio project_file_icons.hpp. See GTG_COMPONENT_PROVENANCE.txt.
#pragma once
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QIcon>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <array>

namespace penta::desktop {
enum class ProjectIconKind { Folder, DataFolder, OutputFolder, Model, Project, Membrane,
    Csv, Tsv, Spreadsheet, Protocol, Parameters, Figure, Image, Report, MathReport,
    Notes, Provenance, Configuration, Source, Header, Module, Log, Archive, File, Count };

inline ProjectIconKind projectIconKind(const QFileInfo& file){
    const auto name=file.fileName().toLower(),ext=file.suffix().toLower();
    if(file.isDir()){
        if(name=="data"||name=="datasets")return ProjectIconKind::DataFolder;
        if(name=="output"||name=="results")return ProjectIconKind::OutputFolder;
        return ProjectIconKind::Folder;
    }
    if(ext=="p")return ProjectIconKind::Model;
    if(ext=="pprj")return ProjectIconKind::Project;
    if(ext=="pws")return ProjectIconKind::Parameters;
    if(name=="provenance.json"||name.endsWith(".provenance.json"))return ProjectIconKind::Provenance;
    if(ext=="pworkspace"||ext=="mat"||ext=="checkpoint"||name.endsWith(".native-fit.json")||name.contains("parameters")||name.contains("checkpoint"))return ProjectIconKind::Parameters;
    if(ext=="csv")return ProjectIconKind::Csv;
    if(ext=="tsv")return ProjectIconKind::Tsv;
    if(ext=="xlsx"||ext=="xls"||ext=="ods")return ProjectIconKind::Spreadsheet;
    if(ext=="dat"||ext=="atf"||ext=="abf")return ProjectIconKind::Protocol;
    if(ext=="pfig"||ext=="fig")return ProjectIconKind::Figure;
    if(ext=="png"||ext=="jpg"||ext=="jpeg"||ext=="tif"||ext=="tiff"||ext=="bmp"||ext=="svg"||ext=="webp")return ProjectIconKind::Image;
    if(ext=="pdf"||ext=="html"||ext=="htm"||ext=="docx")return ProjectIconKind::Report;
    if(ext=="tex")return ProjectIconKind::MathReport;
    if(ext=="md"||ext=="rst")return ProjectIconKind::Notes;
    if(ext=="json"||ext=="ini"||ext=="yaml"||ext=="yml"||ext=="toml"||ext=="xml")return ProjectIconKind::Configuration;
    if(ext=="cpp"||ext=="c"||ext=="cc"||ext=="cxx"||ext=="m"||ext=="py"||ext=="cmake"||name=="cmakelists.txt")return ProjectIconKind::Source;
    if(ext=="hpp"||ext=="h"||ext=="hxx")return ProjectIconKind::Header;
    if(ext=="dll"||ext=="so"||ext=="dylib"||ext=="a"||ext=="lib"||ext=="exe")return ProjectIconKind::Module;
    if(ext=="log")return ProjectIconKind::Log;
    if(ext=="zip"||ext=="gz"||ext=="7z"||ext=="tar")return ProjectIconKind::Archive;
    if(ext=="txt")return ProjectIconKind::Protocol;
    return ProjectIconKind::File;
}

// Small semantic glyphs are rasterised once on the GUI thread at several sizes.
// QFileSystemModel's metadata requests only copy these cached QIcons.
inline QIcon projectKindIcon(ProjectIconKind kind){
    static std::array<QIcon,static_cast<int>(ProjectIconKind::Count)> cache;
    const int index=static_cast<int>(kind);if(index<0||index>=static_cast<int>(cache.size()))return {};
    auto& icon=cache[index];if(!icon.isNull())return icon;
    for(const int size:{12,16,24,32,48,64}){
        QPixmap pixmap(size,size);pixmap.fill(Qt::transparent);QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);p.scale(size/24.,size/24.);
        const QColor blue("#227cae"),teal("#078e8c"),violet("#7959a6"),ink("#536c7b"),gold("#d99529");
        const auto pen=[&](QColor c,qreal w=1.4){p.setPen(QPen(c,w,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);};
        const auto line=[&](std::initializer_list<QPointF> points,QColor c,qreal w=1.45){pen(c,w);QPainterPath path;bool first=true;for(const auto pt:points){if(first){path.moveTo(pt);first=false;}else path.lineTo(pt);}p.drawPath(path);};
        const auto document=[&](QColor c){pen(c,1.15);p.setBrush(QColor("#f6fbfd"));QPainterPath path;path.moveTo(4,2);path.lineTo(14,2);path.lineTo(20,8);path.lineTo(20,22);path.lineTo(4,22);path.closeSubpath();p.drawPath(path);line({{14,2},{14,8},{20,8}},c,.9);};
        const auto folder=[&](QColor c){pen(c.darker(115),1.1);p.setBrush(c.lighter(160));p.drawPolygon(QPolygonF{{2,5},{9,5},{11,8},{22,8},{21,21},{2,21}});p.setBrush(c.lighter(186));p.drawRoundedRect(QRectF(2,9,20,12),1.4,1.4);};
        [[maybe_unused]] const auto channel=[&](QColor c,qreal shift=0){
            pen(c.lighter(145),1.15);p.drawLine(QPointF(3,11+shift),QPointF(7,11+shift));p.drawLine(QPointF(17,11+shift),QPointF(21,11+shift));p.drawLine(QPointF(3,16+shift),QPointF(7,16+shift));p.drawLine(QPointF(17,16+shift),QPointF(21,16+shift));
            pen(c,1.15);p.setBrush(c.lighter(155));QPainterPath a;a.moveTo(7,8+shift);a.cubicTo(12,7+shift,8,13+shift,10,18+shift);a.lineTo(7,18+shift);a.cubicTo(5,14+shift,6,10+shift,7,8+shift);p.drawPath(a);QPainterPath b;b.moveTo(17,8+shift);b.cubicTo(12,7+shift,16,13+shift,14,18+shift);b.lineTo(17,18+shift);b.cubicTo(19,14+shift,18,10+shift,17,8+shift);p.drawPath(b);
            pen(gold,.7);p.setBrush(gold);p.drawEllipse(QPointF(12,10+shift),1.1,1.1);p.drawEllipse(QPointF(12,15+shift),.9,.9);
        };
        const auto curve=[&](QColor c){pen(ink,.9);p.drawLine(6,19,18,19);p.drawLine(6,19,6,10);pen(c,1.6);QPainterPath path;path.moveTo(7,17);path.cubicTo(10,18,12,10,18,11);p.drawPath(path);};
        const auto nodes=[&](QColor c){line({{7,17},{12,10},{18,17},{7,17}},ink,1);pen(c,1.3);p.setBrush(Qt::white);for(auto pt:{QPointF(7,17),QPointF(12,10),QPointF(18,17)})p.drawEllipse(pt,1.9,1.9);};
        switch(kind){
        case ProjectIconKind::Folder:folder(blue);line({{7,17},{12,12},{17,17}},blue,1.2);p.setBrush(teal);for(auto pt:{QPointF(7,17),QPointF(12,12),QPointF(17,17)})p.drawEllipse(pt,1,1);break;
        case ProjectIconKind::DataFolder:folder(teal);nodes(teal);break;
        case ProjectIconKind::OutputFolder:folder(violet);curve(violet);p.setPen(Qt::NoPen);p.setBrush(gold);p.drawEllipse(QPointF(17,12),1.4,1.4);break;
        case ProjectIconKind::Model:document(teal);nodes(teal);break;
        case ProjectIconKind::Project:document(blue);nodes(teal);break;
        case ProjectIconKind::Membrane:document(teal);line({{5,14},{8,14},{10,10},{11,18},{13,13},{18,13}},violet);break;
        case ProjectIconKind::Csv:case ProjectIconKind::Tsv:case ProjectIconKind::Spreadsheet:{
            const auto c=kind==ProjectIconKind::Spreadsheet?teal:kind==ProjectIconKind::Csv?blue:violet;document(c);pen(c,.8);for(int x:{7,12,17})p.drawLine(x,10,x,19);for(int y:{10,14,19})p.drawLine(7,y,17,y);
            p.setPen(Qt::NoPen);p.setBrush(c);p.drawEllipse(QPointF(kind==ProjectIconKind::Tsv?16:9,kind==ProjectIconKind::Spreadsheet?17:12),1.6,1.6);break;}
        case ProjectIconKind::Protocol:document(blue);line({{6,17},{9,17},{9,11},{14,11},{14,17},{18,17}},teal);break;
        case ProjectIconKind::Parameters:document(violet);curve(violet);pen(gold,1);p.setBrush(Qt::white);for(auto pt:{QPointF(8,17),QPointF(13,13),QPointF(17,11)})p.drawEllipse(pt,1.1,1.1);break;
        case ProjectIconKind::Figure:document(blue);curve(teal);pen(gold,1);p.drawLine(15,13,18,10);break;
        case ProjectIconKind::Image:document(violet);pen(teal,1);p.setBrush(QColor("#ceece7"));p.drawEllipse(QPointF(12,15),5,3.6);p.setBrush(violet.lighter(170));p.drawEllipse(QPointF(12,15),1.7,1.3);break;
        case ProjectIconKind::Report:document(blue);pen(ink,1);p.drawLine(7,10,16,10);p.drawLine(7,12,14,12);p.fillRect(QRectF(7,16,2,3),teal);p.fillRect(QRectF(11,14,2,5),blue);p.fillRect(QRectF(15,13,2,6),gold);break;
        case ProjectIconKind::MathReport:document(violet);line({{16,10},{8,10},{12,14},{8,19},{16,19}},violet,1.8);break;
        case ProjectIconKind::Notes:document(blue);for(int y:{10,14,18}){pen(y==14?teal:ink,1);p.drawLine(7,y,y==18?14:17,y);}break;
        case ProjectIconKind::Provenance:document(gold);line({{7,11},{12,14},{17,11}},ink);line({{12,14},{12,19}},ink);pen(teal,1.2);p.setBrush(Qt::white);for(auto pt:{QPointF(7,11),QPointF(17,11),QPointF(12,19)})p.drawEllipse(pt,1.6,1.6);break;
        case ProjectIconKind::Configuration:document(blue);pen(teal,1.2);for(int y:{11,15,19})p.drawLine(7,y,17,y);p.setBrush(Qt::white);p.drawEllipse(QPointF(10,11),1.4,1.4);p.drawEllipse(QPointF(14,15),1.4,1.4);p.drawEllipse(QPointF(11,19),1.4,1.4);break;
        case ProjectIconKind::Source:case ProjectIconKind::Header:document(kind==ProjectIconKind::Header?teal:blue);line({{9,11},{6,14},{9,17}},blue);line({{15,11},{18,14},{15,17}},blue);pen(teal,1.2);if(kind==ProjectIconKind::Header){p.drawLine(11,11,11,17);p.drawLine(13,11,13,17);p.drawLine(10,13,14,13);p.drawLine(10,15,14,15);}else p.drawLine(13,11,11,17);break;
        case ProjectIconKind::Module:document(violet);pen(violet,1);p.setBrush(QColor("#e7dff1"));p.drawRect(QRectF(8,11,8,8));for(int y:{12,15,18}){p.drawLine(6,y,8,y);p.drawLine(16,y,18,y);}p.setBrush(teal);p.drawEllipse(QPointF(12,15),1.7,1.7);break;
        case ProjectIconKind::Log:document(ink);line({{7,11},{10,14},{7,17}},teal);pen(ink,1.1);p.drawLine(12,17,17,17);break;
        case ProjectIconKind::Archive:document(gold);pen(gold,1.4);for(int y:{4,7,10,13,16,19})p.drawLine(11,y,13,y);break;
        case ProjectIconKind::File:case ProjectIconKind::Count:document(ink);pen(teal,1);p.drawEllipse(QPointF(12,15),3.6,3.6);p.setBrush(teal);p.drawEllipse(QPointF(12,15),1,1);break;
        }
        p.end();icon.addPixmap(pixmap);
    }
    return icon;
}

class StudioFileIconProvider final:public QObject,public QFileIconProvider {
public:
    explicit StudioFileIconProvider(QObject* parent=nullptr):QObject(parent){
        for(int i=0;i<static_cast<int>(ProjectIconKind::Count);++i)icons_[i]=projectKindIcon(static_cast<ProjectIconKind>(i));
    }
    QIcon icon(const QFileInfo& info)const override{return icons_[static_cast<int>(projectIconKind(info))];}
    QIcon icon(IconType type)const override{return icons_[static_cast<int>(type==Folder||type==Drive||type==Computer?ProjectIconKind::Folder:ProjectIconKind::File)];}
    QString type(const QFileInfo& info)const override{
        // QFileSystemModel requests types on its GUI thread, including for
        // hidden columns. The default MIME lookup may read each file's contents.
        // Browsing a folder needs only its name and the gathered directory flag.
        if(info.isDir())return tr("Folder");
        const auto suffix=info.suffix();return suffix.isEmpty()?tr("File"):tr("%1 file").arg(suffix.toUpper());
    }
private:std::array<QIcon,static_cast<int>(ProjectIconKind::Count)> icons_;
};
inline void installProjectIcons(QFileSystemModel* model){if(model)model->setIconProvider(new StudioFileIconProvider(model));}
}
