    using ScientificObserver=std::function<void(double,std::span<const double>)>;
    ScientificObserver scientificLiveObserver(const Value* setting,const std::string& family,std::size_t width,SourceSpan span){
        if(!setting)return {};
        const auto* fields=recordFields(*setting);
        if(!fields){const auto enabled=integer(*setting,span);if(enabled==0)return {};if(enabled!=1)fail("R1021","live_plot must be0,1 or an options record.",span);}
        if(!options_.allowFigures)fail("R1021","Live scientific plots require figures to be enabled.",span);
        if(fields)for(const auto& [key,value]:*fields){(void)value;if(key!="figure"&&key!="coordinate"&&key!="max_points"&&key!="interval")fail("R1021","Unknown live plot option: "+key,span);}
        auto count=[&](const char* key,I fallback,I maximum){const auto at=fields?fields->find(key):ChaosFields::const_iterator{};const auto value=fields&&at!=fields->end()?integer(at->second,span):fallback;if(value<1||value>maximum)fail("R1021","Live plot index/budget is outside its documented range.",span);return static_cast<std::size_t>(value);};
        const auto figure=count("figure",60,64),coordinate=count("coordinate",1,static_cast<I>(width)),maximum=count("max_points",1024,4096);
        double interval=.15;if(fields)if(const auto at=fields->find("interval");at!=fields->end())interval=real(at->second,span);
        if(!std::isfinite(interval)||interval<.05||interval>10||maximum<8)fail("R1021","Live plot interval must be0.05..10 seconds and max_points8..4096.",span);
        if(!options_.figureProgress)return {};
        struct Buffer {std::vector<std::array<double,3>> points;std::chrono::steady_clock::time_point last{};};auto buffer=std::make_shared<Buffer>();
        auto publish=options_.figureProgress;
        return [buffer,publish=std::move(publish),family,figure,coordinate,maximum,interval](double time,std::span<const double> values){
            if(values.size()<coordinate||!std::isfinite(time)||!std::isfinite(values[coordinate-1]))return;
            if(buffer->points.size()>=maximum){std::size_t kept=0;for(std::size_t n=0;n<buffer->points.size();n+=2)buffer->points[kept++]=buffer->points[n];buffer->points.resize(kept);}
            buffer->points.push_back({time,values[coordinate-1],0});const auto now=std::chrono::steady_clock::now();if(buffer->points.size()<2||std::chrono::duration<double>(now-buffer->last).count()<interval)return;buffer->last=now;
            FigureAction select;select.kind=FigureActionKind::Select;select.figure=static_cast<int>(figure);select.name="Live "+family;select.numberTitle=false;
            FigureAction style;style.kind=FigureActionKind::ConfigurePlot;style.figure=select.figure;style.plotStyle=PlotStylePatch{};style.plotStyle->hold=false;
            FigureAction plot;plot.kind=FigureActionKind::Plot;plot.figure=select.figure;plot.dimensions=2;plot.axes={-1,-1,-1};plot.curves.push_back({buffer->points,"coordinate "+std::to_string(coordinate)});
            FigureAction title;title.kind=FigureActionKind::SetTitle;title.figure=select.figure;title.text=family+": running (display samples may be thinned)";title.latex=false;
            publish({select,style,plot,title});
        };
    }
