    std::optional<Value> videoCall(const Expr& expression,const std::vector<Value>& arguments){
        if(expression.text!="exportvideo")return {};
        const auto span=expression.span;
        const auto* fields=chaosFields(arguments,2,{"component","mode","fps","loop","first","last","stride","max_frames","width","height","dpi","max_bytes","max_seconds","overwrite"},span);
        const auto* requested=std::get_if<std::string>(&arguments[1]);if(!requested)fail("R1022","Native AVI export requires a destination filename.",span);
        try{
            const auto data=animationInput(arguments[0],fields,span);plotting::VideoExportOptions settings;settings.fps=data->fps;settings.maximumFrames=static_cast<std::size_t>(chaosInteger(fields,"max_frames",240,1,240,span));settings.maximumBytes=static_cast<std::uint64_t>(chaosInteger(fields,"max_bytes",256*1024*1024,1024,1024ll*1024*1024-1,span));settings.maximumSeconds=chaosNumber(fields,"max_seconds",60,span);settings.image.width=static_cast<int>(chaosInteger(fields,"width",1600,160,16000,span));settings.image.height=static_cast<int>(chaosInteger(fields,"height",1000,120,16000,span));settings.image.dpi=static_cast<int>(chaosInteger(fields,"dpi",150,36,1200,span));settings.image.overwrite=chaosInteger(fields,"overwrite",0,0,1,span)!=0;settings.image.cancellation=options_.cancellation;
            const auto report=plotting::exportFieldVideo(*data,dataPath(*requested,span),settings);
            return makeRecord({{"frames",I(report.frames)},{"bytes",I(report.bytes)},{"width",I(settings.image.width)},{"height",I(settings.image.height)},{"fps",report.fps},{"duration_seconds",report.duration},{"source_duration_seconds",report.sourceDuration},{"path",report.path.string()},{"codec",std::string("BI_RGB24")},{"status",std::string("exported")},{"physical_time_interpolated",I(0)}},span);
        }catch(const plotting::ExportError& error){fail(options_.cancellation&&options_.cancellation->load()?"R1006":"R1022",error.what(),span);}
    }
