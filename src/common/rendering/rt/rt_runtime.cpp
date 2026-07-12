#include "rt_helpers.h"

#if HAVE_RT

#include "c_cvars.h"

#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

CVAR(String, rt_asset_dir, "", CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(String, rt_runtime_dir, "", CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(String, rt_prepare_script, "", CVAR_ARCHIVE | CVAR_GLOBALCONFIG)
CVAR(Bool, rt_runtime_autoprepare, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

namespace
{
constexpr std::string_view kCompatVersion = "compat-9";

std::string g_runtimePath;
std::string g_runtimeSource;
std::string g_subpath;

bool Exists(const fs::path& path)
{
    std::error_code ec;
    return fs::exists(path, ec);
}

bool IsDir(const fs::path& path)
{
    std::error_code ec;
    return fs::is_directory(path, ec);
}

bool ValidRuntime(const fs::path& path)
{
    return IsDir(path / "wad") && IsDir(path / "data") && IsDir(path / "mat") &&
           IsDir(path / "shaders") && Exists(path / "data" / "textures.json") &&
           Exists(path / "BlueNoise_LDR_RGBA_128.ktx2") && Exists(path / "WaterNormal_n.ktx2");
}

bool RuntimeCompatCurrent(const fs::path& path)
{
    fs::path manifest = path / ".gzdoom-rt-runtime";
    if (!Exists(manifest))
    {
        return true;
    }

    std::ifstream in(manifest);
    if (!in)
    {
        return true;
    }

    std::string line;
    while (std::getline(in, line))
    {
        constexpr std::string_view prefix = "compat=";
        if (line.rfind(prefix, 0) == 0)
        {
            return line.substr(prefix.size()) == kCompatVersion;
        }
    }
    return true;
}

const char* Env(const char* name)
{
    const char* value = std::getenv(name);
    return value && value[0] ? value : nullptr;
}

fs::path ExpandUser(std::string_view value)
{
    if (value.empty())
    {
        return {};
    }
    if (value[0] == '~' && (value.size() == 1 || value[1] == '/' || value[1] == '\\'))
    {
        if (const char* home = Env("HOME"))
        {
            fs::path path = home;
            if (value.size() > 2)
            {
                path /= std::string(value.substr(2));
            }
            return path;
        }
    }
    return fs::path(std::string(value));
}

fs::path DefaultCacheRuntime()
{
    if (const char* xdg = Env("XDG_CACHE_HOME"))
    {
        return fs::path(xdg) / "gzdoom-rt" / "runtime" / "current";
    }
    if (const char* home = Env("HOME"))
    {
        return fs::path(home) / ".cache" / "gzdoom-rt" / "runtime" / "current";
    }
    return fs::path("rt");
}

fs::path PreferredRuntimePath()
{
    if (const char* env = Env("GZDOOM_RT_RUNTIME_DIR"))
    {
        return ExpandUser(env);
    }
    if (static_cast<const char*>(rt_runtime_dir)[0])
    {
        return ExpandUser(static_cast<const char*>(rt_runtime_dir));
    }
    return DefaultCacheRuntime();
}

std::vector<fs::path> AssetCandidates()
{
    std::vector<fs::path> result;

    if (const char* env = Env("GZDOOM_RT_ASSET_DIR"))
    {
        result.push_back(ExpandUser(env));
    }
    if (static_cast<const char*>(rt_asset_dir)[0])
    {
        result.push_back(ExpandUser(static_cast<const char*>(rt_asset_dir)));
    }
    result.push_back(ExpandUser("~/Games/gzdoom-rt/assets/gzdoom-rt-runtime/1.0.2/rt"));
    result.push_back(ExpandUser("~/Games/gzdoom-rt/rt"));
    result.push_back("rt");

    return result;
}

fs::path FindAssetSource()
{
    for (const fs::path& candidate : AssetCandidates())
    {
        if (ValidRuntime(candidate))
        {
            return candidate;
        }
    }
    return {};
}

std::string ShellQuote(const fs::path& path)
{
    std::string in = path.string();
    std::string out = "'";
    for (char ch : in)
    {
        if (ch == '\'')
        {
            out += "'\\''";
        }
        else
        {
            out += ch;
        }
    }
    out += "'";
    return out;
}

fs::path FindPrepareScript()
{
    if (const char* env = Env("GZDOOM_RT_PREPARE_SCRIPT"))
    {
        fs::path path = ExpandUser(env);
        if (Exists(path))
        {
            return path;
        }
    }
    if (static_cast<const char*>(rt_prepare_script)[0])
    {
        fs::path path = ExpandUser(static_cast<const char*>(rt_prepare_script));
        if (Exists(path))
        {
            return path;
        }
    }

    std::error_code ec;
    fs::path cur = fs::current_path(ec);
    for (int i = 0; !ec && i < 8; ++i)
    {
        fs::path candidate = cur / "tools" / "rt-runtime" / "prepare-runtime.sh";
        if (Exists(candidate))
        {
            return candidate;
        }
        if (!cur.has_parent_path() || cur.parent_path() == cur)
        {
            break;
        }
        cur = cur.parent_path();
    }
    return {};
}

bool RunPrepareScript(const fs::path& source, const fs::path& runtime)
{
#ifdef _WIN32
    return false;
#else
    fs::path script = FindPrepareScript();
    if (script.empty())
    {
        return false;
    }

    std::string cmd = "bash " + ShellQuote(script) + " " + ShellQuote(source) + " " + ShellQuote(runtime);
    return std::system(cmd.c_str()) == 0;
#endif
}

void LinkOrCopy(const fs::path& source, const fs::path& dest)
{
    std::error_code ec;
    fs::remove(dest, ec);
    ec.clear();

    if (IsDir(source))
    {
        fs::create_directory_symlink(source, dest, ec);
    }
    else
    {
        fs::create_symlink(source, dest, ec);
    }
    if (!ec)
    {
        return;
    }

    ec.clear();
    if (IsDir(source))
    {
        fs::copy(source, dest, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    }
    else
    {
        fs::copy_file(source, dest, fs::copy_options::overwrite_existing, ec);
    }
}

void LinkDirectoryContents(const fs::path& source, const fs::path& dest)
{
    std::error_code ec;
    fs::remove_all(dest, ec);
    ec.clear();
    fs::create_directories(dest, ec);
    if (ec)
    {
        return;
    }

    for (const auto& entry : fs::directory_iterator(source, ec))
    {
        if (ec)
        {
            return;
        }
        LinkOrCopy(entry.path(), dest / entry.path().filename());
    }
}

bool BasicPrepareRuntime(const fs::path& source, const fs::path& runtime)
{
    std::error_code ec;
    fs::create_directories(runtime, ec);
    if (ec)
    {
        return false;
    }

    const char* linkEntries[] = {
        "bin", "bin_remix", "launcher", "mat_src", "replace", "scenes", "shaders", "wad",
        "BlueNoise_LDR_RGBA_128.ktx2", "DirtMask.ktx2", "SceneBuildWarning.ktx2", "WaterNormal_n.ktx2",
        "CreateKTX2.py", "LICENSE", "RTGL1.json", "RTGL1.json-example", "RTGL1_Remix.json-example",
    };
    for (const char* name : linkEntries)
    {
        fs::path src = source / name;
        if (Exists(src))
        {
            LinkOrCopy(src, runtime / name);
        }
    }

    LinkDirectoryContents(source / "mat", runtime / "mat");

    fs::remove_all(runtime / "data", ec);
    ec.clear();
    fs::copy(source / "data", runtime / "data",
             fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);

    std::ofstream manifest(runtime / ".gzdoom-rt-runtime");
    manifest << "source=" << source.string() << "\n";
    manifest << "compat=" << kCompatVersion << "\n";
    manifest << "prepared_by=engine-basic\n";

    return ValidRuntime(runtime);
}

fs::path PrepareOrFallback()
{
    fs::path runtime = PreferredRuntimePath();
    if (ValidRuntime(runtime) && RuntimeCompatCurrent(runtime))
    {
        g_runtimeSource = "prepared-runtime";
        return runtime;
    }

    fs::path source = FindAssetSource();
    if (source.empty())
    {
        if (ValidRuntime("rt"))
        {
            g_runtimeSource = "local-rt-fallback";
            return "rt";
        }
        g_runtimeSource = "missing-runtime";
        return runtime;
    }

    if (rt_runtime_autoprepare)
    {
        if (RunPrepareScript(source, runtime) && ValidRuntime(runtime) && RuntimeCompatCurrent(runtime))
        {
            g_runtimeSource = "prepared-runtime";
            return runtime;
        }
    }

    g_runtimeSource = "asset-source-fallback";
    return source;
}

std::string WithTrailingSlash(fs::path path)
{
    std::string out = path.lexically_normal().string();
    if (!out.empty() && out.back() != '/' && out.back() != '\\')
    {
        out += '/';
    }
    return out;
}
}

auto RT_ResolveRuntimePath() -> const char*
{
    if (g_runtimePath.empty())
    {
        g_runtimePath = WithTrailingSlash(PrepareOrFallback());
        std::fprintf(stderr, "GZDoom RT runtime: %s (%s)\n", g_runtimePath.c_str(), g_runtimeSource.c_str());
        if (g_runtimeSource == "asset-source-fallback" || g_runtimeSource == "local-rt-fallback")
        {
            std::fprintf(stderr,
                         "GZDoom RT warning: using an unprepared RT asset source; generated compatibility "
                         "patches may be missing.\n");
        }
    }
    return g_runtimePath.c_str();
}

auto RT_ResolveRuntimeSubpath(const char* subpath) -> const char*
{
    fs::path path = RT_ResolveRuntimePath();
    path /= subpath ? subpath : "";
    g_subpath = path.lexically_normal().string();
    return g_subpath.c_str();
}

#endif
