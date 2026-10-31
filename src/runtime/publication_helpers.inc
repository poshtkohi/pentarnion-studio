#include "histogram_plot_helpers.inc"
    std::optional<Value> publicationCall(const Expr& expression,const std::vector<Value>& arguments) {
        if(auto value=histogramPlotCall(expression,arguments))return value;
        const auto& name=expression.text;const auto span=expression.span;
        if(name!="hold"&&name!="legend"&&name!="plotstyle"&&name!="xlim"&&name!="ylim"&&name!="zlim"&&name!="axis"&&name!="linkaxes"&&name!="exportgraphics"&&name!="saveplottemplate"&&name!="loadplottemplate"&&name!="linkcamera")return {};
        auto textValue=[&](const Value& value){const auto* text=std::get_if<std::string>(&value);if(!text)fail("R1020","Plot property requires a string.",span);return *text;};
        auto boolean=[&](const Value& value){const auto number=integer(value,span);if(number!=0&&number!=1)fail("R1020","Plot boolean properties require 0 or 1.",span);return number!=0;};
        auto action=figureAction(FigureActionKind::ConfigurePlot,span);PlotStylePatch patch;
        try{
            if(name=="linkcamera"){
                if(arguments.size()!=1)fail("R1020","Use linkcamera([subplot_ids]); an empty vector unlinks.",span);
                const auto indices=numericVector(arguments[0],span);if((!indices.empty()&&indices.size()<2)||indices.size()>64)fail("R1020","Link2..64 unique allocated3D subplots, or an empty vector.",span);action.kind=FigureActionKind::LinkCameras;for(const auto value:indices){if(value<1||value>64||std::floor(value)!=value||std::find(action.linkedPanels.begin(),action.linkedPanels.end(),static_cast<int>(value))!=action.linkedPanels.end())fail("R1020","Camera links require unique subplot IDs1..64.",span);action.linkedPanels.push_back(static_cast<int>(value));}
            }else if(name=="saveplottemplate"||name=="loadplottemplate"){
                const auto* fields=chaosFields(arguments,1,{"overwrite"},span);const auto path=dataPath(textValue(arguments[0]),span);
                if(name=="loadplottemplate"){if(fields)fail("R1020","loadplottemplate accepts a path only.",span);action.kind=FigureActionKind::ApplyPlotTemplate;action.plotTemplate=std::make_shared<const plotting::PlotTemplate>(plotting::readPlotTemplate(path));}
                else{const bool overwrite=chaosOption(fields,"overwrite")?boolean(*chaosOption(fields,"overwrite")):false;if(options_.nativeFigureExport){plotting::NativeFigureScene scene;scene.ingest(result_.figureActions);plotting::writePlotTemplate(scene.currentTemplate(),path,overwrite);return I(0);}action.kind=FigureActionKind::SavePlotTemplate;action.text=path.string();action.exportOptions=FigureExportOptions{};action.exportOptions->overwrite=overwrite;}
            }else if(name=="hold"){
                if(arguments.size()!=1)fail("R1020","Use hold(\"on\") or hold(\"off\").",span);
                const auto value=textValue(arguments[0]);if(value!="on"&&value!="off")fail("R1020","Hold mode must be on or off.",span);patch.hold=value=="on";
            }else if(name=="plotstyle"){
                const auto* fields=chaosFields(arguments,0,{"line_width","line_style","font_size","font_family","colors","legend_location","legend_visible","legend_box","equal","hold"},span);
                if(arguments.empty())fail("R1020","plotstyle requires an options record; store that record to reuse the same style.",span);
                if(const auto* value=chaosOption(fields,"line_width"))patch.lineWidth=real(*value,span);
                if(const auto* value=chaosOption(fields,"font_size"))patch.fontSize=real(*value,span);
                if(const auto* value=chaosOption(fields,"line_style"))patch.lineStyle=textValue(*value);
                if(const auto* value=chaosOption(fields,"font_family"))patch.fontFamily=textValue(*value);
                if(const auto* value=chaosOption(fields,"legend_location"))patch.legendLocation=textValue(*value);
                if(const auto* value=chaosOption(fields,"legend_visible"))patch.legendVisible=boolean(*value);
                if(const auto* value=chaosOption(fields,"legend_box"))patch.legendBox=boolean(*value);
                if(const auto* value=chaosOption(fields,"equal"))patch.equalUnits=boolean(*value);
                if(const auto* value=chaosOption(fields,"hold"))patch.hold=boolean(*value);
                if(const auto* value=chaosOption(fields,"colors")){const auto colors=numericArray(*value,span);if(colors.components!=1||colors.columns!=3||colors.rows<1||colors.rows>256)fail("R1020","colors requires 1..256 rows of real RGB triples.",span);std::vector<std::array<double,3>> palette(colors.rows);for(std::size_t row=0;row<colors.rows;++row)std::copy_n(colors.values.begin()+static_cast<std::ptrdiff_t>(row*3),3,palette[row].begin());patch.colors=std::move(palette);}
            }else if(name=="legend"){
                if(arguments.empty()||arguments.size()>257)fail("R1020","legend requires labels, on/off, or labels followed by options(location,box).",span);
                const auto* fields=recordFields(arguments.back());const auto count=arguments.size()-(fields?1u:0u);
                if(count==0)fail("R1020","Supply at least one legend label.",span);
                if(fields)for(const auto& [key,value]:*fields){(void)value;if(key!="location"&&key!="box")fail("R1020","Unknown legend option: "+key,span);}
                std::vector<std::string> labels;for(std::size_t k=0;k<count;++k)labels.push_back(textValue(arguments[k]));
                if(count==1&&(labels[0]=="on"||labels[0]=="off"))patch.legendVisible=labels[0]=="on";
                else{patch.legendLabels=std::move(labels);patch.legendVisible=true;}
                if(const auto* value=chaosOption(fields,"location"))patch.legendLocation=textValue(*value);
                if(const auto* value=chaosOption(fields,"box"))patch.legendBox=boolean(*value);
            }else if(name=="xlim"||name=="ylim"||name=="zlim"||name=="axis"){
                if(arguments.size()!=1)fail("R1020","Axis commands require one bounds vector or mode string.",span);
                const int selected=name=="xlim"?0:name=="ylim"?1:name=="zlim"?2:-1;
                if(const auto* value=std::get_if<std::string>(&arguments[0])){
                    if(name=="axis"&&*value=="equal")patch.equalUnits=true;
                    else if(*value=="auto"){if(selected<0){patch.limitsSet={true,true,true};patch.equalUnits=false;}else patch.limitsSet[static_cast<std::size_t>(selected)]=true;}
                    else fail("R1020","Axis mode must be auto; axis also accepts equal.",span);
                }else{
                    const auto values=numericVector(arguments[0],span,false);
                    if((selected>=0&&values.size()!=2)||(selected<0&&values.size()!=4&&values.size()!=6))fail("R1020","Use two limits per selected axis, or axis([xmin xmax ymin ymax (zmin zmax)]).",span);
                    const auto count=selected<0?values.size()/2:1u;
                    for(std::size_t k=0;k<count;++k){const auto axis=selected<0?k:static_cast<std::size_t>(selected);patch.limitsSet[axis]=true;patch.limits[axis]=AxisLimits{values[k*2],values[k*2+1]};}
                }
            }else if(name=="linkaxes"){
                if(arguments.size()!=2)fail("R1020","Use linkaxes([subplot_ids],\"x\"/\"y\"/\"xy\"/\"xyz\"), or linkaxes([],\"off\").",span);
                const auto indices=numericVector(arguments[0],span,false);if(indices.size()>64)fail("R1020","At most 64 subplot IDs can be linked.",span);
                action.kind=FigureActionKind::LinkAxes;const auto axes=textValue(arguments[1]);action.linkedAxesMask=0;
                if(axes!="off"){for(const char c:axes){const unsigned bit=c=='x'?1u:c=='y'?2u:c=='z'?4u:0u;if(bit==0||(action.linkedAxesMask&bit))fail("R1020","Linked axes must be a unique combination of x, y and z.",span);action.linkedAxesMask|=bit;}if(action.linkedAxesMask==0||indices.size()<2)fail("R1020","Link at least two allocated subplots and one coordinate.",span);}
                for(const double value:indices){if(!std::isfinite(value)||std::floor(value)!=value||value<1||value>64)fail("R1020","Linked subplot IDs must be integers 1..64.",span);const int index=static_cast<int>(value);if(std::find(action.linkedPanels.begin(),action.linkedPanels.end(),index)!=action.linkedPanels.end())fail("R1020","Linked subplot IDs must be unique.",span);action.linkedPanels.push_back(index);}
            }else{
                const auto* fields=chaosFields(arguments,1,{"width_cm","height_cm","dpi","overwrite","subplot"},span);const auto path=dataPath(textValue(arguments[0]),span);
                FigureExportOptions settings;settings.widthCm=chaosNumber(fields,"width_cm",0,span);settings.heightCm=chaosNumber(fields,"height_cm",0,span);settings.dpi=static_cast<int>(chaosInteger(fields,"dpi",150,36,1200,span));settings.subplot=static_cast<int>(chaosInteger(fields,"subplot",0,0,64,span));
                if(const auto* value=chaosOption(fields,"overwrite"))settings.overwrite=boolean(*value);
                if((settings.widthCm==0)!=(settings.heightCm==0)||settings.widthCm<0||settings.heightCm<0||settings.widthCm>100||settings.heightCm>100)fail("R1020","Supply both positive width_cm and height_cm (at most 100), or omit both.",span);
                if(!settings.overwrite&&std::filesystem::exists(path))fail("R1020","Export destination exists; set overwrite=1 explicitly.",span);
                if(options_.nativeFigureExport){
                    auto extension=path.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});if(extension!=".png"&&extension!=".svg"&&extension!=".pdf")fail("R1020","Headless native exportgraphics supports PNG, SVG and PDF.",span);
                    plotting::PngOptions output;output.dpi=settings.dpi;output.overwrite=settings.overwrite;output.cancellation=options_.cancellation;
                    if(settings.widthCm>0){const double width=settings.widthCm*settings.dpi/2.54,height=settings.heightCm*settings.dpi/2.54;if(width>16000||height>16000)fail("R1020","Requested export dimensions exceed 16000 pixels per side.",span);output.width=static_cast<int>(std::lround(width));output.height=static_cast<int>(std::lround(height));}
                    plotting::NativeFigureScene scene;scene.ingest(result_.figureActions);if(extension==".png")scene.exportCurrentPng(path,output,0,settings.subplot);else scene.exportCurrentVector(path,output,0,settings.subplot,options_.numericModuleDirectory);return I(0);
                }
                action.kind=FigureActionKind::ExportPlot;action.text=path.string();action.exportOptions=settings;
            }
            if(action.kind==FigureActionKind::ConfigurePlot){(void)patchedPlotStyle({},patch);action.plotStyle=std::move(patch);}
            result_.figureActions.push_back(std::move(action));return I(0);
        }catch(const plotting::ExportError& error){fail(options_.cancellation&&options_.cancellation->load()?"R1006":"R1020",error.what(),span);}
        catch(const std::invalid_argument& error){fail("R1020",error.what(),span);}
        catch(const Failure&){throw;}
        catch(const std::exception& error){fail("R1020",error.what(),span);}
    }
