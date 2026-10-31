std::shared_ptr<const language::ValueSchema> runtimeSchema(const Value& value,std::size_t depth=0) {
    if(depth>32)return {};
    auto schema=std::make_shared<language::ValueSchema>();
    if(const auto* definition=std::get_if<UniversalDefinition>(&value)){schema->algebra=*definition;return schema;}
    if(const auto* element=std::get_if<UniversalElement>(&value)){schema->algebra=element->definition();return schema;}
    if(const auto* tensor=std::get_if<TensorValue>(&value)){schema->tensorEntryType=(*tensor)->entryType;schema->algebra=(*tensor)->algebra;schema->shape=(*tensor)->shape;return schema;}
    if(const auto* matrix=std::get_if<UniversalMatrix>(&value)){schema->algebra=(*matrix)->algebra;schema->shape=(*matrix)->fixedShape;return schema;}
    if(const auto* matrix=std::get_if<MatrixValue>(&value)){if((*matrix)->fixedShape.empty())return {};schema->shape=(*matrix)->fixedShape;return schema;}
    if(const auto* series=std::get_if<ArraySeries>(&value)){if(series->schema)*schema=*series->schema;schema->seriesElementType=series->valueType;return schema;}
    if(const auto* series=std::get_if<UniversalSeries>(&value)){schema->algebra=series->algebra;schema->seriesElementType=series->complex?Type::Complex:Type::Element;return schema;}
    if(const auto* function=std::get_if<FunctionValue>(&value)){
        if(function->definition){auto prototype=std::make_shared<language::FunctionDefinition>(*function->definition);prototype->body.clear();prototype->captureNames.clear();schema->function=prototype;}return schema;
    }
    if(const auto* record=std::get_if<RecordValue>(&value)){for(const auto& [name,item]:(*record)->fields)schema->fields.push_back({name,valueType(item),runtimeSchema(item,depth+1)});return schema;}
    if(const auto* tuple=std::get_if<TupleValue>(&value)){for(const auto& item:(*tuple)->values)schema->elements.push_back({{},valueType(item),runtimeSchema(item,depth+1)});return schema;}
    return {};
}
