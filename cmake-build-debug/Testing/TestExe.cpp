#include "../../SpiedProgram.h"
#include "TestLib.h"

#include <iostream>
#include <cstdlib>

#include <unistd.h>

int main(int argc, char* argv[], char* envp[])
{
    if(argc<2)
    {
        std::cerr << argv[0] <<": expected at least 1 argument (program name)"<< std::endl;
        std::exit(1);
    }

    try{
        SpiedProgram prog(argv[1], argc-1, argv[1], envp[0]);

        BreakPoint* bp = prog.createBreakPoint("TestFunction2");
        if(bp != nullptr)
        {
            bp->set();
            bp->setOnHitCallback([](BreakPoint& bp, SpiedThread& sp){bp.resumeAndSet(sp);});
        }

        sleep(1);

        prog.run();

        sleep(1);

        auto f = prog.createWrappedFunction("TestProgram", testLibFunction);
        f->set([](int a){std::cout<< "HELLO!" <<std::endl; return a+2;});

        prog.run();

        sleep(5);

        prog.terminate();

        sleep(100);
    }
    catch(const std::invalid_argument& e){
        sleep(10);
        std::cerr << "SpiedProgram failed : " << e.what() << std::endl;
        std::exit(1);
    }

}