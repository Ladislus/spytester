//
// Created by baptiste on 17/09/22.
//

#ifndef SPYTESTER_TRACINGCOMMAND_H
#define SPYTESTER_TRACINGCOMMAND_H


#include <queue>

class Command
{
public:
    virtual void execute() = 0;
};

template <typename T> class TracingCommand : public Command
{
    T& _obj;
    void(T::*_method)();

    virtual void execute()
    {
        (_obj.*_method)();
    }

public:
    TracingCommand(T& obj, void(T::*method)())
    : _obj(obj), _method(method){}
};

#endif //SPYTESTER_TRACINGCOMMAND_H
