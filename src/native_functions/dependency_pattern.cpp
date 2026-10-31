#include "dependency_pattern.hpp"
#include "ode/native_internal.hpp"
#include <algorithm>
#include <bitset>
#include <charconv>
#include <map>
#include <numeric>

namespace penta::native_functions {
namespace {
using namespace language;
using Mask=std::bitset<128>;
struct Data {
    Type type=Type::Real;
    std::shared_ptr<const ValueSchema> schema;
    std::size_t rows=1,columns=1,components=1;
    std::vector<Mask> coordinates;
};
Mask all(const Data& value){Mask result;for(const auto& coordinate:value.coordinates)result|=coordinate;return result;}
bool array(Type type){return type==Type::RealMatrix||type==Type::PentarnionMatrix||type==Type::ElementMatrix;}
class Analyzer {
    const FunctionDefinition& function_;
    std::map<std::string,Data> bindings_;
    std::vector<Mask> outputs_;
    std::size_t nodes_=0;
    bool control_=false;
    [[noreturn]] void invalid(const std::string& text,SourceSpan span={})const{throw ode::OdeError("N2010",text,span.length?span:function_.span);}
    Data data(Type type,const std::shared_ptr<const ValueSchema>& schema={},const Data* fallback=nullptr){
        if(type!=Type::Real&&type!=Type::Integer&&type!=Type::Pentarnion&&type!=Type::Complex&&type!=Type::Element&&type!=Type::Tensor&&!array(type))invalid("Structural patterns require a supported complete native scalar or fixed array/tensor schema.");
        const auto effective=schema?schema:fallback?fallback->schema:nullptr;
        Data result;result.type=type;result.schema=effective;
        result.components=type==Type::Pentarnion||type==Type::PentarnionMatrix?5:type==Type::Complex?2:(type==Type::Element||type==Type::ElementMatrix)&&effective&&effective->algebra?effective->algebra->dimension():1;
        if(type==Type::Tensor){
            const auto width=coordinateWidth(type,effective);
            if(!width||!effective)invalid("A structural tensor requires its complete fixed rank/shape/entry descriptor.");
            result.components=coordinateWidth(effective->tensorEntryType,effective);
            if(!result.components||width%result.components)invalid("Invalid structural tensor entry width.");
            result.rows=width/result.components;result.columns=1;
        }
        if(array(type)){
            if(schema&&schema->shape.size()==2){result.rows=schema->shape[0];result.columns=schema->shape[1];}
            else if(fallback){result.rows=fallback->rows;result.columns=fallback->columns;if(type==Type::ElementMatrix){result.components=fallback->components;result.schema=fallback->schema;}}
            else invalid("A dependency pattern requires explicitly fixed native array shapes.");
        }
        const auto width=result.rows*result.columns*result.components;
        if(!width||width>128)invalid("Dependency patterns retain at most128 complete native coordinates.");
        result.coordinates.resize(width);return result;
    }
    Data promote(const Data& value,Type type,const std::shared_ptr<const ValueSchema>& schema={},const Data* fallback=nullptr){
        auto result=data(type,schema,fallback?fallback:&value);
        if(result.coordinates.size()==value.coordinates.size())result.coordinates=value.coordinates;
        else{const auto mask=all(value);for(auto& coordinate:result.coordinates)coordinate=mask;}
        return result;
    }
    std::size_t integer(const ExprPtr& expression,std::size_t maximum,bool oneBased=true){
        if(expression->kind==ExprKind::Group)return integer(expression->children.at(0),maximum,oneBased);
        if(expression->kind==ExprKind::Unary&&expression->text=="+")return integer(expression->children.at(0),maximum,oneBased);
        if(expression->kind==ExprKind::EndIndex&&oneBased)return maximum;
        std::size_t value=0;const auto parsed=std::from_chars(expression->text.data(),expression->text.data()+expression->text.size(),value);
        if(expression->kind!=ExprKind::IntegerLiteral||parsed.ec!=std::errc{}||parsed.ptr!=expression->text.data()+expression->text.size()||value>maximum||(oneBased&&!value))invalid("Native dependency indices must be checked integer literals.",expression->span);
        return value;
    }
    std::size_t cell(const std::vector<ExprPtr>& indices,const Data& source){
        if(source.type==Type::Tensor){
            if(!source.schema||(indices.size()!=1&&indices.size()!=source.schema->shape.size()))invalid("Native tensor dependencies require one linear index or every declared axis.");
            std::size_t result=0,stride=1;for(std::size_t axis=0;axis<indices.size();++axis){const auto extent=indices.size()==1?source.rows:source.schema->shape[axis];result+=(integer(indices[axis],extent)-1)*stride;stride*=extent;}return result;
        }
        if(indices.size()==1){const auto index=integer(indices[0],source.rows*source.columns)-1;return (index%source.rows)*source.columns+index/source.rows;}
        if(indices.size()==2)return (integer(indices[0],source.rows)-1)*source.columns+integer(indices[1],source.columns)-1;
        invalid("Invalid native fixed-array dependency index.");
    }
    Data expr(const ExprPtr& pointer,std::size_t depth=0){
        if(!pointer||++nodes_>20000||depth>128)invalid("Dependency AST node/depth budget exceeded.");
        const auto& e=*pointer;
        if(e.type==Type::Algebra||e.kind==ExprKind::StringLiteral)return {};
        if(e.kind==ExprKind::EndIndex)return data(Type::Integer);
        if(e.kind==ExprKind::Reference){const auto found=bindings_.find(e.text);if(found!=bindings_.end())return found->second;return data(e.type,e.schema);}
        if(e.kind==ExprKind::IntegerLiteral||e.kind==ExprKind::RealLiteral||e.kind==ExprKind::Member)return data(e.type,e.schema);
        if(e.kind==ExprKind::Group)return expr(e.children.at(0),depth+1);
        std::vector<Data> children;children.reserve(e.children.size());Mask combined;
        for(const auto& child:e.children){children.push_back(expr(child,depth+1));combined|=all(children.back());}
        if(e.kind==ExprKind::TupleLiteral){Data result;result.type=Type::Tuple;for(const auto& child:children)result.coordinates.insert(result.coordinates.end(),child.coordinates.begin(),child.coordinates.end());return result;}
        if(e.kind==ExprKind::Index){const auto& source=children.at(0);std::vector<ExprPtr> indices(e.children.begin()+1,e.children.end());const auto index=cell(indices,source);auto result=data(e.type,e.schema);if(result.coordinates.size()!=source.components)invalid("Native index coordinate schema mismatch.",e.span);std::copy_n(source.coordinates.begin()+static_cast<std::ptrdiff_t>(index*source.components),source.components,result.coordinates.begin());return result;}
        if(e.kind==ExprKind::MatrixLiteral){Data result;result.type=e.type;result.schema=e.schema;result.rows=e.rows;result.columns=e.columns;result.components=e.type==Type::PentarnionMatrix?5:e.type==Type::ElementMatrix&&e.schema&&e.schema->algebra?e.schema->algebra->dimension():1;for(const auto& child:children){auto value=child;if(value.coordinates.size()!=result.components){value.coordinates.assign(result.components,all(child));}result.coordinates.insert(result.coordinates.end(),value.coordinates.begin(),value.coordinates.end());}if(result.coordinates.size()!=result.rows*result.columns*result.components)invalid("Dependency analysis requires plain fixed native matrix literals.",e.span);return result;}
        const Data* fallback=children.empty()?nullptr:&children.front();
        auto result=data(e.type,e.schema,fallback);
        const auto& name=e.text;
        if(array(e.type)&&e.kind==ExprKind::Binary&&name=="*"&&children.size()==2&&array(children[0].type)&&array(children[1].type)){result.rows=children[0].rows;result.columns=children[1].columns;result.components=std::max(children[0].components,children[1].components);result.coordinates.resize(result.rows*result.columns*result.components);}
        if(array(e.type)&&e.kind==ExprKind::Call&&(name=="sum"||name=="mean")){const auto axis=e.children.size()==2?integer(e.children[1],100000):children[0].rows==1?2u:1u;if(axis==1)result.rows=1;else if(axis==2)result.columns=1;result.coordinates.resize(result.rows*result.columns*result.components);}
        if(e.kind==ExprKind::Call&&(name=="size"||name=="numel"||name=="length"||name=="ndims")){if(name=="size"&&e.children.size()==1){const auto rank=children[0].type==Type::Tensor?children[0].schema->shape.size():2;result.rows=1;result.columns=rank;result.components=1;result.coordinates.resize(rank);}return result;}
        if(e.kind==ExprKind::Call&&(name=="tensorcoords"||name=="tensorvalues")){if(result.coordinates.size()!=children.at(0).coordinates.size())invalid("Tensor coordinate extraction must preserve every declared coefficient.",e.span);result.coordinates=children[0].coordinates;return result;}
        if(e.kind==ExprKind::Unary){if(result.coordinates.size()==children[0].coordinates.size()&&(name=="+"||name=="-"))result.coordinates=children[0].coordinates;else for(auto& coordinate:result.coordinates)coordinate=combined;
        return result;}
        if(e.kind==ExprKind::Binary&&(name=="+"||name=="-"||(name==".*"&&result.components==1)||name=="./")){
            for(std::size_t k=0;k<result.coordinates.size();++k)for(const auto& child:children)result.coordinates[k]|=child.coordinates.size()==result.coordinates.size()?child.coordinates[k]:all(child);
            return result;
        }
        if((e.kind==ExprKind::Call||e.kind==ExprKind::PentConstructor)&&((name=="pent")||(name=="element")||e.kind==ExprKind::PentConstructor)){
            const auto first=name=="element"?1u:0u;
            if(children.size()==first+1&&children[first].coordinates.size()==result.coordinates.size())result.coordinates=children[first].coordinates;
            else if(children.size()==first+result.coordinates.size())for(std::size_t k=0;k<result.coordinates.size();++k)result.coordinates[k]=all(children[first+k]);
            else for(auto& coordinate:result.coordinates)coordinate=combined;
            return result;
        }
        if(e.kind==ExprKind::Call&&name=="coeff"){
            const auto& source=children[0];const bool liftedReal=source.type==Type::Real||source.type==Type::Integer;const auto index=integer(e.children.at(1),liftedReal?4:source.components-1,false);
            if(array(source.type)||source.type==Type::Tensor){result.rows=source.rows;result.columns=source.columns;result.components=1;result.coordinates.resize(source.rows*source.columns);for(std::size_t k=0;k<result.coordinates.size();++k)result.coordinates[k]=source.coordinates[k*source.components+index];}
            else if(!liftedReal||index==0)result.coordinates[0]=source.coordinates[index];
            return result;
        }
        if(e.kind==ExprKind::Call&&name=="scalar"){const auto& source=children.at(0);for(std::size_t k=0;k<result.coordinates.size();++k)result.coordinates[k]=source.coordinates[k*source.components];return result;}
        if(e.kind==ExprKind::Call&&name=="pure"){const auto& source=children.at(0);if(source.coordinates.size()==result.coordinates.size()){result.coordinates=source.coordinates;for(std::size_t k=0;k<source.rows*source.columns;++k)result.coordinates[k*source.components].reset();}return result;}
        if(e.kind==ExprKind::Transpose||(e.kind==ExprKind::Call&&name=="transpose")){
            const auto& source=children.at(0);result.rows=source.columns;result.columns=source.rows;result.components=source.components;result.coordinates.resize(source.coordinates.size());
            // General conjugation can mix coefficients. This safely includes
            // all coefficients of the selected cell, never other array cells.
            for(std::size_t row=0;row<source.rows;++row)for(std::size_t col=0;col<source.columns;++col){Mask mask;for(std::size_t c=0;c<source.components;++c)mask|=source.coordinates[(row*source.columns+col)*source.components+c];for(std::size_t c=0;c<source.components;++c)result.coordinates[(col*source.rows+row)*source.components+c]=mask;}
            return result;
        }
        // All remaining accepted native operators conservatively connect every
        // output coordinate to every child coordinate. This covers whole-algebra
        // products/inverses/functions and matrix contractions without any
        // assumption of associativity or a sample-dependent zero.
        for(auto& coordinate:result.coordinates)coordinate=combined;
        return result;
    }
    void statements(const std::vector<Statement>& body,Mask control){
        const auto entry=bindings_;std::vector<std::string> declarations;
        for(const auto& statement:body){if(++nodes_>20000)invalid("Dependency statement budget exceeded.",statement.span);
            switch(statement.kind){
            case StatementKind::Declaration:{auto value=expr(statement.expression);value=promote(value,statement.declaredType,statement.declaredSchema);for(auto& coordinate:value.coordinates)coordinate|=control;bindings_.insert_or_assign(statement.name,std::move(value));declarations.push_back(statement.name);break;}
            case StatementKind::Assignment:{auto found=bindings_.find(statement.name);if(found==bindings_.end())invalid("Undeclared native dependency assignment.",statement.span);auto value=promote(expr(statement.expression),found->second.type,found->second.schema,&found->second);for(auto& coordinate:value.coordinates)coordinate|=control;found->second=std::move(value);break;}
            case StatementKind::IndexedAssignment:{auto found=bindings_.find(statement.name);if(found==bindings_.end())invalid("Undeclared native indexed dependency assignment.",statement.span);const auto index=cell(statement.arguments,found->second);const auto value=expr(statement.expression);for(std::size_t c=0;c<found->second.components;++c)found->second.coordinates[index*found->second.components+c]=(value.coordinates.size()==found->second.components?value.coordinates[c]:all(value))|control;break;}
            case StatementKind::If:{const auto condition=all(expr(statement.expression));control_=true;const auto before=bindings_;statements(statement.body,control|condition);const auto yes=bindings_;bindings_=before;statements(statement.alternative,control|condition);for(auto& [name,value]:bindings_){const auto y=yes.find(name);if(y==yes.end())continue;for(std::size_t k=0;k<value.coordinates.size();++k)value.coordinates[k]|=y->second.coordinates[k]|condition;}control|=condition;break;}
            case StatementKind::Return:{Data value;if(function_.result==Type::Tuple){auto tuple=statement.expression;while(tuple->kind==ExprKind::Group)tuple=tuple->children.at(0);if(tuple->kind!=ExprKind::TupleLiteral||tuple->children.size()!=function_.results.size())invalid("Native dependency tuple return layout mismatch.",statement.span);for(std::size_t n=0;n<tuple->children.size();++n){const auto schema=function_.resultSchema&&n<function_.resultSchema->elements.size()?function_.resultSchema->elements[n].schema:nullptr;const auto child=promote(expr(tuple->children[n]),function_.results[n],schema);value.coordinates.insert(value.coordinates.end(),child.coordinates.begin(),child.coordinates.end());}}else value=promote(expr(statement.expression),function_.result,function_.resultSchema);if(value.coordinates.size()!=outputs_.size())invalid("Native dependency result layout differs from the checked result schema.",statement.span);for(std::size_t k=0;k<outputs_.size();++k)outputs_[k]|=value.coordinates[k]|control;break;}
            default:invalid("Dependency patterns support the checked native numeric statement subset.",statement.span);
            }
        }
        for(const auto& name:declarations){const auto previous=entry.find(name);if(previous==entry.end())bindings_.erase(name);else bindings_.insert_or_assign(name,previous->second);}
    }
public:
    explicit Analyzer(const FunctionDefinition& function):function_(function){}
    DependencyPattern run(){
        validateCallback(function_);std::size_t columns=0;
        for(std::size_t p=0;p<function_.parameters.size();++p){const auto& parameter=function_.parameters[p];auto schema=p<function_.parameterSchemas.size()?function_.parameterSchemas[p]:parameter.schema;auto value=data(parameter.type,schema);for(auto& coordinate:value.coordinates){if(columns>=128)invalid("Dependency pattern input limit is128 coordinates.");coordinate.set(columns++);}bindings_.emplace(parameter.name,std::move(value));}
        if(function_.result==Type::Tuple){for(std::size_t r=0;r<function_.results.size();++r){const auto schema=function_.resultSchema&&r<function_.resultSchema->elements.size()?function_.resultSchema->elements[r].schema:nullptr;outputs_.resize(outputs_.size()+data(function_.results[r],schema).coordinates.size());}}
        else outputs_.resize(data(function_.result,function_.resultSchema).coordinates.size());
        if(outputs_.size()>128)invalid("Dependency pattern output limit is128 coordinates.");
        statements(function_.body,{});DependencyPattern result;result.rows=outputs_.size();result.columns=columns;result.includesControlDependencies=control_;result.rowOffsets.push_back(0);
        for(const auto& mask:outputs_){for(std::size_t column=0;column<columns;++column)if(mask[column])result.columnIndices.push_back(column);result.rowOffsets.push_back(result.columnIndices.size());}
        std::string identity="PENTA_STRUCTURAL_DEPENDENCY_CSR_V1;"+callbackFingerprint(function_);for(const auto offset:result.rowOffsets)identity+=":"+std::to_string(offset);identity+=';';for(const auto column:result.columnIndices)identity+=":"+std::to_string(column);result.key=ode::detail::sha256(identity);return result;
    }
};
}
bool DependencyPattern::contains(std::size_t row,std::size_t column)const{if(row>=rows||column>=columns||rowOffsets.size()!=rows+1)return false;return std::binary_search(columnIndices.begin()+static_cast<std::ptrdiff_t>(rowOffsets[row]),columnIndices.begin()+static_cast<std::ptrdiff_t>(rowOffsets[row+1]),column);}
DependencyPattern dependencyPattern(const language::FunctionDefinition& function){return Analyzer(function).run();}
}
