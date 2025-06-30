if lc_options.toy_c_backend then
    target("lc-backend-toy-c")
    add_deps("lc-core")
    target_end()
    if not is_mode("debug") then
        target("script_compiler")
        set_policy("build.fence", true)
        add_rules("lc_basic_settings", {
            project_kind = "binary"
        })
        add_files("*.cpp")
        add_deps("lc-clangcxx", "lc-runtime", "lc-vstl", "lc-ast", "mimalloc")
        add_deps("lc-backend-toy-c", {
            inherit = false
        })
        target_end()
    else
        target("script_compiler")
        set_kind("phony")
        add_deps("lc-backend-toy-c", {
            inherit = false
        })
        target_end()
    end

    rule('compile_clang_script')
    set_extensions('.lua')
    on_clean(function(target)
        local out_dir = target:extraconf("rules", "compile_clang_script", "out_dir")
        out_dir = path.join(target:targetdir(), out_dir)
        if os.exists(out_dir) then
            os.rm(out_dir)
        end
    end)
    before_buildcmd_file(function(target, batchcmds, sourcefile, opt)
        local out_dir = target:extraconf("rules", "compile_clang_script", "out_dir")
        out_dir = path.join(path.absolute(target:targetdir()), out_dir)
        local mod = import(path.basename(sourcefile), {
            rootdir = path.directory(sourcefile)
        })
        local compiler = path.join(path.directory(target:targetdir()), "release/script_compiler")
        if is_host("windows") then
            compiler = compiler .. ".exe";
        end
        os.vrunv(compiler,
            {'--in=' .. mod.in_dir(), '--backend=toy-c', '--out=' .. out_dir, '--include=' .. mod.include_dir()})
    end)

    on_buildcmd_file(function(target, batchcmds, sourcefile, opt)
        local out_dir = target:extraconf("rules", "compile_clang_script", "out_dir")
        out_dir = path.join(target:targetdir(), out_dir)
        local compile_c = import("compile_c", {
            rootdir = out_dir
        })
        local link_files, compile_files = compile_c.files()
        for _, out_file in ipairs(link_files) do
            local objectfile = target:objectfile(out_file)
            table.insert(target:objectfiles(), objectfile)
        end
        for _, file_idx in ipairs(compile_files) do
            local out_file = link_files[file_idx]
            local objectfile = target:objectfile(out_file)
            batchcmds:compile(out_file, objectfile)
            batchcmds:show('compiling ' .. path.filename(out_file))
        end
    end)
    rule_end()
end
