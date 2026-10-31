#include "tile_layout_widgets.hpp"

#include <QApplication>
#include <QEvent>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QRegion>
#include <QShowEvent>
#include <QTimer>
#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace penta {
namespace {
bool fail(QString* error,const QString& message){if(error)*error=message;return false;}
bool exactInt(const QJsonValue& value,int& out){
    if(!value.isDouble())return false;
    const double x=value.toDouble();
    if(!std::isfinite(x)||x<0||x>1000000||x!=std::floor(x))return false;
    out=static_cast<int>(x);return true;
}
std::vector<int> weights(int count,const std::vector<int>& proposed){return proposed.empty()?std::vector<int>(static_cast<std::size_t>(count),1):proposed;}
QColor itemColor(int id){return QColor::fromHsv((id*47)%360,95,240);}
}

bool validateTileLayout(const TileLayoutSpec& spec,const std::vector<int>& required,QString* error){
    if(spec.rows<1||spec.columns<1||spec.rows>64||spec.columns>64||spec.rows*spec.columns>64)
        return fail(error,QStringLiteral("A tile layout needs positive rows/columns and at most 64 cells."));
    auto validWeights=[](const std::vector<int>& w,int count){return w.empty()||(w.size()==static_cast<std::size_t>(count)&&std::all_of(w.begin(),w.end(),[](int x){return x>=1&&x<=1000000;}));};
    if(!validWeights(spec.rowWeights,spec.rows)||!validWeights(spec.columnWeights,spec.columns))
        return fail(error,QStringLiteral("Tile weights must be positive, bounded, and match the grid."));
    if(spec.placements.size()!=required.size()||required.size()>64)return fail(error,QStringLiteral("The layout must contain each currently open item exactly once."));
    const std::set<int> expected(required.begin(),required.end());std::set<int> seen;std::array<bool,64> used{};
    if(expected.size()!=required.size())return fail(error,QStringLiteral("Tile item identifiers must be unique."));
    for(const auto& p:spec.placements){
        if(!expected.contains(p.id)||!seen.insert(p.id).second||p.id<1||p.id>64)
            return fail(error,QStringLiteral("Unknown or duplicated tile item identifier."));
        if(p.row<0||p.column<0||p.rowSpan<1||p.columnSpan<1||p.row>=spec.rows||p.column>=spec.columns||p.rowSpan>spec.rows-p.row||p.columnSpan>spec.columns-p.column)
            return fail(error,QStringLiteral("A tile span lies outside the selected grid."));
        for(int r=p.row;r<p.row+p.rowSpan;++r)for(int c=p.column;c<p.column+p.columnSpan;++c){
            auto& cell=used[static_cast<std::size_t>(r*spec.columns+c)];
            if(cell)return fail(error,QStringLiteral("Tile spans must not overlap another item."));
            cell=true;
        }
    }
    return true;
}
TileLayoutSpec regularTileLayout(int rows,int columns,const std::vector<int>& ids,bool indexed){
    TileLayoutSpec spec;spec.rows=rows;spec.columns=columns;
    if(rows<1||columns<1||rows>64||columns>64||rows*columns>64)return spec;
    spec.rowWeights=std::vector<int>(static_cast<std::size_t>(std::max(0,rows)),1);
    spec.columnWeights=std::vector<int>(static_cast<std::size_t>(std::max(0,columns)),1);
    for(std::size_t k=0;k<ids.size();++k){const int cell=indexed?ids[k]-1:static_cast<int>(k);spec.placements.push_back({ids[k],cell/columns,cell%columns,1,1});}
    return spec;
}
QJsonObject tileLayoutJson(const TileLayoutSpec& spec){
    QJsonArray cells,rowWeights,columnWeights;
    for(const auto& p:spec.placements)cells.append(QJsonObject{{"id",p.id},{"row",p.row},{"column",p.column},{"row_span",p.rowSpan},{"column_span",p.columnSpan}});
    for(const int x:weights(spec.rows,spec.rowWeights))rowWeights.append(x);
    for(const int x:weights(spec.columns,spec.columnWeights))columnWeights.append(x);
    return {{"rows",spec.rows},{"columns",spec.columns},{"placements",cells},{"row_weights",rowWeights},{"column_weights",columnWeights}};
}
std::optional<TileLayoutSpec> parseTileLayoutJson(const QJsonObject& object,QString* error){
    const std::set<QString> allowed{QStringLiteral("rows"),QStringLiteral("columns"),QStringLiteral("placements"),QStringLiteral("row_weights"),QStringLiteral("column_weights")};
    for(auto it=object.begin();it!=object.end();++it)if(!allowed.contains(it.key())){fail(error,QStringLiteral("Unknown saved tile layout field."));return {};}
    TileLayoutSpec spec;
    if(!exactInt(object.value("rows"),spec.rows)||!exactInt(object.value("columns"),spec.columns)||!object.value("placements").isArray()||object.value("placements").toArray().size()>64){fail(error,QStringLiteral("Malformed saved tile layout dimensions/items."));return {};}
    for(const auto value:object.value("placements").toArray()){
        if(!value.isObject()){fail(error,QStringLiteral("Malformed saved tile placement."));return {};}
        const auto cell=value.toObject();TilePlacement p;
        if(cell.size()!=5||!exactInt(cell.value("id"),p.id)||!exactInt(cell.value("row"),p.row)||!exactInt(cell.value("column"),p.column)||!exactInt(cell.value("row_span"),p.rowSpan)||!exactInt(cell.value("column_span"),p.columnSpan)){fail(error,QStringLiteral("Malformed saved tile placement."));return {};}
        spec.placements.push_back(p);
    }
    auto readWeights=[&](const char* key,std::vector<int>& output){const auto value=object.value(QString::fromLatin1(key));if(!value.isArray()||value.toArray().size()>64)return false;for(const auto x:value.toArray()){int w=0;if(!exactInt(x,w))return false;output.push_back(w);}return true;};
    if(!readWeights("row_weights",spec.rowWeights)||!readWeights("column_weights",spec.columnWeights)){fail(error,QStringLiteral("Malformed saved tile weights."));return {};}
    std::vector<int> ids;for(const auto& p:spec.placements)ids.push_back(p.id);
    if(!validateTileLayout(spec,ids,error))return {};
    return spec;
}

