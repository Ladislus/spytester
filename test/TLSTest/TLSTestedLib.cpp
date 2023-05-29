#include "TLSTestedLib.h"
#include <iostream>

__thread int tab[50] {1,2,3,4,5,6,7,8,9,10};

void update(int idx, int val){
    if(idx < 50){
        tab[idx] = val;
    }
}

void print(int idx){
    std::cout << "tab[" << idx << "] = "<< tab[idx] << std::endl;
}