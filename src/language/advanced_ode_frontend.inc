    bool checkAdvancedOdeOption(const std::string& key,const ExprPtr& argument,Type actual){
        const bool realOption=key=="initial_step"||key=="max_step"||key=="min_step";
        const bool integerOption=key=="max_order"||key=="dense_output"||key=="max_dense_coordinates"||key=="sensitivity_error_control";
        const bool vectorOption=key=="event_direction"||key=="event_terminal"||key=="sensitivity_parameters"||key=="output_times";
        const bool matrixOption=key=="mass"||key=="sensitivity_initial";
        const bool stringOption=key=="sensitivities"||key=="sensitivity_method";
        if(!realOption&&!integerOption&&!vectorOption&&!matrixOption&&!stringOption&&key!="events"&&key!="units"&&key!="live_plot")return false;
        bool valid=actual==Type::Invalid;
        if(realOption)valid=valid||scalarType(actual);else if(integerOption)valid=valid||actual==Type::Integer;
        else if(vectorOption)valid=valid||scalarType(actual)||actual==Type::RealMatrix||rangeType(actual)||actual==Type::LogicalMatrix;
        else if(matrixOption)valid=valid||actual==Type::RealMatrix;
        else if(stringOption)valid=valid||actual==Type::String;
        else if(key=="live_plot")valid=valid||actual==Type::Record||actual==Type::Integer;
        else if(key=="units")valid=valid||actual==Type::Record;
        else valid=valid||actual==Type::Function;
        if(!valid)add("E1812","Advanced ODE option has an incompatible type: "+key,argument->span);
        if(stringOption&&argument->kind==ExprKind::StringLiteral){if(key=="sensitivities"&&argument->text!="none"&&argument->text!="parameters"&&argument->text!="initial"&&argument->text!="all")add("E1812","sensitivities must be none, parameters, initial or all.",argument->span);if(key=="sensitivity_method"&&argument->text!="simultaneous"&&argument->text!="staggered")add("E1812","sensitivity_method must be simultaneous or staggered.",argument->span);}
        return true;
    }
    std::optional<Type> checkAdvancedOdeCall(const ExprPtr& value){
        const auto& name=value->text;const auto count=value->children.size();if(name!="odeevents"&&name!="odesens"&&name!="odestats"&&name!="odewindow"&&name!="odeadjoint")return {};
        bool valid=true;auto require=[&](bool okay,const char* text){if(!okay){add("E1812",text,value->span);valid=false;}};
        require(m_options.languageVersion=="0.2","Advanced native ODE features require language0.2.");require(!hasNamedArguments(value),"Use positional ODE query arguments and a final options record.");
        if(name=="odeadjoint"){
            require(count==3,"Use odeadjoint(model,initial,options(parameters=...,objective=callback,t0=...,t1=...)).");if(count!=3)return Type::Invalid;require(value->children[0]->type==Type::OdeModel,"odeadjoint requires a typed ODE model.");require(value->children[2]->type==Type::Record,"odeadjoint requires a typed options record.");value->ode=value->children[0]->ode;auto schema=std::make_shared<ValueSchema>();schema->fields={{"objective",Type::Real,{}},{"running_cost",Type::Real,{}},{"reverse_table",Type::String,{}},{"parameter_gradient",Type::RealMatrix,{}},{"initial_gradient",Type::RealMatrix,{}},{"backward_steps",Type::Integer,{}},{"checkpoints",Type::Integer,{}},{"solution",Type::Solution,{}},{"cache_hit",Type::Integer,{}},{"cache_key",Type::String,{}}};value->schema=std::move(schema);return valid?Type::Record:Type::Invalid;
        }
        require(count>0&&value->children[0]->type==Type::Solution,"Advanced ODE queries require a solution.");if(count==0)return Type::Invalid;
        if(name=="odesens"){require(count==1||count==2,"Use odesens(solution[,direction_index]).");if(count==2)require(value->children[1]->type==Type::Integer,"Sensitivity direction must be an Integer.");return valid?Type::RealMatrix:Type::Invalid;}
        if(name=="odewindow"){require(count==2||count==3,"Use odewindow(solution,start_time[,stop_time]).");for(std::size_t i=1;i<count;++i)require(scalarType(value->children[i]->type),"Window times must be real scalars.");value->ode=value->children[0]->ode;return valid?Type::Solution:Type::Invalid;}
        require(count==1,"Use odeevents(solution) or odestats(solution).");auto schema=std::make_shared<ValueSchema>();
        if(name=="odeevents")schema->fields={{"t",Type::RealMatrix,{}},{"y",Type::RealMatrix,{}},{"index",Type::RealMatrix,{}},{"direction",Type::RealMatrix,{}},{"terminal",Type::Integer,{}}};
        else schema->fields={{"method",Type::String,{}},{"jacobian",Type::String,{}},{"steps",Type::Integer,{}},{"rhs_evaluations",Type::Integer,{}},{"jacobian_evaluations",Type::Integer,{}},{"event_evaluations",Type::Integer,{}},{"sensitivity_rhs_evaluations",Type::Integer,{}},{"error_test_failures",Type::Integer,{}},{"nonlinear_iterations",Type::Integer,{}},{"last_step",Type::Real,{}},{"last_order",Type::Integer,{}},{"elapsed_seconds",Type::Real,{}},{"cache_hit",Type::Integer,{}},{"cache_key",Type::String,{}},{"terminal_event",Type::Integer,{}},{"dense_segments",Type::Integer,{}},{"sensitivity_count",Type::Integer,{}},{"sensitivity_parameters",Type::RealMatrix,{}}};
        value->schema=std::move(schema);return valid?Type::Record:Type::Invalid;
    }
