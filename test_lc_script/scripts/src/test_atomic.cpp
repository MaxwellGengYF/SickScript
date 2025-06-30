#include <luisa/std.hpp>
#include <luisa/printer.hpp>
using namespace luisa::shader;
void logic(Buffer<uint32>& buf)
{
    auto   v        = atomic_add(buf[0], 1u);
    uint32 expected = 0;
    v               = atomic_compare_exchange<uint32>(buf[0], expected, 1);
    v               = atomic_exchange<uint32>(buf[0], 1);
    v               = atomic_sub<uint32>(buf[0], 1);
    v               = atomic_and<uint32>(buf[0], 1);
    v               = atomic_or<uint32>(buf[0], 1);
    v               = atomic_xor<uint32>(buf[0], dispatch_id().x);
}


[[kernel_1d(1)]] int kernel(Buffer<uint32>& buf)
{
    device_log("{}", buf[0]);
    logic(buf);
}