TileGridPicker::TileGridPicker(QWidget* parent):QWidget(parent){
    setObjectName(QStringLiteral("tileGridPicker"));setMouseTracking(true);setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("Draw tile grid"));setToolTip(tr("Drag over cells to choose a grid. Arrow keys adjust rows/columns; Enter applies."));
    setMinimumSize(sizeHint());
}
QSize TileGridPicker::sizeHint()const{return {232,256};}
QRect TileGridPicker::cellRect(int row,int column)const{return {12+column*26,12+row*26,23,23};}
bool TileGridPicker::valid()const{return m_required>0&&m_rows*m_columns>=m_required&&m_rows*m_columns<=64;}
void TileGridPicker::setRequiredCount(int count){m_required=std::clamp(count,0,64);updatePreview(m_rows,m_columns);}
void TileGridPicker::updatePreview(int rows,int columns){m_rows=std::clamp(rows,1,8);m_columns=std::clamp(columns,1,8);setAccessibleDescription(tr("%1 rows by %2 columns; %3 open items").arg(m_rows).arg(m_columns).arg(m_required));update();emit previewChanged(m_rows,m_columns,valid());}
void TileGridPicker::previewAt(QPoint p){updatePreview(std::clamp((p.y()-12)/26+1,1,8),std::clamp((p.x()-12)/26+1,1,8));}
void TileGridPicker::paintEvent(QPaintEvent*){
    QPainter painter(this);painter.fillRect(rect(),palette().window());
    for(int r=0;r<8;++r)for(int c=0;c<8;++c){const bool chosen=r<m_rows&&c<m_columns;painter.fillRect(cellRect(r,c),chosen?(valid()?QColor(90,155,235):QColor(238,161,142)):palette().base().color());painter.setPen(palette().mid().color());painter.drawRect(cellRect(r,c));}
    painter.setPen(palette().text().color());painter.drawText(QRect(8,224,width()-16,28),Qt::AlignCenter,tr("%1 × %2%3").arg(m_rows).arg(m_columns).arg(valid()?QString():tr(" — not enough cells")));
}
void TileGridPicker::mouseMoveEvent(QMouseEvent* event){previewAt(event->position().toPoint());}
void TileGridPicker::mousePressEvent(QMouseEvent* event){if(event->button()==Qt::LeftButton&&QRect(12,12,208,208).contains(event->position().toPoint())){m_pressed=true;setFocus();previewAt(event->position().toPoint());event->accept();}}
void TileGridPicker::mouseReleaseEvent(QMouseEvent* event){if(event->button()==Qt::LeftButton&&m_pressed){m_pressed=false;if(QRect(12,12,208,208).contains(event->position().toPoint())){previewAt(event->position().toPoint());if(valid())emit layoutChosen(m_rows,m_columns);}event->accept();}}
void TileGridPicker::keyPressEvent(QKeyEvent* event){
    switch(event->key()){
    case Qt::Key_Left:updatePreview(m_rows,m_columns-1);break;
    case Qt::Key_Right:updatePreview(m_rows,m_columns+1);break;
    case Qt::Key_Up:updatePreview(m_rows-1,m_columns);break;
    case Qt::Key_Down:updatePreview(m_rows+1,m_columns);break;
    case Qt::Key_Return:case Qt::Key_Enter:if(valid())emit layoutChosen(m_rows,m_columns);break;
    default:QWidget::keyPressEvent(event);return;
    }event->accept();
}

