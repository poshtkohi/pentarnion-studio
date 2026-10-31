#include "mx_value.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace pm {
namespace {
struct Destroy { void operator()(mxArray* value)const noexcept{if(value)mxDestroyArray(value);} };
using Owned=std::unique_ptr<mxArray,Destroy>;

mxArray* empty(){return mxCreateDoubleMatrix(0,0,mxREAL);}
std::string utf8(const mxArray* value){
    if(!value||!mxIsChar(value))return {};
    char* text=mxArrayToUTF8String(value);
    if(!text){if(mxIsEmpty(value))return {};throw std::runtime_error("Cannot read MATLAB character data.");}
    std::string result(text);mxFree(text);return result;
}
mxArray* characters(std::string_view text){
    // MATLAB char storage is UTF-16. Decode UTF-8 without dependence on the
    // process ANSI code page, preserving supplementary characters in strings.
    std::vector<mxChar> units;units.reserve(text.size());
    for(std::size_t at=0;at<text.size();){
        const auto first=static_cast<unsigned char>(text[at++]);unsigned point=0,count=0,minimum=0;
        if(first<0x80){point=first;}
        else if((first&0xe0)==0xc0){point=first&0x1f;count=1;minimum=0x80;}
        else if((first&0xf0)==0xe0){point=first&0x0f;count=2;minimum=0x800;}
        else if((first&0xf8)==0xf0){point=first&0x07;count=3;minimum=0x10000;}
        else throw std::invalid_argument("Invalid UTF-8 in a compiled MATLAB adapter string.");
        if(count>text.size()-at)throw std::invalid_argument("Incomplete UTF-8 in a compiled MATLAB adapter string.");
        for(unsigned i=0;i<count;++i){const auto next=static_cast<unsigned char>(text[at++]);if((next&0xc0)!=0x80)throw std::invalid_argument("Invalid UTF-8 continuation byte.");point=(point<<6)|(next&0x3f);}
        if(point<minimum||point>0x10ffff||(point>=0xd800&&point<=0xdfff))throw std::invalid_argument("Invalid Unicode scalar in a MATLAB adapter string.");
        if(point<0x10000)units.push_back(static_cast<mxChar>(point));
        else {point-=0x10000;units.push_back(static_cast<mxChar>(0xd800+(point>>10)));units.push_back(static_cast<mxChar>(0xdc00+(point&0x3ff)));}
    }
    const mwSize dimensions[2]={1,static_cast<mwSize>(units.size())};mxArray* result=mxCreateCharArray(2,dimensions);
    if(!result)throw std::bad_alloc();
    if(!units.empty())std::memcpy(mxGetChars(result),units.data(),units.size()*sizeof(mxChar));
    return result;
}
std::string exceptionProperty(const mxArray* value,const char* property){
    if(!value)return {};
    if(mxIsStruct(value))return utf8(mxGetField(value,0,property));
    // Unlike mxGetField's borrowed pointer, mxGetProperty returns a new copy.
    Owned field(mxGetProperty(value,0,property));return utf8(field.get());
}
bool dualDataProperty(const V& value,const std::string& field){return mxIsClass(value.raw(),"penta_dual")&&mxGetNumberOfElements(value.raw())==1&&(field=="value"||field=="tangent"||field=="second");}
std::string exceptionMessage(const mxArray* value){auto text=exceptionProperty(value,"message");return text.empty()?std::string("MATLAB call failed."):text;}
V subscript(const char* type,const Args& indices){
    const char* fields[]={"type","subs"};Owned object(mxCreateStructMatrix(1,1,2,fields));if(!object)throw std::bad_alloc();
    Owned cells(mxCreateCellMatrix(1,static_cast<mwSize>(indices.size())));if(!cells)throw std::bad_alloc();
    for(std::size_t i=0;i<indices.size();++i)mxSetCell(cells.get(),static_cast<mwIndex>(i),indices[i].clone());
    mxSetField(object.get(),0,"type",characters(type));mxSetField(object.get(),0,"subs",cells.release());return V::take(object.release());
}
V dotSubscript(const std::string& field){
    const char* fields[]={"type","subs"};Owned object(mxCreateStructMatrix(1,1,2,fields));if(!object)throw std::bad_alloc();
    mxSetField(object.get(),0,"type",characters("."));mxSetField(object.get(),0,"subs",characters(field));return V::take(object.release());
}
} // namespace

