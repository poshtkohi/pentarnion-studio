"""Build-time MATLAB adapter compiler; implementation is included in public source.

This accepts only the checked export adapter's bounded MATLAB subset and emits
ordinary C++ control flow. MATLAB numeric/solver primitives remain MATLAB calls.
It never embeds or evaluates MATLAB source at runtime.
"""
from __future__ import annotations
import dataclasses, json, re, hashlib, os
from pathlib import Path

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
SOURCE=Path(os.environ['PENTA_SOURCE_ROOT'])/'src/export'

@dataclasses.dataclass
class T:
    v:str; kind:str; pos:int; ws:str=''

def tokenize(s):
    out=[];i=0;ws=''
    while i<len(s):
        c=s[i]
        if c in ' \t\r': ws+=c;i+=1;continue
        if c=='%':
            while i<len(s) and s[i]!='\n': i+=1
            continue
        if s.startswith('...',i):
            i=s.find('\n',i)
            if i<0:break
            i+=1;ws+=' ';continue
        start=i
        if c=='\n':out.append(T('\n','sep',i,ws));ws='';i+=1;continue
        if c=="'":
            previous=out[-1] if out else None
            postfix=previous and (previous.kind in ('id','num','str') or previous.v in (')',']','}',"'",'.\'')) and not ws
            if postfix:out.append(T("'",'op',i,ws));ws='';i+=1;continue
            i+=1;value=''
            while i<len(s):
                if s[i]=="'":
                    if i+1<len(s) and s[i+1]=="'":value+="'";i+=2;continue
                    i+=1;break
                value+=s[i];i+=1
            else:raise ValueError(f'unterminated string at {start}')
            out.append(T(value,'str',start,ws));ws='';continue
        m=re.match(r"(?:\d+(?:\.(?![*/\\^'])\d*)?|\.\d+)(?:[eEdD][+-]?\d+)?",s[i:])
        if m: value=m.group();out.append(T(value,'num',i,ws));i+=len(value);ws='';continue
        m=re.match(r'[A-Za-z_]\w*',s[i:])
        if m:value=m.group();out.append(T(value,'id',i,ws));i+=len(value);ws='';continue
        value=next((op for op in ('.\'', '.*','./','.\\','.^','==','~=','<=','>=','&&','||') if s.startswith(op,i)),c)
        out.append(T(value,'op',i,ws));ws='';i+=len(value)
    out.append(T('<eof>','eof',len(s),ws));return out

@dataclasses.dataclass
class F:
    name:str;args:list;outputs:list;body:list;parent:object=None;uid:int=0
    children:list=dataclasses.field(default_factory=list)
    local:set=dataclasses.field(default_factory=set)
    persistent:set=dataclasses.field(default_factory=set)
    free:set=dataclasses.field(default_factory=set)
    refs:set=dataclasses.field(default_factory=set)
    is_method:bool=False
    anonymous:bool=False

