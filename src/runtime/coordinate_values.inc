    void appendNativeCoordinates(const Value& original,const language::OdeVariable& variable,std::vector<double>& output,SourceSpan span) {
        const auto value=convert(original,variable.type,span,variable.schema);
        if(const auto* item=std::get_if<UniversalElement>(&value))output.insert(output.end(),item->coefficients().begin(),item->coefficients().end());
        else if(const auto* item=std::get_if<std::complex<double>>(&value)){output.push_back(item->real());output.push_back(item->imag());}
        else if(const auto* item=std::get_if<P>(&value))output.insert(output.end(),item->coefficients().begin(),item->coefficients().end());
        else if(const auto* tensor=std::get_if<TensorValue>(&value))output.insert(output.end(),(*tensor)->values.begin(),(*tensor)->values.end());
        else if(const auto* matrix=std::get_if<UniversalMatrix>(&value))output.insert(output.end(),(*matrix)->values.begin(),(*matrix)->values.end());
        else if(const auto* matrix=std::get_if<MatrixValue>(&value)){const auto components=(*matrix)->pentarnion?5u:1u;for(const auto& entry:(*matrix)->values)output.insert(output.end(),entry.coefficients().begin(),entry.coefficients().begin()+components);}
        else output.push_back(real(value,span));
    }
    Value nativeCoordinateValue(const std::vector<double>& coordinates,language::Type type,const std::shared_ptr<const language::ValueSchema>& schema,SourceSpan span) {
        const auto width=language::coordinateWidth(type,schema);
        if(!width||width!=coordinates.size())fail("R2530","Native coordinate result differs from its immutable checked type.",span);
        for(const auto value:coordinates)finite(value,span);
        if(type==Type::Tensor)return tensorValue(schema->tensorEntryType,schema->algebra,context_.gamma(),schema->shape,coordinates,span);
        if(type==Type::Element)return UniversalElement(schema->algebra,coordinates);
        if(type==Type::Complex)return std::complex<double>(coordinates[0],coordinates[1]);
        if(type==Type::ElementMatrix){auto result=newUniversalMatrix(schema->algebra,schema->shape[0],schema->shape[1],span);result->values=coordinates;result->fixedShape=schema->shape;return UniversalMatrix(result);}
        if(type==Type::RealMatrix||type==Type::PentarnionMatrix){const auto value=numericResult(coordinates,schema->shape[0],schema->shape[1],type==Type::PentarnionMatrix?5:1,span);auto result=std::make_shared<Matrix>(*std::get<MatrixValue>(value));result->fixedShape=schema->shape;return MatrixValue(result);}
        return numericResult(coordinates,1,1,width,span,true);
    }
    Value nativeCoordinateValue(const std::vector<double>& coordinates,const language::OdeVariable& variable,SourceSpan span) {
        return nativeCoordinateValue(coordinates,variable.type,variable.schema,span);
    }
    void requireModelCallbackSignature(const language::FunctionDefinition& callback,const language::OdeDefinition& model,SourceSpan span) {
        std::vector<language::OdeVariable> expected{{model.timeName,Type::Real,span}};
        expected.insert(expected.end(),model.states.begin(),model.states.end());expected.insert(expected.end(),model.parameters.begin(),model.parameters.end());
        if(callback.parameters.size()!=expected.size())fail("O1010","Native model callback signature requires time, all states and all parameters.",span);
        for(std::size_t n=0;n<expected.size();++n){
            const auto& actual=callback.parameters[n];const auto schema=n<callback.parameterSchemas.size()?callback.parameterSchemas[n]:actual.schema;
            if(actual.type==Type::Tensor&&(!schema||!expected[n].schema||schema->tensorEntryType!=expected[n].schema->tensorEntryType||schema->shape!=expected[n].schema->shape||bool(schema->algebra)!=bool(expected[n].schema->algebra)||(schema->algebra&&schema->algebra->hash()!=expected[n].schema->algebra->hash())))fail("O1010","Native model tensor callback differs in rank, shape or immutable entry algebra.",span);
            if(actual.type!=expected[n].type)fail("O1010","Native model callback parameter types/order differ from the ODE.",span);
            if((actual.type==Type::Element||actual.type==Type::ElementMatrix)&&(!schema||!schema->algebra||!expected[n].schema||!expected[n].schema->algebra||schema->algebra->hash()!=expected[n].schema->algebra->hash()))fail("O1010","Native model callback parameter uses a different algebra definition.",span);
            if((actual.type==Type::RealMatrix||actual.type==Type::PentarnionMatrix||actual.type==Type::ElementMatrix)&&(!schema||!expected[n].schema||schema->shape!=expected[n].schema->shape))fail("O1010","Native model callback array shape differs from the ODE declaration.",span);
        }
    }