V::V(mxArray* value,Adopt):value_(value,Destroy{}){if(!value)throw std::bad_alloc();mexMakeArrayPersistent(value);}
V::V():V(empty(),Adopt{}){}
V::V(double value):V(mxCreateDoubleScalar(value),Adopt{}){}
V::V(int value):V(static_cast<double>(value)){}
V::V(bool value):V(mxCreateLogicalScalar(value),Adopt{}){}
V::V(std::string_view value):V(characters(value),Adopt{}){}
V::V(const std::string& value):V(std::string_view(value)){}
V::V(const char* value):V(std::string_view(value?value:"")){}
V::V(const mxArray* value):V(value?mxDuplicateArray(value):empty(),Adopt{}){}
V V::take(mxArray* value){return V(value,Adopt{});}
mxArray* V::raw()const noexcept{return value_.get();}
mxArray* V::clone()const{auto* result=mxDuplicateArray(raw());if(!result)throw std::bad_alloc();return result;}
bool V::isEmpty()const{return mxIsEmpty(raw());}
std::string V::text()const{if(mxIsChar(raw()))return utf8(raw());return utf8(one("char",{*this}).raw());}
double V::scalar()const{
    if(mxGetNumberOfElements(raw())!=1||mxIsComplex(raw())||!(mxIsNumeric(raw())||mxIsLogical(raw())||mxIsChar(raw())))throw std::invalid_argument("A real MATLAB scalar is required by the compiled adapter.");
    return mxGetScalar(raw());
}
MatlabError::MatlabError(V value):std::runtime_error(exceptionMessage(value.raw())),exception(std::move(value)),identifier(exceptionProperty(exception.raw(),"identifier")){}

