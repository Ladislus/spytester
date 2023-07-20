#ifndef SPYTESTER_META_H
#define SPYTESTER_META_H


#include <type_traits>

template <auto Start, auto End, auto Inc, class F>
constexpr void constexpr_for(F&& f)
{
    if constexpr (Start < End)
    {
        f(std::integral_constant<decltype(Start), Start>());
        constexpr_for<Start + Inc, End, Inc>(f);
    }
}


#endif //SPYTESTER_META_H
