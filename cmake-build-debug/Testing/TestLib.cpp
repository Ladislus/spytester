//
// Created by baptiste on 25/09/22.
//

#include "TestLib.h"
#include <iostream>

int testLibFunction(int a)
{
    std::cout << __FUNCTION__ <<" : HOLA!"<< std::endl;
    return a+1;
}