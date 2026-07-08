<div align="center">

# Novis

**A compiled, strongly typed language for financial systems, AI, statistics, and the Web.**

*Created by Loth Mejía Martínez · México · 2026*

</div>

---

## ⚡ Novis native build is **1.9× faster than CPython 3.14** at `fib(30)`

```text
INTERPRETER (novis run = tree-walking, slow but portable)
fib(30)                         novis= 26915ms  py=    91ms  ratio=295.77x  (Novis 296x slower)
sum_squares(50000)              novis=    12ms  py=    21ms  ratio=0.57x   (Novis 1.75x faster!)

NATIVE BUILD  (novis build = C++ codegen, no Python)
fib(30)                          native=  38ms  py=    72ms  ratio=0.53x   (Novis 1.89x faster!)
sum_squares(50000)               native=   3ms  py=    17ms  ratio=0.18x   (Novis 5.6x faster!)
```

Reproduce locally:

```bash
make
ITERS=3 N_FIB=30 N_SUM=50000 benchmarks/run_bench.sh
```

The interpreter remains the default (`novis run`) for REPL feedback and code that doesn't fit the native backend yet. The `novis build` command lowers Novis to C++ and shells out to `clang++` to produce a real native binary, with **zero Python involved**.

---

## ✨ What is Novis?

Novis is a programming language designed to be the bridge between **production-grade financial systems** and **modern AI/statistics workloads**, with a clean Python-like syntax and a path toward native compilation.

Most financial systems stay on legacy stacks because they are predictable and auditable. Most modern AI/data stacks stay dynamic because they are expressive. Novis wants both:

| Domain                | Novis primitive                                                                                   |
|-----------------------|---------------------------------------------------------------------------------------------------|
| Banking               | `money`, `decimal`, `round_bankers`, `is_balanced`, currency-safe arithmetic                      |
| AI / statistics       | `Tensor<f32>`, `mean`, `variance`, `stddev`, `dot`, `softmax`, `relu`, `sigmoid`, `risk_score`    |
| Framework contracts   | `type`, `interface`, `pub`, generic annotations like `List<T>`, `Result<T, E>`, `Tensor<f32>`     |
| Module isolation      | `import std.bank`, `.novis/packages/<name>/mod.novis`, `importpy` native provider facade         |
| Compilation pipeline  | Lexer → Parser → AST → Type checker → Runtime → **`NovisIR`** (ready to lower to LLVM or Wasm)    |

> **No CPython, no Jython, no PyPy under the hood.** Novis never shells out to Python. `importpy` installs a Novis-native facade with the name of the Python package you know.

---

## 📦 Status

> **Production-oriented compiler prototype.** End-to-end toolchain is working. Native codegen to LLVM/Wasm is the next step.

<table>
  <tr><th>Component</th><th>Status</th></tr>
  <tr><td>Indentation-aware lexer</td><td>✅ Done</td></tr>
  <tr><td>Pratt parser + recursive descent</td><td>✅ Done</td></tr>
  <tr><td>AST with visitors</td><td>✅ Done</td></tr>
  <tr><td>Static type checker (nominal + generics)</td><td>✅ Done</td></tr>
  <tr><td>User-defined functions</td><td>✅ Done</td></tr>
  <tr><td>Banking + AI runtime primitives</td><td>✅ Done</td></tr>
  <tr><td>Module resolver + embedded stdlib</td><td>✅ Done</td></tr>
  <tr><td><code>novis env</code> project environment</td><td>✅ Done</td></tr>
  <tr><td><code>novis importpy</code> native provider facade</td><td>✅ Done</td></tr>
  <tr><td><code>NovisIR</code> textual backend</td><td>✅ Done</td></tr>
  <tr><td>Test suite (<code>make test</code>)</td><td>✅ Green</td></tr>
  <tr><td>LLVM codegen</td><td>⏳ Next</td></tr>
  <tr><td>Wasm codegen</td><td>⏳ Next</td></tr>
  <tr><td>Async runtime (<code>spawn</code>/<code>await</code>)</td><td>⏳ Next</td></tr>
  <tr><td>Real Arrow/BLAS/ONNX/libtorch bindings</td><td>⏳ Next</td></tr>
</table>

---

## ⚡ Quick start

### 0. Install (one liner)

