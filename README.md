# Novis

**Created by Loth Mejía Martínez · México · 2026**

Novis is a compiled, strongly typed programming language for the next generation of financial systems, AI workloads, statistical computing, and WebAssembly-first applications.

The goal is simple: keep the clean readability people like from Python, but move the language toward native speed, deterministic financial primitives, strong type contracts, and modern tooling.

Novis is currently a production-oriented compiler prototype: it has a lexer, parser, AST, semantic type checker, interpreter/runtime, module resolver, native-provider package facade, project environment, and a textual IR backend (`NovisIR`) ready to evolve toward LLVM/Wasm.

---

## Why Novis?

Most financial institutions still depend on old systems because they are predictable, auditable, and stable. Most modern AI/data ecosystems are productive because they are expressive and rich. Novis is designed to connect those worlds:

- **Banking-grade primitives**: `money`, `decimal`, banker's rounding, currency-safe arithmetic.
- **AI/statistics first**: `Tensor<f32>`, `mean`, `variance`, `dot`, `softmax`, `risk_score`, and native-provider facades for Python ecosystem names.
- **Strong typing with inference**: write fast, but keep contracts checkable.
- **Framework-ready types**: `type`, `interface`, `pub`, generics, and module imports.
- **No CPython dependency**: `importpy` installs Novis-native facades instead of running Python underneath.
- **WebAssembly/LLVM-ready design**: current backend emits `NovisIR`; future backend can lower typed IR to LLVM or Wasm.

---

## Current status

Novis is not claiming to be a finished Go/Rust/Python replacement yet. It is a serious language prototype with a working end-to-end toolchain.

Implemented today:

- Indentation-aware lexer.
- Recursive descent + Pratt parser.
- AST with visitors.
- Static type checker.
- User-defined functions.
- Banking and AI runtime primitives.
- Module resolver.
- Embedded standard library modules.
- `importpy` native-provider installer.
- `.novis` project environment.
- `NovisIR` textual backend.
- Test suite via `make test`.

Not implemented yet:

- Native LLVM code generation.
- Real Wasm emitter.
- Full async scheduler for `spawn` / `await`.
- Real Arrow/BLAS/ONNX/libtorch bindings behind `importpy` providers.
- Package publishing/registry.

---

## Quick start

### Build

```bash
make
```

### Run tests

```bash
make test
```

Expected output:

```text
novis tests: ok
```

### Start the REPL

```bash
./novis
```

Example:

```text
Novis 1.0.0 — type 'exit' or Ctrl-D to quit
Created by Loth Mejía Martínez · México · 2026
>>> x := 2 + 3 * 4
>>> x + 1
15
>>> exit
Bye.
```

---

## CLI

```bash
./novis                         # REPL
./novis run <file.novis>         # type-check and execute
./novis check <file.novis>       # type-check only
./novis emit-ir <file.novis>     # emit NovisIR
./novis importpy <package>       # install native provider facade
./novis importpy --list          # list known native providers
./novis env init                 # create .novis project environment
./novis env info                 # show environment status
./novis env list                 # list installed native providers
```

`./novis <file.novis>` is an alias for `./novis run <file.novis>`.

---

## A first Novis program

Create `risk.novis`:

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
./novis run risk.novis
```

Output:

```text
true
```

Type-check without running:

```bash
./novis check risk.novis
```

Emit IR:

```bash
./novis emit-ir risk.novis
```

---

## Language tour

### Variables and inference

Use `:=` for inferred declarations and `:` + `=` for explicit typed declarations.

```novis
age := 25
name: str = "Loth"
score: f32 = 0.87
active: bool = true
```

### Banking types

```novis
subtotal: money = money("1000.00", "MXN")
fee: money = money("12.345", "MXN")
total: money = round_bankers(subtotal + fee, 2)

print(total)
```

Output:

```text
MXN 1012.34
```

`money` values carry currency. Novis rejects unsafe initialization like:

```novis
bad: money = decimal("10.00")
```

Diagnostic example:

```text
error: type error: Cannot initialize 'bad' of type money with decimal
help: money values need an explicit currency, e.g. money("10.00", "MXN")
```

### Tensors and statistics

```novis
xs: Tensor<f32> = [1, 2, 3, 4]
weights: Tensor<f32> = [0.1, 0.2, 0.3, 0.4]

print(mean(xs))
print(variance(xs))
print(dot(xs, weights))
print(softmax([1, 2, 3]))
```

### Functions

```novis
pub fn risk_for(features: Tensor<f32>) -> f32:
    return risk_score(features)

features: Tensor<f32> = [10000, 2500, 1]
print(risk_for(features))
```

### Domain models

```novis
pub type Account:
    pub id: str
    balance: money
    risk_features: Tensor<f32>
