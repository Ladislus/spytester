#include "../../SpiedProgram.h"
#include "TestLib.h"

#include <iostream>
#include <cstdlib>

#include <unistd.h>


static SpiedThread* lastCreatedThread;

int main(int argc, char* argv[], char* envp[])
{
    if(argc<2)
    {
        std::cerr << argv[0] <<": expected at least 1 argument (program name)"<< std::endl;
        std::exit(1);
    }

    try{
        SpiedProgram prog(argv[1], argc-1, argv[1], envp[0]);

        prog.setOnThreadStart([](SpiedThread& sp) {
            lastCreatedThread = &sp;
        });

        BreakPoint* bp = prog.createBreakPoint("TestFunction2");
        if(bp != nullptr)
        {
            bp->set();
            bp->setOnHitCallback([](BreakPoint& bp, SpiedThread& sp){bp.resumeAndSet(sp);});
        }

        prog.start();

        sleep(1);

        auto f = prog.createWrappedFunction("TestProgram", testLibFunction);
        f->set([](int a){
            std::cout<< "HELLO!" <<std::endl;
            testLibFunction(a);
            return a+2;
        });

        prog.resume();

        sleep(3);

        prog.stop();

        if(lastCreatedThread != nullptr)
        {
            WatchPoint* wp = lastCreatedThread->createWatchPoint();
            wp->setOnHit([](WatchPoint& wp, SpiedThread& sp){
                            sp.resume();
                         });
            wp->set((void*)&b, WatchPoint::READ_WRITE, WatchPoint::_4BYTES);
        }

        prog.resume();

        sleep(10);

        prog.terminate();
    }
    catch(const std::invalid_argument& e){
        sleep(10);
        std::cerr << "SpiedProgram failed : " << e.what() << std::endl;
        std::exit(1);
    }

}