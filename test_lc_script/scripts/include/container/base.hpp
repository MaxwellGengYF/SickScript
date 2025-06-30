#pragma once
#include <luisa/type_traits/concepts.hpp>
trait_struct MemoryType
{
    static constexpr uint32 Persist = 0;
    static constexpr uint32 Temp = 1;
};