#include <luisa/backends/ext/toy_c_ext.h>
#include <luisa/runtime/context.h>
#include <luisa/runtime/device.h>
#include <luisa/runtime/stream.h>
#include <luisa/runtime/shader.h>
#include <luisa/core/fiber.h>
#include <luisa/core/logging.h>
using namespace luisa;
using namespace luisa::compute;
struct ToyCDeviceConfigImpl : public luisa::compute::ToyCDeviceConfig {
    luisa::string dynamic_module_name() const override
    {
        return "d6_scripts";
    }
};

int host_func(int a, int b)
{
    LUISA_INFO("Call from host {}, {}", a, b);
    return a + b;
}

struct FuncRef {
    uint64_t ptr;
};
struct HostBuffer {
    uint64_t ptr;
    uint64_t len;
};

int main(int argc, char* argv[])
{
    fiber::scheduler tpool;
    Context          context{ argv[0] };
    DeviceConfig     config{
            .extension = luisa::make_unique<ToyCDeviceConfigImpl>()
    };
    auto device               = context.create_device("toy-c", &config);
    auto test_fib_shader      = device.load_shader<1, int>("test_fib");
    auto test_atomic          = device.load_shader<1, HostBuffer>("test_atomic");
    auto test_func_ptr_shader = device.load_shader<1, FuncRef>("test_func_ptr");
    uint atomic_value         = 114514;

    auto stream = device.create_stream();
    stream.set_log_callback([](auto&& str) {
        LUISA_INFO("scripts log: {}", str);
    });

    stream << test_fib_shader(9).dispatch(1)
           << test_atomic(HostBuffer{ reinterpret_cast<uint64_t>(&atomic_value), sizeof(atomic_value) }).dispatch(1)
           << test_func_ptr_shader(FuncRef{ reinterpret_cast<uint64_t>(&host_func) }).dispatch(1);
    return 0;
}