class Parser:
    def __init__(self,s,name):self.s=s;self.ts=tokenize(s);self.i=0;self.name=name;self.functions=[];self.current=None
    @property
    def t(self):return self.ts[self.i]
    def pop(self,v=None):
        t=self.t
        if v is not None and t.v!=v:self.fail(f'expected {v}, got {t.v}')
        self.i+=1;return t
    def fail(self,msg):raise ValueError(f'{self.name}:{self.s[:self.t.pos].count(chr(10))+1}: {msg}; near {self.s[max(0,self.t.pos-30):self.t.pos+70]!r}')
    def seps(self):
        while self.t.v in ('\n',';',','):self.i+=1
    def parse(self):
        self.seps()
        if self.t.v=='classdef':
            self.pop();self.pop('penta_dual');self.seps()
            while self.t.v!='<eof>':
                if self.t.v=='function':self.function(None,True)
                else:self.i+=1
        else:
            while self.t.v!='<eof>':
                self.seps()
                if self.t.v=='<eof>':break
                self.function(None)
        return self.functions
    def function(self,parent,method=False):
        self.pop('function');outputs=[]
        if self.t.v=='[':
            self.pop()
            while self.t.v!=']':
                outputs.append(self.pop().v)
                if self.t.v==',':self.pop()
            self.pop(']');self.pop('=');name=self.pop().v
        else:
            name=self.pop().v
            if self.t.v=='=':outputs=[name];self.pop();name=self.pop().v
        args=[]
        if self.t.v=='(':
            self.pop()
            while self.t.v!=')':
                args.append(self.pop().v)
                if self.t.v==',':self.pop()
            self.pop(')')
        f=F(name,args,outputs,[],parent=parent,is_method=method)
        self.functions.append(f)
        if parent:parent.children.append(f)
        old=self.current;self.current=f;self.seps();f.body=self.block({'end'});self.pop('end');self.current=old;return f
    def block(self,stop):
        out=[];self.seps()
        while self.t.v not in stop and self.t.v!='<eof>':
            out.append(self.statement());self.seps()
        return out
    def statement(self):
        v=self.t.v
        if v=='function':self.function(self.current);return ('noop',)
        if v=='if':
            self.pop();branches=[];condition=self.expr();self.seps();branches.append((condition,self.block({'elseif','else','end'})))
            while self.t.v=='elseif':
                self.pop();condition=self.expr();self.seps();branches.append((condition,self.block({'elseif','else','end'})))
            other=[]
            if self.t.v=='else':self.pop();self.seps();other=self.block({'end'})
            self.pop('end');return ('if',branches,other)
        if v in ('for','while'):
            self.pop()
            if v=='for':name=self.pop().v;self.pop('=');condition=(name,self.expr())
            else:condition=self.expr()
            self.seps();body=self.block({'end'});self.pop('end');return(v,condition,body)
        if v=='switch':
            self.pop();key=self.expr();self.seps();cases=[];other=[]
            while self.t.v=='case':
                self.pop();condition=self.expr();self.seps();cases.append((condition,self.block({'case','otherwise','end'})))
            if self.t.v=='otherwise':self.pop();self.seps();other=self.block({'end'})
            self.pop('end');return('switch',key,cases,other)
        if v=='try':
            self.pop();self.seps();body=self.block({'catch','end'});name='';other=[]
            if self.t.v=='catch':
                self.pop()
                if self.t.kind=='id':name=self.pop().v
                self.seps();other=self.block({'end'})
            self.pop('end');return('try',body,name,other)
        if v in ('return','break','continue'):self.pop();return(v,)
        if v=='persistent':
            self.pop();names=[]
            while self.t.kind=='id':names.append(self.pop().v)
            return('persistent',names)
        left=self.expr()
        if self.t.v=='=':self.pop();return('assign',left,self.expr())
        if left[0]=='id' and self.t.kind in ('id','str') and self.t.ws:
            args=[]
            while self.t.v not in (';',',','\n','<eof>'):
                args.append(('str',self.pop().v))
            return('eval',('apply',left,args,False))
        return('eval',left)
    PREC={'||':1,'&&':2,'|':3,'&':4,'==':5,'~=':5,'<':5,'>':5,'<=':5,'>=':5,':':6,'+':7,'-':7,'*':8,'/':8,'\\':8,'.*':8,'./':8,'.\\':8,'^':10,'.^':10}
    def expr(self,minimum=0,concat=False):
        token=self.pop();v=token.v
        if token.kind=='num':left=('num',v)
        elif token.kind=='str':left=('str',v)
        elif v=='~' and self.t.v in (',',']'):left=('id','~')
        elif v in ('+','-','~'):left=('unary',v,self.expr(9,concat))
        elif v=='(':
            left=self.expr();self.pop(')')
        elif v in ('[','{'):
            rows=[];row=[];closing=']' if v=='[' else '}'
            while self.t.v!=closing:
                if self.t.v in (';', '\n'):self.pop();rows.append(row);row=[];continue
                if self.t.v==',':self.pop();continue
                row.append(self.expr(concat=True))
            self.pop(closing)
            if row:rows.append(row)
            left=('matrix',rows,v=='{')
        elif v==':':left=('colon',)
        elif v=='@':
            if self.t.v=='(':
                self.pop();args=[]
                while self.t.v!=')':
                    args.append(self.pop().v)
                    if self.t.v==',':self.pop()
                self.pop(')');body=self.expr();f=F('__anonymous',args,['__result'],[('assign',('id','__result'),body)],parent=self.current,anonymous=True)
                self.functions.append(f);self.current.children.append(f);left=('lambda',f)
            else:left=('handle',self.pop().v)
        elif token.kind=='id' or v=='~':left=('id',v)
        else:self.fail(f'unexpected expression {v}')
        while True:
            t=self.t;op=t.v
            if op in ('(', '{'):
                self.pop();args=[];end=')' if op=='(' else '}'
                while self.t.v!=end:
                    args.append(self.expr())
                    if self.t.v==',':self.pop()
                    else:break
                self.pop(end);left=('apply',left,args,op=='{');continue
            if op=='.':
                self.pop()
                if self.t.v=='(':self.pop();field=self.expr();self.pop(')')
                else:field=('str',self.pop().v)
                left=('member',left,field);continue
            if op in ("'",'.\''):self.pop();left=('transpose',left,op=="'");continue
            if op not in self.PREC or self.PREC[op]<minimum:break
            if concat and op in ('+','-') and t.ws and not self.ts[self.i+1].ws:break
            self.pop();right=self.expr(self.PREC[op]+(0 if op in ('^','.^') else 1),concat)
            if op==':' and left[0]=='range':left=('range',left[1],left[3],right)
            elif op==':':left=('range',left,('num','1'),right)
            else:left=('binary',op,left,right)
        return left

