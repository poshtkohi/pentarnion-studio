// FigureManager members adapting the renderer-independent GTG docking host.
void FigureManager::setGroupedFigures(bool grouped){
    if(grouped==m_grouped)return;
    const int selected=m_selectedNumber;
    if(grouped){
        QHash<int,QRect> cells;
        for(const auto& figure:m_figures)if(figure->state.docked){const auto cell=figureGlobalCell(figure->state.number);if(cell.isValid())cells.insert(figure->state.number,cell);}
        QHash<int,QRect> floating;
        for(const auto& figure:m_figures)if(!figure->state.docked&&figure->floating)floating.insert(figure->state.number,figure->floating->geometry());
        // Bring any independent classic figure frames home before transferring
        // the retained view to the generic nested docking host.
        for(const auto& figure:m_figures)if(!figure->state.docked)dockFigure(figure->state.number);
        m_rearranging=true;
        while(m_tabs->count())m_tabs->removeTab(0);
        while(auto* item=m_grid->takeAt(0))delete item;
        m_grouped=true;
        for(const auto& figure:m_figures){m_groups->addFigure(figure->state.number,displayName(*figure),figure->page);figure->setMinimumCanvasSize(1,1);figure->updateHeading();}
        m_stack->setCurrentWidget(m_groups);m_rearranging=false;
        for(auto it=floating.cbegin();it!=floating.cend();++it)if(auto* controller=m_groups->dockingController(it.key());controller&&controller->undock(false)){
            if(auto* dock=m_groups->dockWidget(it.key()))dock->setGeometry(it.value());
            if(auto* figure=entry(it.key()))figure->state.docked=false;
        }
        QHash<int,QRect> local;
        for(auto it=cells.cbegin();it!=cells.cend();++it)local.insert(it.key(),QRect(m_groups->mapFromGlobal(it->topLeft()),it->size()));
        if(!local.isEmpty())m_groups->arrangeRectangles(local);
    }else{
        m_rearranging=true;
        for(const auto& figure:m_figures)if(m_groups->containsFigure(figure->state.number)){
            m_groups->takeFigure(figure->state.number);figure->state.docked=true;
        }
        m_grouped=false;m_rearranging=false;rebuildInternalLayout();
    }
    m_selectedNumber=selected;if(grouped&&selected)m_groups->setCurrentFigure(selected);
    updateToolbar();emit figuresChanged();
}
bool FigureManager::splitFigures(int first,int second,Qt::Orientation orientation,QString* message){
    if(message)message->clear();
    if(first==second||!entry(first)||!entry(second)||(orientation!=Qt::Horizontal&&orientation!=Qt::Vertical))return fail(message,tr("Choose two different open figures and a valid split direction."));
    setGroupedFigures(true);
    m_tiled=false;m_rows=m_columns=0;
    if(!m_groups->splitFigure(first,second,orientation))return fail(message,tr("The selected figures could not be split."));
    m_selectedNumber=second;updateToolbar();emit figuresChanged();return true;
}
bool FigureManager::tabifyFigures(int first,int second,QString* message){
    if(message)message->clear();
    if(first==second||!entry(first)||!entry(second))return fail(message,tr("Choose two different open figures."));
    setGroupedFigures(true);
    m_tiled=false;m_rows=m_columns=0;
    if(!m_groups->tabifyFigures(first,second))return fail(message,tr("The selected figures could not be grouped as tabs."));
    m_selectedNumber=second;updateToolbar();emit figuresChanged();return true;
}
void FigureManager::populateArrangementMenu(QMenu* menu){
    menu->clear();const int first=currentFigureNumber();
    for(const auto& [label,kind]:std::array<std::pair<QString,int>,5>{{{tr("Split right"),0},{tr("Split below"),1},{tr("Tab with"),2},{tr("Split left"),3},{tr("Split above"),4}}}){
        auto* sub=menu->addMenu(label);sub->setEnabled(m_figures.size()>1);
        for(const auto& figure:m_figures)if(figure->state.number!=first){const int second=figure->state.number;
            auto* action=sub->addAction(displayName(*figure));connect(action,&QAction::triggered,this,[this,first,second,kind]{
                QString message;bool ok;
                if(kind==2)ok=tabifyFigures(first,second,&message);
                else if(kind==3||kind==4){setGroupedFigures(true);m_tiled=false;m_rows=m_columns=0;ok=m_groups->splitFigure(first,second,kind==3?Qt::Horizontal:Qt::Vertical,true);if(ok){m_selectedNumber=second;updateToolbar();emit figuresChanged();}}
                else ok=splitFigures(first,second,kind==0?Qt::Horizontal:Qt::Vertical,&message);
                if(!ok)emit error(message.isEmpty()?tr("The requested figure split is unavailable."):message);
            });
        }
    }
    menu->addSeparator();
    auto* close=menu->addAction(tr("Close selected figure"));close->setEnabled(first!=0);connect(close,&QAction::triggered,this,[this,first]{closeFigure(first);});
    if(auto* closeAll=findChild<QAction*>(QStringLiteral("figuresCloseAll")))menu->addAction(closeAll);
    menu->addSeparator();
    auto* tabs=menu->addAction(tr("All figures as tabs"));connect(tabs,&QAction::triggered,this,&FigureManager::showTabs);
    auto* tile=menu->addAction(tr("Tile figures in Studio"));connect(tile,&QAction::triggered,this,[this]{QString message;if(!tileStudio(0,0,&message))emit error(message);});
    auto* dock=menu->addAction(tr("Dock selected figure"));dock->setEnabled(first!=0);connect(dock,&QAction::triggered,this,[this,first]{QString message;if(!dockFigure(first,&message))emit error(message);});
    menu->addSeparator();
    auto* save=menu->addAction(tr("Save arrangement…"));save->setEnabled(!m_figures.empty());connect(save,&QAction::triggered,this,[this]{const auto path=QFileDialog::getSaveFileName(this,tr("Save figure arrangement"),{},tr("Figure arrangement (*.json)"));if(path.isEmpty())return;QString message;if(!saveFigureArrangement(path,&message))emit error(message);});
    auto* load=menu->addAction(tr("Load arrangement…"));connect(load,&QAction::triggered,this,[this]{const auto path=QFileDialog::getOpenFileName(this,tr("Load figure arrangement"),{},tr("Figure arrangement (*.json)"));if(path.isEmpty())return;QString message;if(!loadFigureArrangement(path,&message))emit error(message);});
}
bool FigureManager::saveFigureArrangement(const QString& path,QString* message)const{
    if(message)message->clear();
    QJsonArray ids,docked,floating;
    std::vector<int> dockedIds;
    for(const auto& figure:m_figures){
        ids.append(figure->state.number);
        if(m_grouped)continue;
        if(figure->state.docked){docked.append(figure->state.number);dockedIds.push_back(figure->state.number);}
        else{
            if(!figure->floating)return fail(message,tr("An independent figure has no frame to save."));
            const auto geometry=figure->floating->saveGeometry();
            if(geometry.isEmpty()||geometry.size()>4096)return fail(message,tr("A figure frame geometry is invalid or too large."));
            floating.append(QJsonObject{{QStringLiteral("id"),figure->state.number},{QStringLiteral("geometry"),QString::fromLatin1(geometry.toBase64())}});
        }
    }
    QJsonObject data{{QStringLiteral("format"),QStringLiteral("pentarnion-figure-arrangement")},{QStringLiteral("version"),1},
        {QStringLiteral("figures"),ids},{QStringLiteral("grouped"),m_grouped},{QStringLiteral("tiled"),m_tiled},
        {QStringLiteral("selected"),m_selectedNumber}};
    if(m_grouped)data.insert(QStringLiteral("dockState"),QString::fromLatin1(m_groups->saveState().toBase64()));
    else{
        data.insert(QStringLiteral("dockedFigures"),docked);
        data.insert(QStringLiteral("floatingFrames"),floating);
        if(m_tiled&&!dockedIds.empty()){
            auto layout=m_studioLayout;
            if(!validateTileLayout(layout,dockedIds)){
                int rows=m_rows,columns=m_columns;
                if(!gridDimensions(static_cast<int>(dockedIds.size()),rows,columns,nullptr)){
                    rows=columns=0;gridDimensions(static_cast<int>(dockedIds.size()),rows,columns,nullptr);
                }
                layout=regularTileLayout(rows,columns,dockedIds);
            }
            data.insert(QStringLiteral("tiles"),tileLayoutJson(layout));
        }
    }
    QSaveFile file(path);file.setDirectWriteFallback(false);const auto bytes=QJsonDocument(data).toJson();
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())return fail(message,tr("Cannot safely save figure arrangement: %1").arg(file.errorString()));
    return true;
}
bool FigureManager::loadFigureArrangement(const QString& path,QString* message){
    if(message)message->clear();
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)||file.size()>2*1024*1024)return fail(message,tr("Cannot read the figure arrangement or it exceeds 2 MiB."));
    QJsonParseError parse;const auto document=QJsonDocument::fromJson(file.readAll(),&parse);
    if(parse.error!=QJsonParseError::NoError||!document.isObject())return fail(message,tr("The figure arrangement is not valid JSON."));
    const auto data=document.object();
    if(data.value(QStringLiteral("format")).toString()!=QStringLiteral("pentarnion-figure-arrangement")||data.value(QStringLiteral("version")).toInt()!=1)return fail(message,tr("Unsupported figure arrangement format."));
    const auto list=data.value(QStringLiteral("figures"));if(!list.isArray()||list.toArray().size()!=figureCount())return fail(message,tr("Open the same numbered figures before loading this arrangement."));
    QSet<int> ids;for(const auto& value:list.toArray()){const int id=value.toInt(-1);if(!entry(id)||ids.contains(id))return fail(message,tr("The arrangement figure list does not match the open figures."));ids.insert(id);}
    if(!data.value(QStringLiteral("grouped")).isBool())return fail(message,tr("The saved grouping mode is invalid."));
    const auto tiledValue=data.value(QStringLiteral("tiled"));
    if(!tiledValue.isUndefined()&&!tiledValue.isBool())return fail(message,tr("The saved tiled mode is invalid."));
    // Older version-one arrangements have no mode flag. An empty initial grid
    // means ordinary tabs; a populated grid retains the former tiled behaviour.
    const auto savedTiles=data.value(QStringLiteral("tiles")).toObject();
    const bool tiled=tiledValue.isUndefined()
        ?savedTiles.value(QStringLiteral("rows")).toInt()>0&&savedTiles.value(QStringLiteral("columns")).toInt()>0
        :tiledValue.toBool();
    if(data.value(QStringLiteral("grouped")).toBool()){
        const auto encoded=data.value(QStringLiteral("dockState")).toString().toLatin1();
        const auto decoded=QByteArray::fromBase64Encoding(encoded,QByteArray::AbortOnBase64DecodingErrors);
        if(!decoded||decoded.decoded.isEmpty()||decoded.decoded.size()>1024*1024)return fail(message,tr("The saved docking state is invalid."));
        const bool wasGrouped=m_grouped;setGroupedFigures(true);
        if(!m_groups->restoreState(decoded.decoded)){if(!wasGrouped)setGroupedFigures(false);return fail(message,tr("The saved docking state could not be restored."));}
    }else{
        std::vector<int> dockedIds;
        std::map<int,QByteArray> floatingFrames;
        const auto docked=data.value(QStringLiteral("dockedFigures"));
        const auto floating=data.value(QStringLiteral("floatingFrames"));
        if(docked.isUndefined()&&floating.isUndefined())dockedIds=tileItemIds(TileLayoutTarget::Studio);
        else{
            if(!docked.isArray()||!floating.isArray()||docked.toArray().size()+floating.toArray().size()!=figureCount())
                return fail(message,tr("The saved docked and floating figure lists are invalid."));
            QSet<int> partition;
            for(const auto& value:docked.toArray()){
                const int id=value.toInt(-1);
                if(!ids.contains(id)||partition.contains(id))return fail(message,tr("The docked/floating figure lists must partition the open figures."));
                partition.insert(id);dockedIds.push_back(id);
            }
            for(const auto& value:floating.toArray()){
                if(!value.isObject())return fail(message,tr("A saved floating frame is malformed."));
                const auto frame=value.toObject();const int id=frame.value(QStringLiteral("id")).toInt(-1);
                const auto encoded=frame.value(QStringLiteral("geometry")).toString().toLatin1();
                if(frame.size()!=2||!ids.contains(id)||partition.contains(id)||encoded.size()>5464)
                    return fail(message,tr("A floating frame identifier or geometry is invalid."));
                const auto decoded=QByteArray::fromBase64Encoding(encoded,QByteArray::AbortOnBase64DecodingErrors);
                if(!decoded||decoded.decoded.isEmpty()||decoded.decoded.size()>4096)
                    return fail(message,tr("A saved floating frame geometry is invalid."));
                // Validate Qt's opaque geometry format before changing any live
                // figure. This unshown widget never owns scientific plot data.
                QWidget probe(nullptr,Qt::Window);probe.setAttribute(Qt::WA_DontShowOnScreen);
                if(!probe.restoreGeometry(decoded.decoded))return fail(message,tr("A saved floating frame geometry cannot be restored."));
                partition.insert(id);floatingFrames.emplace(id,decoded.decoded);
            }
            if(partition!=ids)return fail(message,tr("The docked/floating figure lists must partition the open figures."));
        }
        std::optional<TileLayoutSpec> layout;
        if(tiled&&!dockedIds.empty()){
            QString reason;layout=parseTileLayoutJson(savedTiles,&reason);
            if(!layout||!validateTileLayout(*layout,dockedIds,&reason))return fail(message,reason);
        }
        // All identifiers, the partition, the grid and all opaque geometries
        // have now passed validation; only now transfer live retained views.
        setGroupedFigures(false);
        for(const auto& figure:m_figures)if(!figure->state.docked&&!dockFigure(figure->state.number,message))return false;
        showTabs();
        for(const auto& [id,geometry]:floatingFrames){
            if(!undockFigure(id,message))return false;
            auto* frame=entry(id)->floating.data();
            frame->restoreGeometry(geometry);
            QScreen* display=frame->screen();
            if(!display)display=QGuiApplication::primaryScreen();
            if(display){
                const QRect available=display->availableGeometry();
                QRect safe=available.adjusted(12,32,-12,-12);if(!safe.isValid())safe=available;
                const QRect restored=frame->isMaximized()?frame->normalGeometry():frame->geometry();
                if(!restored.isValid()||!safe.contains(restored)){
                    const int width=std::clamp(restored.width(),std::min(200,safe.width()),safe.width());
                    const int height=std::clamp(restored.height(),std::min(150,safe.height()),safe.height());
                    const int left=std::clamp(restored.x(),safe.left(),safe.right()-width+1);
                    const int top=std::clamp(restored.y(),safe.top(),safe.bottom()-height+1);
                    frame->showNormal();frame->setGeometry(left,top,width,height);
                }
            }
        }
        if(tiled){
            m_tiled=true;m_studioLayout=layout.value_or(TileLayoutSpec{});
            m_rows=m_studioLayout.rows;m_columns=m_studioLayout.columns;rebuildInternalLayout();
        }
    }
    if(entry(data.value(QStringLiteral("selected")).toInt()))selectFigure(data.value(QStringLiteral("selected")).toInt());
    refreshNames();emit figuresChanged();return true;
}

