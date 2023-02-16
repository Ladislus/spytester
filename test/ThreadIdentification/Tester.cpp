#include <iostream>
#include "../../include/SpiedProgram.h"
#include "TestedLib.h"
#include <unistd.h>

int main(int argc, char* argv[], char* envp[])
{
    if(argc < 1){
        std::cerr << argv[0] << " : at least 1 argument expected" << std::endl;
    }

    try{
        SpiedProgram sp(argv[1]);

        sp.setThreadCreationCallback([](SpiedThread& sp){
            sp.resume();
        });

        std::cerr << "START" << std::endl;
        sp.start();

        sleep(1);

        sp.relink("libThreadIdLib.so");

        BreakPoint* bp1 = sp.createBreakPoint((void*)&main1, "BreakPoint1");
        BreakPoint* bp3 = sp.createBreakPoint((void*)&main3, "BreakPoint3");
        BreakPoint* bp4 = sp.createBreakPoint((void*)&main4, "BreakPoint4");

        bp1->set();
        bp3->set();
        bp4->set();

        SpiedThread* st1 = nullptr;
        SpiedThread* st3 = nullptr;
        SpiedThread* st4 = nullptr;

        auto idThread = []( SpiedThread*& psp){
            return [&psp]( BreakPoint& bp, SpiedThread& sp) {
                std::cout << "BREAKPOINT" << std::endl;
                psp = &sp;
                bp.resumeAndUnset(sp);
            };
        };

        bp1->setOnHitCallback(idThread(st1));
        bp3->setOnHitCallback(idThread(st3));
        bp4->setOnHitCallback(idThread(st4));

        sp.resume();
        sleep(1);
        st1->resume();
        sleep(1);
        st1->resume();
        sleep(5);

        std::cerr << "STOP" << std::endl;
        sp.stop();
        sleep(3);

        std::cerr << "RESUME" << std::endl;
        sp.resume();
        sleep(3);

        std::cerr << "TERMINATE" << std::endl;
        sp.terminate();

    }
    catch(const std::invalid_argument& e){
        std::cerr << "SpiedProgram failed : " << e.what() << std::endl;
    }


    return 0;
}