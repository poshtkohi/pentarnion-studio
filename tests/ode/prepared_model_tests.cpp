#include "ode/native_ode.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <iostream>
int main(){
    int checks=0,failures=0;auto require=[&](bool value,const char* message){++checks;if(!value){++failures;std::cerr<<message<<'\n';}};
    try{
        const auto source=penta::language::analyse("ode decay(t,pentarnion p;real rate){p'=-rate*p;}");
        require(source.ok(),"Prepared source must check.");
        const auto& definition=*source.program.statements.front().ode;
        penta::ode::NativeOptions native;native.cacheDirectory=std::filesystem::temp_directory_path()/
            ("penta-prepared-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::size_t compiles=0;native.compilationFinished=[&](double){++compiles;};
        const penta::ode::PreparedModel model(definition,native);
        require(model.coordinateCount()==5&&model.parameterCount()==1,"Prepared dimensions.");
        require(compiles==1,"Cold preparation compiles once.");
        penta::ode::SolveOptions settings;settings.t1=1;settings.sampleStep=.25;settings.rtol=1e-11;settings.atol=1e-13;settings.denseOutput=false;
        std::vector<std::future<bool>> workers;
        for(int task=0;task<8;++task)workers.push_back(std::async(std::launch::async,[&,task]{
            const double rate=.25+.1*task;const std::vector<double> initial{double(task+1),1,-2,.5,.25};
            const auto solution=model.solve(double(task)/8,initial,{rate},settings);
            if(solution->values.size()!=25||solution->stats.key!=model.key())return false;
            for(std::size_t k=0;k<5;++k)if(std::abs(solution->values[20+k]-initial[k]*std::exp(-rate))>1e-8)return false;
            return true;
        }));
        for(auto& worker:workers)require(worker.get(),"Shared immutable kernel concurrent solve.");
        require(compiles==1,"Changed parameters/gamma/initials never recompile.");
        const auto value=model.evaluate(.5,0,{1,2,3,4,5},{2});
        require(value.rhs==std::vector<double>({-2,-4,-6,-8,-10}),"Prepared RHS.");
        for(std::size_t row=0;row<5;++row)for(std::size_t col=0;col<5;++col)
            require(value.jacobian[row*5+col]==(row==col?-2:0),"Prepared analytic Jacobian.");
        penta::ode::NativeOptions stopped;stopped.cancellation=std::make_shared<std::atomic_bool>(true);
        bool cancelled=false;try{(void)model.solve(.5,{1,0,0,0,0},{2},settings,stopped);}catch(const penta::ode::OdeError&){cancelled=true;}
        require(cancelled,"Per-run cancellation.");
        auto copy=model;require(copy.key()==model.key(),"Shared ownership.");
        std::size_t charged=0;penta::ode::NativeOptions budget;budget.chargeOperations=[&](std::size_t n){charged+=n;};
        (void)copy.solve(.5,{1,0,0,0,0},{2},settings,budget);require(charged>0,"Native quota hook.");
    }catch(const std::exception& e){++failures;std::cerr<<e.what()<<'\n';}
    std::cout<<checks<<" prepared native ODE checks, "<<failures<<" failures\n";return failures?1:0;
}