```

### Interfaces and generics

```novis
pub interface Repository<T>:
    fn find(id: str) -> Option<T>
    fn save(item: T) -> Result<T, Error>

pub fn identity<T>(value: T) -> T:
    return value
```

---

## Modules

Novis supports imports:

```novis
import std.bank
import std.ai
import std.math
from std.core import Option
```

Current embedded modules:

- `std.core`
- `std.bank`
- `std.ai`
- `std.math`
- `std.http`
- `std.fs`
- `std.string`

The resolver also looks for local modules:

```text
module_name.novis
module_name/mod.novis
.novis/packages/module_name/mod.novis
```

---

## Project environments

Create an isolated Novis project environment:

```bash
./novis env init
```

This creates:

```text
.novis/
  env.toml
  importpy.lock
  packages/
  build/
  cache/
  bin/
```

Show status:

```bash
./novis env info
```

List installed native providers:

```bash
./novis env list
```

This environment is not a Python virtualenv. It does not create, embed, or require CPython.

---

## `importpy`: Python ecosystem names, native Novis providers

Novis can install familiar Python package facades while keeping execution native:

```bash
./novis importpy pandas numpy
```

This installs generated Novis modules under:

```text
.novis/packages/pandas/mod.novis
.novis/packages/numpy/mod.novis
```

Then use them in source code:

```novis
import pandas
import numpy

xs: Tensor<f32> = [1, 2, 3, 4]
ys: Tensor<f32> = [0.1, 0.2, 0.3, 0.4]

print(pd_mean(xs), pd_count(xs), np_dot(xs, ys))
```

Current native providers in `importpy --list`:

| Python package | Native provider name                          | Python runtime required |
|----------------|------------------------------------------------|-------------------------|
| numpy          | `novis.tensor.native`                          | No                      |
| pandas         | `novis.data.arrow`                             | No                      |
| scipy          | `novis.science.native`                         | No                      |
| torch          | `novis.ai.libtorch_or_onnx`                    | No                      |
| sklearn        | `novis.ai.onnx`                                | No                      |
| requests       | `novis.http.native`                            | No                      |
| cryptography   | `novis.crypto.native`                          | No                      |

If you request an unknown package, Novis fails with a clear error and does not silently fall back to CPython. That is by design: Novis is designed to stay native.

---

## Architecture overview

```text
source.novis
    ↓
Lexer          (indentation-aware tokens)
    ↓
Parser         (Pratt expressions + recursive descent)
    ↓
AST            (visitor-friendly nodes)
    ↓
Type Checker   (nominal types, interfaces, generics, contracts)
    ↓
Evaluator      (runtime / interpreter)
    ↓
NovisIR        (typed text IR, ready to lower to LLVM or Wasm)
```

All of the above live in `src/`:

- `token.h` / `lexer.h` — token kinds and indentation-aware lexing.
- `ast.h` — AST nodes and visitors.
- `parser.h` — statement and expression parser.
- `typechecker.h` — type rules, registry, generic unification.
- `evaluator.h` — runtime, money/decimal/tensor semantics, builtins.
- `compiler.h` — `NovisIR` emitter.
- `module_resolver.h` — imports and embedded standard library.
- `importpy.h` — `importpy` native-provider registry and installer.
- `env.h` — `novis env` project environment manager.
- `diagnostics.h` — friendly error rendering.
- `main.cpp` — CLI entry point.

`main.cpp` is the only file compiled. Everything else is header-only for fast rebuilds.

---

## Roadmap

Short term:

- Real Wasm emitter.
- Real LLVM backend (replacing the textual `NovisIR` once a typed IR is stable).
- Real async runtime for `spawn` / `await`.
- Native bindings: Arrow, BLAS, libtorch/ONNX, OpenSSL.

Medium term:

- Borrow-style ownership for performance-critical kernels.
- Per-module type checking of imported names.
- Package registry + `novis add` over the wire.

Long term:

- Audit trail and source mapping suitable for financial compliance reviews.
- Full self-hosted compiler (Novis compiler written in Novis).

---

## Contributing

Novis is an early-stage language. Most of the value right now is in:

- Adding primitives to the type checker / runtime / IR.
- Writing more language tests under `tests/`.
- Adding new `importpy` native providers.
- Replacing the textual `NovisIR` emitter with a real backend.

Workflow:

1. Build: `make`
2. Run tests: `make test`
3. Add a test under `tests/` and a corresponding entry in `tests/run_tests.sh`.
4. Keep the header-only architecture; touch `main.cpp` only for CLI plumbing.

---

## License

This project is provided as a working compiler prototype by Loth Mejía Martínez, México, 2026. Use it, fork it, teach with it, build on it.
