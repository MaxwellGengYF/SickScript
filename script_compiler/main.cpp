#include <iostream>
#include <luisa/core/clock.h>
#include <luisa/core/logging.h>
#include <luisa/core/stl/filesystem.h>
#include <luisa/vstl/common.h>
#include <luisa/vstl/functional.h>
#include <luisa/vstl/lmdb.hpp>
#include <luisa/vstl/md5.h>
#include <luisa/clangcxx/compiler.h>
#include <luisa/core/fiber.h>
#include <luisa/runtime/context.h>
#include <luisa/core/binary_file_stream.h>
#include <luisa/vstl/v_guid.h>
#include <luisa/core/stl/pdqsort.h>
#include <luisa/vstl/spin_mutex.h>
#include <mimalloc.h>
using namespace luisa;
using namespace luisa::compute;
static bool kTestRuntime = false;
string to_lower(string_view value) {
	string value_str{value};
	for (auto& i : value_str) {
		if (i >= 'A' && i <= 'Z') {
			i += 'a' - 'A';
		}
	}
	return value_str;
}

#include "preprocessor.h"

int main(int argc, char* argv[]) {
	log_level_error();
	//////// Properties
	if (argc <= 1) {
		LUISA_ERROR("Empty argument not allowed.");
	}
	std::filesystem::path src_path;
	std::filesystem::path dst_path;
	luisa::vector<std::filesystem::path> inc_paths;
	luisa::unordered_set<luisa::string> defines;
	luisa::string backend = "toy-c";
	luisa::vector<char> lua_code;
	luisa::vector<std::pair<luisa::vector<char>, bool>> target_files;
	std::mutex code_mtx;
	bool use_optimize = true;
	bool enable_help = false;
	bool enable_lsp = false;
	bool rebuild = false;
	vstd::HashMap<vstd::string, vstd::function<void(vstd::string_view)>> cmds(16);
	auto invalid_arg = []() {
		LUISA_ERROR("Invalid argument, use --help please.");
	};

	cmds.emplace(
		"opt"sv,
		[&](string_view name) {
		auto lower_name = to_lower(name);
		if (lower_name == "on"sv) {
			use_optimize = true;
		} else if (lower_name == "off"sv) {
			use_optimize = false;
		} else {
			invalid_arg();
		}
	});
	cmds.emplace(
		"help"sv,
		[&](string_view name) {
		enable_help = true;
	});
	cmds.emplace(
		"backend"sv,
		[&](string_view name) {
		if (name.empty()) {
			invalid_arg();
		}
		auto lower_name = to_lower(name);
		backend = lower_name;
	});
	cmds.emplace(
		"in"sv,
		[&](string_view name) {
		if (name.empty()) {
			invalid_arg();
		}
		if (!src_path.empty()) {
			LUISA_ERROR("Source path set multiple times.");
		}
		src_path = name;
	});
	cmds.emplace(
		"out"sv,
		[&](string_view name) {
		if (name.empty()) {
			invalid_arg();
		}
		if (!dst_path.empty()) {
			LUISA_ERROR("Dest path set multiple times.");
		}
		dst_path = name;
	});
	cmds.emplace(
		"include"sv,
		[&](string_view name) {
		if (name.empty()) {
			invalid_arg();
		}
		inc_paths.emplace_back(name);
	});
	cmds.emplace(
		"D"sv,
		[&](string_view name) {
		if (name.empty()) {
			invalid_arg();
		}
		defines.emplace(name);
	});
	cmds.emplace(
		"lsp"sv,
		[&](string_view name) {
		enable_lsp = true;
	});
	luisa::filesystem::path cache_path;
	cmds.emplace(
		"cache_dir"sv,
		[&](string_view sv) {
		cache_path = luisa::filesystem::path{sv} / ".cache";
	});

	cmds.emplace(
		"rebuild"sv,
		[&](string_view name) {
		rebuild = true;
	});
	// TODO: define
	for (auto i : vstd::ptr_range(argv + 1, argc - 1)) {
		string arg = i;
		string_view kv_pair = arg;
		for (auto i : vstd::range(arg.size())) {
			if (arg[i] == '-')
				continue;
			else {
				kv_pair = string_view(arg.data() + i, arg.size() - i);
				break;
			}
		}
		if (kv_pair.empty() || kv_pair.size() == arg.size()) {
			invalid_arg();
		}
		string_view key = kv_pair;
		string_view value;
		for (auto i : vstd::range(kv_pair.size())) {
			if (kv_pair[i] == '=') {
				key = string_view(kv_pair.data(), i);
				value = string_view(kv_pair.data() + i + 1, kv_pair.size() - i - 1);
				break;
			}
		}
		auto iter = cmds.find(key);
		if (!iter) {
			invalid_arg();
		}
		iter.value()(value);
	}
	if (cache_path.empty()) {
		cache_path = dst_path / ".cache";
	}
	if (enable_help) {
		// print help list
		string_view helplist = R"(
Argument format:
    argument should be -ARG=VALUE or --ARG=VALUE, invalid argument will cause fatal error and never been ignored.

Argument list:
    --opt: enable or disable optimize, E.g --opt=on, --opt=off, case ignored.
    --backend: backend name, currently support "dx", "cuda", "metal", case ignored.
    --in: input file or dir, E.g --in=./my_dir/my_shader.cpp
    --out: output file or dir, E.g --out=./my_dir/my_shader.c
    --include: include file directory, E.g --include=./shader_dir/
    --D: shader predefines, this can be set multiple times, E.g --D=MY_MACRO
    --lsp: enable compile_commands.json generation, E.g --lsp
)"sv;
		std::cout << helplist << '\n';
		return 0;
	}
	if (src_path.empty()) {
		LUISA_ERROR("Input file path not defined.");
	}

	Context context{argv[0]};
	if (src_path.is_relative()) {
		src_path = std::filesystem::current_path() / src_path;
	}
	std::error_code code;
	src_path = std::filesystem::canonical(src_path, code);
	if (code.value() != 0) {
		LUISA_ERROR("Invalid source file path {} {}", luisa::to_string(src_path), code.message());
	}
	auto format_path = [&]() {
		for (auto&& i : inc_paths) {
			if (i.is_relative()) {
				i = std::filesystem::current_path() / i;
			}
			i = std::filesystem::weakly_canonical(i, code);
			if (code.value() != 0) {
				LUISA_ERROR("Invalid include file path");
			}
		}

		if (dst_path.is_relative()) {
			dst_path = std::filesystem::current_path() / dst_path;
		}
		if (src_path == dst_path) {
			LUISA_ERROR("Source file and dest file path can not be the same.");
		}
		dst_path = std::filesystem::weakly_canonical(dst_path, code);
		if (code.value() != 0) {
			LUISA_ERROR("Invalid dest file path");
		}
	};
	auto ite_dir = [&](auto&& ite_dir, auto const& path, auto&& func) -> void {
		for (auto& i : std::filesystem::directory_iterator(path)) {
			if (i.is_directory()) {
				auto path_str = luisa::to_string(i.path().filename());
				if (path_str[0] == '.') {
					continue;
				}
				ite_dir(ite_dir, i.path(), func);
				continue;
			}
			func(i.path());
		}
	};
	//////// LSP print
	if (enable_lsp) {
		if (!std::filesystem::is_directory(src_path)) {
			LUISA_ERROR("Source path must be a directory.");
		}
		if (dst_path.empty()) {
			dst_path = "compile_commands.json";
		} else if (std::filesystem::exists(dst_path) && !std::filesystem::is_regular_file(dst_path)) {
			LUISA_ERROR("Dest path must be a file.");
		}
		if (inc_paths.empty()) {
			inc_paths.emplace_back(src_path);
		}
		format_path();

		luisa::vector<char> result;
		result.reserve(16384);
		result.emplace_back('[');
		luisa::vector<std::filesystem::path> paths;
		auto func = [&](auto const& file_path_ref) {
			// auto file_path = file_path_ref;
			auto const& ext = file_path_ref.extension();
			if (ext != ".cpp" && ext != ".h" && ext != ".hpp") return;
			paths.emplace_back(file_path_ref);
		};
		ite_dir(ite_dir, src_path, func);
		if (!paths.empty()) {
			luisa::fiber::scheduler thread_pool(std::min<uint>(std::thread::hardware_concurrency(), paths.size()));
			std::mutex mtx;
			luisa::fiber::parallel(paths.size(), [&](size_t i) {
				auto& file_path = paths[i];
				if (file_path.is_absolute()) {
					file_path = std::filesystem::relative(file_path, src_path);
				}
				luisa::vector<char> local_result;
				auto iter = vstd::range_linker{
					vstd::make_ite_range(defines),
					vstd::transform_range{[&](auto&& v) { return luisa::string_view{v}; }}}
								.i_range();
				auto inc_iter = vstd::range_linker{
					vstd::make_ite_range(inc_paths),
					vstd::transform_range{
						[&](auto&& path) { return luisa::to_string(path); }}}
									.i_range();
				luisa::clangcxx::Compiler::lsp_compile_commands(
					iter,
					src_path,
					file_path,
					inc_iter,
					local_result);
				local_result.emplace_back(',');
				size_t idx = [&]() {
					std::lock_guard lck{mtx};
					auto sz = result.size();
					result.push_back_uninitialized(local_result.size());
					return sz;
				}();
				memcpy(result.data() + idx, local_result.data(), local_result.size());
			});
		}
		if (result.size() > 1) {
			result.pop_back();
		}
		result.emplace_back(']');
		auto dst_path_str = luisa::to_string(dst_path);
		auto f = fopen(dst_path_str.c_str(), "wb");
		fwrite(result.data(), result.size(), 1, f);
		fclose(f);
		return 0;
	}
	//////// Compile all
	if (std::filesystem::is_directory(src_path)) {
		if (dst_path.empty()) {
			dst_path = src_path / "out";
		} else if (std::filesystem::exists(dst_path) && !std::filesystem::is_directory(dst_path)) {
			LUISA_ERROR("Dest path must be a directory.");
		}
		luisa::vector<std::filesystem::path> paths;
		auto create_dir = [&](auto&& path) {
			auto const& parent_path = path.parent_path();
			if (!std::filesystem::exists(parent_path))
				std::filesystem::create_directories(parent_path);
		};
		auto func = [&](auto const& file_path_ref) {
			if (file_path_ref.extension() != ".cpp") return;
			paths.emplace_back(file_path_ref);
		};
		ite_dir(ite_dir, src_path, func);
		if (paths.empty()) return 0;
		luisa::fiber::scheduler thread_pool(std::min<uint>(std::thread::hardware_concurrency(), paths.size()));

		format_path();
		log_level_info();
		auto iter = vstd::range_linker{
			vstd::make_ite_range(defines),
			vstd::transform_range{[&](auto&& v) { return luisa::string_view{v}; }}}
						.i_range();
		auto inc_iter = vstd::range_linker{
			vstd::make_ite_range(inc_paths),
			vstd::transform_range{
				[&](auto&& path) { return luisa::to_string(path); }}}
							.i_range();
		auto lmdb_cache_path = cache_path / ".lmdb";
		if (rebuild) {
			if (std::filesystem::exists(cache_path)) {
				std::error_code ec;
				std::filesystem::remove_all(cache_path, ec);
				if (ec) [[unlikely]] {
					LUISA_ERROR("Try clear cache dir {} failed {}.", luisa::to_string(cache_path), ec.message());
				}
			}
			if (std::filesystem::exists(dst_path)) {
				std::error_code ec;
				std::filesystem::remove_all(dst_path, ec);
				if (ec) [[unlikely]] {
					LUISA_ERROR("Try clear out dir {} failed {}.", luisa::to_string(dst_path), ec.message());
				}
			}
		}
		Preprocessor processor{
			lmdb_cache_path,
			cache_path / ".obj",
			iter,
			inc_iter};

		void* main_fn{};
		std::atomic_bool failed = false;
		luisa::fiber::parallel(
			paths.size(),
			[&](size_t idx) {
			auto const& src_file_path = paths[idx];
			auto file_path = src_file_path;
			if (file_path.is_absolute()) {
				file_path = std::filesystem::relative(file_path, src_path);
			}
			auto out_path = dst_path / file_path;
			if (!processor.require_recompile(src_path, file_path)) {
				auto local_out_path = out_path;
				auto out_filename = luisa::to_string(local_out_path.replace_extension("").filename());
				local_out_path.replace_filename(out_filename).replace_extension(".c");
				{
					auto out_name = luisa::to_string(local_out_path);
					luisa::vector<char> cc;
					cc.reserve(out_name.size());
					for (auto& i : out_name) {
						switch (i) {
							case '"':
								vstd::push_back_all(cc, "\\\"", 2);
								break;
							case '\\':
								cc.push_back('/');
								break;
							default:
								cc.push_back(i);
								break;
						}
					}
					std::lock_guard lck{code_mtx};
					target_files.emplace_back(std::move(cc), false);
				}
				return;
			}
			create_dir(out_path);
			// out_path.replace_extension("bin");
			int result = 0;
			auto exec_func = [&](luisa::span<luisa::string_view> extra_defines) {
				luisa::vector<char> vec;
				auto local_out_path = out_path;
				auto out_filename = luisa::to_string(local_out_path.replace_extension("").filename());
				for (auto& i : extra_defines) {
					if (i.empty()) continue;
					out_filename += "_";
					out_filename += i;
				}
				local_out_path.replace_filename(out_filename).replace_extension(".c");
				{
					auto out_name = luisa::to_string(local_out_path);
					luisa::vector<char> cc;
					cc.reserve(out_name.size());
					for (auto& i : out_name) {
						switch (i) {
							case '"':
								vstd::push_back_all(cc, "\\\"", 2);
								break;
							case '\\':
								cc.push_back('/');
								break;
							default:
								cc.push_back(i);
								break;
						}
					}
					std::lock_guard lck{code_mtx};
					target_files.emplace_back(std::move(cc), true);
				}
				add(vec, argv[0]);
				add(vec, ' ');
				add(vec, "-opt="sv);
				add(vec, use_optimize ? "on"sv : "off"sv);
				add(vec, ' ');
				add(vec, "-backend="sv);
				add(vec, backend);
				add(vec, ' ');
				add(vec, "-in="sv);
				add(vec, luisa::to_string(src_file_path));
				add(vec, ' ');
				add(vec, "-out="sv);
				add(vec, luisa::to_string(local_out_path));
				for (auto& i : inc_paths) {
					add(vec, ' ');
					add(vec, "-include="sv);
					add(vec, luisa::to_string(i));
				}
				for (auto& i : defines) {
					add(vec, ' ');
					add(vec, "-D="sv);
					add(vec, i);
				}
				luisa::string macro;
				for (auto& i : extra_defines) {
					if (i.empty()) continue;
					add(vec, ' ');
					add(vec, "-D="sv);
					add(vec, i);
					macro += " ";
					macro += i;
				}
				vec.emplace_back(0);
				LUISA_INFO("compiling {}{}", luisa::to_string(file_path.filename()), macro);
				result = system(vec.data());
			};
			exec_func({});
			// TODO: variant
			if (result != 0) {
				processor.remove_file(luisa::to_string(std::filesystem::weakly_canonical(src_path / file_path)));
				failed = true;
			}
		});
		processor.post_process();
		pdqsort(target_files.begin(), target_files.end(), [](auto&& a, auto&& b) {
			auto&& astr = a.first;
			auto&& bstr = b.first;
			if (astr.size() < bstr.size()) return true;
			if (astr.size() > bstr.size()) return false;
			return std::memcmp(astr.data(), bstr.data(), astr.size()) < 0;
		});
		auto push_lua_code = [&](luisa::string_view strv) {
			auto idx = lua_code.size();
			lua_code.push_back_uninitialized(strv.size());
			std::memcpy(lua_code.data() + idx, strv.data(), strv.size());
		};
		lua_code.reserve(4096);
		luisa::vector<size_t> compile_indices;
		compile_indices.reserve(target_files.size());
		push_lua_code("function files()\nlocal link = {");
		bool comma = false;
		size_t compile_idx = 1;
		for (auto& i : target_files) {
			if (comma) {
				push_lua_code(",");
			}
			comma = true;
			push_lua_code("\"");
			push_lua_code(luisa::string_view(i.first.data(), i.first.size()));
			push_lua_code("\"");
			if (i.second) {
				compile_indices.emplace_back(compile_idx);
			}
			compile_idx++;
		}
		push_lua_code("}\nlocal compile = {");
		comma = false;
		for (auto& i : compile_indices) {
			if (comma) {
				push_lua_code(",");
			}
			comma = true;
			push_lua_code(luisa::format("{}", i));
		}
		push_lua_code("}\nreturn link, compile\nend");
		auto lua_path = luisa::to_string(dst_path / "compile_c.lua");
		auto f = fopen(lua_path.c_str(), "wb");
		if (f) {
			fwrite(lua_code.data(), lua_code.size(), 1, f);
			fclose(f);
		}
		if (failed) {
			return 1;
		}
		return 0;
	}
	//////// Compile
	if (dst_path.empty()) {
		dst_path = src_path.filename();
	}
	dst_path.replace_extension(".c");
	format_path();
	DeviceConfig config{
		.headless = true};
	Device device = context.create_device(backend, &config);
	auto iter = vstd::range_linker{
		vstd::make_ite_range(defines),
		vstd::transform_range{[&](auto&& v) { return luisa::string_view{v}; }}}
					.i_range();
	auto inc_iter = vstd::range_linker{
		vstd::make_ite_range(inc_paths),
		vstd::transform_range{
			[&](auto&& path) { return luisa::to_string(path); }}}
						.i_range();
	if (!luisa::clangcxx::Compiler::create_shader(
			ShaderOption{
				.enable_fast_math = use_optimize,
				.enable_debug_info = !use_optimize,
				.compile_only = true,
				.name = luisa::to_string(dst_path)},
			device, iter, src_path, inc_iter)) {
		LUISA_ERROR("Compile {} failed.", luisa::to_string(dst_path));
		return 1;
	}
	return 0;
}