The repository ships an official installer that detects your platform, checks for `git` and a C++17 compiler, clones, builds, runs the test suite, and installs the `novis` binary to `/usr/local/bin` (or `~/.local/bin` if you don't have write access there).

```bash
curl -fsSL https://raw.githubusercontent.com/lobami/novis/main/install.sh | bash
```

The installer is **upgrade-aware**: it reads the version of the binary already on disk and the version declared in `src/main.cpp` on the cloned source. It will skip rebuilding when the local install is up to date, install when there is a newer release, and refuse to downgrade.

Force a reinstall:

```bash
curl -fsSL https://raw.githubusercontent.com/lobami/novis/main/install.sh | bash -s -- --force
```

Install a different branch (e.g. for early testing):

```bash
curl -fsSL https://raw.githubusercontent.com/lobami/novis/main/install.sh | bash -s -- --channel dev
```

Manual uninstall:

```bash
curl -fsSL https://raw.githubusercontent.com/lobami/novis/main/install.sh | bash -s -- --uninstall
```

### 1. Build from source

```bash
make
```

### 2. Run the test suite

```bash
make test
```

Expected output:

```text
novis tests: ok
```

### 3. Start the REPL

```bash
./novis
```

```text
Novis 1.0.0 — type 'exit' or Ctrl-D to quit
Created by Loth Mejía Martínez · México · 2026
>>> x := 2 + 3 * 4
>>> x + 1
15
>>> exit
Bye.
```

### 4. Run a script (interpreter)

```bash
./novis run examples/bank_risk.novis
```

Output:

```text
true
```

### 5. Build a native binary (real speed)

The interpreter is portable, but if you want **actual native speed**, the Novis compiler can lower the AST to C++ and shell out to clang++ to produce a native binary. The native binary has zero Python dependency and runs at C++ speed.

```bash
novis build examples/bank_risk.novis
```

That command:

1. Parses and type-checks `examples/bank_risk.novis`.
2. Emits C++ from the AST (using the `src/native.h` codegen).
3. Calls `clang++ -std=c++17 -O2` to compile that C++ into a native executable.
4. Runs the executable.

#### Benchmark: Novis native vs CPython 3.14 on Apple M1

```text
INTERPRETER (novis run = tree-walking, slow but portable)
fib(30)                         novis= 26915ms  py=    91ms  ratio=295.77x
sum_squares(50000)              novis=    12ms  py=    21ms  ratio=0.57x   (Novis 1.75x faster!)

NATIVE BUILD (novis build = C++ codegen, fast)
fib(30)                          native=  38ms  py=    72ms  ratio=0.53x   (Novis 1.89x faster!)
sum_squares(50000)               native=   3ms  py=    17ms  ratio=0.18x   (Novis 5.6x faster!)
```

The native build path is **faster than CPython 3.14** on both `fib(30)` (1.9×) and `sum_squares(50000)` (5.6×). Run it yourself:

```bash
ITERS=3 N_FIB=30 N_SUM=50000 benchmarks/run_bench.sh
```

The interpreter is intentionally kept as a fallback for code that hasn't been migrated to the native backend yet. It also remains useful for fast REPL feedback while iterating.

---

## 🧰 CLI

```text
novis                              # REPL
novis run <file.novis>              # type-check and execute
novis check <file.novis>            # type-check only
novis emit-ir <file.novis>          # emit NovisIR
novis importpy <package>            # install native provider facade
novis importpy --list               # list known native providers
novis env init                      # create .novis project environment
novis env info                      # show environment status
novis env list                      # list installed native providers
novis <file.novis>                  # alias for run
novis --emit-ir <file.novis>        # legacy flag alias
```

---

## 🧪 Demo: banking + AI

This is the `examples/bank_risk.novis` demo. It blends a financial request, a tensor feature vector, and a custom approval rule. The same shape works for real credit scoring pipelines.

```novis
import std.bank
import std.ai
import std.math

pub type CreditApplication:
    pub id: str
    requested: money
    features: Tensor<f32>

pub fn approve(requested: money, features: Tensor<f32>) -> bool:
    score: f32 = risk_score(features)
    adjusted: f32 = clamp(score, 0, 1)
    if requested > money("5000.00", "MXN"):
        return adjusted < 0.35
    return adjusted < 0.60

application_amount: money = money("3500.00", "MXN")
features: Tensor<f32> = [10000, 2500, 1]
print(approve(application_amount, features))
```

Run it:

```bash
./novis run examples/bank_risk.novis
```

Output:

```text
true
```

Type-check only:

```bash
./novis check examples/bank_risk.novis
```

Output:

```text
ok: examples/bank_risk.novis checked (modules: std.bank std.core std.ai std.math)
```

Emit IR:

```bash
./novis emit-ir examples/bank_risk.novis
```

Output (excerpt):

```text
; NovisIR v1.0
; Created by Loth Mejía Martínez · México · 2026
; target: wasm-first | llvm-ready | banking-ai-runtime | framework-ready-types | module-aware
module @main {
  pub type @CreditApplication {
    pub id: str
    requested: money(decimal(scale=6), currency)
    features: Tensor<f32>
  }
  pub fn @approve(%requested: money(decimal(scale=6), currency), %features: Tensor<f32>) -> bool {
    ...
  }
}
```

---

## 🧮 Demo: tensors, statistics, scoring

`tests/banking_ai.novis`

```novis
import std.bank
import std.ai
import std.math

capital: money = money("1000.00", "MXN")
fee: money = money("12.345", "MXN")
total: money = round_bankers(capital + fee, 2)

features: Tensor<f32> = [1, 2, 3]
weights: Tensor<f32> = [0.1, 0.2, 0.3]
score: f32 = classify(features, weights)

print(total)
print(clamp(score, 0, 1))
print(balanced(total, debit(total)))
```

```text
MXN 1012.34
0.802183888559
true
```

---

## 🐼 Demo: `importpy` native providers

You can ask Novis to install a familiar Python package name. It does **not** pull CPython; it writes a Novis-native facade under `.novis/packages/`.

```bash
./novis importpy pandas numpy
```

```text
✓ importpy installed: pandas
  Pyra module: import pandas
  Native provider: novis.data.arrow
  Backend: Apache Arrow native memory + Novis kernels
  Python runtime required: no

✓ importpy installed: numpy
  Pyra module: import numpy
  Native provider: novis.tensor.native
  Backend: Accelerate/OpenBLAS/oneDNN/Wasm SIMD
  Python runtime required: no
```

> The line that reads `Pyra module: import ...` is the visible user message. The internal provider name is `novis.*`.

Use it:

```novis
import pandas
import numpy

xs: Tensor<f32> = [1, 2, 3, 4]
ys: Tensor<f32> = [0.1, 0.2, 0.3, 0.4]

print(pd_mean(xs), pd_count(xs), np_dot(xs, ys))
```

```text
2.5 4 3
```

Try an unknown package:

```bash
./novis importpy paquete_raro
```

```text
error: no native importpy provider for 'paquete_raro'
help: run `pyra importpy --list` to see supported packages.
note: Pyra will not silently fall back to CPython because this language is designed to stay compiled/native.
```

> Novis fails loudly. No silent CPython fallback. That is by design.

---

## 🧱 Demo: domain models + interfaces + generics

`tests/framework_contracts.novis`

```novis
import std.core

pub type Account:
    pub id: str
    balance: money
    tags: List<str>

pub interface Repository<T>:
    fn find(id: str) -> Option<T>
    fn save(item: T) -> Result<T, Error>

pub fn identity_account<T>(item: T) -> T:
    return item

value: money = money("25.00", "USD")
print(identity_account(value))
```

```text
USD 25.00
```

---

## 📚 Language tour

### Variables and inference

```novis
age := 25
name: str = "Loth"
score: f32 = 0.87
active: bool = true
```

`:=` is inferred; `:` + `=` is explicit. Both work in any scope.

### Banking arithmetic

```novis
subtotal: money = money("1000.00", "MXN")
fee: money = money("12.345", "MXN")
total: money = round_bankers(subtotal + fee, 2)
print(total)
```

```text
MXN 1012.34
```

Bad usage is caught by the type checker:

```novis
bad: money = decimal("10.00")
```

```text
error: type error: Cannot initialize 'bad' of type money with decimal
help: money values need an explicit currency, e.g. money("10.00", "MXN")
```

### Functions

```novis
pub fn risk_for(features: Tensor<f32>) -> f32:
    return risk_score(features)

features: Tensor<f32> = [10000, 2500, 1]
print(risk_for(features))
```

### Domain models and contracts

```novis
pub type Account:
    pub id: str
    balance: money
    risk_features: Tensor<f32>

pub interface Repository<T>:
    fn find(id: str) -> Option<T>
    fn save(item: T) -> Result<T, Error>
```

### `pub` for stable APIs

```novis
pub type Account: ...
pub interface Repository<T>: ...
pub fn approve(...) -> bool: ...
```

---

## 📐 Builtins (runtime)

| Category | Functions |
|---|---|
| I/O | `print`, `read_text`, `write_text` |
| Containers | `len` |
| Math | `sqrt`, `pow` |
| Banking | `decimal`, `money`, `currency`, `round_bankers`, `is_balanced` |
| Tensor | `tensor` / `Tensor`, `shape`, `sum`, `mean`, `variance`, `stddev`, `min`, `max`, `dot` |
| AI | `relu`, `sigmoid`, `softmax`, `argmax`, `risk_score` |

All are native, no Python involved.

---

## 🧩 Modules and `importpy` providers

```novis
import std.bank
import std.ai
import std.math
from std.core import Option
```

Embedded modules: `std.core`, `std.bank`, `std.ai`, `std.math`, `std.http`, `std.fs`, `std.string`.

Resolver order for `import foo.bar`:

```text
./foo/bar.novis
./foo/bar/mod.novis
./.novis/packages/foo/bar.novis
./.novis/packages/foo/bar/mod.novis
foo/bar.novis
foo/bar/mod.novis
```

`importpy` currently maps:

| Python name | Native provider | Runtime |
|---|---|---|
| `numpy` | `novis.tensor.native` | Native |
| `pandas` | `novis.data.arrow` | Native |
| `scipy` | `novis.science.native` | Native |
| `torch` | `novis.ai.libtorch_or_onnx` | Native |
| `sklearn` | `novis.ai.onnx` | Native |
| `requests` | `novis.http.native` | Native |
| `cryptography` | `novis.crypto.native` | Native |

---

## 🌍 Project environment (`novis env`)

```bash
./novis env init
```

```text
✓ Novis env ready
  Created by: Loth Mejía Martínez · México · 2026
  Root: .
  Manifest: ./.novis/env.toml
  Packages: ./.novis/packages
  Python runtime: not required
```

Layout:

```text
.novis/
  env.toml
  importpy.lock
  packages/
  build/
  cache/
  bin/
```

This is **not** a Python virtualenv. It is a project-isolated directory for Novis-native artifacts. `python_runtime = false` is enforced in `env.toml`.

---

## 🏗 Architecture

```text
            source.novis
                 │
            ┌────▼────┐
            │ Lexer   │  indentation-aware tokens
            └────┬────┘
                 │
            ┌────▼────┐
            │ Parser  │  Pratt expressions + recursive descent
            └────┬────┘
                 │
            ┌────▼────┐
            │   AST   │  visitor-friendly nodes
            └────┬────┘
                 │
            ┌────▼────────┐
            │ Type check  │  nominal + generics + contracts
            └────┬────────┘
                 │
            ┌────▼───────┐
            │ Evaluator  │  money/decimal/tensor runtime
            └────┬───────┘
                 │
            ┌────▼─────┐
            │ NovisIR  │  textual typed IR
            └────┬─────┘
                 │
        (next) LLVM or Wasm
```

Source layout:

```text
src/
  token.h          # token kinds
  lexer.h          # indentation-aware lexing
  ast.h            # AST + visitors
  parser.h         # statements + expressions
  typechecker.h    # type rules + generic unification
  evaluator.h      # runtime + money/decimal/tensor primitives
  compiler.h       # NovisIR emitter
  module_resolver.h# imports + embedded stdlib
  importpy.h       # native provider registry + installer
  env.h            # novis env project environment
  diagnostics.h    # friendly error rendering
  main.cpp         # CLI (the only compiled file)
```

Header-only design: only `main.cpp` is compiled. Edits to `.h` files trigger a single recompilation of `main.cpp`.

---

## 🛣 Roadmap

**Now**

- Real Wasm emitter.
- Replace textual `NovisIR` with a typed IR ready for LLVM lowering.
- Async runtime for `spawn` / `await`.

**Next**

- Native bindings: Arrow, BLAS, libtorch/ONNX, OpenSSL.
- Borrow-style ownership for hot kernels.
- Package registry + `novis add` over the wire.

**Later**

- Audit trail + source maps for financial compliance reviews.
- Self-hosted compiler (Novis written in Novis).

---

## 🧪 Tests

Run the full suite:

```bash
make test
```

Coverage:

| Test                          | What it checks                                                |
|-------------------------------|---------------------------------------------------------------|
| `tests/banking_ai.novis`      | Banking math, tensor scoring, balanced ledger                 |
| `tests/framework_contracts.novis` | `type`, `interface`, generic functions                   |
| `tests/stdlib_fs_string.novis`| Embedded stdlib for `fs` and `string`                         |
| `tests/importpy_native.novis` | `importpy` native providers in real source                    |
| `tests/diagnostic_bad.novis`  | Friendly type error with help text                            |
| `examples/bank_risk.novis`    | Banking + AI demo referenced by `make test`                   |

---

## 🤝 Contributing

1. `make`
2. `make test`
3. Add a test under `tests/` and wire it in `tests/run_tests.sh`.
4. Keep the header-only architecture; touch `main.cpp` only for CLI plumbing.
5. Add new builtins in `evaluator.h` and their type rules in `typechecker.h`.
6. Add new `importpy` providers in `importpy.h`.

The most valuable contributions right now are:

- More language tests.
- New runtime primitives.
- New `importpy` native providers.
- A real Wasm or LLVM backend.

---

## 🪪 License

Prototype by Loth Mejía Martínez, México, 2026. Use it, fork it, teach with it, build on it.
