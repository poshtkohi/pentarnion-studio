"""Generate declaration and forwarding-only MATLAB facade; no math bodies."""
import json
from pathlib import Path
HERE=Path(__file__).resolve().parent
data=json.loads((HERE/'transpilation.json').read_text(encoding='utf-8'))
sdk=HERE/'sdk';sdk.mkdir(exist_ok=True)
(sdk/'penta_runtime.m').write_text("""function varargout=penta_runtime(varargin)
% Compiled checked-export runtime interface.
[varargout{1:nargout}]=penta_runtime_native(varargin{:});
end
""",encoding='utf-8')
(sdk/'penta_runtime_bind.m').write_text("""function handle=penta_runtime_bind(context)
% Callback ABI forwarding only; MATLAB owns the context lifetime.
handle=@(varargin)penta_runtime_native('__callback',context,varargin{:});
end
""",encoding='utf-8')
(sdk/'penta_runtime_context.m').write_text("""classdef penta_runtime_context < handle
properties(SetAccess=private)
    token
end
methods
    function context=penta_runtime_context(token)
        context.token=token;
    end
    function delete(context)
        penta_runtime_native('__release',context.token);
    end
end
end
""",encoding='utf-8')
regular=[];static=[]
staticnames={'hessian','chain','align','cat'}
for name in data['methods']:
    if name=='penta_dual':continue
    body=f"    function varargout={name}(varargin)\n        [varargout{{1:nargout}}]=penta_runtime_native('__dual','{name}',varargin{{:}});\n    end\n"
    (static if name in staticnames else regular).append(body)
(sdk/'penta_dual.m').write_text("""classdef penta_dual
% Data layout and compiled operation declarations for checked target AD.
properties
    value
    tangent
    second
end
methods
    function object=penta_dual(varargin)
        state=penta_runtime_native('__dual_construct',varargin{:});
        object.value=state.value;
        object.tangent=state.tangent;
        object.second=state.second;
    end
"""+''.join(regular)+"end\nmethods(Static)\n"+''.join(static)+"end\nend\n",encoding='utf-8')
mapping='\n'.join(f'    if(name=={json.dumps(name)})return {uid};' for name,uid in data['methods'].items())
(HERE/'private/mex_entry.cpp').write_text("""#include "compiled_runtime.hpp"
#include <string>
#include <stdexcept>
namespace {
int method(const std::string& name) {
"""+mapping+"""
    throw std::runtime_error("Unknown compiled AD operation: "+name);
}
pm::Outs dispatch(const pm::Args& all,int requested) {
    if(all.empty())throw std::runtime_error("Compiled target runtime needs an operation");
    const std::string operation=all[0].text();
    if(operation=="__release") {if(all.size()>1)pm::release(all[1]);return {};}
    if(operation=="__callback")return pm::callback(all.at(1),pm::Args(all.begin()+2,all.end()),requested);
    if(operation=="__dual_construct")return pm::run(method("penta_dual"),{},pm::Args(all.begin()+1,all.end()),requested);
    if(operation=="__dual")return pm::run(method(all.at(1).text()),{},pm::Args(all.begin()+2,all.end()),requested);
    return pm::run(0,{},all,requested);
}
}
extern "C" __declspec(dllexport) void mexFunction(int nlhs,mxArray* plhs[],int nrhs,const mxArray* prhs[]) {
    static bool initialized=false;if(!initialized){mexAtExit(pm::cleanup);initialized=true;}
    mxArray* failure=nullptr;std::string message;
    try {
        pm::Args args;for(int i=0;i<nrhs;++i)args.emplace_back(prhs[i]);
        auto outputs=dispatch(args,nlhs);
        for(int i=0;i<nlhs;++i)plhs[i]=pm::output(outputs,i).clone();
    } catch(const pm::MatlabError& error) { failure=error.exception.clone(); }
    catch(const std::exception& error) {message=error.what();}
    if(failure){mexCallMATLAB(0,nullptr,1,&failure,"throw");mexErrMsgIdAndTxt("PentaExport:Native","Target MATLAB exception");}
    if(!message.empty())mexErrMsgIdAndTxt("PentaExport:Native","%s",message.c_str());
}
""",encoding='utf-8')
print('Generated four forwarding-only MATLAB files and native dispatcher')
