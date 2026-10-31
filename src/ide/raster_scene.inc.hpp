// Members of FigurePanel. VTK is used for immutable CPU geometry/projection;
// this path never creates a GL context or calls vtkRenderWindow::Render.
std::function<double(int,int)> cursorDepths(QPointF target,double radius) {
    radius=std::clamp(radius,1.0,256.0);
    const int left=std::max(0,static_cast<int>(std::floor(target.x()-radius)));
    const int bottom=std::max(0,static_cast<int>(std::floor(target.y()-radius)));
    const int right=std::min(window->GetSize()[0]-1,static_cast<int>(std::ceil(target.x()+radius)));
    const int top=std::min(window->GetSize()[1]-1,static_cast<int>(std::ceil(target.y()+radius)));
    const int width=std::max(1,right-left+1),height=std::max(1,top-bottom+1);
    auto depth=std::make_shared<std::vector<double>>(static_cast<std::size_t>(width)*height,1.0);
    auto write=[&](int x,int y,double z){if(x>=left&&x<=right&&y>=bottom&&y<=top&&z>=0&&z<=1){auto& value=(*depth)[static_cast<std::size_t>(y-bottom)*width+x-left];value=std::min(value,z);}};
    auto* actors=renderer->GetActors();actors->InitTraversal();
    while(auto* actor=actors->GetNextActor()){
        if(!actor->GetVisibility()||actor->GetProperty()->GetOpacity()<.05)continue;
        auto* mapper=vtkPolyDataMapper::SafeDownCast(actor->GetMapper());if(!mapper)continue;
        mapper->Update();auto* data=mapper->GetInput();if(!data)continue;
        auto project=[&](vtkIdType id){double point[3],input[4],world[4];data->GetPoint(id,point);for(int d=0;d<3;++d)input[d]=point[d];input[3]=1;actor->GetMatrix()->MultiplyPoint(input,world);renderer->SetWorldPoint(world);renderer->WorldToDisplay();const auto* p=renderer->GetDisplayPoint();return std::array<double,3>{p[0],p[1],p[2]};};
        auto triangle=[&](auto a,auto b,auto c){
            if((a[2]<0&&b[2]<0&&c[2]<0)||(a[2]>1&&b[2]>1&&c[2]>1))return;
            const double divisor=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1]);if(std::abs(divisor)<1e-15)return;
            const int x0=std::max(left,static_cast<int>(std::floor(std::max(-1e8,std::min({a[0],b[0],c[0]}))))),x1=std::min(right,static_cast<int>(std::ceil(std::min(1e8,std::max({a[0],b[0],c[0]})))));
            const int y0=std::max(bottom,static_cast<int>(std::floor(std::max(-1e8,std::min({a[1],b[1],c[1]}))))),y1=std::min(top,static_cast<int>(std::ceil(std::min(1e8,std::max({a[1],b[1],c[1]})))));
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){const double u=((b[1]-c[1])*(x-c[0])+(c[0]-b[0])*(y-c[1]))/divisor,v=((c[1]-a[1])*(x-c[0])+(a[0]-c[0])*(y-c[1]))/divisor,w=1-u-v;if(u>=-1e-7&&v>=-1e-7&&w>=-1e-7)write(x,y,u*a[2]+v*b[2]+w*c[2]);}
        };
        vtkIdType count=0;const vtkIdType* ids=nullptr;
        data->GetPolys()->InitTraversal();while(data->GetPolys()->GetNextCell(count,ids))for(vtkIdType i=2;i<count;++i)triangle(project(ids[0]),project(ids[i-1]),project(ids[i]));
        data->GetStrips()->InitTraversal();while(data->GetStrips()->GetNextCell(count,ids))for(vtkIdType i=2;i<count;++i)triangle(project(ids[i-2]),project(ids[i-1]),project(ids[i]));
        data->GetLines()->InitTraversal();while(data->GetLines()->GetNextCell(count,ids))for(vtkIdType i=1;i<count;++i){
            const auto a=project(ids[i-1]),b=project(ids[i]);const double dx=b[0]-a[0],dy=b[1]-a[1],square=dx*dx+dy*dy,r=std::max(.7,actor->GetProperty()->GetLineWidth()*.5);
            if(std::max(a[0],b[0])+r<left||std::min(a[0],b[0])-r>right||std::max(a[1],b[1])+r<bottom||std::min(a[1],b[1])-r>top)continue;
            for(int y=bottom;y<=top;++y)for(int x=left;x<=right;++x){const double t=square>0?std::clamp(((x-a[0])*dx+(y-a[1])*dy)/square,0.,1.):0,px=a[0]+t*dx-x,py=a[1]+t*dy-y;if(px*px+py*py<=r*r)write(x,y,a[2]+t*(b[2]-a[2]));}
        }
    }
    return [depth,left,bottom,right,top,width](int x,int y){return x>=left&&x<=right&&y>=bottom&&y<=top?(*depth)[static_cast<std::size_t>(y-bottom)*width+x-left]:1.;};
}

