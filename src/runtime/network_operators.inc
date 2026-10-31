    NumericArray networkArray(const Value& value,SourceSpan span){
        if(const auto* matrix=std::get_if<UniversalMatrix>(&value))return {(*matrix)->rows,(*matrix)->columns,(*matrix)->algebra->dimension(),false,false,(*matrix)->values};
        if(const auto* entry=std::get_if<UniversalElement>(&value))return {1,1,entry->definition()->dimension(),false,true,entry->coefficients()};
        return numericArray(value,span);
    }
    void networkOperators(const language::OdeDefinition& model,std::size_t edges,const ChaosFields* fields,networks::Options& settings,SourceSpan span){
        const auto* left=chaosOption(fields,"edge_left");const auto* right=chaosOption(fields,"edge_right");
        if(!left&&!right){if(chaosOption(fields,"edge_grouping"))fail("R2153","edge_grouping requires both edge_left and edge_right.",span);return;}
        if(!settings.generalCoupling)fail("R2153","Algebra edge actions require coupling=general; dissipativity is not implied.",span);
        const auto grouping=chaosText(fields,"edge_grouping","",span);
        if(left&&right&&grouping!="(left*delta)*right"&&grouping!="left*(delta*right)")fail("R2153","Two-sided edge actions require explicit edge_grouping: (left*delta)*right or left*(delta*right).",span);
        if((!left||!right)&&!grouping.empty())fail("R2153","edge_grouping is only used for two-sided edge actions.",span);
        bool legacy=false;UniversalDefinition definition;std::size_t coordinates=0,dimension=0;
        for(const auto& state:model.states){const bool pentarnion=state.type==Type::Pentarnion||state.type==Type::PentarnionMatrix;const bool generic=state.type==Type::Element||state.type==Type::ElementMatrix;const bool complex=state.type==Type::Complex;
            if(!pentarnion&&!generic&&!complex)fail("R2153","Ordered edge actions require homogeneous algebra-valued local states.",span);
            const auto current=complex?algebra::universal::builtin("complex"):generic&&state.schema?state.schema->algebra:UniversalDefinition{};
            if(coordinates&&(legacy!=pentarnion||(!pentarnion&&(!current||current->hash()!=definition->hash()))))fail("R2153","Every coupled state must use the same algebra definition.",span);
            legacy=pentarnion;if(!legacy){if(!current)fail("R2153","Missing local algebra definition.",span);definition=current;}dimension=legacy?5:definition->dimension();coordinates+=language::coordinateWidth(state.type,state.schema);
        }
        if(!coordinates||coordinates>32||!dimension||coordinates%dimension||edges>8000000/coordinates/coordinates)fail("R2153","Ordered edge blocks exceed the bounded network coordinate allowance.",span);
        auto entries=[&](const Value* source){std::vector<Value> values;if(!source)return values;
            if(const auto* tuple=std::get_if<TupleValue>(source))values=(*tuple)->values;
            else if(const auto* matrix=std::get_if<MatrixValue>(source)){for(const auto& value:(*matrix)->values)values.emplace_back((*matrix)->pentarnion?Value(value):Value(value.coefficients()[0]));}
            else if(const auto* matrix=std::get_if<UniversalMatrix>(source)){const auto& m=**matrix;if(legacy||m.algebra->hash()!=definition->hash())fail("R2153","Edge coefficients must use the local immutable algebra definition.",span);for(std::size_t n=0;n<m.rows*m.columns;++n)values.emplace_back(UniversalElement(m.algebra,{m.values.begin()+n*dimension,m.values.begin()+(n+1)*dimension}));}
            else values.push_back(*source);
            if(values.size()!=1&&values.size()!=edges)fail("R2153","Supply one shared edge element or one element per physical graph edge.",span);
            return values;
        };
        const auto leftValues=entries(left),rightValues=entries(right);
        auto multiplication=[&](const Value& value,bool onLeft){std::vector<double> matrix;
            if(legacy){const auto p=element(value,span);const auto block=onLeft?algebra::leftMatrix(p):algebra::rightMatrix(p);for(const auto& row:block)matrix.insert(matrix.end(),row.begin(),row.end());}
            else {UniversalElement p=UniversalElement::scalar(definition,0);if(const auto* supplied=std::get_if<UniversalElement>(&value)){if(supplied->definition()->hash()!=definition->hash())fail("R2153","Edge element and local state algebra identities differ.",span);p=*supplied;}else if(const auto* supplied=std::get_if<std::complex<double>>(&value)){if(definition->hash()!=algebra::universal::builtin("complex")->hash())fail("R2153","A complex edge requires a complex local state.",span);p=UniversalElement(definition,{supplied->real(),supplied->imag()});}else p=UniversalElement::scalar(definition,real(value,span));matrix=onLeft?algebra::universal::leftMatrix(p):algebra::universal::rightMatrix(p);}
            return matrix;
        };
        settings.edgeOperators.assign(edges*coordinates*coordinates,0);
        for(std::size_t edge=0;edge<edges;++edge){tick(span);std::vector<double> block;
            if(left)block=multiplication(leftValues[leftValues.size()==1?0:edge],true);
            if(right){const auto R=multiplication(rightValues[rightValues.size()==1?0:edge],false);if(!left)block=R;else{const auto L=block;std::fill(block.begin(),block.end(),0);for(std::size_t row=0;row<dimension;++row)for(std::size_t col=0;col<dimension;++col){long double sum=0;for(std::size_t k=0;k<dimension;++k)sum+=grouping=="(left*delta)*right"?static_cast<long double>(R[row*dimension+k])*L[k*dimension+col]:static_cast<long double>(L[row*dimension+k])*R[k*dimension+col];block[row*dimension+col]=finite(static_cast<double>(sum),span);}}}
            for(std::size_t base=0;base<coordinates;base+=dimension)for(std::size_t row=0;row<dimension;++row)for(std::size_t col=0;col<dimension;++col)settings.edgeOperators[(edge*coordinates+base+row)*coordinates+base+col]=block[row*dimension+col];
        }
        chargeNumericWork(std::max<std::size_t>(1,edges*coordinates*coordinates),span);
    }
