#pragma once

// =============================================================================
// Novis Wasm backend — compile Novis to Wasm32 + run under node.
// =============================================================================
// Pipeline:
//   1) Lower the AST to C++ via NativeCompiler.
//   2) `clang --target=wasm32-unknown-unknown -O2 -emit-llvm -S` -> .ll
//   3) `llc -march=wasm32 -filetype=obj` -> .o
//   4) `wasm-ld` -> .wasm  (or `clang -target wasm32` final link)
//   5) Run the .wasm under `node` (with a tiny JS shim that exports main()).
//
// Why a Wasm target? Banking and AI workloads that need sandboxed execution
// (e.g. customer-side fraud scoring, on-device inference, browser-side
// preprocessing) benefit from Wasm's security model and portability. Novis
// can compile the same source to either a native binary or a Wasm module
// without changes to the user's code.
//
// Note: a small piece of platform glue is required because Novis's runtime
// touches a few POSIX-ish headers (sys/stat.h, unistd.h). On wasm32 these
// are absent; the Wasm driver uses `-nostdlib` and links a stub libc that
// forwards prints via JS. We keep that stub in this file's WasmStub.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "ast.h"
#include "evaluator.h"
#include "native.h"
#include "parser.h"
#include "typechecker.h"

class WasmDriver {
public:
    int run(const std::string& src_path) {
        std::ifstream in(src_path);
        if (!in) { std::fprintf(stderr, "error: cannot open %s\n", src_path.c_str()); return 1; }
        std::stringstream ss; ss << in.rdbuf();
        std::string source = ss.str();

        Lexer lex(source);
        std::vector<Token> tokens;
        for (auto& t : lex.tokenize()) tokens.push_back(std::move(t));
        Parser parser(tokens);
        auto program = parser.parse_program();
        try {
            TypeChecker tc;
            tc.check(program);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            return 2;
        }

        NativeCompiler nc;
        std::string cpp = nc.compile(program, src_path);

        char tmpl_cpp[] = "/tmp/novis_wasm_src_XXXXXX.cpp";
        int fd_cpp = mkstemps(tmpl_cpp, 4);
        if (fd_cpp < 0) return 3;
        ::close(fd_cpp);
        std::string cpp_path = tmpl_cpp;
        std::ofstream(cpp_path) << cpp;

        char tmpl_wat[] = "/tmp/novis_wasm_XXXXXX.wasm";
        int fd_wat = mkstemps(tmpl_wat, 5);
        if (fd_wat < 0) return 4;
        ::close(fd_wat);
        std::string wasm_path = tmpl_wat;

        // Build a tiny JS shim that loads the .wasm and calls its exports.
        // We use `node` (available on the dev box) instead of wabt's wasmtime
        // so we don't need a separate runtime install. The shim:
        //   * instantiates the module
        //   * calls main() once
        //   * mirrors stdout from the Wasm linear memory to process.stdout
        char tmpl_shim[] = "/tmp/novis_wasm_shim_XXXXXX.js";
        int fd_shim = mkstemps(tmpl_shim, 3);
        if (fd_shim < 0) return 5;
        std::string shim_path = tmpl_shim;
        std::string shim_js = build_shim_js(wasm_path);
        write_all(shim_path, shim_js);

        // Step 1: clang --target=wasm32 -O2 -> .wasm directly. We rely on
        // the embedded `main` to call nv_print which uses a JS-imported
        // `host_print` defined in the shim.
        //
        // We use `-nostdlib++` (NOT `-nostdlib`) so we keep C++ standard
        // headers, but explicitly opt out of the C++ runtime; everything
        // we use is header-only. The wasm_stub provides the few C symbols
        // (printf, putchar, puts) we touch.
        //
        // We prefer the Homebrew llvm@22 clang because the Apple system
        // clang doesn't ship the wasm32 target.
        std::string clang_bin = "clang++";
        if (std::system("command -v /opt/homebrew/opt/llvm@22/bin/clang++ >/dev/null 2>&1") == 0) {
            clang_bin = "/opt/homebrew/opt/llvm@22/bin/clang++";
        }
        std::string cmd = clang_bin + " --target=wasm32-unknown-unknown "
                        "-O2 -std=c++17 -nostdlib++ "
                        "-Wl,--no-entry "
                        "-Wl,--export=main -Wl,--export=host_print "
                        "-Wl,--export=__wasm_call_ctors "
                        "-Wl,--allow-undefined -fno-exceptions "
                        "-Isrc "
                        + cpp_path + " src/wasm_stub.cpp "
                        + " -o " + wasm_path + " 2>&1";
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) return 6;
        char buf[4096]; std::string log;
        while (std::fgets(buf, sizeof(buf), p)) log += buf;
        int rc = pclose(p);
        if (rc != 0) {
            if (log.find("wasm32") != std::string::npos &&
                log.find("No available targets") != std::string::npos) {
                std::fprintf(stderr,
                    "error: clang on this machine does not ship the wasm32 target.\n"
                    "       Install one of:\n"
                    "         brew install llvm          # adds wasm32 to the LLVM clang\n"
                    "         brew install wabt          # adds wasm2wat and wat2wasm CLI\n"
                    "         brew install wabt/wasi-sdk/wasi-sdk   # full wasi-sysroot\n"
                    "       Then re-run: novis wasm %s\n", src_path.c_str());
            } else {
                std::fprintf(stderr, "error: clang wasm32 build failed:\n%s\n", log.c_str());
            }
            return rc;
        }

        // Step 2: node runs the shim which loads the .wasm
        std::string runcmd = "node " + shim_path + " 2>&1";
        int rrc = std::system(runcmd.c_str());
        return rrc;
    }

private:
    static void write_all(const std::string& path, const std::string& s) {
        std::ofstream f(path);
        f << s;
    }

    static std::string build_shim_js(const std::string& wasm_path) {
        // Minimal host shim: a WebAssembly.Memory is shared between JS and
        // the Wasm module. The Wasm module imports `host_print(ptr, len)`
        // which writes the bytes pointed to by (ptr, len) in the Wasm
        // memory to process.stdout. The module also exports `main()`.
        std::ostringstream s;
        s << "// Auto-generated by Novis wasm driver\n";
        s << "const fs = require('fs');\n";
        s << "const wasmBytes = fs.readFileSync('" << wasm_path << "');\n";
        s << "const memory = new WebAssembly.Memory({ initial: 256, maximum: 256 });\n";
        s << "const view = new Uint8Array(memory.buffer);\n";
        s << "const importObj = {\n";
        s << "  env: {\n";
        s << "    memory: memory,\n";
        s << "    host_print: (ptr, len) => {\n";
        s << "      const bytes = view.subarray(ptr, ptr + len);\n";
        s << "      process.stdout.write(Buffer.from(bytes));\n";
        s << "    }\n";
        s << "  }\n";
        s << "};\n";
        s << "WebAssembly.instantiate(wasmBytes, importObj).then(mod => {\n";
        s << "  if (mod.instance.exports.main) mod.instance.exports.main();\n";
        s << "}).catch(err => { console.error(err); process.exit(1); });\n";
        return s.str();
    }
};
