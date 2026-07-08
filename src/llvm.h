#pragma once

// =============================================================================
// Novis LLVM backend — emit C++, lower to LLVM IR with clang, then link.
// =============================================================================
// This is the production-grade LLVM path:
//   1) Generate clean C++ from the AST (via NativeCompiler).
//   2) Invoke `clang -O2 -emit-llvm -S` to lower it to a `.ll` file.
//   3) Invoke `clang -O2 -x ir` to assemble the `.ll` into a native binary.
//   4) Run the binary.
//
// The intermediate `.ll` is real LLVM IR that you can read, run through
// `opt`, or feed to any other LLVM tool. The point of having an explicit
// LLVM path (vs. the C++ path) is twofold:
//   * Auditability: `novis emit-llvm foo.novis` writes the .ll next to the
//     source so the user can inspect what the compiler produced.
//   * Interop: any LLVM tool (sanitizers, profile-guided opt, Polly, custom
//     passes) can be plugged in by editing the driver command in
//     LLVMDriver::run.

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

class LLVMDriver {
public:
    // emit-llvm <file> — produce a textual .ll file alongside the source.
    int emit_llvm(const std::string& src_path) {
        std::vector<std::unique_ptr<Stmt>> program;
        if (!parse_and_check(src_path, program)) return 2;

        NativeCompiler nc;
        std::string cpp = nc.compile(program, src_path);

        char tmpl_cpp[] = "/tmp/novis_llvm_src_XXXXXX.cpp";
        int fd_cpp = mkstemps(tmpl_cpp, 4);
        if (fd_cpp < 0) return 3;
        ::close(fd_cpp);
        std::string cpp_path = tmpl_cpp;
        std::ofstream(cpp_path) << cpp;

        // Write the .ll next to the source for visibility: foo.novis -> foo.ll
        std::string ll_path = src_path + ".ll";
        std::string cmd = "clang++ -std=c++17 -O2 -emit-llvm -S -Isrc "
                        + cpp_path + " -o " + ll_path + " 2>&1";
        FILE* p = popen(cmd.c_str(), "r");
        if (!p) return 4;
        char buf[4096]; std::string log;
        while (std::fgets(buf, sizeof(buf), p)) log += buf;
        int rc = pclose(p);
        if (rc != 0) {
            std::fprintf(stderr, "error: clang -emit-llvm failed:\n%s\n", log.c_str());
            return rc;
        }
        std::printf("wrote %s\n", ll_path.c_str());
        return 0;
    }

    // llvm <file> — full pipeline: C++ -> .ll -> native -> run.
    int run(const std::string& src_path) {
        std::vector<std::unique_ptr<Stmt>> program;
        if (!parse_and_check(src_path, program)) return 2;

        NativeCompiler nc;
        std::string cpp = nc.compile(program, src_path);

        char tmpl_cpp[] = "/tmp/novis_llvm_src_XXXXXX.cpp";
        int fd_cpp = mkstemps(tmpl_cpp, 4);
        if (fd_cpp < 0) return 3;
        ::close(fd_cpp);
        std::string cpp_path = tmpl_cpp;
        std::ofstream(cpp_path) << cpp;

        char tmpl_ll[] = "/tmp/novis_llvm_ir_XXXXXX.ll";
        int fd_ll = mkstemps(tmpl_ll, 3);
        if (fd_ll < 0) return 4;
        ::close(fd_ll);
        std::string ll_path = tmpl_ll;

        char tmpl_bin[] = "/tmp/novis_llvm_bin_XXXXXX";
        int bin_p = mkstemp(tmpl_bin);
        if (bin_p < 0) return 5;
        ::close(bin_p);
        std::string bin_path = std::string(tmpl_bin) + ".out";

        // Step 1: clang -emit-llvm -S to lower C++ to textual IR
        std::string cmd1 = "clang++ -std=c++17 -O2 -emit-llvm -S -Isrc "
                         + cpp_path + " -o " + ll_path + " 2>&1";
        FILE* p1 = popen(cmd1.c_str(), "r");
        if (!p1) return 6;
        char buf[4096]; std::string log1;
        while (std::fgets(buf, sizeof(buf), p1)) log1 += buf;
        int rc1 = pclose(p1);
        if (rc1 != 0) {
            std::fprintf(stderr, "error: clang -emit-llvm failed:\n%s\n", log1.c_str());
            return rc1;
        }

        // Step 2: clang -x ir to assemble IR into a native executable
        std::string cmd2 = "clang++ -std=c++17 -O2 -x ir "
                         + ll_path + " -o " + bin_path + " 2>&1";
        FILE* p2 = popen(cmd2.c_str(), "r");
        if (!p2) return 7;
        std::string log2;
        while (std::fgets(buf, sizeof(buf), p2)) log2 += buf;
        int rc2 = pclose(p2);
        if (rc2 != 0) {
            std::fprintf(stderr, "error: clang -x ir failed:\n%s\n", log2.c_str());
            return rc2;
        }

        // Step 3: run it
        return std::system(bin_path.c_str());
    }

private:
    bool parse_and_check(const std::string& src_path,
                         std::vector<std::unique_ptr<Stmt>>& out) {
        std::ifstream in(src_path);
        if (!in) {
            std::fprintf(stderr, "error: cannot open %s\n", src_path.c_str());
            return false;
        }
        std::stringstream ss; ss << in.rdbuf();
        std::string source = ss.str();
        Lexer lex(source);
        std::vector<Token> tokens;
        for (auto& t : lex.tokenize()) tokens.push_back(std::move(t));
        Parser parser(tokens);
        out = parser.parse_program();
        try {
            TypeChecker tc;
            tc.check(out);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "error: %s\n", e.what());
            return false;
        }
        return true;
    }
};
