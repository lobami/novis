#pragma once

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

// =============================================================================
// Novis project environment
// =============================================================================
// Novis env is intentionally closer to Rust's target dir + Python's venv UX than
// to a CPython virtualenv. It isolates generated native providers, build/cache
// artifacts and lockfiles under `.novis/`, but it never creates or requires a
// Python interpreter.

class NovisEnv {
public:
    explicit NovisEnv(std::string root = ".") : root_(std::move(root)) {}

    bool init(std::ostream& out, std::ostream& err) const {
        if (!ensure_dir(path(".novis")) ||
            !ensure_dir(path(".novis/packages")) ||
            !ensure_dir(path(".novis/build")) ||
            !ensure_dir(path(".novis/cache")) ||
            !ensure_dir(path(".novis/bin"))) {
            err << "error: could not create Novis env directories: " << std::strerror(errno) << "\n";
            return false;
        }

        std::string env_path = path(".novis/env.toml");
        if (!file_exists(env_path)) {
            if (!write_file(env_path, default_manifest())) {
                err << "error: could not write " << env_path << "\n";
                return false;
            }
        }

        std::string lock_path = path(".novis/importpy.lock");
        if (!file_exists(lock_path)) {
            if (!write_file(lock_path, "# Novis importpy native provider lockfile\n")) {
                err << "error: could not write " << lock_path << "\n";
                return false;
            }
        }

        out << "✓ Novis env ready\n";
        out << "  Created by: Loth Mejía Martínez · México · 2026\n";
        out << "  Root: " << root_ << "\n";
        out << "  Manifest: " << env_path << "\n";
        out << "  Packages: " << path(".novis/packages") << "\n";
        out << "  Python runtime: not required\n";
        out << "\n";
        out << "Next:\n";
        out << "  novis importpy pandas\n";
        out << "  novis check your_file.novis\n";
        return true;
    }

    bool info(std::ostream& out, std::ostream& err) const {
        (void)err;
        out << "Novis env\n";
        out << "  Created by: Loth Mejía Martínez · México · 2026\n";
        out << "  Root: " << root_ << "\n";
        out << "  Active: " << (file_exists(path(".novis/env.toml")) ? "yes" : "no") << "\n";
        out << "  Manifest: " << path(".novis/env.toml") << "\n";
        out << "  Packages: " << path(".novis/packages") << "\n";
        out << "  Build dir: " << path(".novis/build") << "\n";
        out << "  Cache dir: " << path(".novis/cache") << "\n";
        out << "  ImportPy lock: " << path(".novis/importpy.lock") << "\n";
        out << "  Python runtime: not required\n";
        return true;
    }

    bool list(std::ostream& out, std::ostream& err) const {
        std::string lock = path(".novis/importpy.lock");
        out << "Novis env packages\n";
        if (!file_exists(lock)) {
            out << "  No env lockfile yet. Run `novis env init`.\n";
            return true;
        }
        std::ifstream in(lock);
        if (!in) {
            err << "error: cannot read " << lock << "\n";
            return false;
        }
        std::string line;
        bool any = false;
        std::set<std::string> printed;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            if (!printed.insert(line).second) continue;
            any = true;
            out << "  " << line << "\n";
        }
        if (!any) out << "  No importpy providers installed yet.\n";
        return true;
    }

    static void help(std::ostream& out) {
        out << "usage:\n";
        out << "  novis env init    # create .novis project environment\n";
        out << "  novis env info    # show env paths/status\n";
        out << "  novis env list    # list installed importpy native providers\n";
    }

private:
    std::string root_;

    std::string path(const std::string& child) const {
        if (root_.empty() || root_ == ".") return "./" + child;
        return root_ + "/" + child;
    }

    static bool file_exists(const std::string& p) {
        std::ifstream in(p);
        return static_cast<bool>(in);
    }

    static bool ensure_dir(const std::string& p) {
        struct stat st {};
        if (stat(p.c_str(), &st) == 0) return S_ISDIR(st.st_mode);

        std::size_t slash = p.find_last_of('/');
        if (slash != std::string::npos && slash > 0) {
            if (!ensure_dir(p.substr(0, slash))) return false;
        }
        if (mkdir(p.c_str(), 0755) == 0) return true;
        return errno == EEXIST;
    }

    static bool write_file(const std::string& p, const std::string& content) {
        std::ofstream out(p);
        if (!out) return false;
        out << content;
        return static_cast<bool>(out);
    }

    static std::string default_manifest() {
        return R"TOML(# Novis project environment
# This is not a Python virtualenv. It isolates Novis-native packages and build artifacts.

[env]
version = "1"
created_by = "Loth Mejía Martínez"
created_in = "México"
year = 2026
python_runtime = false
package_dir = ".novis/packages"
build_dir = ".novis/build"
cache_dir = ".novis/cache"

[importpy]
mode = "native-provider"
allow_cpython_fallback = false
)TOML";
    }
};
