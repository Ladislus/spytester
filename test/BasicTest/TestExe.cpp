#include "../../include/SpiedProgram.h"
#include "TestLib.h"

#include <iostream>
#include <cstdlib>

#include <unistd.h>


static SpiedThread* lastCreatedThread;

int main(int argc, char* argv[], char* envp[])
{
    std::cout << "testLibFunction = " << (void*)&testLibFunction << std::endl;
    if(argc<2)
    {
        std::cerr << argv[0] <<": expected at least 1 argument (program name)"<< std::endl;
        std::exit(1);
    }

    try{
        std::string holi("holi");

        SpiedProgram prog(argv[1], "hola", holi);

        prog.relink("libTestLib.so");
        std::cout << "testLibFunction = " << (void*)&testLibFunction << std::endl;

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

        auto f = prog.wrapFunction<testLibFunction>("TestProgram");
        f->setWrapper([](int a){
            std::cout<< "HELLO!" <<std::endl;
            testLibFunction(a);
            return a+2;
        });
        f->wrapping(true);

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

        f->wrapping(false);

        prog.resume();

        sleep(10);

        prog.terminate();
    }
    catch(const std::invalid_argument& e){
        std::cerr << "SpiedProgram failed : " << e.what() << std::endl;
        std::exit(1);
    }

}