TileLayoutHost::TileLayoutHost(QWidget* parent):QWidget(parent){}
void TileLayoutHost::setTileGrid(QGridLayout* grid){m_grid=grid;m_cellsValid=false;queueHandleRefresh();}
void TileLayoutHost::setTileWeights(int rows,int columns,const std::vector<int>& rw,const std::vector<int>& cw){
    m_rows=rows;m_columns=columns;m_rowWeights=weights(rows,rw);m_columnWeights=weights(columns,cw);m_cellsValid=false;
    if(m_grid){for(int r=0;r<m_grid->rowCount()||r<rows;++r)m_grid->setRowStretch(r,r<rows?m_rowWeights[static_cast<std::size_t>(r)]:0);for(int c=0;c<m_grid->columnCount()||c<columns;++c)m_grid->setColumnStretch(c,c<columns?m_columnWeights[static_cast<std::size_t>(c)]:0);
        // Explicit grids reserve their empty rows/columns immediately. Only
        // divider positioning waits for the enclosing layouts to settle.
        m_grid->activate();
    }
    queueHandleRefresh();
}
void TileLayoutHost::queueHandleRefresh(){
    if(!m_grid||m_handleRefreshQueued)return;
    m_handleRefreshQueued=true;
    QTimer::singleShot(0,this,[this]{m_handleRefreshQueued=false;refreshHandles();});
}
void TileLayoutHost::refreshHandles(){
    if(!m_grid)return;
    // A nested layout can settle after the host's resize event. Activate the
    // current grid before reading its cells; LayoutRequest also covers changes
    // in toolbars, margins and spans that do not resize the host itself.
    m_grid->activate();
    const auto margins=m_grid->contentsMargins();
    std::vector<int> parameters{m_rows,m_columns,m_grid->rowCount(),m_grid->columnCount(),
        margins.left(),margins.top(),margins.right(),margins.bottom(),
        m_grid->horizontalSpacing(),m_grid->verticalSpacing()};
    for(int r=0;r<m_grid->rowCount();++r){parameters.push_back(m_grid->rowStretch(r));parameters.push_back(m_grid->rowMinimumHeight(r));}
    for(int c=0;c<m_grid->columnCount();++c){parameters.push_back(m_grid->columnStretch(c));parameters.push_back(m_grid->columnMinimumWidth(c));}
    std::vector<QRect> itemGeometries;
    for(int i=0;i<m_grid->count();++i){
        auto* item=m_grid->itemAt(i);itemGeometries.push_back(item->geometry());
        int row=0,column=0,rowSpan=0,columnSpan=0;m_grid->getItemPosition(i,&row,&column,&rowSpan,&columnSpan);
        parameters.insert(parameters.end(),{row,column,rowSpan,columnSpan,item->isEmpty()?1:0});
    }
    if(!m_cellsValid||m_gridGeometry!=m_grid->geometry()||m_itemGeometries!=itemGeometries||m_gridParameters!=parameters){
        // QWidget::show() and selection overlays may trigger a parent size-hint
        // probe. QGridLayout::cellRect then exposes Qt's height-for-width scratch
        // calculation although the laid-out items have not moved. Recalculate
        // only for changed real geometry; never invalidate on every idle event.
        bool staleCells=false;
        for(int i=0;i<m_grid->count();++i){
            auto* item=m_grid->itemAt(i);if(!item)continue;
            if(auto* widget=item->widget();widget&&widget->isHidden())continue;
            int row=0,column=0,rowSpan=0,columnSpan=0;
            m_grid->getItemPosition(i,&row,&column,&rowSpan,&columnSpan);
            const QRect cells=m_grid->cellRect(row,column).united(m_grid->cellRect(row+rowSpan-1,column+columnSpan-1));
            if(item->geometry().isValid()&&!cells.contains(item->geometry())){staleCells=true;break;}
        }
        if(staleCells){m_grid->invalidate();m_grid->activate();}
        // Snapshot ALL tracks before any overlay geometry/mask/visibility change.
        m_columnCells.clear();m_rowCells.clear();
        for(int c=0;c<m_columns;++c)m_columnCells.push_back(m_grid->cellRect(0,c));
        for(int r=0;r<m_rows;++r)m_rowCells.push_back(m_grid->cellRect(r,0));
        m_itemGeometries.clear();
        for(int i=0;i<m_grid->count();++i)m_itemGeometries.push_back(m_grid->itemAt(i)->geometry());
        m_gridGeometry=m_grid->geometry();m_gridParameters=std::move(parameters);m_cellsValid=true;
    }
    QRegion gutters(rect());
    for(int i=0;i<m_grid->count();++i){
        auto* item=m_grid->itemAt(i);if(!item)continue;
        if(auto* widget=item->widget();widget&&widget->isHidden())continue;
        gutters-=item->geometry();
    }
    auto updateHandles=[&](bool vertical,int count,std::vector<QWidget*>& handles){
        while(handles.size()<static_cast<std::size_t>(std::max(0,count-1))){
            auto* handle=new QWidget(this);
            handle->setObjectName(vertical?QStringLiteral("tileColumnDivider"):QStringLiteral("tileRowDivider"));handle->setProperty("tileDividerVertical",vertical);handle->setProperty("tileDividerIndex",static_cast<int>(handles.size()));handle->setCursor(vertical?Qt::SplitHCursor:Qt::SplitVCursor);handle->setToolTip(tr("Drag to resize tiles"));handle->setAccessibleName(vertical?tr("Tile column divider"):tr("Tile row divider"));
            handle->setStyleSheet(QStringLiteral("background: rgba(90,110,135,35);"));handle->installEventFilter(this);handles.push_back(handle);
        }
        for(std::size_t k=0;k<handles.size();++k){
            auto* h=handles[k];
            if(static_cast<int>(k)>=count-1){if(!h->isHidden()){h->hide();}continue;}
            const int index=static_cast<int>(k);
            const auto& cells=vertical?m_columnCells:m_rowCells;
            const QRect first=cells[static_cast<std::size_t>(index)],second=cells[static_cast<std::size_t>(index+1)];
            const int start=vertical?first.right()+1:first.bottom()+1;
            const int gap=(vertical?second.left():second.top())-start;
            if(!first.isValid()||!second.isValid()||gap<=0){if(!h->isHidden()){h->hide();}continue;}
            const int thickness=std::min(6,gap),position=start+(gap-thickness)/2;
            const QRect geometry=vertical?QRect(position,0,thickness,height()):QRect(0,position,width(),thickness);
            QRegion visible=gutters.intersected(geometry);visible.translate(-geometry.topLeft());
            // Logical boundaries may run through a spanning tile. Restrict both
            // painting and mouse hits to the actual unoccupied gutter segments.
            if(visible.isEmpty()){if(!h->isHidden()){h->hide();}continue;}
            if(h->geometry()!=geometry)h->setGeometry(geometry);
            if(h->mask()!=visible)h->setMask(visible);
            if(h->isHidden()){h->show();}
        }
    };
    updateHandles(true,m_columns,m_verticalHandles);updateHandles(false,m_rows,m_horizontalHandles);

}
void TileLayoutHost::dragHandle(bool vertical,int index,int coordinate){
    if(!m_grid)return;
    auto& w=vertical?m_columnWeights:m_rowWeights;
    if(index<0||index+1>=static_cast<int>(w.size()))return;
    refreshHandles();
    const auto& cells=vertical?m_columnCells:m_rowCells;
    if(index+1>=static_cast<int>(cells.size()))return;
    const QRect first=cells[static_cast<std::size_t>(index)],second=cells[static_cast<std::size_t>(index+1)];
    const int start=vertical?first.left():first.top(),end=vertical?second.right()+1:second.bottom()+1;
    if(end-start<32)return;
    const double fraction=std::clamp(static_cast<double>(coordinate-start)/(end-start),0.05,0.95);
    // Normalize to bounded integer weights, retaining the proportions of
    // unrelated rows/columns and preventing repeated drags from overflowing.
    double total=0;for(const auto x:w)total+=x;
    for(auto& x:w)x=std::max(1,static_cast<int>(std::lround(x*100000.0/total)));
    const int sum=w[static_cast<std::size_t>(index)]+w[static_cast<std::size_t>(index+1)];
    const int left=std::clamp(static_cast<int>(std::lround(fraction*sum)),1,sum-1);
    w[static_cast<std::size_t>(index)]=left;w[static_cast<std::size_t>(index+1)]=sum-left;
    if(vertical)for(int c=0;c<m_columns;++c)m_grid->setColumnStretch(c,w[static_cast<std::size_t>(c)]);else for(int r=0;r<m_rows;++r)m_grid->setRowStretch(r,w[static_cast<std::size_t>(r)]);
    m_cellsValid=false;m_grid->invalidate();m_grid->activate();refreshHandles();if(weightsChanged)weightsChanged(m_rowWeights,m_columnWeights);
}
bool TileLayoutHost::eventFilter(QObject* watched,QEvent* event){
    auto* handle=qobject_cast<QWidget*>(watched);if(!handle)return QWidget::eventFilter(watched,event);
    if(event->type()==QEvent::MouseButtonPress){auto* e=static_cast<QMouseEvent*>(event);if(e->button()==Qt::LeftButton){handle->grabMouse();return true;}}
    if(event->type()==QEvent::MouseMove){auto* e=static_cast<QMouseEvent*>(event);if(e->buttons()&Qt::LeftButton){const QPoint p=mapFromGlobal(e->globalPosition().toPoint());const bool vertical=handle->property("tileDividerVertical").toBool();dragHandle(vertical,handle->property("tileDividerIndex").toInt(),vertical?p.x():p.y());return true;}}
    if(event->type()==QEvent::MouseButtonRelease){handle->releaseMouse();return true;}
    return QWidget::eventFilter(watched,event);
}
bool TileLayoutHost::event(QEvent* event){
    const bool layoutChanged=event->type()==QEvent::LayoutRequest||event->type()==QEvent::ContentsRectChange||event->type()==QEvent::StyleChange;
    const bool handled=QWidget::event(event);if(layoutChanged)queueHandleRefresh();return handled;
}
void TileLayoutHost::resizeEvent(QResizeEvent* event){QWidget::resizeEvent(event);queueHandleRefresh();}
void TileLayoutHost::showEvent(QShowEvent* event){QWidget::showEvent(event);queueHandleRefresh();}

