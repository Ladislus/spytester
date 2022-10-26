//
// Created by baptiste on 25/09/22.
//

#include "TestLib.h"
#include <iostream>

volatile int b = 87;

int testLibFunction(int a)
{
    std::cout << __FUNCTION__ <<" : HOLA! b = "<< b << std::endl;
    return a+1;
}