def walk(node):
    if isinstance(node,F):return
    if isinstance(node,(tuple,list)):
        yield node
        for x in node:yield from walk(x)

def rootname(node):
    while node[0] in ('member','apply'):node=node[1]
    return node[1] if node[0]=='id' else None

def q(s):return json.dumps(s,ensure_ascii=True)

class Emitter:
    def __init__(self,functions):
        self.fs=functions;self.globalnames={f.name:f for f in functions if f.parent is None and not f.is_method};self.methods={f.name:f for f in functions if f.is_method};self.temp=0
        for i,f in enumerate(functions):f.uid=i
        for f in functions:
            f.local=set(f.args+f.outputs+['nargin','nargout'])-{'~'}
            for n in walk(f.body):
                if not n:continue
                if n[0]=='assign':
                    left=n[1]
                    if left[0]=='matrix':
                        for row in left[1]:
                            for x in row:
                                if (r:=rootname(x)):f.local.add(r)
                    elif (r:=rootname(left)):f.local.add(r)
                elif n[0]=='for':f.local.add(n[1][0])
                elif n[0]=='try' and n[2]:f.local.add(n[2])
                elif n[0]=='persistent':f.persistent.update(n[1]);f.local.update(n[1])
            # A nested assignment to an outer declaration is shared in MATLAB.
        for f in functions:
            identifiers={n[1] for n in walk(f.body) if n and n[0]=='id'}
            f.refs=identifiers
        self.capture={f.uid:self.freevars(f) for f in functions}
    def lexical(self,f,name):
        # Unqualified class-method calls use MATLAB's argument-class dispatch.
        # sin(a.value) must call numeric sin rather than recurse into dual sin.
        if f.is_method:return self.globalnames.get(name)
        p=f
        while p:
            if p.name==name:return p
            for child in p.children:
                if child.name==name and not child.anonymous:return child
            p=p.parent
        return self.globalnames.get(name)
    def freevars(self,f):
        outer=set();p=f.parent
        while p:outer.update(p.local);p=p.parent
        needed=set(f.refs)&outer
        # Include sibling/nested function dependencies used via calls/handles.
        pending=[f];seen=set()
        while pending:
            g=pending.pop()
            if g.uid in seen:continue
            seen.add(g.uid);needed|=(g.refs-set(g.args)-set(g.outputs)-{'nargin','nargout'})&outer
            for n in walk(g.body):
                if n and n[0]=='handle':
                    if (target:=self.lexical(g,n[1])) and target.parent is not None:pending.append(target)
                elif n and n[0]=='apply' and n[1][0]=='id':
                    if (target:=self.lexical(g,n[1][1])) and target.parent is not None:pending.append(target)
                elif n and n[0]=='lambda':pending.append(n[1])
        return sorted(needed-set(f.args)-set(f.outputs)-{'nargin','nargout'})
    def var(self,f,name):
        # Any declaration in this frame or lexical ancestors takes precedence.
        p=f
        while p:
            if name in p.local:return True
            p=p.parent
        return False
    def args(self,args,f,endctx=None,indexed=False):
        parts=[]
        for position,a in enumerate(args):
            itemctx=(endctx[0],endctx[1],position+1) if endctx and indexed else endctx
            if a[0]=='apply' and a[3]:
                base=self.expr(a[1],f,itemctx);indices=self.args(a[2],f,(base,len(a[2])),True)
                parts.append(f'expandCell({base},{indices})')
            else:parts.append('Args{'+self.expr(a,f,itemctx)+'}')
        return 'join({'+','.join(parts)+'})'
    def call(self,n,f,nout=1,endctx=None):
        if n[0]=='apply' and not n[3]:
            callee,args=n[1],n[2]
            if callee[0]=='id' and not self.var(f,callee[1]):
                target=self.lexical(f,callee[1]);arguments=self.args(args,f,endctx)
                if target:return f'run({target.uid},e,{arguments},{nout})'
                return f'call({q(callee[1])},{arguments},{nout})'
            if callee[0]=='member' and callee[1]==('id','penta_dual') and callee[2][0]=='str':
                target=self.methods[callee[2][1]];return f'run({target.uid},e,{self.args(args,f,endctx)},{nout})'
            base=self.expr(callee,f,endctx)
            return f'apply({base},{self.args(args,f,(base,len(args)),True)},{nout})'
        return 'Outs{'+self.expr(n,f,endctx)+'}'
    def expr(self,n,f,endctx=None):
        tag=n[0]
        if tag=='num':return 'V('+re.sub('[dD]','e',n[1])+('' if '.' in n[1] or 'e' in n[1].lower() else '.0')+')'
        if tag=='str':return 'V('+q(n[1])+')'
        if tag=='id':
            if n[1]=='end':
                if not endctx:raise ValueError('end outside index')
                return f'lastIndex({endctx[0]},{endctx[2] if len(endctx)>2 else 1},{endctx[1]})'
            if self.var(f,n[1]):return f'e->get({q(n[1])})'
            if (target:=self.lexical(f,n[1])):return f'first(run({target.uid},e,{{}},1))'
            return f'one({q(n[1])},{{}})'
        if tag=='colon':return 'V(":")'
        if tag=='unary':return f'unary({q(n[1])},{self.expr(n[2],f,endctx)})'
        if tag=='binary':
            a=self.expr(n[2],f,endctx);b=self.expr(n[3],f,endctx)
            if n[1] in ('&&','||'):return f'V(truth({a}) {n[1]} truth({b}))'
            return f'binary({q(n[1])},{a},{b})'
        if tag=='range':return f'range({self.expr(n[1],f,endctx)},{self.expr(n[2],f,endctx)},{self.expr(n[3],f,endctx)})'
        if tag=='member':return f'member({self.expr(n[1],f,endctx)},{self.expr(n[2],f,endctx)}.text())'
        if tag=='transpose':return f'one({q("ctranspose" if n[2] else "transpose")},{{{self.expr(n[1],f,endctx)}}})'
        if tag=='apply':
            if n[3]:
                base=self.expr(n[1],f,endctx);return f'index({base},{self.indexargs(n[2],f,base)},true)'
            return f'first({self.call(n,f,1,endctx)})'
        if tag=='matrix':
            return 'matrix({'+','.join(self.args(row,f,endctx) for row in n[1])+'},'+str(n[2]).lower()+')'
        if tag=='handle':
            target=self.lexical(f,n[1])
            return f'handle({target.uid},e)' if target else f'one("str2func",{{V({q(n[1])})}})'
        if tag=='lambda':return f'handle({n[1].uid},e,true)'
        raise ValueError(f'unsupported expression {tag}')
    def indexargs(self,args,f,base):
        parts=[]
        for i,a in enumerate(args):parts.append(self.expr(a,f,(base,len(args),i+1)))
        return 'Args{'+','.join(parts)+'}'
    def assign(self,left,right,f):
        tag=left[0]
        if tag=='id':return f'e->put({q(left[1])},{right});' if left[1]!='~' else ''
        if tag=='member':
            value=f'setmember({self.expr(left[1],f)},{self.expr(left[2],f)}.text(),{right})'
            return self.assign(left[1],value,f)
        if tag=='apply':
            base=self.expr(left[1],f);value=f'assign({base},{self.indexargs(left[2],f,base)},{right},{str(left[3]).lower()})'
            return self.assign(left[1],value,f)
        raise ValueError(f'unsupported assignment {left}')
    def statements(self,body,f,indent='    '):
        lines=[]
        def add(s):lines.append(indent+s)
        for n in body:
            tag=n[0]
            if tag in ('noop','persistent'):continue
            if tag=='assign':
                left,right=n[1:]
                if left[0]=='matrix':
                    targets=[a for row in left[1] for a in row];self.temp+=1;tmp=f'o{self.temp}';add(f'auto {tmp}={self.call(right,f,len(targets))};')
                    for i,target in enumerate(targets):add(self.assign(target,f'output({tmp},{i})',f))
                else:add(self.assign(left,self.expr(right,f),f))
            elif tag=='eval':add('(void)'+self.call(n[1],f,0)+';')
            elif tag=='if':
                for i,(condition,child) in enumerate(n[1]):
                    add(('if' if i==0 else 'else if')+f' (truth({self.expr(condition,f)})) {{');lines+=self.statements(child,f,indent+'    ');add('}')
                if n[2]:add('else {');lines+=self.statements(n[2],f,indent+'    ');add('}')
            elif tag=='while':add(f'while (truth({self.expr(n[1],f)})) {{');lines+=self.statements(n[2],f,indent+'    ');add('}')
            elif tag=='for':
                self.temp+=1;tmp=f'loop{self.temp}';add(f'for (const V& {tmp}: columns({self.expr(n[1][1],f)})) {{');add('    '+self.assign(('id',n[1][0]),tmp,f));lines+=self.statements(n[2],f,indent+'    ');add('}')
            elif tag=='switch':
                self.temp+=1;tmp=f'switch{self.temp}';add('{');add(f'    V {tmp}={self.expr(n[1],f)};')
                for i,(condition,child) in enumerate(n[2]):
                    add('    '+('if' if i==0 else 'else if')+f' (matches({tmp},{self.expr(condition,f)})) {{');lines+=self.statements(child,f,indent+'        ');add('    }')
                if n[3]:add('    else {');lines+=self.statements(n[3],f,indent+'        ');add('    }')
                add('}')
            elif tag=='try':
                add('try {');lines+=self.statements(n[1],f,indent+'    ');add('} catch (const MatlabError& failure) {')
                if n[2]:add('    '+self.assign(('id',n[2]),'failure.exception',f))
                lines+=self.statements(n[3],f,indent+'    ');add('}')
            elif tag=='return':add('return results(e,outputs);')
            elif tag in ('break','continue'):add(tag+';')
            else:raise ValueError(tag)
        return lines
    def emit(self):
        header=['// Generated C++ adapter implementation; regenerate from the public export runtime.','#include "compiled_runtime.hpp"','namespace pm {']
        for f in self.fs:header.append(f'Outs f{f.uid}(EnvPtr e,const Args& a,int requested);')
        for f in self.fs:
            header+=['',f'Outs f{f.uid}(EnvPtr e,const Args& a,int requested) {{']
            header.append('    const std::vector<std::string> outputs{'+','.join(q(s) for s in f.outputs)+'};')
            if f.is_method and f.name=='penta_dual':header.append('    e->put("a",one("struct",{V("value"),V(),V("tangent"),V(),V("second"),V()}));')
            header+=self.statements(f.body,f)
            header+=['    return results(e,outputs);','}']
        header+=['','const FunctionDef definitions[]={']
        for f in self.fs:
            header.append('    {'+str(f.uid)+',f'+str(f.uid)+',{'+','.join(q(s) for s in f.args)+'},{'+','.join(q(s) for s in sorted(f.local))+'},{'+','.join(q(s) for s in self.capture[f.uid])+'},{'+','.join(q(s) for s in sorted(f.persistent))+'}},')
        header+=['};',f'const size_t definitionCount={len(self.fs)};','}']
        return '\n'.join(header)+'\n'

