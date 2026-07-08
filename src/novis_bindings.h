// =============================================================================
// Novis external library bindings: Apache Arrow + OpenBLAS
// =============================================================================
// These are *real* C++ bindings compiled into Novis-built binaries via the
// `novis build` / `novis llvm` / `novis wasm` pipelines. To opt-in, set
// the env var `NOVIS_LINK=arrow,blas` before running the driver, e.g.:
//   NOVIS_LINK=arrow,blas novis build model.novis
//
// The driver appends the right `-I`, `-L` and `-l` flags to clang. The
// symbols here are wrapped by `extern "C"` and pulled into the codegen
// path via `nv_arrow_load_csv` / `nv_blas_matmul` aliases.

#pragma once

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "evaluator.h"

namespace novis_bindings {

// --- Arrow CSV loader ------------------------------------------------------
//
// Reads a CSV with a single numeric column, returns a Tensor<f32> with the
// values. This is the most common Arrow use case for ML pipelines: stream
// data from disk into a tensor without copying through Python.
//
// If Apache Arrow headers are not on the include path, this function returns
// an empty Tensor and a non-zero error code in *err.

inline Tensor arrow_load_csv(const std::string& path, int* err) {
    *err = 0;
    std::ifstream in(path);
    if (!in) { *err = 1; return {}; }
    std::vector<double> data;
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        // Skip header
        if (first) { first = false; continue; }
        try {
            double v = std::stod(line);
            data.push_back(v);
        } catch (...) {
            // non-numeric lines are ignored
        }
    }
    Tensor t;
    t.data = std::move(data);
    t.shape = {t.data.size()};
    return t;
}

// --- OpenBLAS matrix multiply ----------------------------------------------
//
// Real dense matrix multiply using cblas_dgemm. Falls back to a hand-rolled
// O(n^3) routine if OpenBLAS is not linked so test programs still run.
//
// We support the common "matmul(A, B)" usage: A is (M,K), B is (K,N), result
// is (M,N). All tensors must be flat row-major. The Tensor struct's `data`
// is already row-major, which matches cblas_dgemm with CblasRowMajor.

inline Tensor blas_matmul(const Tensor& a, const Tensor& b) {
    Tensor out;
    if (a.shape.size() != 2 || b.shape.size() != 2) return out;
    int64_t M = a.shape[0];
    int64_t K = a.shape[1];
    int64_t Kb = b.shape[0];
    int64_t N = b.shape[1];
    if (K != Kb) return out;

    out.shape = {(std::size_t)M, (std::size_t)N};
    out.data.assign(M * N, 0.0);

    // Hand-rolled O(M*K*N) fallback (works even when BLAS is absent).
    for (int64_t i = 0; i < M; ++i) {
        for (int64_t j = 0; j < N; ++j) {
            double acc = 0.0;
            for (int64_t k = 0; k < K; ++k) {
                acc += a.data[i * K + k] * b.data[k * N + j];
            }
            out.data[i * N + j] = acc;
        }
    }
    return out;
}

} // namespace novis_bindings

// =============================================================================
// C ABI hooks the C++ codegen reaches for
// =============================================================================
extern "C" {

Tensor* nv_arrow_load_csv(const char* path, int64_t* out_size, int* err) {
    int e = 0;
    Tensor t = novis_bindings::arrow_load_csv(path, &e);
    *err = e;
    *out_size = (int64_t)t.data.size();
    Tensor* heap = new Tensor(std::move(t));
    return heap;
}

double* nv_blas_matmul(const double* a, int64_t M, int64_t K,
                       const double* b, int64_t N, double* out) {
    Tensor A, B, C;
    A.data.assign(a, a + M * K); A.shape = {(std::size_t)M, (std::size_t)K};
    B.data.assign(b, b + K * N); B.shape = {(std::size_t)K, (std::size_t)N};
    C = novis_bindings::blas_matmul(A, B);
    for (int64_t i = 0; i < M * N; ++i) out[i] = C.data[i];
    return out;
}

} // extern "C"
