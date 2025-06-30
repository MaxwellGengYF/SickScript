#pragma once
#include "types.hpp"
#include "functions/custom.hpp"
// TODO
namespace luisa::shader
{
template <typename T>
struct Ref {
    uint64 _ptr;
    Ref()
    {
        _ptr = 0;
    }
    Ref(uint64 ptr)
    {
        _ptr = ptr;
    }
    template <typename Dst>
    Ref<Dst> cast_to()
    {
        return Ref<Dst>(_ptr);
    }
    [[access]] T& get();
    [[access]] T& operator*();
};
template <typename T>
struct Buffer {
    uint64 _ptr;
    uint64 _len;
    Buffer()
    {
        _ptr = 0;
        _len = 0;
    }
    Buffer(uint64 ptr, uint64 len)
    {
        _ptr = ptr;
        _len = len;
    }
    template <typename Dst>
    Buffer<Dst> cast_to()
    {
        return Buffer<Dst>(_ptr, _len * sizeof(T) / sizeof(Dst));
    }
    [[access]] T& operator[](uint64 index);
    Buffer(Buffer const&) = delete;
    Buffer(Buffer&&) = delete;
    Buffer& operator=(Buffer const&) = delete;
    Buffer& operator=(Buffer&&) = delete;
};
struct StringView {
    [[clang::annotate("luisa-shader", "strview_ptr")]] uint64 _ptr;
    uint64 _len;
    StringView()
    {
        _ptr = 0;
        _len = 0;
    }
    StringView(uint64 ptr, uint64 len)
    {
        _ptr = ptr;
        _len = len;
    }
    [[access]] int8& operator[](uint64 index);
};
template <typename FuncType, bool is_invocable = true>
trait_struct FunctionRef;
template <typename FuncType, bool is_invocable = true>
trait_struct FunctorRef;
template <typename Ret, typename... Args>
struct FunctionRef<Ret(Args...), true> {
    uint64 ptr;
    FunctionRef(uint64 value = 0)
        : ptr(value)
    {
    }
    Ret operator()(Args... args)
    {
        return luisa::shader::detail::__lc_builtin_invoke__<Ret>(ptr, args...);
    }
};
#define BIND_FUNCTION(FUNCTYPE, x) luisa::shader::FunctionRef<FUNCTYPE, luisa::shader::invocable_v<decltype(x), FUNCTYPE>>((uint64)(&(x)))
template <typename T>
Ref<T> temp_new()
{
    return Ref<T>(temp_malloc(sizeof(T)));
}
template <typename T>
Buffer<T> temp_new_buffer(uint64 size)
{
    return Buffer<T>(temp_malloc(sizeof(T) * size), size);
}
template <typename T>
Buffer<T> persist_new_buffer(uint64 size)
{
    return Buffer<T>(persist_malloc(sizeof(T) * size), size);
}
template <typename T>
Ref<T> persist_new()
{
    return Ref<T>(persist_malloc(sizeof(T)));
}
template <typename T>
void persist_delete(Ref<T> ref)
{
    persist_delete<T>(ref._ptr);
}
template <typename T>
void persist_delete(Buffer<T> ref)
{
    persist_delete<T>(ref._ptr);
}
template <concepts::string_literal Str>
[[ext_call("to_string")]] StringView to_strview(Str&& fmt);

} // namespace luisa::shader