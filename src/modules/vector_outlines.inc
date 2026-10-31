// Turn only Qt's own generated SVG text into glyph paths. Stream the XML so
// large scientific curves never require a DOM, and retain accessible labels.
void outlineSvg(const QByteArray& bytes,const penta_vector_host_v1& host,uint64_t maximum){
    Output output(host,maximum);QXmlStreamReader xml(bytes);QXmlStreamWriter writer(&output);
    struct Font {QString family="Arial",style="normal";double pixels=18;int weight=400;};std::vector<Font> stack;uint64_t glyphs=0,elements=0;
    auto attributes=[&](Font font,const QXmlStreamAttributes& values){for(const auto& value:values){const auto key=value.name();if(key==QLatin1String("font-family"))font.family=value.value().toString();else if(key==QLatin1String("font-size")){bool okay=false;font.pixels=value.value().toDouble(&okay);if(!okay||!std::isfinite(font.pixels)||font.pixels<=0||font.pixels>1024)throw std::runtime_error("Invalid generated SVG font size.");}else if(key==QLatin1String("font-style"))font.style=value.value().toString();else if(key==QLatin1String("font-weight")){bool okay=false;font.weight=value.value().toInt(&okay);if(!okay)font.weight=400;}}return font;};
    while(!xml.atEnd()){
        if(host.cancelled&&host.cancelled(host.context))throw std::runtime_error("Native SVG outlining cancelled.");
        xml.readNext();
        if(xml.isStartElement()){
            if(stack.size()>128)throw std::runtime_error("Generated SVG nesting exceeds its allowance.");
            const auto font=attributes(stack.empty()?Font{}:stack.back(),xml.attributes());
            if(xml.name()==QLatin1String("text")){
                std::vector<std::pair<QString,QString>> saved;for(const auto& attribute:xml.attributes())saved.emplace_back(attribute.qualifiedName().toString(),attribute.value().toString());bool xOkay=false,yOkay=false;const auto x=xml.attributes().value("x").toDouble(&xOkay),y=xml.attributes().value("y").toDouble(&yOkay);if(!xOkay||!yOkay)throw std::runtime_error("Unsupported generated SVG text coordinates.");const auto text=xml.readElementText();if(xml.hasError())throw std::runtime_error("Unexpected generated SVG text markup.");
                QFont selected(font.family);QImage measure(1,1,QImage::Format_ARGB32_Premultiplied);selected.setPointSizeF(font.pixels*72.0/measure.logicalDpiY());selected.setWeight(static_cast<QFont::Weight>(std::clamp(font.weight,1,1000)));selected.setItalic(font.style=="italic"||font.style=="oblique");QTextLayout layout(text,selected,&measure);QTextOption option;option.setAlignment(Qt::AlignLeft);option.setWrapMode(QTextOption::NoWrap);layout.setTextOption(option);layout.beginLayout();auto line=layout.createLine();if(line.isValid())line.setLineWidth(1000000);layout.endLayout();QPainterPath combined;combined.setFillRule(Qt::WindingFill);
                if(line.isValid())for(const auto& run:layout.glyphRuns()){const auto indexes=run.glyphIndexes();const auto positions=run.positions();if(indexes.size()!=positions.size()||static_cast<uint64_t>(indexes.size())>200000-glyphs)throw std::runtime_error("SVG glyph budget exceeded.");glyphs+=indexes.size();const auto raw=run.rawFont();for(qsizetype i=0;i<indexes.size();++i){auto path=raw.pathForGlyph(indexes[i]);if(static_cast<uint64_t>(path.elementCount())>4000000-elements)throw std::runtime_error("SVG glyph outline complexity exceeded.");elements+=path.elementCount();path.translate(x+positions[i].x(),y+positions[i].y()-line.ascent());combined.addPath(path);}}
                QString data;auto number=[](double value){return QString::number(value,'g',12);};for(int i=0;i<combined.elementCount();++i){const auto item=combined.elementAt(i);if(item.isMoveTo())data+='M'+number(item.x)+' '+number(item.y);else if(item.isLineTo())data+='L'+number(item.x)+' '+number(item.y);else if(item.type==QPainterPath::CurveToElement){const auto control=combined.elementAt(++i),end=combined.elementAt(++i);data+='C'+number(item.x)+' '+number(item.y)+' '+number(control.x)+' '+number(control.y)+' '+number(end.x)+' '+number(end.y);}}
                writer.writeStartElement("http://www.w3.org/2000/svg","path");for(const auto& [key,value]:saved)if(key!="x"&&key!="y"&&!key.startsWith("font-")&&key!="text-anchor")writer.writeAttribute(key,value);writer.writeAttribute("d",data);writer.writeAttribute("fill-rule","nonzero");writer.writeTextElement("http://www.w3.org/2000/svg","title",text);writer.writeEndElement();
            }else{stack.push_back(font);writer.writeCurrentToken(xml);}
        }else if(xml.isEndElement()){writer.writeCurrentToken(xml);if(!stack.empty())stack.pop_back();}
        else writer.writeCurrentToken(xml);
        if(output.failed)throw std::runtime_error("SVG outline output quota or host failure.");
    }
    if(xml.hasError()||writer.hasError()||output.failed)throw std::runtime_error("Cannot finalize outlined scientific SVG.");
}
