#pragma once

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

// =============================================================================
// Novis importpy
// =============================================================================
// User-facing command: `novis importpy pandas`.
//
// Important design rule: importpy never shells out to Python and never embeds
// CPython. It installs a tiny Novis facade backed by a declared native provider.
// Today those facades map to Novis's built-in native kernels (Tensor, money,
// math, I/O). Future work can swap the facade bodies for direct Arrow/BLAS/
// ONNX/libtorch/openssl bindings without changing user code.

struct NativeProvider {
    std::string python_package;
    std::string module_name;
    std::string provider;
    std::string backend;
    bool python_required = false;
    std::string description;
    std::string source;
};

class ImportPyRegistry {
public:
    static const std::vector<NativeProvider>& providers() {
        static const std::vector<NativeProvider> registry = {
            {
                "numpy",
                "numpy",
                "novis.tensor.native",
                "Accelerate/OpenBLAS/oneDNN/Wasm SIMD",
                false,
                "NumPy-compatible tensor facade over Novis Tensor kernels",
                R"NOVIS(
import std.ai
import std.math

pub fn np_array(values: Tensor<f32>) -> Tensor<f32>:
    return values

pub fn np_sum(values: Tensor<f32>) -> f32:
    return sum(values)

pub fn np_mean(values: Tensor<f32>) -> f32:
    return mean(values)

pub fn np_std(values: Tensor<f32>) -> f32:
    return stddev(values)

pub fn np_dot(a: Tensor<f32>, b: Tensor<f32>) -> f32:
    return dot(a, b)

pub fn np_relu(values: Tensor<f32>) -> Tensor<f32>:
    return relu(values)

pub fn np_sigmoid(values: Tensor<f32>) -> Tensor<f32>:
    return sigmoid(values)
)NOVIS"
            },
            {
                "pandas",
                "pandas",
                "novis.data.arrow",
                "Apache Arrow native memory + Novis kernels",
                false,
                "Pandas-inspired facade without CPython; uses Arrow-style typed columns",
                R"NOVIS(
import std.core
import std.ai
import std.fs

pub type DataFrame:
    rows: int
    columns: int

pub type Series<T>:
    length: int

pub fn pd_read_csv_text(path: str) -> str:
    return read_file(path)

pub fn pd_mean(values: Tensor<f32>) -> f32:
    return mean(values)

pub fn pd_sum(values: Tensor<f32>) -> f32:
    return sum(values)

pub fn pd_std(values: Tensor<f32>) -> f32:
    return stddev(values)

pub fn pd_count(values: Tensor<f32>) -> int:
    return len(values)
)NOVIS"
            },
            {
                "scipy",
                "scipy",
                "novis.science.native",
                "BLAS/LAPACK/SuiteSparse-compatible kernels",
                false,
                "SciPy-inspired math facade over native numerical kernels",
                R"NOVIS(
import std.math
import std.ai

pub fn sp_sqrt(x: f32) -> f32:
    return sqrt(x)

pub fn sp_pow(x: f32, y: f32) -> f32:
    return pow(x, y)

pub fn sp_dot(a: Tensor<f32>, b: Tensor<f32>) -> f32:
    return dot(a, b)
)NOVIS"
            },
            {
                "torch",
                "torch",
                "novis.ai.libtorch_or_onnx",
                "libtorch/ONNX Runtime/native Tensor kernels",
                false,
                "PyTorch-inspired inference facade over compiled tensor kernels",
                R"NOVIS(
import std.ai

pub fn torch_tensor(values: Tensor<f32>) -> Tensor<f32>:
    return values

pub fn torch_relu(values: Tensor<f32>) -> Tensor<f32>:
    return relu(values)

pub fn torch_sigmoid(values: Tensor<f32>) -> Tensor<f32>:
    return sigmoid(values)

pub fn torch_softmax(values: Tensor<f32>) -> Tensor<f32>:
    return softmax(values)
)NOVIS"
            },
            {
                "sklearn",
                "sklearn",
                "novis.ai.onnx",
                "ONNX Runtime / compiled model scoring",
                false,
                "Scikit-learn-inspired scoring facade using native risk/model kernels",
                R"NOVIS(
import std.ai

pub fn sk_risk_score(features: Tensor<f32>) -> f32:
    return risk_score(features)

pub fn sk_linear_score(features: Tensor<f32>, weights: Tensor<f32>) -> f32:
    return dot(features, weights)
)NOVIS"
            },
            {
                "requests",
                "requests",
                "novis.http.native",
                "native HTTP client/runtime placeholder",
                false,
                "Requests-inspired HTTP contract facade; transport backend comes later",
                R"NOVIS(
import std.http

pub fn requests_status_ok(status: int) -> bool:
    return status == 200
)NOVIS"
            },
            {
                "cryptography",
                "cryptography",
                "novis.crypto.native",
                "OpenSSL/libsodium native bindings placeholder",
                false,
                "Crypto facade reserved for native OpenSSL/libsodium bindings",
                R"NOVIS(
pub type Digest:
    bytes: Tensor<f32>

pub fn crypto_available() -> bool:
    return true
)NOVIS"
            }
        };
        return registry;
    }

    static const NativeProvider* find(const std::string& name) {
        for (const auto& provider : providers()) {
            if (provider.python_package == name || provider.module_name == name) return &provider;
        }
        return nullptr;
    }

    static void list(std::ostream& out) {
        out << "Known importpy native providers:\n";
        for (const auto& provider : providers()) {
            out << "  " << provider.python_package
                << " -> import " << provider.module_name
                << " | " << provider.provider
                << " | Python required: " << (provider.python_required ? "yes" : "no")
                << "\n";
        }
    }

    static bool install(const std::string& package,
                        const std::string& project_root,
                        std::ostream& out,
                        std::ostream& err) {
        const NativeProvider* provider = find(package);
        if (!provider) {
            err << "error: no native importpy provider for '" << package << "'\n";
            err << "help: run `novis importpy --list` to see supported packages.\n";
            err << "note: Novis will not silently fall back to CPython because this language is designed to stay compiled/native.\n";
            return false;
        }

        std::string module_dir = project_root + "/.novis/packages/" + provider->module_name;
        std::string module_path = module_dir + "/mod.novis";
        std::string manifest_path = project_root + "/.novis/importpy.lock";

        if (!ensure_dir(project_root + "/.novis") ||
            !ensure_dir(project_root + "/.novis/packages") ||
            !ensure_dir(module_dir)) {
            err << "error: could not create .novis package directories: " << std::strerror(errno) << "\n";
            return false;
        }

        if (!write_file(module_path, provider_banner(*provider) + provider->source)) {
            err << "error: could not write " << module_path << "\n";
            return false;
        }

        append_lock(manifest_path, *provider);

        out << "✓ importpy installed: " << provider->python_package << "\n";
        out << "  Novis module: import " << provider->module_name << "\n";
        out << "  Native provider: " << provider->provider << "\n";
        out << "  Backend: " << provider->backend << "\n";
        out << "  Python runtime required: " << (provider->python_required ? "yes" : "no") << "\n";
        out << "  Files: " << module_path << "\n";
        out << "\n";
        out << "Use it in Novis:\n";
        out << "  import " << provider->module_name << "\n";
        return true;
    }

