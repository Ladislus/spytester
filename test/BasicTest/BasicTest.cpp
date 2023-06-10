#include "../../include/SpiedProgram.h"
#include "TestLib.h"

#include <iostream>
#include <cstdlib>

#include <unistd.h>


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
        SpiedThread* mainThread = nullptr;
        SpiedThread* lastCreatedThread = nullptr;

        prog.setThreadCreationCallback([&mainThread, &lastCreatedThread](SpiedThread& spiedThread){
            lastCreatedThread = &spiedThread;
            if(mainThread == nullptr){
                mainThread = &spiedThread;
            }
            spiedThread.resume();
        });

        prog.start();

        std::cout << "Program started" << std::endl;
        sleep(1);

        prog.relink("libTestLib.so");

        std::cout << "TestLibFunction2 = " << (void*)&TestLibFunction2 << std::endl;

        BreakPoint* bp = prog.createBreakPoint((void*)&TestLibFunction2, "TestFunction2");
        if(bp != nullptr)
        {
            bp->set();
            bp->setOnHitCallback([](BreakPoint& bp, SpiedThread& sp){bp.resumeAndSet(sp);});
        }
        mainThread->resume();

        sleep(5);

        prog.stop();

        auto f = prog.wrapFunction<testLibFunction>("TestProgram");
        f->setWrapper([](int a){
            std::cout<< "HELLO!" <<std::endl;
            return a+2;
        });
        f->wrapping(true);

        prog.resume();
        sleep(5);
        prog.stop();

        f->wrapping(false);

        WatchPoint* wp = lastCreatedThread->createWatchPoint();
        wp->setOnHit([](WatchPoint& wp, SpiedThread& sp){
            std::cout << "Watchpoint hit" << std::endl;
            sp.resume();
        });
        wp->set((void*)&b, WatchPoint::READ_WRITE, WatchPoint::_4BYTES);

        prog.resume();
        sleep(5);

        prog.terminate();
    }
    catch(const std::invalid_argument& e){
        std::cerr << "SpiedProgram failed : " << e.what() << std::endl;
        std::exit(1);
    }

}