TileSpanEditor::TileSpanEditor(QWidget* parent):QWidget(parent){setObjectName(QStringLiteral("tileSpanEditor"));setMouseTracking(true);setMinimumSize(320,260);setAccessibleName(tr("Draw tile spans"));}
QSize TileSpanEditor::sizeHint()const{return {480,340};}
void TileSpanEditor::setSpec(const TileLayoutSpec& spec){m_spec=spec;m_start.reset();m_end.reset();update();}
void TileSpanEditor::setSelectedId(int id){m_selected=id;update();}
QRect TileSpanEditor::cellRect(int row,int column)const{const int cw=std::max(1,(width()-16)/std::max(1,m_spec.columns)),ch=std::max(1,(height()-16)/std::max(1,m_spec.rows));return {8+column*cw,8+row*ch,cw,ch};}
std::optional<QPoint> TileSpanEditor::cellAt(QPoint p)const{for(int r=0;r<m_spec.rows;++r)for(int c=0;c<m_spec.columns;++c)if(cellRect(r,c).contains(p))return QPoint(c,r);return {};}
void TileSpanEditor::paintEvent(QPaintEvent*){
    QPainter painter(this);painter.fillRect(rect(),palette().base());
    for(int r=0;r<m_spec.rows;++r)for(int c=0;c<m_spec.columns;++c){painter.setPen(palette().mid().color());painter.drawRect(cellRect(r,c));}
    for(const auto& p:m_spec.placements){QRect area=cellRect(p.row,p.column).united(cellRect(p.row+p.rowSpan-1,p.column+p.columnSpan-1));area.adjust(2,2,-2,-2);painter.fillRect(area,itemColor(p.id));painter.setPen(p.id==m_selected?QPen(Qt::black,3):QPen(Qt::darkGray,1));painter.drawRect(area);painter.setPen(Qt::black);painter.drawText(area,Qt::AlignCenter,QString::number(p.id));}
    if(m_start&&m_end){const QRect selection=cellRect(std::min(m_start->y(),m_end->y()),std::min(m_start->x(),m_end->x())).united(cellRect(std::max(m_start->y(),m_end->y()),std::max(m_start->x(),m_end->x())));painter.fillRect(selection,QColor(30,100,220,70));painter.setPen(QPen(QColor(20,80,190),2));painter.drawRect(selection);}
}
void TileSpanEditor::mousePressEvent(QMouseEvent* e){if(e->button()==Qt::LeftButton&&m_selected){m_start=cellAt(e->position().toPoint());m_end=m_start;update();}}
void TileSpanEditor::mouseMoveEvent(QMouseEvent* e){if(m_start){if(const auto cell=cellAt(e->position().toPoint()))m_end=cell;update();}}
void TileSpanEditor::mouseReleaseEvent(QMouseEvent* e){
    if(e->button()!=Qt::LeftButton||!m_start||!m_end)return;
    const auto released=cellAt(e->position().toPoint());
    if(!released){m_start.reset();m_end.reset();update();return;}
    m_end=released;
    TileLayoutSpec proposed=m_spec;const auto found=std::find_if(proposed.placements.begin(),proposed.placements.end(),[this](const auto& p){return p.id==m_selected;});
    if(found!=proposed.placements.end())*found={m_selected,std::min(m_start->y(),m_end->y()),std::min(m_start->x(),m_end->x()),std::abs(m_start->y()-m_end->y())+1,std::abs(m_start->x()-m_end->x())+1};
    m_start.reset();m_end.reset();std::vector<int> ids;for(const auto& p:proposed.placements)ids.push_back(p.id);QString error;
    if(validateTileLayout(proposed,ids,&error)){m_spec=std::move(proposed);emit layoutEdited();}else emit editRejected(error);update();
}
}
