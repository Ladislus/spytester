#include "TestLib.h"
#include <iostream>

volatile int b = 87;

int TestLibFunction2()
{
    return 17;
}

int testLibFunction(int a)
{
    std::cout << __FUNCTION__ <<" : HOLA! b = "<< b << std::endl;
    return a+1;
}