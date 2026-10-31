#include "field_query_helpers.inc"
    std::shared_ptr<const plotting::FieldAnimationData> animationInput(const Value& value,const ChaosFields* settings,SourceSpan span){
        const auto* fields=recordFields(value);if(!fields||!fields->contains("values")||!fields->contains("x")||!fields->contains("t"))fail("R1021","Field animation requires a PDE result with x, t and values.",span);
        auto data=std::make_shared<plotting::FieldAnimationData>();data->x=numericVector(fields->at("x"),span,false);data->t=numericVector(fields->at("t"),span,false);const auto found=fields->find("dimension");data->dimension=found==fields->end()?1:static_cast<int>(integer(found->second,span));
        if(data->dimension==2){if(!fields->contains("y"))fail("R1021","Two-dimensional field animation requires y coordinates.",span);data->y=numericVector(fields->at("y"),span,false);}
        data->nx=data->x.size();data->ny=data->dimension==2?data->y.size():1;
        auto values=numericArray(fields->at("values"),span);data->components=values.components;bool raw=false;
        if(fields->contains("value_layout")){const auto* layout=std::get_if<std::string>(&fields->at("value_layout"));if(!layout||*layout!="node_coefficients"||!fields->contains("components"))fail("R1021","Unknown raw field layout metadata.",span);const auto components=integer(fields->at("components"),span);if((components<1||components>64)||values.components!=1)fail("R1021","Raw fields require real node-major coefficient columns and components=1..64.",span);data->components=static_cast<std::size_t>(components);raw=true;}
        if(data->nx==0||data->ny==0||data->nx>16384/data->ny||values.rows!=data->t.size()||values.columns!=data->nx*data->ny*(raw?data->components:1)||values.values.size()>std::min<std::size_t>(8000000,options_.maxMatrixCoordinates))fail("R1021","Field values must have one time row and x-fastest spatial columns within the retained coordinate budget.",span);
        data->values=std::move(values.values);data->coefficient=static_cast<std::size_t>(chaosInteger(settings,"component",0,0,static_cast<I>(data->components-1),span));data->fps=chaosNumber(settings,"fps",15,span);
        const auto mode=chaosText(settings,"mode","surface",span);if(mode!="surface"&&mode!="image")fail("R1021","Animation mode must be surface or image.",span);data->surface=mode=="surface";
        const auto loop=chaosInteger(settings,"loop",0,0,1,span);data->loop=loop!=0;
        const auto maximum=static_cast<std::size_t>(chaosInteger(settings,"max_frames",240,1,240,span));
        const auto first=static_cast<std::size_t>(chaosInteger(settings,"first",1,1,static_cast<I>(data->t.size()),span)),last=static_cast<std::size_t>(chaosInteger(settings,"last",static_cast<I>(data->t.size()),1,static_cast<I>(data->t.size()),span)),stride=static_cast<std::size_t>(chaosInteger(settings,"stride",1,1,100000,span));
        if(last<first||(last-first)/stride+1>maximum)fail("R1021","Selected animation frames exceed max_frames; choose first/last/stride explicitly.",span);
        for(std::size_t k=first-1;k<last;k+=stride)data->selectedFrames.push_back(k);
        data->algebraId=chaosText(fields,"algebra_id",result_.analysis.program.algebraId,span);data->algebraHash=chaosText(fields,"algebra_hash","",span);const auto labels=chaosText(fields,"basis_labels","",span);if(!labels.empty()){std::istringstream stream(labels);std::string label;while(std::getline(stream,label,','))data->basisLabels.push_back(label);}
        data->gamma=fields->contains("gamma")?finite(real(fields->at("gamma"),span),span):context_.gamma();data->context="Saved PDE field";chargeNumericWork(data->values.size(),span);
        plotting::validateFieldAnimation(*data);return data;
    }
    std::optional<Value> animationCall(const Expr& expression,const std::vector<Value>& arguments){
        if(auto queried=fieldQueryCall(expression,arguments))return queried;
        const auto& name=expression.text;const auto span=expression.span;if(name!="animatefield"&&name!="animationframe"&&name!="playanimation"&&name!="exportframes")return {};
        try{
            if(name=="animationframe"||name=="playanimation"){
                if(arguments.size()!=1)fail("R1021","Use animationframe(one_based_frame) or playanimation(\"on\"/\"off\").",span);
                auto action=figureAction(name=="animationframe"?FigureActionKind::SelectAnimationFrame:FigureActionKind::SetAnimationPlaying,span);
                if(name=="animationframe"){const auto index=integer(arguments[0],span);if(index<1||index>240)fail("R1021","Animation frame must be an integer 1..240 in the retained selection.",span);action.animationFrame=static_cast<std::size_t>(index-1);}
                else{const auto* mode=std::get_if<std::string>(&arguments[0]);if(!mode||(*mode!="on"&&*mode!="off"))fail("R1021","Playback mode must be on or off.",span);action.animationPlaying=*mode=="on";}
                result_.figureActions.push_back(std::move(action));return I(0);
            }
            std::set<std::string> allowed{"component","mode","fps","loop","first","last","stride","max_frames"};const bool output=name=="exportframes";
            if(output)allowed.insert({"prefix","width","height","dpi","overwrite","max_bytes","create_directory"});
            const auto* settings=chaosFields(arguments,output?2:1,allowed,span);const auto data=animationInput(arguments[0],settings,span);
            if(!output){std::size_t retained=data->values.size();std::set<const plotting::FieldAnimationData*> seen;for(const auto& action:result_.figureActions)if(action.animation&&seen.insert(action.animation.get()).second){if(action.animation->values.size()>options_.maxMatrixCoordinates-retained)fail("R1021","Retained animation frames exceed the request coordinate budget.",span);retained+=action.animation->values.size();}auto action=figureAction(FigureActionKind::SetAnimation,span);action.algebraId=data->algebraId;action.gamma=data->gamma;action.animation=data;reservePlotPoints(data->nx*data->ny,span);result_.figureActions.push_back(std::move(action));return I(0);}
            const auto* requested=std::get_if<std::string>(&arguments[1]);if(!requested)fail("R1021","Frame export directory must be a string.",span);const auto directory=dataPath(*requested,span);plotting::FrameExportOptions options;options.prefix=chaosText(settings,"prefix","field",span);options.maximumFrames=static_cast<std::size_t>(chaosInteger(settings,"max_frames",240,1,240,span));options.maximumBytes=static_cast<std::uint64_t>(chaosInteger(settings,"max_bytes",256*1024*1024,1024,1024ll*1024*1024,span));options.createDirectory=chaosInteger(settings,"create_directory",0,0,1,span)!=0;options.image.width=static_cast<int>(chaosInteger(settings,"width",1600,160,16000,span));options.image.height=static_cast<int>(chaosInteger(settings,"height",1000,120,16000,span));options.image.dpi=static_cast<int>(chaosInteger(settings,"dpi",150,36,1200,span));options.image.overwrite=chaosInteger(settings,"overwrite",0,0,1,span)!=0;options.image.cancellation=options_.cancellation;
            const auto report=plotting::exportFieldFrames(*data,directory,options);
            return makeRecord({{"frames",I(report.frames)},{"bytes",I(report.bytes)},{"width",I(options.image.width)},{"height",I(options.image.height)},{"dpi",I(options.image.dpi)},{"directory",report.directory.string()},{"manifest",report.manifest.string()},{"status",std::string("exported")}},span);
        }catch(const plotting::ExportError& error){fail(options_.cancellation&&options_.cancellation->load()?"R1006":"R1021",error.what(),span);}
    }
