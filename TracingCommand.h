//
// Created by baptiste on 17/09/22.
//

#ifndef SPYTESTER_TRACINGCOMMAND_H
#define SPYTESTER_TRACINGCOMMAND_H


#include <queue>
#include <functional>
#include <iostream>

class Command
{
public:
    virtual void execute() = 0;
    virtual ~Command() = default;
};

template <typename T, typename ... TArgs> class TracingCommand : public Command
{
    void(T::*_method)(TArgs ...);
    std::tuple<T&, TArgs ...> _args;

    void execute() override
    {
        std::apply(std::mem_fn(_method), _args);
    }

public:
    TracingCommand(T& obj, void(T::*method)(TArgs ...), TArgs ... args)
    : _method(method), _args(obj, args ...){}
};


#endif //SPYTESTER_TRACINGCOMMAND_H
