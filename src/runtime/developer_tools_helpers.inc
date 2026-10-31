    Value moduleInformation(const ModuleInfo& item,SourceSpan span){const auto path=item.path.u8string();return makeRecord({{"name",item.name},{"path",std::string(path.begin(),path.end())},{"available",I(item.available)},{"loaded",I(item.loaded)},{"abi",I(item.abi)},{"status",item.status}},span);}
    std::optional<Value> developerToolsCall(const Expr& expression,const std::vector<Value>& arguments){
        const auto& name=expression.text;if(name!="module_info"&&name!="module_load")return {};
        try{
            if(options_.cancellation&&options_.cancellation->load())fail("R1006","Module operation cancelled.",expression.span);
            if(name=="module_load"){if(arguments.size()!=1)fail("R2100","Use module_load(name).",expression.span);return moduleInformation(loadModule(std::get<std::string>(arguments[0]),options_.numericModuleDirectory),expression.span);}
            const auto modules=moduleInventory(options_.numericModuleDirectory);chargeNumericWork(modules.size(),expression.span);
            if(arguments.size()==1){const auto& selected=std::get<std::string>(arguments[0]);for(const auto& module:modules)if(module.name==selected)return moduleInformation(module,expression.span);fail("R2100","Unknown catalogue module: "+selected,expression.span);}
            std::map<std::string,Value> fields;for(const auto& module:modules)fields.emplace(module.name,moduleInformation(module,expression.span));return makeRecord(std::move(fields),expression.span);
        }catch(const Failure&){throw;}catch(const std::exception& error){fail("R2100",error.what(),expression.span);}
    }