private:
    static bool ensure_dir(const std::string& path) {
        if (path.empty()) return true;
        struct stat st {};
        if (stat(path.c_str(), &st) == 0) return S_ISDIR(st.st_mode);

        std::size_t slash = path.find_last_of('/');
        if (slash != std::string::npos && slash > 0) {
            if (!ensure_dir(path.substr(0, slash))) return false;
        }
        if (mkdir(path.c_str(), 0755) == 0) return true;
        return errno == EEXIST;
    }

    static bool write_file(const std::string& path, const std::string& content) {
        std::ofstream out(path);
        if (!out) return false;
        out << content;
        return static_cast<bool>(out);
    }

    static void append_lock(const std::string& path, const NativeProvider& provider) {
        std::string entry;
        {
            std::ostringstream ss;
            ss << provider.python_package << " -> " << provider.module_name
               << " | provider=" << provider.provider
               << " | backend=" << provider.backend
               << " | python_required=" << (provider.python_required ? "true" : "false");
            entry = ss.str();
        }

        std::ifstream existing(path);
        std::string line;
        while (std::getline(existing, line)) {
            if (line == entry) return;
        }

        std::ofstream out(path, std::ios::app);
        if (!out) return;
        out << entry << "\n";
    }

    static std::string provider_banner(const NativeProvider& provider) {
        std::ostringstream ss;
        ss << "# Generated by novis importpy " << provider.python_package << "\n";
        ss << "# Native provider: " << provider.provider << "\n";
        ss << "# Backend: " << provider.backend << "\n";
        ss << "# Python runtime required: " << (provider.python_required ? "yes" : "no") << "\n\n";
        return ss.str();
    }
};