namespace {
// Adapted from GTG FigureBook: a non-activating, input-transparent half-cell
// preview. It never renders, captures or modifies any scientific plot scene.
class FigureDropPreview final:public QWidget {
public:
    explicit FigureDropPreview(QWidget* owner):QWidget(owner,Qt::ToolTip|Qt::FramelessWindowHint|Qt::WindowTransparentForInput|Qt::WindowDoesNotAcceptFocus){
        setObjectName(QStringLiteral("figureDockDropPreview"));setAttribute(Qt::WA_TransparentForMouseEvents);setAttribute(Qt::WA_ShowWithoutActivating);setAttribute(Qt::WA_TranslucentBackground);setAttribute(Qt::WA_QuitOnClose,false);
    }
protected:
    void paintEvent(QPaintEvent*)override{
        QPainter painter(this);painter.fillRect(rect(),QColor(28,134,198,65));painter.setPen(QPen(QColor(14,107,167),2));painter.drawRect(rect().adjusted(1,1,-2,-2));
        const auto zone=property("studioDropZone").toString();const auto text=zone==QStringLiteral("tab")?QStringLiteral("Tab with figure"):QStringLiteral("Split %1 · 50/50").arg(zone);
        auto font=this->font();font.setBold(true);painter.setFont(font);const auto label=painter.fontMetrics().elidedText(text,Qt::ElideRight,std::max(0,width()-20));
        const auto size=painter.fontMetrics().size(Qt::TextSingleLine,label)+QSize(16,10);QRect box(QPoint(),size);box.moveCenter(rect().center());painter.fillRect(box,QColor(247,252,255,240));painter.setPen(QColor(13,72,108));painter.drawText(box,Qt::AlignCenter,label);
    }
};
}
QWidget* FigureManager::figureFloatingWindow(int number)const {
    const auto* figure=entry(number);if(!figure)return nullptr;
    if(m_grouped){auto* dock=m_groups->dockWidget(number);return dock&&dock->isFloating()?dock:nullptr;}
    return figure->floating;
}
QRect FigureManager::figureGlobalCell(int number)const {
    const auto* figure=entry(number);if(!figure||!figure->state.docked)return {};
    if(m_grouped){const auto cell=m_groups->figureCellRect(number);return QRect(m_groups->mapToGlobal(cell.topLeft()),cell.size());}
    if(!m_tiled&&m_tabs->currentWidget())return QRect(m_tabs->currentWidget()->mapToGlobal(QPoint()),m_tabs->currentWidget()->size());
    return QRect(figure->page->mapToGlobal(QPoint()),figure->page->size());
}
void FigureManager::cancelFigureDrag(){
    if(m_dragDock&&m_dragAreasRestricted)m_dragDock->setAllowedAreas(m_dragAllowedAreas);
    ++m_dragEpoch;m_dragFigure=m_dropFigure=0;m_dragPage.clear();m_dropPage.clear();m_dragTabBar.clear();m_dragDock.clear();
    m_dragMoved=m_sourceTabDrop=m_dragAreasRestricted=m_nativeMoveActive=m_nativeInputDrag=false;m_nativeMoveWindow=0;
    m_dropBounds={};m_dropCells.clear();m_dropZone.clear();if(m_dropPreview)m_dropPreview->hide();
}
void FigureManager::beginFigureDrag(int number,const QPoint& global){
    const auto* figure=entry(number);if(!figure)return;
    if(number==m_dragFigure&&global==m_dragStart)return;
    cancelFigureDrag();m_dragFigure=number;m_dragPage=figure->page;m_dragStart=global;
    if(m_grouped){m_dragDock=m_groups->dockWidget(number);if(m_dragDock)m_dragAllowedAreas=m_dragDock->allowedAreas();}
}
void FigureManager::updateFigureDropPreview(const QPoint& global){
    if(!m_dragFigure||!m_dragPage||m_destroying)return;
    m_dragMoved|=(global-m_dragStart).manhattanLength()>=QApplication::startDragDistance();if(!m_dragMoved)return;
    m_sourceTabDrop=m_dragTabBar&&m_dragTabBar->isVisible()&&QRect(m_dragTabBar->mapToGlobal(QPoint()),m_dragTabBar->size()).contains(global);
    if(m_sourceTabDrop&&!figureFloatingWindow(m_dragFigure)){m_dropFigure=0;m_dropPage.clear();if(m_dropPreview)m_dropPreview->hide();return;}
    // Suspend only Qt's broad native dock target overlay during this drag; the
    // exact promised cell is applied through the identity-checked drop below.
    if(m_dragDock&&!m_dragAreasRestricted){m_dragAreasRestricted=true;m_dragDock->setAllowedAreas(Qt::NoDockWidgetArea);}
#ifdef Q_OS_WIN
    HWND beneath=nullptr;
    if(QGuiApplication::platformName()==QStringLiteral("windows")&&m_nativeInputDrag){
        POINT nativePoint{};if(GetCursorPos(&nativePoint)){
            auto candidate=GetAncestor(WindowFromPoint(nativePoint),GA_ROOT);
            const auto* floating=figureFloatingWindow(m_dragFigure);
            const auto source=floating?reinterpret_cast<HWND>(floating->internalWinId()):nullptr;
            const auto preview=m_dropPreview?reinterpret_cast<HWND>(m_dropPreview->internalWinId()):nullptr;
            while(candidate){RECT rectangle{};if(candidate!=source&&candidate!=preview&&IsWindowVisible(candidate)&&GetWindowRect(candidate,&rectangle)&&PtInRect(&rectangle,nativePoint)){beneath=candidate;break;}candidate=GetWindow(candidate,GW_HWNDNEXT);}
        }
    }
#endif
    int target=0;QRect bounds;
    for(const auto& figure:m_figures){
        if(figure->state.number==m_dragFigure||!figure->state.docked)continue;
        if(!m_grouped&&m_tiled&&!figure->page->isVisible())continue;
        if(figure->page->window()->isMinimized())continue;
#ifdef Q_OS_WIN
        if(beneath&&reinterpret_cast<HWND>(figure->page->window()->internalWinId())!=beneath)continue;
#endif
        if(m_grouped){auto* dock=m_groups->dockWidget(figure->state.number);if(!dock||dock->isFloating()||dock->isHidden())continue;}
        const auto cell=figureGlobalCell(figure->state.number);
        const bool exposed=figure->page->isVisible()&&!figure->page->visibleRegion().isEmpty();
        const bool previousExposed=target&&entry(target)->page->isVisible()&&!entry(target)->page->visibleRegion().isEmpty();
        if(cell.contains(global)&&(!target||qint64(cell.width())*cell.height()<qint64(bounds.width())*bounds.height()||(cell==bounds&&exposed&&!previousExposed))){target=figure->state.number;bounds=cell;}
    }
    if(!target){m_dropFigure=0;m_dropPage.clear();m_dropBounds={};m_dropCells.clear();if(m_dropPreview)m_dropPreview->hide();return;}
    const double x=double(global.x()-bounds.left())/std::max(1,bounds.width()),y=double(global.y()-bounds.top())/std::max(1,bounds.height());
    const double nearest=std::min({x,1-x,y,1-y});QString zone=QStringLiteral("tab");
    bool heading=false;
    if(!m_grouped)if(const auto* figure=entry(target);figure&&figure->heading&&figure->heading->isVisible())heading=QRect(figure->heading->mapToGlobal(QPoint()),figure->heading->size()).contains(global);
    if(m_grouped)if(auto* dock=m_groups->dockWidget(target)){
        const QPoint point=dock->mapFromGlobal(global);heading=point.y()<dock->widget()->geometry().top();
        for(auto* bar:m_groups->findChildren<QTabBar*>())if(bar->isVisible()&&QRect(bar->mapToGlobal(QPoint()),bar->size()).contains(global))
            for(int index=0;index<bar->count();++index)if(bar->tabData(index).toULongLong()==reinterpret_cast<quintptr>(dock))heading=true;
    }
    if(!heading&&nearest<.30){if(nearest==x)zone=QStringLiteral("left");else if(nearest==1-x)zone=QStringLiteral("right");else if(nearest==y)zone=QStringLiteral("top");else zone=QStringLiteral("bottom");}
    QRect preview=bounds;
    if(zone==QStringLiteral("left"))preview.setWidth(bounds.width()/2);else if(zone==QStringLiteral("right"))preview.setLeft(bounds.left()+bounds.width()/2);
    else if(zone==QStringLiteral("top"))preview.setHeight(bounds.height()/2);else if(zone==QStringLiteral("bottom"))preview.setTop(bounds.top()+bounds.height()/2);
    if(!m_dropPreview)m_dropPreview=new FigureDropPreview(this);
    m_dropFigure=target;m_dropPage=entry(target)->page;m_dropZone=zone;m_dropBounds=bounds;m_dropCells.clear();
    for(const auto& figure:m_figures)if(figure->state.docked){const auto cell=figureGlobalCell(figure->state.number);if(cell.isValid())m_dropCells.insert(figure->state.number,cell);}
    if(m_dropPreview->property("studioDropZone").toString()!=zone){m_dropPreview->setProperty("studioDropZone",zone);m_dropPreview->update();}
    if(m_dropPreview->geometry()!=preview)m_dropPreview->setGeometry(preview);
    m_dropPreview->show();m_dropPreview->raise();
}
void FigureManager::finishFigureDrag(const QPoint& global){
    if(!m_dragFigure)return;
    const int source=m_dragFigure;const auto sourcePage=m_dragPage;
    const int promised=m_dropFigure;const auto promisedPage=m_dropPage;const auto promisedBounds=m_dropBounds;const auto promisedZone=m_dropZone;const auto promisedCells=m_dropCells;
    updateFigureDropPreview(global);
    if(promised&&promisedPage&&promisedBounds.contains(global)&&(!m_sourceTabDrop||figureFloatingWindow(source))){m_dropFigure=promised;m_dropPage=promisedPage;m_dropZone=promisedZone;m_dropCells=promisedCells;m_sourceTabDrop=false;}
    const int target=m_dropFigure;const auto targetPage=m_dropPage;const auto zone=m_dropZone;const auto cells=m_dropCells;const bool moved=m_dragMoved;
    const bool floatLegacy=moved&&!target&&!m_sourceTabDrop&&!m_grouped&&entry(source)&&entry(source)->state.docked&&!figureGlobalCell(source).contains(global);
    cancelFigureDrag();const auto epoch=m_dragEpoch;
    if(floatLegacy){
        QTimer::singleShot(0,this,[this,source,sourcePage,global,epoch]{
            if(m_destroying||epoch!=m_dragEpoch||!sourcePage||!entry(source)||entry(source)->page!=sourcePage)return;
            QString message;if(undockFigure(source,&message)){if(auto* window=figureFloatingWindow(source))window->move(global-QPoint(40,12));}
            else if(!message.isEmpty())emit error(message);
        });return;
    }
    if(!moved||!target)return;
    QTimer::singleShot(0,this,[this,source,target,sourcePage,targetPage,zone,cells,epoch]{
        if(m_destroying||epoch!=m_dragEpoch||!sourcePage||!targetPage||!entry(source)||!entry(target)||entry(source)->page!=sourcePage||entry(target)->page!=targetPage)return;
        const bool migrating=!m_grouped;setGroupedFigures(true);m_tiled=false;m_rows=m_columns=0;
        if(migrating){
            QHash<int,QRect> local;
            for(auto it=cells.cbegin();it!=cells.cend();++it)if(auto* item=entry(it.key());item&&item->state.docked)local.insert(it.key(),QRect(m_groups->mapFromGlobal(it->topLeft()),it->size()));
            if(!local.isEmpty())m_groups->arrangeRectangles(local);
        }
        const bool okay=zone==QStringLiteral("tab")?m_groups->tabifyFigures(target,source):m_groups->splitFigure(target,source,(zone==QStringLiteral("left")||zone==QStringLiteral("right"))?Qt::Horizontal:Qt::Vertical,zone==QStringLiteral("left")||zone==QStringLiteral("top"));
        if(okay){m_selectedNumber=source;m_groups->setCurrentFigure(source);refreshNames();updateToolbar();emit figuresChanged();}
    });
}
bool FigureManager::eventFilter(QObject* watched,QEvent* event){
    if(m_destroying||m_rearranging)return QWidget::eventFilter(watched,event);
    const auto type=event->type();
    if(type==QEvent::FocusIn||type==QEvent::WindowActivate||type==QEvent::MouseButtonPress)activateFigureWidget(qobject_cast<QWidget*>(watched));
    if(type==QEvent::KeyPress&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape){cancelFigureDrag();return false;}
    if(type==QEvent::ContextMenu){
        auto* widget=qobject_cast<QWidget*>(watched);auto* menuEvent=static_cast<QContextMenuEvent*>(event);int number=0;
        for(const auto& figure:m_figures){
            auto* dock=m_grouped?m_groups->dockWidget(figure->state.number):nullptr;
            if(widget==figure->heading||(dock&&!dock->isFloating()&&widget&&(widget==dock||dock->isAncestorOf(widget))&&dock->mapFromGlobal(menuEvent->globalPos()).y()<dock->widget()->geometry().top())){number=figure->state.number;break;}
        }
        if(auto* bar=qobject_cast<QTabBar*>(widget)){
            const int index=bar->tabAt(bar->mapFromGlobal(menuEvent->globalPos()));
            if(index>=0){
                if(bar==m_tabs->tabBar()){for(const auto& figure:m_figures)if(m_tabs->widget(index)==figure->page){number=figure->state.number;break;}}
                else if(m_grouped)for(const auto& figure:m_figures)if(auto* dock=m_groups->dockWidget(figure->state.number);dock&&bar->tabData(index).toULongLong()==reinterpret_cast<quintptr>(dock)){number=figure->state.number;break;}
            }
        }
        if(number){m_selectedNumber=number;updateToolbar();QMenu menu(widget);populateArrangementMenu(&menu);menu.exec(menuEvent->globalPos());return true;}
    }
    if(m_dragFigure&&(type==QEvent::MouseMove||type==QEvent::NonClientAreaMouseMove)){
        auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->buttons()&Qt::LeftButton){m_nativeInputDrag|=mouse->spontaneous();updateFigureDropPreview(mouse->globalPosition().toPoint());}
        else if(m_dragMoved&&!m_nativeMoveActive)finishFigureDrag(mouse->globalPosition().toPoint());
    }
    if(type==QEvent::NonClientAreaMouseButtonDblClick){cancelFigureDrag();return false;}
    if(type==QEvent::MouseButtonPress||type==QEvent::NonClientAreaMouseButtonPress){
        auto* mouse=static_cast<QMouseEvent*>(event);auto* widget=qobject_cast<QWidget*>(watched);if(mouse->button()!=Qt::LeftButton||!widget)return false;
        if(type==QEvent::NonClientAreaMouseButtonPress&&desktop::isFloatingCaptionControl(widget,mouse)){cancelFigureDrag();return false;}
        const auto global=mouse->globalPosition().toPoint();
        const bool propagation=m_dragPage&&global==m_dragStart&&(widget==m_dragPage||widget->isAncestorOf(m_dragPage));
        if(!propagation)cancelFigureDrag();
        for(const auto& figure:m_figures){
            QDockWidget* dock=m_grouped?m_groups->dockWidget(figure->state.number):nullptr;auto* floating=figureFloatingWindow(figure->state.number);
            const bool inside=widget==figure->page||figure->page->isAncestorOf(widget)||(dock&&(widget==dock||dock->isAncestorOf(widget)))||widget==floating;
            if(!inside)continue;
            if(type==QEvent::NonClientAreaMouseButtonPress)activateFigureWidget(widget);
            bool title=widget->property("studioFigureDragTitle").toInt()==figure->state.number;
            if(dock&&dock->widget()&&!qobject_cast<QAbstractButton*>(widget))title|=dock->mapFromGlobal(global).y()<dock->widget()->geometry().top();
            if(type==QEvent::NonClientAreaMouseButtonPress&&floating)title=true;
#ifdef Q_OS_WIN
            if(type==QEvent::NonClientAreaMouseButtonPress&&mouse->spontaneous()&&floating&&QGuiApplication::platformName()==QStringLiteral("windows")){
                const auto handle=floating->internalWinId();POINT point{};if(handle&&GetCursorPos(&point))title=SendMessageW(reinterpret_cast<HWND>(handle),WM_NCHITTEST,0,MAKELPARAM(point.x,point.y))==HTCAPTION;
            }
#endif
            if(title){beginFigureDrag(figure->state.number,global);m_nativeInputDrag|=mouse->spontaneous();}break;
        }
        if(auto* bar=qobject_cast<QTabBar*>(widget)){
            const int index=bar->tabAt(mouse->position().toPoint());if(index>=0){
                int number=0;
                if(bar==m_tabs->tabBar()){
                    for(const auto& figure:m_figures)if(m_tabs->widget(index)==figure->page){number=figure->state.number;break;}
                }else if(m_grouped)for(const auto& figure:m_figures)if(auto* dock=m_groups->dockWidget(figure->state.number)){
                    const auto identity=bar->tabData(index).toULongLong();if(identity==reinterpret_cast<quintptr>(dock)||(!identity&&m_groups->isAncestorOf(bar)&&bar->tabText(index)==dock->windowTitle())){number=figure->state.number;break;}
                }
                if(number){m_selectedNumber=number;updateToolbar();beginFigureDrag(number,global);m_dragTabBar=bar;m_nativeInputDrag|=mouse->spontaneous();}
            }
        }
    }
    if((type==QEvent::MouseButtonRelease||type==QEvent::NonClientAreaMouseButtonRelease)&&m_dragFigure)finishFigureDrag(static_cast<QMouseEvent*>(event)->globalPosition().toPoint());
    if(type==QEvent::Close&&(watched==m_dragPage||watched==m_dropPage||watched==m_dragDock))cancelFigureDrag();
    return QWidget::eventFilter(watched,event);
}
bool FigureManager::nativeEventFilter(const QByteArray&,void* message,qintptr*){
#ifdef Q_OS_WIN
    const auto* native=static_cast<MSG*>(message);if(!native)return false;
    if(native->message==WM_NCLBUTTONDOWN&&native->wParam==HTCAPTION)for(const auto& figure:m_figures)if(auto* floating=figureFloatingWindow(figure->state.number);floating&&floating->internalWinId()&&reinterpret_cast<HWND>(floating->internalWinId())==native->hwnd){beginFigureDrag(figure->state.number,QCursor::pos());m_nativeInputDrag=true;break;}
    if(!m_dragFigure)return false;
    if(native->message==WM_EXITSIZEMOVE&&m_nativeMoveActive&&m_nativeMoveWindow==reinterpret_cast<quintptr>(native->hwnd)){
        if(GetAsyncKeyState(VK_ESCAPE)&0x8000)cancelFigureDrag();else finishFigureDrag(QCursor::pos());return false;
    }
    auto* floating=figureFloatingWindow(m_dragFigure);if(!floating||!floating->internalWinId()||reinterpret_cast<HWND>(floating->internalWinId())!=native->hwnd)return false;
    if(native->message==WM_ENTERSIZEMOVE){m_nativeMoveActive=true;m_nativeMoveWindow=reinterpret_cast<quintptr>(native->hwnd);m_nativeInputDrag|=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;}
    else if(native->message==WM_MOVING)updateFigureDropPreview(QCursor::pos());
    else if(native->message==WM_KEYDOWN&&native->wParam==VK_ESCAPE)cancelFigureDrag();
#else
    Q_UNUSED(message);
#endif
    return false;
}