Outs call(const std::string& name,const Args& args,int nout){
    if(name.empty()||name.find('\0')!=std::string::npos)throw std::invalid_argument("Invalid MATLAB function name.");
    if(nout<0||args.size()>static_cast<std::size_t>(INT_MAX))throw std::length_error("MATLAB call exceeds its output/argument count interface.");
    if(name=="builtin"&&args.size()>=3&&mxIsClass(args[1].raw(),"penta_dual")&&mxIsStruct(args[2].raw())&&!mxIsEmpty(args[2].raw())){
        const auto operation=args[0].text();const auto* kind=mxGetField(args[2].raw(),0,"type");const auto* subs=mxGetField(args[2].raw(),0,"subs");
        if((operation=="subsref"||operation=="subsasgn")&&kind&&subs&&utf8(kind)=="."){
            const auto field=utf8(subs);
            if(dualDataProperty(args[1],field)){
                const auto count=mxGetNumberOfElements(args[2].raw());
                V rest;if(count>1)rest=index(args[2],{range(V(2),V(1),V(static_cast<double>(count)))});
                if(operation=="subsref"){
                    const auto property=member(args[1],field);
                    if(count>1)return call("subsref",{property,rest},nout);
                    if(nout>1){call("error",{V("MATLAB:maxlhs"),V("Too many output arguments.")},0);}
                    return nout==0?Outs{}:Outs{property};
                }
                if(args.size()!=4)throw std::invalid_argument("Compiled builtin property assignment requires one replacement.");
                const auto replacement=count>1?one("subsasgn",{member(args[1],field),rest,args[3]}):args[3];
                const auto object=setmember(args[1],field,replacement);
                if(nout>1){call("error",{V("MATLAB:maxlhs"),V("Too many output arguments.")},0);}
                return nout==0?Outs{}:Outs{object};
            }
        }
    }
    std::vector<Owned> inputs;std::vector<mxArray*> pointers;inputs.reserve(args.size());pointers.reserve(args.size());
    for(const auto& value:args){inputs.emplace_back(value.clone());pointers.push_back(inputs.back().get());}
    std::vector<mxArray*> results(static_cast<std::size_t>(nout),nullptr);
    struct ResultsGuard {std::vector<mxArray*>& values;~ResultsGuard(){for(auto* item:values)if(item)mxDestroyArray(item);}} guard{results};
    // A trapped exception was caught by C++, not by MATLAB's catch statement.
    // MATLAB rethrow requires that internal caught flag; throw preserves the
    // exception's identifier/message when replaying the compiled catch branch.
    const char* target=name=="rethrow"?"throw":name.c_str();
    mxArray* trapped=mexCallMATLABWithTrap(nout,results.empty()?nullptr:results.data(),static_cast<int>(pointers.size()),pointers.empty()?nullptr:pointers.data(),target);
    if(trapped)throw MatlabError(V::take(trapped));
    Outs outputs;outputs.reserve(results.size());
    for(auto*& result:results){auto* transferred=result;result=nullptr;outputs.push_back(V::take(transferred));}
    return outputs;
}
V one(const std::string& name,const Args& args){return call(name,args,1).front();}
V unary(const std::string& operation,const V& value){
    static const std::unordered_map<std::string,std::string> operations={{"+","uplus"},{"-","uminus"},{"~","not"},{"'","ctranspose"},{".'","transpose"}};
    const auto found=operations.find(operation);return one(found==operations.end()?operation:found->second,{value});
}
V binary(const std::string& operation,const V& left,const V& right){
    static const std::unordered_map<std::string,std::string> operations={{"+","plus"},{"-","minus"},{"*","mtimes"},{".*","times"},{"/","mrdivide"},{"./","rdivide"},{"\\","mldivide"},{".\\","ldivide"},{"^","mpower"},{".^","power"},{"==","eq"},{"~=","ne"},{"<","lt"},{">","gt"},{"<=","le"},{">=","ge"},{"&","and"},{"|","or"}};
    const auto found=operations.find(operation);return one(found==operations.end()?operation:found->second,{left,right});
}
Outs indexMany(const V& value,const Args& indices,bool braces,int nout){return call("subsref",{value,subscript(braces?"{}":"()",indices)},nout);}
V index(const V& value,const Args& indices,bool braces){return indexMany(value,indices,braces,1).front();}
V assign(const V& value,const Args& indices,const V& replacement,bool braces){return one("subsasgn",{value,subscript(braces?"{}":"()",indices),replacement});}
Args expandCell(const V& value,const Args& indices){
    const double count=one("numel",{index(value,indices,false)}).scalar();
    if(!std::isfinite(count)||count<0||count>INT_MAX||std::trunc(count)!=count)throw std::length_error("Cell expansion exceeds the MATLAB output-count interface.");
    return indexMany(value,indices,true,static_cast<int>(count));
}
V member(const V& value,const std::string& field){
    // These are the value class's declared data fields. Its own MATLAB method
    // bodies access them directly rather than re-entering overloaded subsref.
    if(dualDataProperty(value,field)){
        auto* property=mxGetProperty(value.raw(),0,field.c_str());
        if(property)return V::take(property); // mxGetProperty already copied it.
    }
    return one("subsref",{value,dotSubscript(field)});
}
V setmember(const V& value,const std::string& field,const V& replacement){
    if(dualDataProperty(value,field)){
        Owned object(value.clone());mxSetProperty(object.get(),0,field.c_str(),replacement.raw());
        return V::take(object.release()); // mxSetProperty copies replacement.
    }
    return one("subsasgn",{value,dotSubscript(field),replacement});
}
bool truth(const V& value){
    if(one("isempty",{value}).scalar()!=0)return false;
    return one("all",{one("logical",{index(value,{V(":" )})})}).scalar()!=0;
}
V range(const V& start,const V& step,const V& end){return one("colon",{start,step,end});}
V lastIndex(const V& value,int dimension,int dimensions){
    if(dimension<1||dimensions<dimension)throw std::invalid_argument("Invalid MATLAB end indexing dimensions.");
    // mxIsObject does not classify all MCOS value classes in legacy MEX APIs.
    // Dispatch this adapter's class explicitly, then query MATLAB for any other
    // custom end implementation; classes without one use ordinary size folding.
    if(mxIsClass(value.raw(),"penta_dual"))return one("end",{value,V(dimension),V(dimensions)});
    if(one("isobject",{value}).scalar()!=0&&one("ismethod",{value,V("end")}).scalar()!=0)return one("end",{value,V(dimension),V(dimensions)});
    if(dimensions==1)return one("numel",{value});
    if(dimension<dimensions)return one("size",{value,V(dimension)});
    const auto sizes=one("size",{value});const auto count=one("numel",{sizes});
    return one("prod",{index(sizes,{range(V(dimension),V(1),count)})});
}
V matrix(const std::vector<Args>& rows,bool cell){
    if(rows.empty())return cell?V::take(mxCreateCellMatrix(0,0)):V();
    Args pieces;pieces.reserve(rows.size());
    for(const auto& row:rows){
        if(cell){Owned array(mxCreateCellMatrix(1,static_cast<mwSize>(row.size())));if(!array)throw std::bad_alloc();for(std::size_t column=0;column<row.size();++column)mxSetCell(array.get(),static_cast<mwIndex>(column),row[column].clone());pieces.push_back(V::take(array.release()));}
        else pieces.push_back(one("horzcat",row));
    }
    return one("vertcat",pieces);
}
} // namespace pm
