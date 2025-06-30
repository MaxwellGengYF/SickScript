target("d6_scripts")
add_rules("lc_basic_settings", {
    project_kind = "shared"
})
add_rules("compile_clang_script", {out_dir = "_temp_c"})
add_deps("script_compiler", {inherit = false})
add_files("desc.lua")
add_files("builtin/lib.c")
add_includedirs("builtin")
target_end()