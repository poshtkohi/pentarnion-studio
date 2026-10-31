#pragma once
// Private compiled MATLAB adapter plumbing; never stage this implementation.
#include <mex.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pm {
class V {
    std::shared_ptr<mxArray> value_;
    struct Adopt {};
    V(mxArray* value,Adopt);
public:
    V();
    V(double value);
    V(int value);
    V(bool value);
    V(std::string_view value);
    V(const std::string& value);
    V(const char* value);
    V(const mxArray* value);
    static V take(mxArray* value);
    mxArray* raw() const noexcept;
    mxArray* clone() const;
    bool isEmpty() const;
    std::string text() const;
    double scalar() const;
};
using Args=std::vector<V>;
using Outs=std::vector<V>;
class MatlabError : public std::runtime_error {
public:
    V exception;
    std::string identifier;
    explicit MatlabError(V value);
};
Outs call(const std::string& name,const Args& args,int nout=1);
V one(const std::string& name,const Args& args);
V unary(const std::string& operation,const V& value);
V binary(const std::string& operation,const V& left,const V& right);
Outs indexMany(const V& value,const Args& indices,bool braces,int nout);
V index(const V& value,const Args& indices,bool braces=false);
V assign(const V& value,const Args& indices,const V& replacement,bool braces=false);
Args expandCell(const V& value,const Args& indices);
V member(const V& value,const std::string& field);
V setmember(const V& value,const std::string& field,const V& replacement);
bool truth(const V& value);
V range(const V& start,const V& step,const V& end);
V lastIndex(const V& value,int dimension,int dimensions);
V matrix(const std::vector<Args>& rows,bool cell=false);
} // namespace pm
