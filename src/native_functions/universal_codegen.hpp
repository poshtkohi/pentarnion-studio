#pragma once
#include "universal_native.hpp"
#include "universal_support.hpp"
#ifdef PENTA_DISTRIBUTION_BUILD
#include "distribution_support.hpp"
#endif
#include "native_array_support.hpp"
#include "native_tensor_support.hpp"
#include <numeric>
#include "callback_support.hpp"
#include "third_support.hpp"
#include "ode/native_internal.hpp"
#include "ode/native_build_config.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>

namespace penta::native_functions::detail {
// Descriptor-aware mode of the existing typed native backend. Both callback and
// ODE entries use this expression emitter; there is no additional source parser.
class UniversalGenerator {
    using Type=language::Type;using Expr=language::Expr;using ExprPtr=language::ExprPtr;
    using Statement=language::Statement;using Schema=std::shared_ptr<const language::ValueSchema>;
    struct Binding {Type type;Schema schema;std::string code;};
    const language::FunctionDefinition& function_;
    std::map<std::string,Binding> bindings_;
    std::map<std::string,AlgebraDefinitionPtr> definitions_;
    std::map<std::string,unsigned> tags_;
    std::size_t serial_=0,nodes_=0,work_=0,localCoordinates_=0,tensorExpressionCoordinates_=0;
    bool third_=false,ode_=false,tensor_=false;
    std::vector<Type> outputs_;std::vector<Schema> outputSchemas_;
    [[noreturn]]void invalid(const std::string& message,language::SourceSpan span={})const{throw ode::OdeError("N2401",message,span.length?span:function_.span);}
    static bool scalar(Type type){return type==Type::Real||type==Type::Integer;}
    static bool generic(Type type){return type==Type::Element||type==Type::Complex;}
    static bool array(Type type){return type==Type::RealMatrix||type==Type::PentarnionMatrix||type==Type::ElementMatrix;}
    static Type cellType(Type type){return type==Type::ElementMatrix?Type::Element:type==Type::PentarnionMatrix?Type::Pentarnion:Type::Real;}
#include "tensor_codegen.inc"
    Schema arraySchema(const Expr& expression)const{
        if(expression.schema&&!expression.schema->shape.empty())return expression.schema;
        if(expression.kind==language::ExprKind::Group&&expression.children.size()==1)return arraySchema(*expression.children[0]);
        if(expression.kind==language::ExprKind::Reference){const auto found=bindings_.find(expression.text);if(found!=bindings_.end()&&found->second.schema&&!found->second.schema->shape.empty())return found->second.schema;}
        if((expression.kind==language::ExprKind::Binary||expression.kind==language::ExprKind::Call||expression.kind==language::ExprKind::Transpose)&&!expression.children.empty()){
            Schema source;std::vector<std::size_t> dimensions;
            for(const auto& child:expression.children)if(array(child->type)){source=arraySchema(*child);dimensions=source->shape;break;}
            if(source){if(expression.kind==language::ExprKind::Binary&&expression.text=="*"&&expression.children.size()==2&&array(expression.children[0]->type)&&array(expression.children[1]->type)){const auto right=arraySchema(*expression.children[1]);dimensions[1]=right->shape[1];}
                if(expression.kind==language::ExprKind::Transpose||(expression.kind==language::ExprKind::Call&&expression.text=="transpose"))std::swap(dimensions[0],dimensions[1]);
                if(expression.kind==language::ExprKind::Call&&(expression.text=="sum"||expression.text=="mean")){std::size_t axis=dimensions[0]==1?2:1;if(expression.children.size()==2){const auto& argument=expression.children[1];if(argument->kind!=language::ExprKind::IntegerLiteral)invalid("A native reduction dimension must be an integer literal.",argument->span);unsigned long long parsed=0;const auto result=std::from_chars(argument->text.data(),argument->text.data()+argument->text.size(),parsed);if(result.ec!=std::errc{}||!parsed)invalid("A native reduction dimension must be a positive integer.",argument->span);axis=static_cast<std::size_t>(parsed);}if(axis<=2)dimensions[axis-1]=1;}
                auto schema=std::make_shared<language::ValueSchema>(*source);schema->shape=std::move(dimensions);return schema;
            }
        }
        if(expression.kind!=language::ExprKind::MatrixLiteral||!expression.rows||!expression.columns)invalid("A native array requires an explicit fixed shape or a plain scalar matrix literal.",expression.span);
        if(expression.children.size()!=expression.rows*expression.columns||(!expression.rowWidths.empty()&&std::any_of(expression.rowWidths.begin(),expression.rowWidths.end(),[&](auto width){return width!=expression.columns;})))invalid("Native matrix literals cannot expand dynamic blocks or ranges.",expression.span);
        auto schema=expression.schema?std::make_shared<language::ValueSchema>(*expression.schema):std::make_shared<language::ValueSchema>();schema->shape={expression.rows,expression.columns};return schema;
    }
    std::pair<std::size_t,std::size_t> shape(const Schema& schema)const{
        if(!schema||schema->shape.size()!=2||!schema->shape[0]||!schema->shape[1]||schema->shape[0]>128/schema->shape[1])invalid("Native matrices require a positive fixed shape with at most128 cells.");
        return {schema->shape[0],schema->shape[1]};
    }
    int arrayTag(Type type,const Schema& schema)const{return type==Type::ElementMatrix?static_cast<int>(tag(Type::Element,schema)):type==Type::PentarnionMatrix?-2:-1;}
    static bool canonicalReciprocal(const AlgebraDefinitionPtr& definition){const auto& kind=definition->builtinKind();return kind=="real"||kind=="complex"||kind=="quaternion"||kind=="octonion"||kind=="sedenion";}
    static bool exactProperty(const algebra::universal::Property& property){return property.value&&property.evidence==algebra::universal::Evidence::ProvedExact;}
    void requireInverse(const AlgebraDefinitionPtr& definition,language::SourceSpan span)const{
        if(definition->unit().empty())invalid("A native inverse requires a declared algebra unit.",span);
        if(!canonicalReciprocal(definition)&&!exactProperty(definition->properties().associative))invalid("Native inverse derivatives require a certified associative algebra or a defined canonical reciprocal.",span);
    }
    void requireWhole(const AlgebraDefinitionPtr& definition,language::SourceSpan span)const{
        const auto& kind=definition->builtinKind();const bool quadratic=canonicalReciprocal(definition)||kind=="dual"||kind=="split_complex"||kind=="split_quaternion";
        if(definition->unit().empty()||(!quadratic&&!exactProperty(definition->properties().associative)&&!exactProperty(definition->properties().powerAssociative)))invalid("No native one-element calculus is certified for this algebra.",span);
    }
    static std::string number(double value){if(!std::isfinite(value))throw ode::OdeError("N2401","Nonfinite algebra table coefficient.");std::ostringstream out;out.imbue(std::locale::classic());out<<std::hexfloat<<value;return out.str()+"L";}
    void charge(std::size_t amount){if(amount>5000000||work_>5000000-amount)invalid("Generated algebra callback exceeds its bounded scalar-operation estimate.");work_+=amount;}
    void collect(Type type,const Schema& schema){if(type==Type::Tensor){tensor_=true;(void)tensorShape(schema);collect(tensorEntry(schema),schema);return;}if(generic(type)||type==Type::ElementMatrix){const auto definition=elementDefinition(type,schema);if(definition->dimension()<1||definition->dimension()>64)invalid("Native algebra dimension must be1..64.");definitions_.emplace(definition->hash(),definition);}}
    void collect(const ExprPtr& e){if(!e)return;collect(e->type,e->schema);for(const auto& child:e->children)collect(child);}
    void collect(const std::vector<Statement>& statements){for(const auto& statement:statements){if(statement.kind==language::StatementKind::Declaration)collect(statement.declaredType,statement.declaredSchema?statement.declaredSchema:statement.expression?statement.expression->schema:nullptr);collect(statement.expression);collect(statement.body);collect(statement.alternative);}}
    unsigned tag(Type type,const Schema& schema)const{const auto definition=elementDefinition(type,schema);const auto found=tags_.find(definition->hash());if(found==tags_.end())invalid("Missing native algebra descriptor.");return found->second;}
    std::string typeName(Type type,const Schema& schema)const{if(type==Type::Tensor)return "TypedTensor<"+tensorTemplate(schema)+">";if(scalar(type))return "S";if(type==Type::Pentarnion)return "Pent<S>";if(generic(type))return "Universal<S,"+std::to_string(tag(type,schema))+">";if(array(type)){const auto [rows,columns]=shape(schema);return "FixedArray<S,"+std::to_string(rows)+","+std::to_string(columns)+","+std::to_string(arrayTag(type,schema))+">";}invalid("Native locals/results must have checked numeric scalar or fixed array types.");}
    std::string promoted(Type target,const Schema& schema,const Expr& source,const std::string& code)const{
        if(target==Type::Tensor){if(source.type!=Type::Tensor||!sameNativeTensor(schema,source.schema))invalid("Native tensor conversion preserves rank, shape and complete entry identity.",source.span);return code;}
        if(array(target)){
            if(!array(source.type))invalid("Native array conversion requires an array value with the declared shape.",source.span);
            const auto sourceSchema=arraySchema(source);if(shape(schema)!=shape(sourceSchema))invalid("Native array conversion cannot change its fixed shape.",source.span);
            const auto targetTag=arrayTag(target,schema),sourceTag=arrayTag(source.type,sourceSchema);
            if(targetTag!=sourceTag&&sourceTag!=-1)invalid("Native array conversion cannot change its entry algebra.",source.span);
            if(target==Type::ElementMatrix&&source.type==Type::ElementMatrix&&elementDefinition(target,schema)->hash()!=elementDefinition(source.type,sourceSchema)->hash())invalid("Native array operands have different immutable algebra definitions.",source.span);
            return targetTag==sourceTag?code:"array_cast<"+std::to_string(targetTag)+">("+code+")";
        }
        if(target==Type::Pentarnion){if(source.type==Type::Pentarnion)return code;if(scalar(source.type))return "lift<S>("+code+")";invalid("Implicit conversion between algebra definitions is forbidden.",source.span);}
        if(generic(target)){
            const auto wanted=elementDefinition(target,schema);
            if(generic(source.type)){if(elementDefinition(source.type,source.schema)->hash()!=wanted->hash())invalid("Native operands have different immutable algebra definitions.",source.span);return code;}
            if(scalar(source.type)){if(wanted->unit().empty())invalid("Real scalar embedding requires an algebra unit.",source.span);return "universal_lift<S,"+std::to_string(tag(target,schema))+">("+code+")";}
            invalid("Implicit conversion between algebra definitions is forbidden.",source.span);
        }
        if(!scalar(source.type))invalid("A real native result cannot discard algebra coefficients.",source.span);
        return code;
    }
    long long literal(const ExprPtr& e,long long maximum){
        if(!e)invalid("Missing native literal.");
        if(e->kind==language::ExprKind::Group)return literal(e->children.at(0),maximum);
        if(e->kind==language::ExprKind::Unary&&(e->text=="+"||e->text=="-")){const auto value=literal(e->children.at(0),maximum);return e->text=="-"?-value:value;}
        long long result=0;if(e->kind!=language::ExprKind::IntegerLiteral)invalid("Native algebra index/power requires an integer literal.",e->span);
        const auto parsed=std::from_chars(e->text.data(),e->text.data()+e->text.size(),result);if(parsed.ec!=std::errc{}||parsed.ptr!=e->text.data()+e->text.size()||result<0||result>maximum)invalid("Native algebra literal exceeds its bound.",e->span);return result;
    }
    std::string mul(Type type,const Schema& schema,const std::string& a,const std::string& b){if(type==Type::Pentarnion){charge(100);return "product("+a+","+b+",gamma)";}if(generic(type)){charge(3*elementDefinition(type,schema)->terms().size());return "universal_product("+a+","+b+")";}charge(1);return "("+a+"*"+b+")";}
    std::string arrayBinary(const Expr& e,const std::string& a,const std::string& b){
        const auto& left=*e.children[0];const auto& right=*e.children[1];const auto& op=e.text;
        const bool leftArray=array(left.type),rightArray=array(right.type);
        const auto& owner=leftArray?left:right;Schema resultSchema=array(e.type)?e.schema:arraySchema(owner);
        Type resultType=array(e.type)?e.type:owner.type;
        if(left.type==Type::ElementMatrix||right.type==Type::ElementMatrix)resultType=Type::ElementMatrix;
        else if(left.type==Type::PentarnionMatrix||right.type==Type::PentarnionMatrix)resultType=Type::PentarnionMatrix;
        if(resultType==Type::ElementMatrix&&(!resultSchema||!resultSchema->algebra))resultSchema=left.type==Type::ElementMatrix?left.schema:right.schema;
        const auto tagValue=arrayTag(resultType,resultSchema);const auto element=cellType(resultType);
        auto cast=[&](const Expr& source,const std::string& code){const auto sourceSchema=arraySchema(source);const auto from=arrayTag(source.type,sourceSchema);if(from!=tagValue&&from!=-1)invalid("Native arrays cannot mix different entry algebras.",e.span);if(source.type==Type::ElementMatrix&&resultType==Type::ElementMatrix&&elementDefinition(source.type,sourceSchema)->hash()!=elementDefinition(resultType,resultSchema)->hash())invalid("Native arrays have different immutable definitions.",e.span);return from==tagValue?code:"array_cast<"+std::to_string(tagValue)+">("+code+")";};
        if((op=="*"||op==".*")&&((leftArray&&scalar(right.type))||(rightArray&&scalar(left.type)))){charge(nativeCoordinateWidth(e.type,e.schema));return "("+(leftArray?cast(left,a):cast(right,b))+"*"+(leftArray?b:a)+")";}
        if((op=="/"||op=="./")&&leftArray&&scalar(right.type)){charge(3*nativeCoordinateWidth(e.type,e.schema));return "array_divide("+cast(left,a)+","+b+")";}
        if(op=="./"&&leftArray&&right.type==Type::RealMatrix){if(shape(arraySchema(left))!=shape(arraySchema(right)))invalid("Native entrywise division requires matching fixed shapes.",e.span);charge(3*nativeCoordinateWidth(e.type,e.schema));return "array_entry_divide("+cast(left,a)+","+b+")";}
        if((op=="^"||op==".^")&&leftArray&&right.type==Type::Integer){
            const auto exponent=literal(e.children[1],64);const auto dimensions=shape(arraySchema(left));const auto input=cast(left,a);
            if(op=="^"){if(exponent<0||dimensions.first!=dimensions.second)invalid("Native matrix powers require a square shape and literal exponent0..64.",e.span);if(exponent==0&&tagValue>=0&&elementDefinition(Type::Element,resultSchema)->unit().empty())invalid("Native zeroth matrix power requires an algebra unit.",e.span);charge(nativeCoordinateWidth(e.type,e.schema)*dimensions.first*static_cast<std::size_t>(exponent));return "array_power("+input+","+std::to_string(exponent)+",gamma)";}
            if(tagValue>=0&&exponent<0)requireInverse(elementDefinition(Type::Element,resultSchema),e.span);
            const auto power=tagValue>=0?"universal_power(v,"+std::to_string(exponent)+")":tagValue==-2?"cb_pent_power(v,"+std::to_string(exponent)+",gamma)":"cb_ipow(v,"+std::to_string(exponent)+")";
            charge(nativeCoordinateWidth(e.type,e.schema)*std::max<std::size_t>(1,static_cast<std::size_t>(std::abs(exponent))));return "array_map_as<"+std::to_string(tagValue)+">("+input+",[&](const auto& v){return "+power+";})";
        }
        const auto aa=leftArray?cast(left,a):promoted(element,resultSchema,left,a),bb=rightArray?cast(right,b):promoted(element,resultSchema,right,b);
        const auto leftShape=leftArray?shape(arraySchema(left)):std::pair<std::size_t,std::size_t>{1,1};const auto rightShape=rightArray?shape(arraySchema(right)):std::pair<std::size_t,std::size_t>{1,1};
        if(op=="=="||op=="!="||op=="~="){if(!leftArray||!rightArray||leftShape!=rightShape)return op=="=="?"false":"true";const auto code="array_equal("+aa+","+bb+")";return op=="=="?code:"(!"+code+")";}
        if(op=="+"||op=="-"){
            charge(nativeCoordinateWidth(e.type,e.schema));
            if(leftArray&&rightArray){if(leftShape!=rightShape)invalid("Native array addition requires equal fixed shapes.",e.span);return "("+aa+op+bb+")";}
            const auto arrayCode=leftArray?aa:bb,scalarCode=leftArray?bb:aa;
            const auto code="array_add_element("+arrayCode+","+scalarCode+","+(op=="-"?"true":"false")+")";
            return op=="-"&&!leftArray?"(-"+code+")":code;
        }
        if(op=="*"||op==".*"){
            const auto width=nativeCoordinateWidth(e.type,e.schema);charge(width*std::max<std::size_t>(1,leftArray&&rightArray?leftShape.second:1)*(tagValue>=0?3*elementDefinition(Type::Element,resultSchema)->terms().size():tagValue==-2?100:1));
            if(leftArray&&rightArray){if(op==".*"){if(leftShape!=rightShape)invalid("Native entrywise products require equal fixed shapes.",e.span);return "array_entry_product("+aa+","+bb+",gamma)";}if(leftShape.second!=rightShape.first)invalid("Native matrix product dimensions do not contract.",e.span);return "array_matmul("+aa+","+bb+",gamma)";}
            return "array_scale_element("+(leftArray?aa:bb)+","+(leftArray?bb:aa)+",gamma,"+(leftArray?"false":"true")+")";
        }
        if((op=="/"||op=="./")&&leftArray){
            charge(3*nativeCoordinateWidth(e.type,e.schema));
            if(!rightArray&&scalar(right.type))return "array_divide("+aa+","+b+")";
            if(op=="./"&&right.type==Type::RealMatrix&&leftShape==rightShape)return "array_entry_divide("+aa+","+b+")";
            invalid("Native array division requires a real scalar denominator or entrywise real array denominator.",e.span);
        }
        invalid("Unsupported native fixed-array operator: "+op+".",e.span);
    }
    std::string arrayCall(const Expr& e,std::size_t depth){
        const auto& input=*e.children[0];const auto schema=arraySchema(input);const auto [rows,columns]=shape(schema);const auto inputTag=arrayTag(input.type,schema);const auto a=expr(e.children[0],depth+1);const auto& name=e.text;
        auto arity=[&](std::size_t count){if(e.children.size()!=count)invalid("Incorrect native fixed-array intrinsic arity.",e.span);};
        if(name=="transpose"){arity(1);return "array_transpose("+a+",false)";}
        if(name=="size"){if(e.children.size()==1)return "array_make<S,1,2,-1>(S("+std::to_string(rows)+"),S("+std::to_string(columns)+"))";arity(2);const auto axis=literal(e.children[1],100000);if(axis<1)invalid("A native size dimension must be positive.",e.span);return "S("+std::to_string(axis==1?rows:axis==2?columns:1)+")";}
        if(name=="numel"||name=="length"){arity(1);return "S("+std::to_string(name=="numel"?rows*columns:std::max(rows,columns))+")";}
        if(name=="sum"||name=="mean"){if(e.children.size()>2)invalid("Use native sum/mean(A[,literal_dimension]).",e.span);const auto axis=e.children.size()==2?literal(e.children[1],100000):static_cast<long long>(rows==1?2:1);if(axis<1)invalid("A native reduction dimension must be positive.",e.span);charge(nativeCoordinateWidth(input.type,schema));return "array_reduce<"+std::to_string(axis)+">("+a+","+(name=="mean"?"true":"false")+")";}
        if(name=="norm"){if(e.children.size()>2)invalid("Use norm(native_array[,\"fro\"]).",e.span);if(e.children.size()==2){const auto& selector=*e.children[1];if(selector.kind!=language::ExprKind::StringLiteral||selector.text!="fro")invalid("A native matrix norm currently requires the literal Frobenius selector.",e.span);}else if(rows>1&&columns>1)invalid("Native nonvector matrix norms require explicit \"fro\"; spectral norms are a distinct solver capability.",e.span);charge(nativeCoordinateWidth(input.type,schema));return "array_norm("+a+")";}
        std::string mapped;int outputTag=inputTag;
        if(name=="coeff"){arity(2);const auto index=literal(e.children[1],inputTag==-1?0:inputTag==-2?4:elementDefinition(Type::Element,schema)->dimension()-1);if(index<0)invalid("Native array coefficient indices must be nonnegative.",e.span);mapped=inputTag==-1?"v":"v.c["+std::to_string(index)+"]";outputTag=-1;}
        else{arity(1);const std::map<std::string,unsigned> whole{{"sin",1},{"cos",2},{"exp",3},{"sinh",4},{"cosh",5}};const auto wholeFound=whole.find(name);
            if(inputTag>=0){const auto definition=elementDefinition(Type::Element,schema);if(name=="conj"&&definition->conjugation().empty())invalid("Native entry conjugation requires a declared linear map.",e.span);if(name=="norm2"&&definition->metric().empty())invalid("Native entry norm2 requires a declared metric.",e.span);if(name=="scalar"||name=="pure"){if(definition->unit().size()!=definition->dimension()||definition->unit()[0]!=1||std::any_of(definition->unit().begin()+1,definition->unit().end(),[](double value){return value!=0;}))invalid("Native entry scalar/pure requires a scalar-first unit coordinate.",e.span);}}
            if(name=="conj")mapped=inputTag==-1?"v":inputTag==-2?"conjugate(v)":"universal_conjugate(v)";
            else if(name=="scalar"){mapped=inputTag==-1?"v":"v.c[0]";outputTag=-1;}
            else if(name=="pure")mapped=inputTag==-1?"S(0)":inputTag==-2?"pure(v)":"universal_pure(v)";
            else if(name=="norm2"){mapped=inputTag==-1?"(v*v)":inputTag==-2?"norm2(v)":"universal_norm2(v)";outputTag=-1;}
            else if(inputTag>=0){if(wholeFound==whole.end())invalid("No native whole-entry definition exists for this fixed-array intrinsic.",e.span);requireWhole(elementDefinition(Type::Element,schema),e.span);mapped="universal_whole(v,"+std::to_string(wholeFound->second)+")";charge(rows*columns*(768*elementDefinition(Type::Element,schema)->terms().size()+1024*elementDefinition(Type::Element,schema)->dimension()));}
            else if(inputTag==-2){if(wholeFound==whole.end())invalid("No native whole-Pentarnion array intrinsic exists for this function.",e.span);mapped="cb_whole(v,"+std::to_string(wholeFound->second)+")";}
            else{const std::set<std::string> unary{"sin","cos","exp","sinh","cosh","tan","asin","acos","atan","tanh","asinh","acosh","atanh","log","log10","log2","sqrt","abs","floor","ceil","round","expm1","log1p","erf","erfc","sign"};if(!unary.contains(name))invalid("Unsupported native real array intrinsic: "+name+".",e.span);mapped="cb_"+name+"(v)";}
        }
        charge(nativeCoordinateWidth(input.type,schema));const auto mappedCode="array_map_as<"+std::to_string(outputTag)+">("+a+",[&](const auto& v){return "+mapped+";})";const auto expectedTag=arrayTag(e.type,e.schema);if(expectedTag!=outputTag){if(outputTag!=-1)invalid("Native array intrinsic cannot discard or change its checked entry algebra.",e.span);return "array_cast<"+std::to_string(expectedTag)+">("+mappedCode+")";}return mappedCode;
    }
    std::string expr(const ExprPtr& pointer,std::size_t depth=0){
        if(!pointer)invalid("Missing native algebra expression.");
        const auto& e=*pointer;
        if(++nodes_>(third_?4096u:20000u)||depth>128||(generic(e.type)&&depth*nativeCoordinateWidth(e.type,e.schema)>1024))invalid("Native algebra expression node/depth limit exceeded.",e.span);
        if(!scalar(e.type)&&e.type!=Type::Pentarnion&&!generic(e.type)&&!array(e.type)&&e.type!=Type::Tensor)invalid("Native expressions require numeric scalar, fixed array or typed tensor elements.",e.span);
        result.normalized+=std::to_string(static_cast<int>(e.kind))+":"+std::to_string(static_cast<int>(e.type))+":"+std::to_string(e.text.size())+":"+e.text+"[";
        if(e.type==Type::Tensor){const auto width=nativeCoordinateWidth(Type::Tensor,e.schema);if(width>4096-tensorExpressionCoordinates_)invalid("Native tensor expression coordinates exceed their4096-coordinate allowance.",e.span);tensorExpressionCoordinates_+=width;result.normalized+="tensor:"+tensorDescriptor(e.schema)+";";}
        if(generic(e.type))result.normalized+="algebra="+elementDefinition(e.type,e.schema)->hash()+";";
        if(array(e.type)){const auto [rows,columns]=shape(arraySchema(e));result.normalized+="shape="+std::to_string(rows)+","+std::to_string(columns)+";";}
        auto child=[&](std::size_t n){if(n>=e.children.size())invalid("Malformed native algebra expression.",e.span);return expr(e.children[n],depth+1);};
        auto arity=[&](std::size_t n){if(e.children.size()!=n)invalid("Malformed native function arity.",e.span);};
        auto promotedChild=[&](std::size_t n){const auto code=child(n);return promoted(e.type,e.schema,*e.children[n],code);};
        std::string code;
        switch(e.kind){
        case language::ExprKind::IntegerLiteral:{long long value=0;const auto parsed=std::from_chars(e.text.data(),e.text.data()+e.text.size(),value);if(parsed.ec!=std::errc{}||parsed.ptr!=e.text.data()+e.text.size())invalid("Native integer literal is outside int64.",e.span);code="S(std::int64_t("+std::to_string(value)+"))";break;}
        case language::ExprKind::RealLiteral:{double value=0;const auto parsed=std::from_chars(e.text.data(),e.text.data()+e.text.size(),value,std::chars_format::general);if(parsed.ec!=std::errc{}||parsed.ptr!=e.text.data()+e.text.size()||!std::isfinite(value))invalid("Native real literal must be finite binary64.",e.span);code="S("+number(value)+")";break;}
        case language::ExprKind::Reference:{const auto found=bindings_.find(e.text);if(found!=bindings_.end())code=found->second.code;else{const std::map<std::string,unsigned> basis{{"i",1},{"j",2},{"k",3},{"ell",4}};const auto b=basis.find(e.text);if(e.type!=Type::Pentarnion||b==basis.end())invalid("Native callbacks cannot capture mutable or unsupported references.",e.span);code="basis<S>("+std::to_string(b->second)+")";}break;}
        case language::ExprKind::Member:{arity(1);if(e.children[0]->type!=Type::Algebra||!generic(e.type))invalid("Only immutable algebra basis members are supported in native scalar code.",e.span);const auto definition=elementDefinition(e.type,e.schema);const auto found=std::find(definition->basis().begin(),definition->basis().end(),e.text);if(found==definition->basis().end())invalid("Unknown native algebra basis member.",e.span);code="universal_basis<S,"+std::to_string(tag(e.type,e.schema))+">("+std::to_string(found-definition->basis().begin())+")";break;}
        case language::ExprKind::Group:arity(1);code="("+child(0)+")";break;
        case language::ExprKind::MatrixLiteral:{const auto schema=arraySchema(e);const auto [rows,columns]=shape(schema);code="array_make<S,"+std::to_string(rows)+","+std::to_string(columns)+","+std::to_string(arrayTag(e.type,schema))+">(";for(std::size_t n=0;n<e.children.size();++n){if(n)code+=",";code+=promoted(cellType(e.type),schema,*e.children[n],child(n));}code+=")";break;}
        case language::ExprKind::Index:{if(!e.children.empty()&&e.children[0]->type==Type::Tensor){code="tensor_element("+child(0)+","+std::to_string(tensorIndex(e.children[0]->schema,std::vector<ExprPtr>(e.children.begin()+1,e.children.end())))+")";break;}if(e.children.size()!=2&&e.children.size()!=3)invalid("Native fixed-array indexing requires one linear index or row,column.",e.span);const auto& base=*e.children[0];if(!array(base.type))invalid("Native indexing requires a fixed array value.",e.span);const auto [rows,columns]=shape(arraySchema(base));auto index=[&](std::size_t n,std::size_t extent){if(e.children[n]->kind==language::ExprKind::EndIndex)return static_cast<long long>(extent);const auto value=literal(e.children[n],extent);if(value<1)invalid("Native array indices are positive one-based integer literals.",e.span);return value;};code=e.children.size()==2?"array_linear_at("+child(0)+","+std::to_string(index(1,rows*columns))+")":"array_at("+child(0)+","+std::to_string(index(1,rows)-1)+","+std::to_string(index(2,columns)-1)+")";break;}
        case language::ExprKind::Transpose:{arity(1);if(!array(e.children[0]->type))invalid("Native transpose requires a fixed array.",e.span);if(e.children[0]->type==Type::ElementMatrix&&e.text=="'"&&elementDefinition(Type::Element,e.children[0]->schema)->conjugation().empty())invalid("Native conjugate transpose requires an explicit conjugation map.",e.span);code="array_transpose("+child(0)+","+(e.text=="'"?"true":"false")+")";break;}
        case language::ExprKind::Unary:{arity(1);if(e.text!="+"&&e.text!="-"&&e.text!="!"&&e.text!="~")invalid("Unsupported native unary operation.",e.span);if((e.text=="!"||e.text=="~")&&!scalar(e.children[0]->type))invalid("Native logical negation requires a real scalar.",e.span);code="("+(e.text=="~"?std::string("!"):e.text)+child(0)+")";break;}
        case language::ExprKind::Binary:{arity(2);const auto a=child(0),b=child(1);const auto& left=*e.children[0];const auto& right=*e.children[1];const auto& op=e.text; if(e.type==Type::Integer&&(op=="+"||op=="-"||op=="*"||op=="^"))invalid("Native integer arithmetic requires real literals or real parameters to preserve checked integer semantics.",e.span);
            if(left.type==Type::Tensor||right.type==Type::Tensor){code=tensorBinary(e,a,b);break;}
            if(array(left.type)||array(right.type)){code=arrayBinary(e,a,b);break;}
            if(op=="+"||op=="-"){charge(nativeCoordinateWidth(e.type,e.schema));code="("+promoted(e.type,e.schema,left,a)+op+promoted(e.type,e.schema,right,b)+")";}
            else if(op=="*"){if(scalar(left.type)||scalar(right.type)){charge(nativeCoordinateWidth(e.type,e.schema));code="("+a+"*"+b+")";}else code=mul(e.type,e.schema,promoted(e.type,e.schema,left,a),promoted(e.type,e.schema,right,b));}
            else if(op=="/"){if(!scalar(right.type))invalid("Native equation division requires an explicit left/right solver; scalar denominators are allowed.",e.span);charge(3*nativeCoordinateWidth(e.type,e.schema));code="divide("+a+","+b+")";}
            else if(op=="^"){const auto exponent=literal(e.children[1],generic(left.type)?64:100000);if(generic(left.type)){const auto definition=elementDefinition(left.type,left.schema);if(exponent<0)requireInverse(definition,e.span);if(exponent==0&&definition->unit().empty())invalid("Zeroth algebra power requires a declared unit.",e.span);charge(static_cast<std::size_t>(std::abs(exponent))*3*definition->terms().size());code="universal_power("+a+","+std::to_string(exponent)+")";}else code=left.type==Type::Pentarnion?"cb_pent_power("+a+","+std::to_string(exponent)+",gamma)":"cb_ipow("+a+","+std::to_string(exponent)+")";}
            else if(op=="=="||op=="!="||op=="~="){Type compare=generic(left.type)?left.type:generic(right.type)?right.type:left.type==Type::Pentarnion||right.type==Type::Pentarnion?Type::Pentarnion:Type::Real;const auto schema=generic(left.type)?left.schema:right.schema;code=generic(compare)?"universal_equal("+promoted(compare,schema,left,a)+","+promoted(compare,schema,right,b)+")":compare==Type::Pentarnion?"pent_equal("+promoted(compare,schema,left,a)+","+promoted(compare,schema,right,b)+")":"("+a+"=="+b+")";if(op!="==")code="(!"+code+")";}
            else if(op=="<"||op=="<="||op==">"||op==">="||op=="&&"||op=="||"){if(!scalar(left.type)||!scalar(right.type))invalid("Native ordering/boolean operations require real scalars.",e.span);code="("+a+op+b+")";}
            else invalid("Unsupported native algebra binary operator.",e.span);
            break;}
        case language::ExprKind::PentConstructor:case language::ExprKind::Call:{const std::string name=e.kind==language::ExprKind::PentConstructor?"pent":e.text;
            if(name=="tensor"){code=tensorConstruct(e,depth);break;}
            if(!e.children.empty()&&e.children[0]->type==Type::Tensor){code=tensorCall(e,depth);break;}
            if(!e.children.empty()&&array(e.children[0]->type)){code=arrayCall(e,depth);break;}
            if(name=="element"){
                if(!generic(e.type)||e.children.size()<2||e.children[0]->type!=Type::Algebra)invalid("Native element construction requires a fixed algebra descriptor and literal scalar coefficients.",e.span);
                const auto dimension=elementDefinition(e.type,e.schema)->dimension();std::vector<ExprPtr> coefficients;
                if(e.children.size()==2&&e.children[1]->kind==language::ExprKind::MatrixLiteral){if(e.children[1]->rows>1&&e.children[1]->columns>1)invalid("Native element coefficients require a row/column vector literal.",e.span);coefficients=e.children[1]->children;}
                else coefficients.assign(e.children.begin()+1,e.children.end());
                if(coefficients.size()!=dimension)invalid("Native element constructor must retain every algebra coefficient.",e.span);
                code=typeName(e.type,e.schema)+"{{{";for(std::size_t n=0;n<coefficients.size();++n){if(!scalar(coefficients[n]->type))invalid("Native element coefficients are real scalars.",coefficients[n]->span);if(n)code+=",";code+=expr(coefficients[n],depth+1);}code+="}}}";
            }else if(name=="algebra_basis"){
                arity(2);const auto dimension=elementDefinition(e.type,e.schema)->dimension();const auto index=literal(e.children[1],dimension-1);if(index<0)invalid("Native basis index must be nonnegative.",e.span);code="universal_basis<S,"+std::to_string(tag(e.type,e.schema))+">("+std::to_string(index)+")";
            }else if(name=="complex"){
                arity(2);if(!scalar(e.children[0]->type)||!scalar(e.children[1]->type))invalid("Native complex constructor requires two real coordinates.",e.span);code=typeName(Type::Complex,e.schema)+"{{{"+child(0)+","+child(1)+"}}}";
            }else if(name=="pent"){
                arity(5);code="pent<S>(";for(unsigned n=0;n<5;++n){if(n)code+=",";if(!scalar(e.children[n]->type))invalid("Native pent coefficients must be real.",e.span);code+=child(n);}code+=")";
            }else if(name=="coeff"){
                arity(2);const auto width=nativeCoordinateWidth(e.children[0]->type,e.children[0]->schema);const auto index=literal(e.children[1],width-1);if(index<0)invalid("Native coefficient index must be nonnegative.",e.span);code=scalar(e.children[0]->type)?child(0):"("+child(0)+").c["+std::to_string(index)+"]";
            }else if(name=="scalar"||name=="pure"||name=="conj"||name=="norm2"||name=="inv"||name=="algebra_norm"||name=="realpart"||name=="imagpart"||name=="arg"){
                arity(1);const auto& input=*e.children[0];const auto a=child(0);
                if(generic(input.type)){
                    const auto definition=elementDefinition(input.type,input.schema);const auto dimension=definition->dimension();
                    if(name=="scalar"||name=="realpart"){if(definition->unit().size()!=dimension||definition->unit()[0]!=1||std::any_of(definition->unit().begin()+1,definition->unit().end(),[](double value){return value!=0;}))invalid("No scalar-first coordinate functional is declared for this algebra.",e.span);code="("+a+").c[0]";}
                    else if(name=="imagpart"){if(input.type!=Type::Complex)invalid("imagpart requires the explicit complex type.",e.span);code="("+a+").c[1]";}
                    else if(name=="arg"){if(input.type!=Type::Complex)invalid("arg requires the explicit complex type.",e.span);code="cb_atan2(("+a+").c[1],("+a+").c[0])";}
                    else if(name=="algebra_norm")code="cb_sqrt(universal_coordinate_norm2("+a+"))";
                    else{if(name=="conj"&&definition->conjugation().empty())invalid("Native conjugation requires a declared linear map.",e.span);if(name=="norm2"&&definition->metric().empty())invalid("Native norm2 requires a declared metric.",e.span);if(name=="inv"){requireInverse(definition,e.span);charge(dimension*dimension*dimension*8);}code=std::string(name=="pure"?"universal_pure":name=="conj"?"universal_conjugate":name=="norm2"?"universal_norm2":"universal_inverse")+"("+a+")";}
                }else if(input.type==Type::Pentarnion){code=name=="scalar"?"("+a+").c[0]":name=="pure"?"pure("+a+")":name=="conj"?"conjugate("+a+")":name=="norm2"?"norm2("+a+")":name=="inv"?"cb_inverse("+a+")":"cb_sqrt(norm2("+a+"))";}
                else if(e.type==Type::Pentarnion&&(name=="conj"||name=="pure"||name=="inv"))code=std::string(name=="conj"?"conjugate":name=="pure"?"pure":"cb_inverse")+"(lift<S>("+a+"))"; else if(name=="inv")code="divide(S(1),"+a+")"; else if(name=="conj"||name=="scalar"||name=="realpart")code=a; else if(name=="norm2")code="("+a+"*"+a+")"; else if(name=="algebra_norm")code="cb_abs("+a+")"; else invalid("This native algebra intrinsic requires an algebra element.",e.span);
            }else if(name=="commutator"||name=="associator"){
                arity(name=="commutator"?2:3);const auto a=promotedChild(0),b=promotedChild(1);if(name=="commutator")code="("+mul(e.type,e.schema,a,b)+"-"+mul(e.type,e.schema,b,a)+")";else{const auto c=promotedChild(2);code="("+mul(e.type,e.schema,mul(e.type,e.schema,a,b),c)+"-"+mul(e.type,e.schema,a,mul(e.type,e.schema,b,c))+")";}
            }else{
                const std::map<std::string,unsigned> whole{{"sin",1},{"cos",2},{"exp",3},{"sinh",4},{"cosh",5}};
                if(e.children.size()==1&&generic(e.children[0]->type)){
                    const auto& input=*e.children[0];const auto a=child(0);const auto definition=elementDefinition(input.type,input.schema);const auto found=whole.find(name);
                    if(found!=whole.end()){requireWhole(definition,e.span);charge(768*definition->terms().size()+1024*definition->dimension());code="universal_whole("+a+","+std::to_string(found->second)+")";}
                    else if(name=="log"&&input.type==Type::Complex)code="universal_complex_log("+a+")";
                    else if(name=="abs"&&input.type==Type::Complex)code="cb_hypot(("+a+").c[0],("+a+").c[1])";
                    else if(name=="tan"||name=="tanh"){requireWhole(definition,e.span);requireInverse(definition,e.span);const auto odd=name=="tan"?1:4,even=name=="tan"?2:5;code="universal_product(universal_whole("+a+","+std::to_string(odd)+"),universal_inverse(universal_whole("+a+","+std::to_string(even)+")))";charge(1536*definition->terms().size()+2048*definition->dimension()+2*definition->dimension()*definition->dimension()*definition->dimension());}
                    else invalid("This whole-algebra elementary function has no implemented native contract.",e.span);
                }else if(e.children.size()==1&&e.children[0]->type==Type::Pentarnion){const auto found=whole.find(name);if(found==whole.end())invalid("No whole-Pentarnion native definition exists for this function.",e.span);code="cb_whole("+child(0)+","+std::to_string(found->second)+")";}
                else{const std::set<std::string> unary{"sin","cos","exp","sinh","cosh","tan","asin","acos","atan","tanh","asinh","acosh","atanh","log","log10","log2","sqrt","abs","floor","ceil","round","expm1","log1p","erf","erfc","sign"};
                    if(unary.contains(name)&&e.children.size()==1&&scalar(e.children[0]->type))code="cb_"+name+"("+child(0)+")";
                    else if((name=="atan2"||name=="hypot")&&e.children.size()==2&&scalar(e.children[0]->type)&&scalar(e.children[1]->type))code="cb_"+name+"("+child(0)+","+child(1)+")";
                    else invalid("Unsupported native function in descriptor-aware code: "+name+".",e.span);}
            }break;}
        default:invalid("Native algebra callbacks use numeric expressions, not general array/record operations.",e.span);
        }
        result.normalized+="]";return code;
    }
    std::string returned(const Statement& statement){
        if(!statement.expression)invalid("A native callback must return numeric values.",statement.span);
        std::vector<ExprPtr> values;
        if(function_.result==Type::Tuple){auto tuple=statement.expression;while(tuple->kind==language::ExprKind::Group&&tuple->children.size()==1)tuple=tuple->children[0];if(tuple->kind!=language::ExprKind::TupleLiteral)invalid("Native multiple results require an explicit tuple.",statement.span);values=tuple->children;}else values.push_back(statement.expression);
        if(values.size()!=outputs_.size())invalid("Native callback result arity mismatch.",statement.span);
        std::string code="{std::array<S,"+std::to_string(result.coordinates)+"> out{};\n";std::size_t offset=0;
        for(std::size_t n=0;n<values.size();++n){const auto expression=expr(values[n]);const auto name="r"+std::to_string(serial_++);code+="const auto "+name+"="+promoted(outputs_[n],outputSchemas_[n],*values[n],expression)+";\n";const auto width=nativeCoordinateWidth(outputs_[n],outputSchemas_[n]);if(scalar(outputs_[n]))code+="out["+std::to_string(offset++)+"]=S("+name+");\n";else for(std::size_t c=0;c<width;++c)code+="out["+std::to_string(offset++)+"]="+name+".c["+std::to_string(c)+"];\n";}
        return code+"return out;}\n";
    }
    std::string statements(const std::vector<Statement>& body){const auto saved=bindings_;std::string code;
        for(const auto& statement:body){if(++nodes_>(third_?4096u:20000u))invalid("Native statement limit exceeded.",statement.span);result.normalized+="stmt:"+std::to_string(static_cast<int>(statement.kind))+";";code+="host_charge(host,1);\n";
            switch(statement.kind){
            case language::StatementKind::Declaration:{Schema schema=statement.declaredSchema?statement.declaredSchema:statement.expression?statement.expression->schema:nullptr;if(array(statement.declaredType)&&(!schema||schema->shape.empty()))schema=arraySchema(*statement.expression);const auto width=nativeCoordinateWidth(statement.declaredType,schema);localCoordinates_+=width;if(localCoordinates_>1024)invalid("Native algebra local-coordinate budget exceeded.",statement.span);const auto name="v"+std::to_string(serial_++),expression=expr(statement.expression);code+=typeName(statement.declaredType,schema)+" "+name+"="+promoted(statement.declaredType,schema,*statement.expression,expression)+";\n";bindings_.insert_or_assign(statement.name,Binding{statement.declaredType,schema,name});break;}
            case language::StatementKind::Assignment:{const auto found=bindings_.find(statement.name);if(found==bindings_.end())invalid("Native assignment requires a declared local.",statement.span);const auto expression=expr(statement.expression);code+=found->second.code+"="+promoted(found->second.type,found->second.schema,*statement.expression,expression)+";\n";break;}
            case language::StatementKind::IndexedAssignment:{const auto found=bindings_.find(statement.name);if(found!=bindings_.end()&&found->second.type==Type::Tensor){const auto cell=tensorIndex(found->second.schema,statement.arguments);const auto expression=expr(statement.expression);code+="tensor_put("+found->second.code+","+std::to_string(cell)+","+promoted(tensorEntry(found->second.schema),found->second.schema,*statement.expression,expression)+");\n";break;}if(found==bindings_.end()||!array(found->second.type)||statement.arguments.empty()||statement.arguments.size()>2)invalid("Native indexed assignment requires a declared fixed array and one linear or two row,column indices.",statement.span);const auto [rows,columns]=shape(found->second.schema);auto index=[&](std::size_t n,std::size_t extent){if(statement.arguments[n]->kind==language::ExprKind::EndIndex)return extent;const auto value=literal(statement.arguments[n],static_cast<long long>(extent));if(value<1)invalid("Native indexed assignment indices must be positive literals or end.",statement.span);return static_cast<std::size_t>(value);};const auto cell=statement.arguments.size()==1?[&]{const auto n=index(0,rows*columns)-1;return (n%rows)*columns+n/rows;}():(index(0,rows)-1)*columns+index(1,columns)-1;const auto expression=expr(statement.expression);code+="array_put("+found->second.code+","+std::to_string(cell)+","+promoted(cellType(found->second.type),found->second.schema,*statement.expression,expression)+");\n";break;}
            case language::StatementKind::If:{const auto condition=expr(statement.expression);code+="if("+condition+"){\n"+statements(statement.body)+"}\n";if(!statement.alternative.empty())code+="else{\n"+statements(statement.alternative)+"}\n";break;}
            case language::StatementKind::Return:code+=returned(statement);break;
            default:invalid("Native algebra callbacks support numeric declarations,assignment,if/else and return.",statement.span);
            }
        }bindings_=saved;return code;
    }
    std::string traits()const{
        std::string source="namespace penta_generated {\n";
        for(const auto& [hash,definition]:definitions_){const auto t=tags_.at(hash);const auto n=definition->dimension();const auto& kind=definition->builtinKind();const bool canonical=kind=="real"||kind=="complex"||kind=="quaternion"||kind=="octonion"||kind=="sedenion";const bool quadratic=canonical||kind=="dual"||kind=="split_complex"||kind=="split_quaternion";const auto& p=definition->properties();const bool associative=p.associative.value&&p.associative.evidence==algebra::universal::Evidence::ProvedExact,powerAssociative=associative||(p.powerAssociative.value&&p.powerAssociative.evidence==algebra::universal::Evidence::ProvedExact);const bool first=definition->unit().size()==n&&definition->unit()[0]==1&&std::all_of(definition->unit().begin()+1,definition->unit().end(),[](double value){return value==0;});
            source+="// Immutable algebra "+hash+"\ntemplate<>struct UniversalTraits<"+std::to_string(t)+">{static constexpr unsigned dimension="+std::to_string(n)+";\n";
            source+="static constexpr bool has_unit="+std::to_string(!definition->unit().empty())+",has_conjugation="+std::to_string(!definition->conjugation().empty())+",has_metric="+std::to_string(!definition->metric().empty())+",scalar_first="+std::to_string(first)+",canonical_reciprocal="+std::to_string(canonical)+",quadratic="+std::to_string(quadratic&&first)+",associative_exact="+std::to_string(associative)+",power_associative_exact="+std::to_string(powerAssociative)+";\n";
            auto array=[&](std::string_view name,const std::vector<double>& data,std::size_t count){source+="static constexpr std::array<long double,"+std::to_string(count)+"> "+std::string(name)+"{{";for(std::size_t k=0;k<count;++k){if(k)source+=",";source+=number(k<data.size()?data[k]:0);}source+="}};\n";};
            array("unit",definition->unit(),n);array("conjugation",definition->conjugation(),n*n);array("metric",definition->metric(),n*n);std::vector<double> pure(n,0);for(std::size_t k=1;k<n;++k)pure[k]=-definition->coefficient(k,k,0);array("pure_square",pure,n);
            long double bound=0;std::map<std::pair<std::size_t,std::size_t>,long double> bounds;for(const auto& term:definition->terms()){const auto sum=(bounds[{term.left,term.right}]+=std::abs(static_cast<long double>(term.coefficient)));bound=std::max(bound,sum);}std::ostringstream boundText;boundText.imbue(std::locale::classic());boundText<<std::hexfloat<<bound;source+="static constexpr long double product_bound="+boundText.str()+"L;\n";
            source+="static constexpr std::array<UniversalTerm,"+std::to_string(definition->terms().size())+"> terms{{";for(const auto& term:definition->terms())source+="{"+std::to_string(term.left)+","+std::to_string(term.right)+","+std::to_string(term.output)+","+number(term.coefficient)+"},";source+="}};};\n";
        }return source+"}\n";
    }
public:
    ode::detail::Generated result;
    explicit UniversalGenerator(const language::FunctionDefinition& function):function_(function){}
    ode::detail::Generated run(bool third=false,bool asOde=false,std::size_t states=0,std::size_t parameters=0){
        third_=third;ode_=asOde;if(function_.body.empty())invalid("A callable prototype has no native body.");if(!function_.captureNames.empty())invalid("Native callbacks cannot capture mutable values.");
        outputs_=function_.result==Type::Tuple?function_.results:std::vector<Type>{function_.result};outputSchemas_=callbackResultSchemas(function_);if(outputs_.empty()||outputs_.size()>64)invalid("Native callback output count exceeds64.");
        for(std::size_t n=0;n<outputs_.size();++n){if(outputs_[n]==Type::Integer)invalid("Compiled callbacks return real or algebra scalar coordinates; int64 results require an integer-preserving native ABI.");collect(outputs_[n],outputSchemas_[n]);result.coordinates+=nativeCoordinateWidth(outputs_[n],outputSchemas_[n]);}
        if(result.coordinates>(ode_?64u:128u)||function_.parameters.size()>193)invalid("Native callback coordinate budget exceeded.");
        for(std::size_t n=0;n<function_.parameters.size();++n){if(function_.parameters[n].type==Type::Integer)invalid("Compiled callback int64 parameters require an integer-preserving ABI; declare a real numerical parameter explicitly.");collect(function_.parameters[n].type,n<function_.parameterSchemas.size()?function_.parameterSchemas[n]:function_.parameters[n].schema);}
        collect(function_.body);
        unsigned next=0;for(const auto& [hash,definition]:definitions_){(void)definition;tags_[hash]=next++;}
        result.normalized=third?"PENTA_GENERIC_CALLBACK_AD3_V1;":ode_?"PENTA_GENERIC_ODE_JET2_V1;":"PENTA_GENERIC_CALLBACK_AD2_V1;";for(const auto& [hash,definition]:definitions_){(void)definition;result.normalized+="definition:"+hash+";";}
        std::string setup;
        for(std::size_t n=0;n<function_.parameters.size();++n){const auto& parameter=function_.parameters[n];const auto schema=n<function_.parameterSchemas.size()?function_.parameterSchemas[n]:parameter.schema;const auto width=nativeCoordinateWidth(parameter.type,schema),offset=result.parameters;const auto name="a"+std::to_string(serial_++);bindings_.emplace(parameter.name,Binding{parameter.type,schema,name});setup+=typeName(parameter.type,schema)+" "+name+"=";
            if(scalar(parameter.type))setup+="parameters["+std::to_string(offset)+"]";
            else if(parameter.type==Type::Pentarnion){setup+="pent<S>(";for(unsigned c=0;c<5;++c){if(c)setup+=",";setup+="parameters["+std::to_string(offset+c)+"]";}setup+=")";}
            else if(parameter.type==Type::Tensor)setup+="tensor_load<"+tensorTemplate(schema)+">(parameters+"+std::to_string(offset)+")";
            else if(array(parameter.type)){const auto [rows,columns]=shape(schema);setup+="array_load<S,"+std::to_string(rows)+","+std::to_string(columns)+","+std::to_string(arrayTag(parameter.type,schema))+">(parameters+"+std::to_string(offset)+")";}
            else setup+="universal_load<S,"+std::to_string(tag(parameter.type,schema))+">(parameters+"+std::to_string(offset)+")";
            setup+=";\n";result.parameters+=width;result.normalized+="param:"+std::to_string(static_cast<int>(parameter.type))+":"+std::to_string(width)+";";if(array(parameter.type)){const auto [rows,columns]=shape(schema);result.normalized+="shape:"+std::to_string(rows)+","+std::to_string(columns)+";";}
            if(parameter.type==Type::Tensor){result.normalized+="tensor:"+tensorDescriptor(schema)+";";}
        }
        if(!result.parameters||result.parameters>(ode_?193u:128u))invalid("Native callback input-coordinate budget exceeded.");
        const auto body=statements(function_.body);for(const auto type:outputs_)result.normalized+="out:"+std::to_string(static_cast<int>(type))+";";
        for(std::size_t n=0;n<outputs_.size();++n)if(outputs_[n]==Type::Tensor)result.normalized+="tensor_out:"+tensorDescriptor(outputSchemas_[n])+";";
#ifdef PENTA_DISTRIBUTION_BUILD
        result.source=std::string(ode::detail::embeddedNativeSupport)+std::string(distributionModelSupport)+traits();
#else
        result.source=std::string(ode::detail::embeddedNativeSupport)+std::string(callbackSupport)+(third?std::string(thirdSupport):std::string())+std::string(universalSupport)+std::string(nativeArraySupport)+(tensor_?std::string(nativeTensorSupport):std::string())+traits();
#endif
        result.source+=R"CB(
#include <cstdint>
using namespace penta_generated;
struct CallbackHost{std::uint32_t abi;void* context;int(*charge)(void*,std::size_t,char*,std::size_t);};
inline void host_charge(const CallbackHost*h,std::size_t n){char e[1024]{};if(!h||h->abi!=1||!h->charge||h->charge(h->context,n,e,sizeof(e)))throw std::domain_error(e[0]?e:"Invalid native callback host ABI.");}
#ifdef _WIN32
#define CB_EXPORT extern "C" __declspec(dllexport)
#else
#define CB_EXPORT extern "C" __attribute__((visibility("default")))
#endif
)CB";
        result.source+="template<class S>std::array<S,"+std::to_string(result.coordinates)+"> callback_body(double gamma,const S*parameters,const CallbackHost*host){\n(void)gamma;\nhost_charge(host,"+std::to_string(work_+nodes_)+");\n"+setup+body+"throw std::domain_error(\"Compiled callback reached its end without returning.\");}\n";
        if(ode_){
            const auto inputs=result.parameters,outputs=result.coordinates;
            result.source+="inline int ode_charge(void*,std::size_t,char*,std::size_t){return 0;}\nCB_EXPORT unsigned penta_model_abi(){return 1;}\nCB_EXPORT unsigned penta_model_coordinates(){return "+std::to_string(states)+";}\nCB_EXPORT unsigned penta_model_parameters(){return "+std::to_string(parameters)+";}\n";
            result.source+="CB_EXPORT int penta_model_evaluate(double time,double gamma,const double*y,const double*p,double*out,double*jac,char*error,std::size_t capacity){try{CallbackHost host{1,nullptr,ode_charge};std::array<Jet2,"+std::to_string(inputs)+"> input{};for(unsigned k=0;k<"+std::to_string(states)+";++k)input[k]=Jet2(y[k]);for(unsigned k=0;k<"+std::to_string(parameters)+";++k)input["+std::to_string(states)+"+k]=Jet2(p[k]);input["+std::to_string(inputs-1)+"]=Jet2(time);std::array<double,"+std::to_string(outputs)+"> values{};std::array<double,"+std::to_string(outputs*states)+"> derivatives{};const auto plain=callback_body(gamma,input.data(),&host);for(unsigned row=0;row<"+std::to_string(outputs)+";++row)values[row]=binary64(plain[row].v);if(jac){for(unsigned k=0;k<"+std::to_string(states)+";++k)input[k].dependent=true;for(unsigned column=0;column<"+std::to_string(states)+";++column){input[column].x=1;const auto result=callback_body(gamma,input.data(),&host);for(unsigned row=0;row<"+std::to_string(outputs)+";++row)derivatives[row*"+std::to_string(states)+"+column]=binary64(result[row].x);input[column].x=0;}}std::copy(values.begin(),values.end(),out);if(jac)std::copy(derivatives.begin(),derivatives.end(),jac);return 0;}catch(const std::exception&e){if(error&&capacity){std::strncpy(error,e.what(),capacity-1);error[capacity-1]=0;}return -1;}}\n";
            result.parameters=parameters;
        }else{
            result.source+="CB_EXPORT unsigned penta_model_abi(){return 1;}\nCB_EXPORT unsigned penta_model_coordinates(){return "+std::to_string(result.coordinates)+";}\nCB_EXPORT unsigned penta_model_parameters(){return "+std::to_string(result.parameters)+";}\n";
            if(third)result.source+=thirdEntry(result.parameters,result.coordinates,work_+nodes_);
            else result.source+="CB_EXPORT int penta_callback_evaluate(double gamma,const double*input,const double*sx,const double*sy,const unsigned char*dependent,double*out,double*first,double*mixed,const CallbackHost*host,char*error,std::size_t capacity){try{std::array<Jet2,"+std::to_string(result.parameters)+"> inputJets{};for(unsigned k=0;k<"+std::to_string(result.parameters)+";++k){inputJets[k]=Jet2(input[k]);inputJets[k].x=sx?sx[k]:0;inputJets[k].y=sy?sy[k]:0;inputJets[k].dependent=dependent&&dependent[k];}const auto result=callback_body(gamma,inputJets.data(),host);std::array<double,"+std::to_string(result.coordinates)+"> values{},derivatives{},seconds{};for(unsigned k=0;k<"+std::to_string(result.coordinates)+";++k){values[k]=binary64(result[k].v);if(first)derivatives[k]=binary64(result[k].x);if(mixed)seconds[k]=binary64(result[k].xy);}std::copy(values.begin(),values.end(),out);if(first)std::copy(derivatives.begin(),derivatives.end(),first);if(mixed)std::copy(seconds.begin(),seconds.end(),mixed);return 0;}catch(const std::exception&e){if(error&&capacity){std::strncpy(error,e.what(),capacity-1);error[capacity-1]=0;}return -1;}}\nCB_EXPORT int penta_model_evaluate(double,double,const double*,const double*,double*,double*,char*,std::size_t){return -1;}\n";
        }
        return std::move(result);
    }
};
inline ode::detail::Generated generateUniversalCallback(const language::FunctionDefinition& function,bool third=false){return UniversalGenerator(function).run(third);}
inline ode::detail::Generated generateUniversalOde(const language::OdeDefinition& model){
    if(model.states.empty()||model.derivatives.size()!=model.states.size())throw ode::OdeError("O2401","Generic ODE requires exactly one derivative per declared state.",model.span);
    language::FunctionDefinition function;function.name=model.name;function.result=language::Type::Tuple;function.span=model.span;function.native=true;
    auto outputSchema=std::make_shared<language::ValueSchema>();auto tuple=std::make_shared<language::Expr>();tuple->kind=language::ExprKind::TupleLiteral;tuple->type=language::Type::Tuple;tuple->span=model.span;
    std::size_t coordinates=0,parameters=0;
    for(std::size_t n=0;n<model.states.size();++n){const auto& state=model.states[n];coordinates+=nativeCoordinateWidth(state.type,state.schema);function.parameters.push_back(state);function.parameterSchemas.push_back(state.schema);function.results.push_back(state.type);outputSchema->elements.push_back({{},state.type,state.schema});tuple->children.push_back(model.derivatives[n]);}
    for(const auto& parameter:model.parameters){parameters+=nativeCoordinateWidth(parameter.type,parameter.schema);function.parameters.push_back(parameter);function.parameterSchemas.push_back(parameter.schema);}
    if(coordinates>64||parameters>128)throw ode::OdeError("O2401","Generic ODE supports at most64 real state and128 parameter coordinates.",model.span);
    function.parameters.push_back({model.timeName,language::Type::Real,model.span,{}});function.parameterSchemas.push_back({});function.resultSchema=outputSchema;tuple->schema=outputSchema;
    language::Statement returned;returned.kind=language::StatementKind::Return;returned.span=model.span;returned.expression=tuple;function.body.push_back(std::move(returned));
    return UniversalGenerator(function).run(false,true,coordinates,parameters);
}
}
