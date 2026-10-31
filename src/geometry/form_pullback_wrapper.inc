namespace {
const penta_form_pullback_api_v1& pullbackCapability(const std::filesystem::path& path){std::lock_guard lock(moduleMutex);const auto handle=modules.at(path).handle;
#ifdef _WIN32
    const auto query=reinterpret_cast<penta_form_pullback_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle),"penta_form_pullback_query_v1")));
#else
    const auto query=reinterpret_cast<penta_form_pullback_query_function_v1>(dlsym(handle,"penta_form_pullback_query_v1"));
#endif
    const auto* result=query?query(PENTA_FORM_PULLBACK_ABI_V1):nullptr;if(!result||result->struct_size<sizeof(*result)||result->abi_version!=PENTA_FORM_PULLBACK_ABI_V1||!result->compute)throw Error(5,"Loaded geometry module lacks native form-pullback jets.");return *result;
}
}
FormResult ExtraModule::pullback(penta_form_pullback_request_v1 request,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge)const{
    const auto n=request.domain_dimension;if(n<1||n>6||request.source.dimension<n||request.source.dimension>6||request.source.degree>request.source.dimension)throw Error(1,"Pullback dimensions exceed the immersed-chart bound.");(void)api();const auto& capability=pullbackCapability(path_);FormResult result;auto& form=result.form;const auto count=combinations(n,request.source.degree);form.dimension=n;form.degree=request.source.degree;form.order=2;form.constant=false;form.orientation=request.source.orientation;form.source="native_analytic_pullback_jets";form.coefficients.resize(count);form.first.resize(count*n);form.second.resize(count*n*n);form.metric.resize(n*n);form.metricFirst.resize(n*n*n);form.metricSecond.resize(n*n*n*n);penta_form_pullback_result_v1 output{};output.struct_size=sizeof(output);output.form.struct_size=sizeof(output.form);output.form.capacity=count;output.form.coefficients=form.coefficients.data();output.form.first=form.first.data();output.form.second=form.second.data();output.metric=form.metric.data();output.metric_first=form.metricFirst.data();output.metric_second=form.metricSecond.data();Host context{cancellation,charge,{},{},{}};const auto host=context.descriptor();request.struct_size=sizeof(request);char error[1024]{};const auto status=capability.compute(&host,&request,&output,error,sizeof(error));check(status,error,context);if(output.form.count!=count)throw Error(5,"Invalid native pullback coefficient count.");form.constant=output.form.constant_coefficients!=0;result.work=output.form.work_units;return result;
}
