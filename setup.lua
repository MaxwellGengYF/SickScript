import("core.base.scheduler")
import("private.async.jobpool")
import("async.runjobs")

local function is_empty_folder(dir)
    if os.exists(dir) and not os.isfile(dir) then
        for _, v in ipairs(os.filedirs(path.join(dir, '*'))) do
            return false
        end
        return true
    else
        return true
    end
end
local function git_clone_or_pull(git_address, subdir, branch)
    local args
    if is_empty_folder(subdir) then
        args = {'clone', git_address}
        if branch then
            table.insert(args, '-b')
            table.insert(args, branch)
        end
        table.insert(args, path.translate(subdir))
    else
        args = {'-C', subdir, 'pull'}
    end
    local done = false
    print("pulling " .. git_address)
    for i = 1, 4, 1 do
        try {function()
            os.execv('git', args)
            done = true
        end}
        if done then
            return
        end
    end
    utils.error("git clone error.")
    os.exit(1)
end

function install_lc(llvm_path)

    local lc_path = path.absolute("LuisaCompute")
    ------------------------------ git ------------------------------
    print("Clone LuisaCompute? (y/n)")
    local clone_lc = io.read()
    if clone_lc == 'Y' or clone_lc == 'y' then
        print("git...")
        git_clone_or_pull("https://github.com/LuisaGroup/LuisaCompute.git", lc_path, "next")
        local jobs = jobpool.new()
        local function add_git_job(subdir, git_address, branch, dict)
            return jobs:addjob(git_address, function(index, total, opt)
                git_clone_or_pull(git_address, subdir, branch)
            end, dict)
        end

        -- add_git_job("https://github.com/MaxwellGengYF/cgltf.git", "thirdparty/cgltf")
        add_git_job("LuisaCompute/src/ext/xxhash", "https://github.com/Cyan4973/xxHash.git")
        add_git_job("LuisaCompute/src/ext/spdlog", "https://github.com/LuisaGroup/spdlog.git")
        local eastl_package_job = add_git_job("LuisaCompute/src/ext/EASTL/packages/EABase",
            "https://github.com/LuisaGroup/EABase.git")
        add_git_job("LuisaCompute/src/ext/EASTL", "https://github.com/LuisaGroup/EASTL.git", nil, {
            rootjob = eastl_package_job
        })
        add_git_job("LuisaCompute/src/ext/glfw", "https://github.com/glfw/glfw.git")
        add_git_job("LuisaCompute/src/ext/magic_enum", "https://github.com/Neargye/magic_enum")
        add_git_job("LuisaCompute/src/ext/reproc", "https://github.com/LuisaGroup/reproc.git")
        add_git_job("LuisaCompute/src/ext/pybind11", "https://github.com/LuisaGroup/pybind11.git")
        add_git_job("LuisaCompute/src/ext/stb/stb", "https://github.com/nothings/stb.git")
        add_git_job("LuisaCompute/src/ext/marl", "https://github.com/LuisaGroup/marl.git")
        runjobs("git", jobs, {
            comax = 1000,
            timeout = -1,
            timer = function(running_jobs_indices)
                utils.error("git timeout.")
            end
        })
    end
    if is_empty_folder(lc_path) then
        utils.error("LuisaCompute not installed.")
        os.exit(1)
    end
    ------------------------------ llvm ------------------------------
    local builddir
    if os.is_host("windows") then
        builddir = path.absolute("build/windows/x64/release")
    elseif os.is_host("macosx") then
        builddir = path.absolute("build/macosx/x64/release")
    end
    local lib = import("lib", {
        rootdir = "LuisaCompute/scripts"
    })
    if llvm_path then
        if not llvm_path or type(llvm_path) ~= "string" then
            utils.error("Bad argument, should be 'xmake l setup.lua #LLVM_PATH#'")
            os.exit(1)
        elseif not os.isdir(llvm_path) then
            utils.error("LLVM path illegal")
            os.exit(1)
        end

        print("copying llvm...")

        local llvm_code_path = path.absolute("src/clangcxx/llvm", lc_path)
        os.tryrm(path.join(llvm_code_path, "include"))
        os.tryrm(path.join(llvm_code_path, "lib"))
        os.cp(path.join(llvm_path, "include"), path.join(llvm_code_path, "include"))
        os.cp(path.join(llvm_path, "lib"), path.join(llvm_code_path, "lib"))
        if builddir then
            lib.mkdirs(builddir)
            if os.is_host("windows") then
                os.cp(path.join(llvm_path, "bin", "clang.exe"), builddir)
            elseif os.is_host("macosx") then
                os.cp(path.join(llvm_path, "bin", "clang"), builddir)
            end
        else
            utils.error("build dir not set.")
            os.exit(1)
        end
    end
end

function main(llvm_path)
    ------------------------------ Das ------------------------------
    -- if os.isdir(".vscode") then
    --     try {function()
    --         local js_name = '.vscode/settings.json'
    --         local make = false
    --         local is_backup = false
    --         if not (os.isfile(js_name) and (#io.readfile(js_name) ~= 0)) then
    --             js_name = js_name .. ".backup"
    --             make = true
    --             is_backup = true
    --         end
    --         local map = {}
    --         local json = import("core.base.json")
    --         try {function()
    --             map = json.loadfile(js_name)
    --         end}

    --         if not (map["dascript.project.roots"] and map["dascript.compiler"]) then
    --             make = true
    --         end
    --         if make then
    --             print("making .vscode/settings.json")
    --             local str = path.absolute(os.scriptdir())
    --             json.savefile(".vscode/settings.json", map)
    --         else
    --             print(".vscode/settings.json is good, no need to make");
    --         end
    --     end}
    --     catch {function()
    --         print("load settings.json error")
    --     end}
    --     -- os.cp(".vscode/settings.json.backup", ".vscode/settings.json")
    -- end
    ------------------------------ LC ------------------------------
    install_lc(llvm_path)
    -- lc setup
    ------------------------------ SKR ------------------------------
    local lib = import("lib", {
        rootdir = "LuisaCompute/scripts"
    })
    local bdir = path.join("build", os.host(), os.arch())
    for i, v in ipairs({"debug", "release", "releasedbg"}) do
        local dst = path.join(bdir, v)
        lib.mkdirs(dst)
    end
end
