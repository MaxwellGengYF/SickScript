-- windows flags
if (is_os("windows")) then
    add_defines("UNICODE", "_UNICODE", "NOMINMAX", "_WINDOWS")
    add_defines("_GAMING_DESKTOP")
    add_defines("_CRT_SECURE_NO_WARNINGS")
    add_defines("_ENABLE_EXTENDED_ALIGNED_STORAGE")
    add_defines("_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR") -- for preventing std::mutex crash when lock
    if (is_mode("release")) then
        set_runtimes("MD")
    elseif (is_mode("asan")) then
        add_defines("_DISABLE_VECTOR_ANNOTATION")
        -- else
        --     set_runtimes("MDd")
    end
end

lc_options = {
    cpu_backend = false,
    cuda_backend = false,
    dx_backend = false,
    enable_mimalloc = true,
    enable_cuda = false,
    enable_custom_malloc = false,
    enable_api = false,
    enable_clangcxx = true,
    enable_dsl = false,
    enable_gui = false,
    enable_osl = false,
    enable_ir = false,
    enable_tests = false,
    external_marl = false,
    lc_backend_lto = false,
    sdk_dir = os.projectdir(),
    vk_support = false,
    metal_backend = false,
    dx_cuda_interop = false,
    toy_c_backend = true
}

includes("LuisaCompute", "test_lc_script", "script_compiler")