def main():
    names=['matlab_runtime','matlab_scientific','matlab_optimize','matlab_graph','matlab_pde','matlab_geometry','matlab_mechanics','matlab_advanced_ode','matlab_hybrid','matlab_reverse','matlab_dual']
    functions=[];sources=[]
    for name in names:
        p=SOURCE/(name+'.inc');raw=p.read_text(encoding='utf-8');match=re.match(r'R"(\w+)\((.*)\)\1"\s*$',raw,re.S)
        if not match:raise ValueError(p)
        fs=Parser(match[2],name).parse();functions+=fs;sources.append({'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'functions':len(fs)})
    emitter=Emitter(functions);private=HERE/'private';private.mkdir(parents=True,exist_ok=True)
    (private/'compiled_adapters.cpp').write_text(emitter.emit(),encoding='utf-8')
    (HERE/'transpilation.json').write_text(json.dumps({'sources':sources,'functions':len(functions),'top_level':{f.name:f.uid for f in functions if f.parent is None and not f.is_method},'methods':{f.name:f.uid for f in functions if f.is_method},'functions_detail':[{'id':f.uid,'name':f.name,'parent':f.parent.uid if f.parent else None,'args':f.args,'outputs':f.outputs,'locals':sorted(f.local),'captures':emitter.capture[f.uid]} for f in functions]},indent=2),encoding='utf-8')
    print(f'Compiled adapter AST: {len(functions)} C++ functions, {len(sources)} immutable source inputs')

if __name__=='__main__':main()
