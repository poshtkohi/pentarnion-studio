// Adapted from GTG Cell Studio studio_icons.hpp. See GTG_COMPONENT_PROVENANCE.txt.
#pragma once
#include <QAction>
#include <QColor>
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QMenuBar>
#include <QString>
#include <QWidget>
#include <cmath>

namespace penta::desktop {
namespace studio_icons_detail {
enum class Kind {
    NewModel,NewProject,OpenModel,OpenProject,Save,SaveAs,Print,Preview,Exit,
    Check,Run,Fit,Stop,Undo,Redo,Cut,Copy,Paste,SelectAll,Find,Replace,
    FindPrevious,FindNext,Edit,Report,Log,Help,About,View,Tabbed,Split,
    ProjectMinimize,CommandMinimize,Restore,Classic,Ribbon,Collapse,Expand,
    Font,FontSize,Location,DefaultView,Outline,Project,Command,Distill,
    Sensitivity,Generate,Build,SelectFit,Chart,ZoomIn,ZoomOut,Pointer,Pan,
    DataCursor,Grid,Legend,Styles,Tile,Float,Close,Previous,Next,Apply,Cancel,
    FitPage,FitWidth,Convert,Options,Statistics,Copilot,Attach,Shuffle,CloseAll,Snapshot
};
inline Kind kind(const QString& text,const QString& objectName){
    auto label=text;label.remove('&');label.remove(QChar(0x2026));label=label.simplified().toLower();
    const auto name=objectName.toLower();const auto key=name+' '+label;
    if(label.startsWith("snapshot")||name=="editsnapshot"||name=="pdfsnapshot")return Kind::Snapshot;
    if(label=="add file"||label=="attach file"||name=="copilotaddfile")return Kind::Attach;
    if(label.startsWith("shuffle"))return Kind::Shuffle;
    if(label=="close all"||name=="filecloseall")return Kind::CloseAll;
    if(label=="copilot"||name=="viewcopilot")return Kind::Copilot;
    if(label=="cancel"||label=="no")return Kind::Cancel;
    if(label=="close")return Kind::Close;
    if(label=="ok"||label=="yes"||label=="apply"||label=="accept"||label=="done")return Kind::Apply;
    if(label=="fit page"||label=="fit to page")return Kind::FitPage;
    if(label=="fit width"||label=="fit to width")return Kind::FitWidth;
    if(key.contains("plotstylepreview"))return Kind::Chart;
    if(key.contains("minimizecommand")||label=="minimize command window")return Kind::CommandMinimize;
    if(key.contains("minimizeproject")||label=="minimize project")return Kind::ProjectMinimize;
    if(key.contains("restore")&&!key.contains("view"))return Kind::Restore;
    if(key.contains("default")||key.contains("restoreview")||label=="reset view")return Kind::DefaultView;
    if(key.contains("fontsize")||label.contains("font size"))return Kind::FontSize;
    if(key.contains("font"))return Kind::Font;
    if(key.contains("location")||label.contains("current folder"))return Kind::Location;
    if(key.contains("sidebyside")||label=="side by side")return Kind::Split;
    if(key.contains("tabbed"))return Kind::Tabbed;
    if(key.contains("interfaceclassic")||label=="classic")return Kind::Classic;
    if(key.contains("collapse")||key.contains("tabsonly")||label=="tabs only")return Kind::Collapse;
    if(key.contains("ribbonexpanded")||label=="full ribbon")return Kind::Expand;
    if(key.contains("toolstrip")||key.contains("ribbon"))return Kind::Ribbon;
    if(label.contains("new project"))return Kind::NewProject;
    if(label.startsWith("new"))return Kind::NewModel;
    if(label.contains("open project"))return Kind::OpenProject;
    if(label.startsWith("open"))return Kind::OpenModel;
    if(label.contains("save as"))return Kind::SaveAs;
    if(label.startsWith("save"))return Kind::Save;
    if(key.contains("printpreview")||label.contains("preview"))return Kind::Preview;
    if(key.contains("print"))return Kind::Print;
    if(label=="exit"||label=="quit")return Kind::Exit;
    if(key.contains("findprevious")||label=="find previous")return Kind::FindPrevious;
    if(key.contains("findnext")||label=="find next")return Kind::FindNext;
    if(key.contains("replace"))return Kind::Replace;
    if(key.contains("find")||label.contains("search"))return Kind::Find;
    if(label=="undo")return Kind::Undo;
    if(label=="redo")return Kind::Redo;
    if(label=="cut")return Kind::Cut;
    if(key.contains("copy"))return Kind::Copy;
    if(label=="paste")return Kind::Paste;
    if(label=="select all")return Kind::SelectAll;
    if(key.contains("stop")||label=="abort")return Kind::Stop;
    if(key.contains("cancel")||label=="no")return Kind::Cancel;
    if(label=="close"||key.contains("figureclose"))return Kind::Close;
    if(key.contains("parse")||key.contains("check")||label=="check model")return Kind::Check;
    if(key.contains("selectfitted")||key.contains("selectfit")||label.contains("select fitted"))return Kind::SelectFit;
    if(key.contains("distill")||label.contains("finalize"))return Kind::Distill;
    if(label.startsWith("convert"))return Kind::Convert;
    if(key.contains("sensitivity"))return Kind::Sensitivity;
    if(key.contains("native-compile")||label.contains("generate model"))return Kind::Generate;
    if(key.contains("build"))return Kind::Build;
    if(key.contains("report"))return Kind::Report;
    if(key.contains("log")||label=="command window")return Kind::Log;
    if(key.contains("simulate")||label.startsWith("run ")||label=="run")return Kind::Run;
    if(key.contains("statistics")||label=="fit statistics")return Kind::Statistics;
    if(key.contains("fit"))return Kind::Fit;
    if(label=="ok"||label=="yes"||label=="apply"||label=="accept")return Kind::Apply;
    if(key.contains("about"))return Kind::About;
    if(key.contains("help")||key.contains("workflow"))return Kind::Help;
    if(key.contains("outline")||label.contains("topology"))return Kind::Outline;
    if(label=="project")return Kind::Project;
    if(key.contains("zoomout")||label=="zoom out")return Kind::ZoomOut;
    if(key.contains("zoomin")||label=="zoom in")return Kind::ZoomIn;
    if(key.contains("datacursor")||label=="data cursor")return Kind::DataCursor;
    if(key.contains("inspect")||label=="pointer")return Kind::Pointer;
    if(label=="pan"||key.contains("figurepan"))return Kind::Pan;
    if(key.contains("grid"))return Kind::Grid;
    if(key.contains("legend"))return Kind::Legend;
    if(key.contains("styles")||label=="appearance")return Kind::Styles;
    if(key.contains("tile")||label=="subplots")return Kind::Tile;
    if(key.contains("float")||key.contains("undock")||key.contains("docktoggle"))return Kind::Float;
    if(key.contains("previous"))return Kind::Previous;
    if(key.contains("next"))return Kind::Next;
    if(label.contains("figure")||label.contains("plot")||label=="results")return Kind::Chart;
    if(label=="editing"||label.startsWith("edit "))return Kind::Edit;
    if(label.contains("view options")||label=="axes")return Kind::View;
    return Kind::Options;
}
inline void draw(QPainter& p,Kind icon){
    const QColor blue("#287bb5"),teal("#1b9a91"),green("#33975b"),red("#c24e50"),purple("#7960b5"),gold("#dea53f"),ink("#4d6172"),pale("#e8f1f7");
    p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(ink,1.55,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    const auto pen=[&](QColor c,qreal width=1.7){p.setPen(QPen(c,width,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);};
    const auto plus=[&](qreal x,qreal y,QColor c){pen(c,2);p.drawLine(QPointF(x-3,y),QPointF(x+3,y));p.drawLine(QPointF(x,y-3),QPointF(x,y+3));};
    const auto check=[&](qreal x,qreal y,QColor c){pen(c,2.1);p.drawPolyline(QPolygonF{QPointF(x-3,y),QPointF(x-1,y+2.5),QPointF(x+4,y-3)});};
    const auto arrow=[&](bool right,QColor c,qreal y=12){pen(c,1.8);p.drawLine(QPointF(5,y),QPointF(19,y));const qreal end=right?19:5,back=right?15:9;p.drawPolyline(QPolygonF{QPointF(back,y-4),QPointF(end,y),QPointF(back,y+4)});};
    const auto doc=[&]{pen(ink,1.35);p.setBrush(Qt::white);QPainterPath path;path.moveTo(5,2.5);path.lineTo(14,2.5);path.lineTo(19,7.5);path.lineTo(19,21);path.lineTo(5,21);path.closeSubpath();p.drawPath(path);p.drawPolyline(QPolygonF{QPointF(14,2.5),QPointF(14,7.5),QPointF(19,7.5)});};
    const auto folder=[&]{pen(QColor("#b9872f"),1.3);p.setBrush(QColor("#f4cc75"));p.drawPolygon(QPolygonF{QPointF(2,6),QPointF(9,6),QPointF(11,8),QPointF(22,8),QPointF(21,20),QPointF(2,20)});p.drawLine(2,10,21,10);};
    const auto lens=[&](qreal x=10,qreal y=10,qreal radius=5){pen(blue,1.9);p.setBrush(QColor(255,255,255,200));p.drawEllipse(QPointF(x,y),radius,radius);p.setBrush(Qt::NoBrush);pen(ink,2.3);p.drawLine(QPointF(x+radius*.75,y+radius*.75),QPointF(x+radius+4,y+radius+4));};
    const auto window=[&]{pen(ink,1.35);p.setBrush(Qt::white);p.drawRoundedRect(QRectF(2,3,20,18),1,1);p.fillRect(QRectF(3,4,18,4),blue);};
    const auto chart=[&](QColor c=QColor("#287bb5")){pen(ink,1.3);p.drawLine(3,3,3,21);p.drawLine(3,21,22,21);pen(c,1.8);p.drawPolyline(QPolygonF{QPointF(5,18),QPointF(9,15),QPointF(12,16),QPointF(16,7),QPointF(21,5)});};
    const auto gear=[&](QColor c){pen(c,1.35);p.setBrush(pale);QPolygonF cog;for(int i=0;i<24;++i){const qreal a=i*3.141592653589793/12,r=(i%3==0||i%3==1)?9:7;cog<<QPointF(12+std::cos(a)*r,12+std::sin(a)*r);}p.drawPolygon(cog);p.setBrush(Qt::white);p.drawEllipse(QPointF(12,12),3.1,3.1);};
    const auto graph=[&]{pen(ink,1.5);p.drawLine(12,5,12,12);p.drawLine(5,12,20,12);p.drawLine(5,12,5,19);p.drawLine(20,12,20,19);p.setBrush(blue);p.drawEllipse(QPointF(12,4),2.4,2.4);p.setBrush(teal);p.drawEllipse(QPointF(5,19),2.4,2.4);p.drawEllipse(QPointF(20,19),2.4,2.4);};
    switch(icon){
    case Kind::CloseAll:pen(blue,1.2);p.setBrush(pale);p.drawRoundedRect(QRectF(3,6,13,16),1,1);pen(ink,1.2);p.setBrush(Qt::white);p.drawRoundedRect(QRectF(8,2,13,16),1,1);pen(red,1.8);p.drawLine(12,7,17,12);p.drawLine(12,12,17,7);break;
    case Kind::Snapshot:pen(teal,1.5);p.setBrush(QColor("#e3f3f4"));p.drawRoundedRect(QRectF(2,6,20,15),2,2);p.drawRoundedRect(QRectF(6,3,8,4),1,1);p.setBrush(Qt::white);p.drawEllipse(QPointF(12,13),5,5);pen(blue,1.5);p.drawArc(QRectF(9,10,6,6),20*16,250*16);p.setBrush(gold);pen(gold,1);p.drawEllipse(QPointF(18.5,9),1,1);break;
    case Kind::Attach:{pen(teal,1.8);QPainterPath clip;clip.moveTo(9,8);clip.lineTo(9,17);clip.cubicTo(9,21,15,21,15,17);clip.lineTo(15,6);clip.cubicTo(15,0,5,0,5,6);clip.lineTo(5,17);clip.cubicTo(5,25,20,25,20,17);clip.lineTo(20,6);p.drawPath(clip);break;}
    case Kind::Shuffle:{pen(teal,1.8);QPainterPath first;first.moveTo(3,6);first.cubicTo(11,6,12,18,21,18);p.drawPath(first);QPainterPath second;second.moveTo(3,18);second.cubicTo(11,18,12,6,21,6);p.drawPath(second);p.drawPolyline(QPolygonF{QPointF(18,3),QPointF(21,6),QPointF(18,9)});p.drawPolyline(QPolygonF{QPointF(18,15),QPointF(21,18),QPointF(18,21)});break;}
    case Kind::NewModel:doc();plus(16,17,green);break;
    case Kind::NewProject:folder();plus(16,16,green);break;
    case Kind::OpenModel:doc();arrow(true,blue,16);break;
    case Kind::OpenProject:folder();arrow(true,blue,15);break;
    case Kind::Project:folder();break;
    case Kind::SelectFit:folder();check(15,15,green);break;
    case Kind::Save:case Kind::SaveAs:pen(blue,1.3);p.setBrush(blue);p.drawRoundedRect(QRectF(3,3,18,18),1,1);p.fillRect(QRectF(7,3,10,6),pale);p.fillRect(QRectF(7,13,10,8),Qt::white);pen(ink,1);p.drawLine(9,16,15,16);p.drawLine(9,18,15,18);if(icon==Kind::SaveAs){pen(gold,3);p.drawLine(14,20,21,13);pen(ink,1.2);p.drawLine(13,22,15,18);}break;
    case Kind::Print:case Kind::Preview:pen(ink,1.3);p.setBrush(Qt::white);p.drawRect(QRectF(6,2,12,6));p.setBrush(pale);p.drawRoundedRect(QRectF(2,8,20,10),2,2);p.setBrush(Qt::white);p.drawRect(QRectF(6,13,12,9));pen(blue,1.3);p.drawLine(8,16,16,16);p.drawLine(8,19,15,19);p.setBrush(green);p.drawEllipse(QPointF(18.5,10.5),.8,.8);if(icon==Kind::Preview)lens(17,16,3.5);break;
    case Kind::Exit:pen(ink,1.4);p.setBrush(pale);p.drawRect(QRectF(3,3,10,18));p.setBrush(Qt::white);p.drawEllipse(QPointF(10,12),.7,.7);pen(red,1.9);p.drawLine(11,12,22,12);p.drawPolyline(QPolygonF{QPointF(18,8),QPointF(22,12),QPointF(18,16)});break;
    case Kind::Check:doc();check(12,15,green);break;
    case Kind::Apply:pen(green,2.7);p.drawPolyline(QPolygonF{QPointF(4,12),QPointF(9,18),QPointF(21,5)});break;
    case Kind::Run:pen(QColor("#247a46"),1.2);p.setBrush(green);p.drawPolygon(QPolygonF{QPointF(6,3),QPointF(21,12),QPointF(6,21)});break;
    case Kind::Fit:chart(red);pen(green,1.4);p.setBrush(Qt::white);for(auto point:{QPointF(6,17),QPointF(10,15),QPointF(15,9),QPointF(20,6)})p.drawEllipse(point,1.7,1.7);break;
    case Kind::Stop:pen(red,1.3);p.setBrush(red);p.drawRoundedRect(QRectF(4,4,16,16),1.5,1.5);break;
    case Kind::Cancel:case Kind::Close:pen(icon==Kind::Cancel?red:ink,2.2);p.drawLine(6,6,18,18);p.drawLine(6,18,18,6);break;
    case Kind::Undo:case Kind::Redo:{const bool right=icon==Kind::Redo;pen(blue,2.1);QPainterPath path;path.moveTo(right?18:6,7);path.cubicTo(right?5:19,1,right?1:23,18,right?12:12,20);p.drawPath(path);pen(blue,1.7);p.drawPolyline(right?QPolygonF{QPointF(17,2),QPointF(19,7),QPointF(13,8)}:QPolygonF{QPointF(7,2),QPointF(5,7),QPointF(11,8)});break;}
    case Kind::Cut:pen(ink,1.6);p.drawLine(9,13,20,3);p.drawLine(9,10,20,21);pen(blue,1.7);p.drawEllipse(QRectF(2,7,7,6));p.drawEllipse(QRectF(2,14,7,6));break;
    case Kind::Copy:pen(blue,1.3);p.setBrush(pale);p.drawRoundedRect(QRectF(3,3,12,15),1,1);p.setBrush(Qt::white);p.drawRoundedRect(QRectF(9,7,12,15),1,1);pen(ink,1);p.drawLine(12,11,18,11);p.drawLine(12,14,18,14);p.drawLine(12,17,17,17);break;
    case Kind::Paste:pen(ink,1.3);p.setBrush(QColor("#f2d69c"));p.drawRoundedRect(QRectF(4,4,16,18),1,1);p.setBrush(pale);p.drawRoundedRect(QRectF(8,2,8,5),1,1);p.setBrush(Qt::white);p.drawRect(QRectF(7,10,10,9));pen(blue,1);p.drawLine(9,13,15,13);p.drawLine(9,16,14,16);break;
    case Kind::SelectAll:pen(blue,1.3);p.setPen(QPen(blue,1.5,Qt::DashLine));p.drawRect(QRectF(3,3,18,18));pen(ink,1.2);for(int y:{7,12,17})p.drawLine(7,y,17,y);break;
    case Kind::Find:case Kind::FindNext:case Kind::FindPrevious:case Kind::Replace:lens();if(icon==Kind::Replace){pen(green,1.5);p.drawLine(13,3,21,3);p.drawPolyline(QPolygonF{QPointF(18,1),QPointF(21,3),QPointF(18,5)});pen(purple,1.5);p.drawLine(2,21,10,21);p.drawPolyline(QPolygonF{QPointF(5,19),QPointF(2,21),QPointF(5,23)});}else if(icon!=Kind::Find){pen(green,1.8);const bool down=icon==Kind::FindNext;p.drawLine(20,3,20,11);p.drawPolyline(down?QPolygonF{QPointF(17,8),QPointF(20,11),QPointF(23,8)}:QPolygonF{QPointF(17,6),QPointF(20,3),QPointF(23,6)});}break;
    case Kind::Edit:doc();pen(gold,3.4);p.drawLine(10,19,21,8);pen(ink,1);p.drawLine(8,21,11,20);break;
    case Kind::Report:doc();p.fillRect(QRectF(8,14,2,5),blue);p.fillRect(QRectF(12,11,2,8),teal);p.fillRect(QRectF(16,9,2,10),gold);break;
    case Kind::Log:case Kind::Command:pen(ink,1.3);p.setBrush(pale);p.drawRoundedRect(QRectF(2,4,20,16),1,1);pen(blue,1.8);p.drawPolyline(QPolygonF{QPointF(5,9),QPointF(8,12),QPointF(5,15)});p.drawPolyline(QPolygonF{QPointF(10,9),QPointF(13,12),QPointF(10,15)});pen(ink,1.6);p.drawLine(15,16,19,16);break;
    case Kind::Help:case Kind::About:pen(blue,1.4);p.setBrush(pale);p.drawEllipse(QRectF(3,3,18,18));pen(blue,2);if(icon==Kind::Help){QPainterPath question;question.moveTo(9,8);question.cubicTo(9,5,16,5,16,9);question.cubicTo(16,11,12,11,12,14);p.drawPath(question);p.drawPoint(QPointF(12,17));}else{p.drawLine(12,10,12,17);p.drawLine(10,17,14,17);p.drawPoint(QPointF(12,7));}break;
    case Kind::Tabbed:window();p.fillRect(QRectF(4,9,6,3),pale);pen(blue,1);p.drawRect(QRectF(4,9,6,3));p.drawLine(11,9,18,9);break;
    case Kind::Split:window();pen(blue,1.5);p.drawLine(12,9,12,20);p.fillRect(QRectF(4,10,6,9),pale);p.fillRect(QRectF(14,10,6,9),QColor("#dceeea"));break;
    case Kind::ProjectMinimize:case Kind::CommandMinimize:window();p.fillRect(icon==Kind::ProjectMinimize?QRectF(3,9,5,11):QRectF(3,16,18,4),pale);pen(blue,1.4);if(icon==Kind::ProjectMinimize)p.drawLine(8,9,8,20);else p.drawLine(3,16,21,16);pen(red,2);p.drawLine(14,13,19,13);break;
    case Kind::Restore:window();pen(green,1.6);p.drawRect(QRectF(7,10,10,8));p.drawPolyline(QPolygonF{QPointF(15,12),QPointF(18,9),QPointF(21,12)});break;
    case Kind::Classic:window();pen(blue,1.5);for(int y:{10,14,18}){p.drawLine(5,y,10,y);p.drawLine(14,y,19,y);}break;
    case Kind::Ribbon:case Kind::Expand:case Kind::Collapse:window();pen(blue,1);p.drawLine(3,10,21,10);if(icon!=Kind::Collapse){for(int x:{5,10,15}){p.fillRect(QRectF(x,12,3,3),x==10?teal:blue);p.drawLine(x,18,x+3,18);}}if(icon==Kind::Expand){pen(green,1.8);p.drawPolyline(QPolygonF{QPointF(8,12),QPointF(12,16),QPointF(16,12)});}else if(icon==Kind::Collapse){pen(blue,1.8);p.drawPolyline(QPolygonF{QPointF(8,17),QPointF(12,13),QPointF(16,17)});}break;
    case Kind::Font:case Kind::FontSize:pen(blue,2);p.drawPolyline(QPolygonF{QPointF(3,20),QPointF(10,3),QPointF(17,20)});p.drawLine(6,14,14,14);if(icon==Kind::FontSize){pen(ink,1.5);p.drawLine(21,4,21,20);p.drawPolyline(QPolygonF{QPointF(19,6),QPointF(21,3),QPointF(23,6)});p.drawPolyline(QPolygonF{QPointF(19,18),QPointF(21,21),QPointF(23,18)});}else{pen(gold,2.6);p.drawLine(14,21,21,14);}break;
    case Kind::Location:folder();pen(blue,1.8);p.drawPolyline(QPolygonF{QPointF(5,12),QPointF(8,15),QPointF(5,18)});p.drawPolyline(QPolygonF{QPointF(12,12),QPointF(15,15),QPointF(12,18)});break;
    case Kind::DefaultView:window();pen(teal,1.8);p.drawArc(QRectF(7,10,10,8),30*16,290*16);p.drawPolyline(QPolygonF{QPointF(17,9),QPointF(18,13),QPointF(14,13)});break;
    case Kind::Outline:graph();break;
    case Kind::Distill:pen(purple,1.5);p.setBrush(QColor("#e7dff4"));p.drawPolygon(QPolygonF{QPointF(3,4),QPointF(21,4),QPointF(14,13),QPointF(14,20),QPointF(10,22),QPointF(10,13)});pen(purple,1.2);p.drawLine(7,8,17,8);break;
    case Kind::Sensitivity:chart(purple);pen(teal,1.2);p.drawPolyline(QPolygonF{QPointF(5,15),QPointF(9,10),QPointF(13,12),QPointF(17,4),QPointF(21,3)});pen(gold,1.2);p.drawPolyline(QPolygonF{QPointF(5,20),QPointF(9,18),QPointF(13,19),QPointF(17,11),QPointF(21,9)});break;
    case Kind::Generate:doc();pen(purple,1.7);p.drawPolyline(QPolygonF{QPointF(9,11),QPointF(6,14),QPointF(9,17)});p.drawPolyline(QPolygonF{QPointF(15,11),QPointF(18,14),QPointF(15,17)});p.drawLine(13,10,11,18);break;
    case Kind::Build:gear(blue);pen(gold,2.5);p.drawLine(13,18,20,11);pen(ink,2);p.drawLine(17,9,22,14);break;
    case Kind::Chart:chart();break;
    case Kind::ZoomIn:case Kind::ZoomOut:lens();pen(green,1.7);p.drawLine(7,10,13,10);if(icon==Kind::ZoomIn)p.drawLine(10,7,10,13);break;
    case Kind::Pointer:pen(ink,1.3);p.setBrush(pale);p.drawPolygon(QPolygonF{QPointF(5,2),QPointF(20,14),QPointF(13,15),QPointF(17,21),QPointF(13,23),QPointF(9,16),QPointF(5,21)});break;
    case Kind::Pan:{pen(blue,1.5);p.setBrush(pale);QPainterPath hand;hand.moveTo(7,21);hand.lineTo(3,12);hand.quadTo(3,9,6,11);hand.lineTo(8,14);hand.lineTo(8,4);hand.quadTo(9,1,11,4);hand.lineTo(11,10);hand.lineTo(12,3);hand.quadTo(14,1,15,4);hand.lineTo(15,11);hand.lineTo(16,5);hand.quadTo(18,4,19,7);hand.lineTo(19,14);hand.lineTo(21,10);hand.quadTo(23,10,22,14);hand.lineTo(18,21);hand.closeSubpath();p.drawPath(hand);break;}
    case Kind::DataCursor:pen(ink,1.4);p.drawLine(12,2,12,22);p.drawLine(2,12,22,12);pen(red,1.7);p.drawEllipse(QRectF(7,7,10,10));break;
    case Kind::Grid:pen(ink,1);for(int x:{4,9,14,19})p.drawLine(x,3,x,21);for(int y:{3,9,15,21})p.drawLine(4,y,19,y);break;
    case Kind::Legend:pen(ink,1.2);p.setBrush(Qt::white);p.drawRect(QRectF(2,4,20,16));for(int y:{8,12,16}){pen(y==8?blue:y==12?red:teal,1.8);p.drawLine(5,y,10,y);pen(ink,1.2);p.drawLine(13,y,19,y);}break;
    case Kind::Styles:pen(blue,2);p.drawLine(3,6,20,6);pen(red,2);p.setPen(QPen(red,2,Qt::DashLine));p.drawLine(3,12,20,12);pen(teal,2);p.setPen(QPen(teal,2,Qt::DotLine));p.drawLine(3,18,20,18);break;
    case Kind::Tile:pen(blue,1.3);p.setBrush(pale);for(int x:{3,13})for(int y:{3,13})p.drawRect(QRectF(x,y,8,8));break;
    case Kind::Float:window();pen(teal,1.4);p.setBrush(Qt::white);p.drawRect(QRectF(10,10,12,12));pen(blue,1.7);p.drawLine(12,12,21,3);p.drawPolyline(QPolygonF{QPointF(15,3),QPointF(21,3),QPointF(21,9)});break;
    case Kind::Previous:arrow(false,blue);break;case Kind::Next:arrow(true,blue);break;
    case Kind::View:pen(blue,1.5);for(int x:{5,12,19})p.drawLine(x,3,x,21);p.setBrush(Qt::white);p.drawRoundedRect(QRectF(2,6,6,4),1,1);p.drawRoundedRect(QRectF(9,14,6,4),1,1);p.drawRoundedRect(QRectF(16,9,6,4),1,1);break;
    case Kind::FitPage:pen(ink,1.3);p.setBrush(Qt::white);p.drawRect(QRectF(6,3,12,18));pen(blue,1.7);p.drawLine(2,12,9,12);p.drawPolyline(QPolygonF{QPointF(6,9),QPointF(9,12),QPointF(6,15)});p.drawLine(22,12,15,12);p.drawPolyline(QPolygonF{QPointF(18,9),QPointF(15,12),QPointF(18,15)});break;
    case Kind::FitWidth:pen(ink,1.3);p.setBrush(Qt::white);p.drawRect(QRectF(3,5,18,14));pen(blue,1.7);p.drawLine(5,12,19,12);p.drawPolyline(QPolygonF{QPointF(8,9),QPointF(5,12),QPointF(8,15)});p.drawPolyline(QPolygonF{QPointF(16,9),QPointF(19,12),QPointF(16,15)});break;
    case Kind::Convert:doc();pen(teal,1.8);p.drawLine(7,12,18,12);p.drawPolyline(QPolygonF{QPointF(15,9),QPointF(18,12),QPointF(15,15)});pen(purple,1.8);p.drawLine(18,18,7,18);p.drawPolyline(QPolygonF{QPointF(10,15),QPointF(7,18),QPointF(10,21)});break;
    case Kind::Copilot:{pen(blue,1.5);p.setBrush(pale);p.drawRoundedRect(QRectF(2,3,18,14),3,3);p.drawPolyline(QPolygonF{QPointF(6,17),QPointF(6,21),QPointF(11,17)});pen(teal,1.7);p.drawLine(7,10,15,10);p.drawLine(11,6,11,14);pen(gold,1.3);p.drawLine(20,1,20,7);p.drawLine(17,4,23,4);break;}
    case Kind::Options:gear(ink);break;
    case Kind::Statistics:pen(ink,1.2);p.drawLine(3,3,3,21);p.drawLine(3,21,22,21);p.setPen(Qt::NoPen);p.fillRect(QRectF(6,13,4,7),blue);p.fillRect(QRectF(12,8,4,12),teal);p.fillRect(QRectF(18,4,4,16),green);break;
    }
}
}
// Rasterize the same antialiased vector glyph at menu, ribbon and high-DPI sizes.
// The icon engine selects the appropriate resolution without platform logos.
inline QIcon studioCommandIcon(const QString& text,const QString& objectName={}){
    const auto kind=studio_icons_detail::kind(text,objectName);static QHash<int,QIcon> icons;
    const int key=int(kind);if(auto found=icons.constFind(key);found!=icons.cend())return *found;
    QIcon icon;for(int size:{16,24,32,48,64}){QPixmap image(size,size);image.fill(Qt::transparent);QPainter painter(&image);painter.scale(size/24.,size/24.);studio_icons_detail::draw(painter,kind);painter.end();icon.addPixmap(image);}
    icons.insert(key,icon);return icon;
}
inline QIcon studioActionIcon(const QAction* action,QWidget* =nullptr){return !action?QIcon():!action->icon().isNull()?action->icon():studioCommandIcon(action->text(),action->objectName());}
inline void applyStudioActionIcons(QObject* owner){
    if(!owner)return;
    // Classic menubars display the application menu names. Some Qt styles
    // replace that text with an assigned icon, which obscures File/Edit/View.
    QList<QAction*> menuNames;for(auto* bar:owner->findChildren<QMenuBar*>())menuNames.append(bar->actions());
    if(auto* bar=qobject_cast<QMenuBar*>(owner))menuNames.append(bar->actions());
    for(auto* action:owner->findChildren<QAction*>()){
        if(menuNames.contains(action)){action->setIcon({});continue;}
        if(!action->isSeparator()&&action->icon().isNull())action->setIcon(studioActionIcon(action));
    }
}
inline void applyStudioButtonIcons(QWidget* owner){if(!owner)return;for(auto* button:owner->findChildren<QPushButton*>())if(button->icon().isNull()&&!button->text().isEmpty())button->setIcon(studioCommandIcon(button->text(),button->objectName()));}
}