RasterPlotTask rasterSnapshot(QSize pixels,double dpr) {
    if(!renderer||!window||pixels.isEmpty())return {};
    window->SetSize(pixels.width(),pixels.height());positionOverlays();
    auto snapshot=std::make_shared<FigurePanel>();snapshot->detachedPaintScene=true;
    auto& target=snapshot->state;
    target.dimensions=state.dimensions;target.axes=state.axes;target.style=state.style;
    target.algebraId=state.algebraId;target.gamma=state.gamma;target.basisLabels=state.basisLabels;
    target.title=state.title;target.xLabel=state.xLabel;target.yLabel=state.yLabel;target.zLabel=state.zLabel;target.caption=state.caption;
    target.titleLatex=state.titleLatex;target.xLabelLatex=state.xLabelLatex;target.yLabelLatex=state.yLabelLatex;target.zLabelLatex=state.zLabelLatex;target.captionLatex=state.captionLatex;
    target.vectors=state.vectors;
    for(const auto& curve:state.curves){target.curves.emplace_back();target.curves.back().label=curve.label;}
    if(state.mesh){target.mesh.emplace();if(!state.mesh->scalars.empty()){const auto [low,high]=std::minmax_element(state.mesh->scalars.begin(),state.mesh->scalars.end());target.mesh->scalars={*low,*high};}}
    snapshot->labelImages=rasterLabelImages;snapshot->capturedDefaultFont=QApplication::font();
    snapshot->window=vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();snapshot->window->SetReadyForRendering(false);
    snapshot->window->SetSize(pixels.width(),pixels.height());snapshot->renderer=vtkSmartPointer<vtkRenderer>::New();
    snapshot->renderer->SetViewport(renderer->GetViewport());snapshot->renderer->GetActiveCamera()->DeepCopy(renderer->GetActiveCamera());
    snapshot->window->AddRenderer(snapshot->renderer);
    snapshot->annotationRenderer=vtkSmartPointer<vtkRenderer>::New();snapshot->window->AddRenderer(snapshot->annotationRenderer);
    std::size_t bytes=static_cast<std::size_t>(pixels.width())*pixels.height()*4;
    for(const auto& image:rasterLabelImages)bytes+=static_cast<std::size_t>(image.sizeInBytes());
    for(const auto& vector:target.vectors)bytes+=vector.allCoefficients().size()*sizeof(double);
    for(const auto& curve:target.curves)bytes+=curve.label.size();
    auto* actors=renderer->GetActors();actors->InitTraversal();
    while(auto* actor=actors->GetNextActor()){
        auto* mapper=vtkPolyDataMapper::SafeDownCast(actor->GetMapper());if(!mapper)continue;
        mapper->Update();auto* input=mapper->GetInput();if(!input)continue;
        const auto geometryBytes=static_cast<std::size_t>(input->GetActualMemorySize())*1024;
        constexpr std::size_t maximumSceneBytes=256U*1024U*1024U;
        if(bytes>maximumSceneBytes||geometryBytes>maximumSceneBytes-bytes)throw std::runtime_error("The figure scene exceeds the 256 MiB display budget.");
        // Triangulate in world space. A valid cylinder cap/quad can collapse to
        // a touching or edge-on polygon after projection; triangulating that
        // projected outline would reject otherwise valid VTK geometry.
        vtkNew<vtkTriangleFilter> triangles;vtkPolyData* triangleData=input;
        if(input->GetNumberOfStrips()||input->GetPolys()->GetMaxCellSize()>3){
            if(geometryBytes>(maximumSceneBytes-bytes)/3)throw std::runtime_error("The figure triangulation exceeds the 256 MiB display budget.");
            triangles->SetInputData(input);triangles->PassLinesOn();triangles->PassVertsOn();triangles->Update();triangleData=triangles->GetOutput();
        }
        const auto triangulatedBytes=static_cast<std::size_t>(triangleData->GetActualMemorySize())*1024;
        if(triangulatedBytes>maximumSceneBytes-bytes)throw std::runtime_error("The triangulated figure exceeds the 256 MiB display budget.");
        auto data=vtkSmartPointer<vtkPolyData>::New();data->DeepCopy(triangleData);bytes+=triangulatedBytes;
        auto copyMapper=vtkSmartPointer<vtkPolyDataMapper>::New();copyMapper->SetInputData(data);
        copyMapper->SetScalarVisibility(mapper->GetScalarVisibility());copyMapper->SetScalarMode(mapper->GetScalarMode());
        copyMapper->SetColorMode(mapper->GetColorMode());copyMapper->SetScalarRange(mapper->GetScalarRange());
        if(auto* colors=vtkLookupTable::SafeDownCast(mapper->GetLookupTable())){auto lut=vtkSmartPointer<vtkLookupTable>::New();lut->DeepCopy(colors);copyMapper->SetLookupTable(lut);}
        auto copy=vtkSmartPointer<vtkActor>::New();copy->SetMapper(copyMapper);copy->GetProperty()->DeepCopy(actor->GetProperty());copy->SetVisibility(actor->GetVisibility());
        auto matrix=vtkSmartPointer<vtkMatrix4x4>::New();matrix->DeepCopy(actor->GetMatrix());copy->SetUserMatrix(matrix);snapshot->renderer->AddActor(copy);
    }
    const auto textCopy=[](vtkTextActor* source){
        vtkSmartPointer<vtkTextActor> copy;if(!source)return copy;copy=vtkSmartPointer<vtkTextActor>::New();copy->SetInput(source->GetInput());copy->GetTextProperty()->ShallowCopy(source->GetTextProperty());copy->SetVisibility(source->GetVisibility());copy->SetDisplayPosition(static_cast<int>(source->GetPosition()[0]),static_cast<int>(source->GetPosition()[1]));return copy;
    };
    for(const auto& tick:tickLabels)snapshot->tickLabels.push_back({tick.point,tick.axis,textCopy(tick.actor)});
    for(const auto& text:graphText)snapshot->graphText.push_back({text.point,textCopy(text.actor)});
    snapshot->contextCaption=textCopy(contextCaption);
    for(const auto& tip:dataTips){auto copy=tip;copy.text=textCopy(tip.text);copy.marker=textCopy(tip.marker);snapshot->dataTips.push_back(std::move(copy));}
    for(const auto& overlay:overlays){auto actor=vtkSmartPointer<vtkTexturedActor2D>::New();actor->SetPosition(overlay.actor->GetPosition());snapshot->overlays.push_back({overlay.kind,overlay.size,overlay.displayed,actor,{}});}
    if(legend){snapshot->legend=vtkSmartPointer<vtkLegendBoxActor>::New();snapshot->legend->SetVisibility(legend->GetVisibility());
        snapshot->legend->GetPositionCoordinate()->SetCoordinateSystem(legend->GetPositionCoordinate()->GetCoordinateSystem());snapshot->legend->GetPositionCoordinate()->SetValue(legend->GetPositionCoordinate()->GetValue());
        snapshot->legend->GetPosition2Coordinate()->SetCoordinateSystem(legend->GetPosition2Coordinate()->GetCoordinateSystem());snapshot->legend->GetPosition2Coordinate()->SetValue(legend->GetPosition2Coordinate()->GetValue());}
    auto cancelled=std::make_shared<std::atomic_bool>(false);snapshot->paintCancelled=cancelled;
    return {[snapshot,pixels,dpr]{
        if(snapshot->paintCancelled->load())return QImage{};
        QImage image(pixels,QImage::Format_ARGB32_Premultiplied);if(image.isNull())return image;image.fill(Qt::white);
        QPainter painter(&image);std::size_t primitives=0;QString error;
        const bool ok=snapshot->paintVector(painter,primitives,&error);painter.end();if(!ok){if(snapshot->paintCancelled->load())return QImage{};throw std::runtime_error(error.toStdString());}
        image.setDevicePixelRatio(dpr);return image;
    },cancelled,bytes};
}
