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
        std::cerr << "INIT" << std::endl;

        SpiedProgram sp(argv[1]);

        sp.setOnThreadStart([](SpiedThread& sp){
            sp.resume();
        });

        sp.relink("libThreadIdLib.so");

        BreakPoint* bp1 = sp.createBreakPoint((void*)&main1);
        BreakPoint* bp3 = sp.createBreakPoint((void*)&main3);
        BreakPoint* bp4 = sp.createBreakPoint((void*)&main4);

        bp1->set();
        bp3->set();
        bp4->set();

        SpiedThread* sp1 = nullptr;
        SpiedThread* sp3 = nullptr;
        SpiedThread* sp4 = nullptr;

        auto idThread = []( SpiedThread*& psp){
            return [&psp]( BreakPoint& bp, SpiedThread& sp) {
                std::cout << "BREAKPOINT" << std::endl;
                psp = &sp;
                bp.resumeAndUnset(sp);
            };
        };

        bp1->setOnHitCallback(idThread(sp1));
        bp3->setOnHitCallback(idThread(sp3));
        bp4->setOnHitCallback(idThread(sp4));

        std::cerr << "START" << std::endl;
        sp.start();
        sleep(3);

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