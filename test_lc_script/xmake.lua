includes("scripts")
target("test_scripts")
add_rules("lc_basic_settings", {
    project_kind = "binary"
})
add_deps("lc-runtime")
add_files("src/*.cpp")
add_deps("d6_scripts", {
    inherit = false
})
target_end()
