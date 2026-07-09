// =============================================================================
// Novis compiler entry point
// =============================================================================
// Novis 1.0 practical toolchain: REPL, check, run, import expansion and NovisIR.
// The implementation is header-only except this file to keep builds fast.

#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ast.h"
#include "compiler.h"
#include "diagnostics.h"
#include "env.h"
#include "evaluator.h"
#include "importpy.h"
#include "lexer.h"
#include "module_resolver.h"
#include "native.h"
#include "parser.h"
#include "token.h"
#include "typechecker.h"
#include "llvm.h"
#include "wasm.h"
#ifdef NOVIS_HAS_ZYNTA
#include "zynta_runtime.h"
#endif
namespace {

constexpr const char* NOVIS_VERSION = "1.0.0";
constexpr const char* NOVIS_CREATED_BY = "Created by Loth Mejía Martínez · México · 2026";

std::string format_double(double value) {
    std::ostringstream ss;
    ss << std::setprecision(12) << value;
    return ss.str();
}

std::string format_decimal(Decimal d, int min_frac = 0) {
    int64_t units = d.units;
    bool neg = units < 0;
    if (neg) units = -units;
    int64_t whole = units / Decimal::SCALE_FACTOR;
    int64_t frac = units % Decimal::SCALE_FACTOR;

    std::ostringstream ss;
    if (neg) ss << '-';
    ss << whole;
    if (frac != 0 || min_frac > 0) {
        std::ostringstream fs;
        fs << std::setw(6) << std::setfill('0') << frac;
        std::string frac_text = fs.str();
        while (static_cast<int>(frac_text.size()) > min_frac &&
               !frac_text.empty() && frac_text.back() == '0') {
            frac_text.pop_back();
        }
        ss << '.' << frac_text;
    }
    return ss.str();
}

std::string value_to_repr(const Value& v) {
    if (std::holds_alternative<int64_t>(v))     return std::to_string(std::get<int64_t>(v));
    if (std::holds_alternative<double>(v))      return format_double(std::get<double>(v));
    if (std::holds_alternative<bool>(v))        return std::get<bool>(v) ? "true" : "false";
    if (std::holds_alternative<Decimal>(v))     return format_decimal(std::get<Decimal>(v));
    if (std::holds_alternative<Money>(v)) {
        const Money& m = std::get<Money>(v);
        return m.currency + " " + format_decimal(m.amount, 2);
    }
    if (std::holds_alternative<Tensor>(v)) {
        const Tensor& t = std::get<Tensor>(v);
        std::ostringstream ss;
        ss << '[';
        for (std::size_t i = 0; i < t.data.size(); ++i) {
            if (i) ss << ", ";
            ss << format_double(t.data[i]);
        }
        ss << ']';
        return ss.str();
    }
    if (std::holds_alternative<std::shared_ptr<UserFunction>>(v)) return "<function>";
    return std::get<std::string>(v);
}

std::string read_source_file(const std::string& path) {
    if (path == "-") {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        return ss.str();
    }
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open '" + path + "'");
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool render_failure(const std::string& path,
                    const std::string& active_source,
                    const std::exception& ex) {
    SourceFile file(path, active_source);
    std::cerr << file.render_error(ex.what());
    return false;
}

bool check_source(const std::string& path, const std::string& source, bool quiet = false) {
    std::string active_source = source;
    try {
        ModuleResolver resolver;
        ResolvedSource resolved = resolver.resolve_virtual(path, source);
        active_source = resolved.source;

        Lexer lex(active_source);
        std::vector<Token> tokens = lex.tokenize();
        Parser parser(std::move(tokens));
        auto program = parser.parse_program();
        TypeChecker checker;
        checker.check(program);

        if (!quiet) {
            std::cout << "ok: " << path << " checked";
            if (!resolved.loaded_modules.empty()) {
                std::cout << " (modules:";
                for (const auto& m : resolved.loaded_modules) std::cout << ' ' << m;
                std::cout << ')';
            }
            std::cout << '\n';
        }
        return true;
    } catch (const std::exception& ex) {
        return render_failure(path, active_source, ex);
    }
}

bool run_source(const std::string& path, const std::string& source, Evaluator& eval) {
    std::string active_source = source;
    try {
        ModuleResolver resolver;
        ResolvedSource resolved = resolver.resolve_virtual(path, source);
        active_source = resolved.source;

        Lexer lex(active_source);
        std::vector<Token> tokens = lex.tokenize();
        Parser parser(std::move(tokens));
        auto program = parser.parse_program();
        TypeChecker checker;
        checker.check(program);
        (void)eval.evaluate(program);
        return true;
    } catch (const ReturnSignal&) {
        SourceFile file(path, active_source);
        std::cerr << file.render_error("return outside function");
        return false;
    } catch (const std::exception& ex) {
        return render_failure(path, active_source, ex);
    }
}

bool compile_source_to_ir(const std::string& path,
                          const std::string& source,
                          std::string& ir_out) {
    std::string active_source = source;
    try {
        ModuleResolver resolver;
        ResolvedSource resolved = resolver.resolve_virtual(path, source);
        active_source = resolved.source;

        Lexer lex(active_source);
        std::vector<Token> tokens = lex.tokenize();
        Parser parser(std::move(tokens));
        auto program = parser.parse_program();
        TypeChecker checker;
        checker.check(program);
        IRCompiler compiler;
        ir_out = compiler.compile(program);
        return true;
    } catch (const std::exception& ex) {
        return render_failure(path, active_source, ex);
    }
}

bool needs_more_input(const std::vector<Token>& tokens) {
    int balance = 0;
    for (const auto& t : tokens) {
        if (t.type == TokenType::INDENT) balance++;
        else if (t.type == TokenType::DEDENT) balance--;
    }
    if (balance > 0) return true;
    for (auto it = tokens.rbegin(); it != tokens.rend(); ++it) {
        if (it->type == TokenType::NEWLINE)   continue;
        if (it->type == TokenType::EOF_TOKEN) continue;
        if (it->type == TokenType::COLON)     return true;
        return false;
    }
    return false;
}

void run_repl() {
    Evaluator eval;
    TypeChecker checker;
    std::printf("Novis %s — type 'exit' or Ctrl-D to quit\n", NOVIS_VERSION);
    std::printf("%s\n", NOVIS_CREATED_BY);
    std::printf("%s", ">>> ");
    std::fflush(stdout);

    std::string buffer;
    std::string line;
    bool first_prompt = true;

    while (true) {
        if (!std::getline(std::cin, line)) {
            std::printf("\n");
            return;
        }
        if (!line.empty() && line.back() == '\r') line.pop_back();

        std::string trimmed = line;
        while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
            trimmed.erase(trimmed.begin());
        }
        if (trimmed == "exit" || trimmed == "quit") {
            std::printf("Bye.\n");
            return;
        }
        if (trimmed.empty()) {
            std::printf("%s", first_prompt ? ">>> " : "... ");
            std::fflush(stdout);
            continue;
        }

        if (!buffer.empty()) buffer += "\n";
        buffer += line;

        try {
            Lexer lex(buffer);
            auto tokens = lex.tokenize();
            if (needs_more_input(tokens)) {
                first_prompt = false;
                std::printf("%s", "... ");
                std::fflush(stdout);
                continue;
            }
        } catch (const std::exception& ex) {
            SourceFile file("<repl>", buffer);
            std::cerr << file.render_error(ex.what());
            buffer.clear();
            first_prompt = true;
            std::printf("%s", ">>> ");
            std::fflush(stdout);
            continue;
        }

        std::string active_source = buffer;
        try {
            ModuleResolver resolver;
            auto resolved = resolver.resolve_virtual("<repl>", buffer);
            active_source = resolved.source;

            Lexer lex(active_source);
            auto tokens = lex.tokenize();
            Parser parser(std::move(tokens));
            auto program = parser.parse_program();
            checker.check(program);
            Value last = eval.evaluate(program);

            bool all_decls = true;
            for (const auto& s : program) {
                if (dynamic_cast<ExprStmt*>(s.get()) ||
                    dynamic_cast<IfStmt*>(s.get()) ||
                    dynamic_cast<WhileStmt*>(s.get())) {
                    all_decls = false;
                    break;
                }
            }
            if (!all_decls || program.empty()) {
                std::printf("%s\n", value_to_repr(last).c_str());
            }
        } catch (const ReturnSignal&) {
            SourceFile file("<repl>", active_source);
            std::cerr << file.render_error("return outside function");
        } catch (const std::exception& ex) {
            SourceFile file("<repl>", active_source);
            std::cerr << file.render_error(ex.what());
        }

        buffer.clear();
        first_prompt = true;
        std::printf("%s", ">>> ");
        std::fflush(stdout);
    }
}

void print_help() {
    std::printf("Novis %s\n", NOVIS_VERSION);
    std::printf("%s\n", NOVIS_CREATED_BY);
    std::printf("usage:\n");
    std::printf("  novis                         # REPL\n");
    std::printf("  novis run <file.novis>          # type-check and execute (interpreter)\n");
    std::printf("  novis build <file.novis>        # compile to a native binary and run it\n");
    std::printf("  novis check <file.novis>        # type-check only\n");
    std::printf("  novis emit-ir <file.novis> [-o out.pir]\n");
    std::printf("  novis emit-llvm <file.novis>     # dump textual LLVM IR (.ll) alongside source\n");
    std::printf("  novis llvm <file.novis>          # full LLVM pipeline: C++ -> .ll -> native -> run\n");
    std::printf("  novis wasm <file.novis>          # Wasm32 pipeline: C++ -> .wat -> wasm32 -> run in node\n");
#ifdef NOVIS_HAS_ZYNTA
    std::printf("  novis zynta-serve <file.novis>   # REST server via the zynta framework\n");
#endif
    std::printf("  novis importpy <python-package>  # install native Novis provider facade\n");
    std::printf("  novis importpy --list            # list known Python native providers\n");
    std::printf("  novis env init                   # create isolated .novis environment\n");
    std::printf("  novis env info                   # show env details\n");
    std::printf("  novis env list                   # list installed native providers\n");
    std::printf("  novis <file.novis>              # alias for run\n");
    std::printf("  novis --emit-ir <file.novis> [-o out.pir]\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc <= 1) {
        run_repl();
        return 0;
    }

    std::string command = argv[1];
    if (command == "--help" || command == "-h" || command == "help") {
        print_help();
        return 0;
    }

    if (command == "llvm" || command == "emit-llvm" || command == "wasm") {
        if (argc < 3) { std::fprintf(stderr, "error: %s expects a file\n", command.c_str()); return 1; }
        std::string in_path = argv[2];
        if (command == "emit-llvm") return LLVMDriver{}.emit_llvm(in_path);
        if (command == "llvm")     return LLVMDriver{}.run(in_path);
        if (command == "wasm")     return WasmDriver{}.run(in_path);
    }

#ifdef NOVIS_HAS_ZYNTA
    if (command == "zynta-serve") {
        // Run a novis source file that uses the zynta builtins. Same
        // load + run as the regular `novis run` path, but with the
        // zynta runtime registered first so calls to zynta_app_new,
        // zynta_route, zynta_run resolve to the C++ side.
        if (argc < 3) {
            std::fprintf(stderr, "error: zynta-serve expects a file\n");
            return 1;
        }
        std::string in_path = argv[2];
        std::ifstream in(in_path);
        if (!in) { std::fprintf(stderr, "error: cannot open %s\n", in_path.c_str()); return 1; }
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
            SourceFile file(in_path, source);
            std::cerr << file.render_error(e.what());
            return 1;
        }
        Evaluator eval;
        zynta_runtime::register_zynta_builtins(eval);
        try {
            for (const auto& s : program) eval.execute_value(s.get());
        } catch (const std::exception& e) {
            std::cerr << "error: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }
#endif // NOVIS_HAS_ZYNTA

    if (command == "env") {
        NovisEnv env(".");
        if (argc < 3 || std::string(argv[2]) == "--help" || std::string(argv[2]) == "-h") {
            NovisEnv::help(std::cout);
            return argc < 3 ? 1 : 0;
        }
        std::string action = argv[2];
        if (action == "init") return env.init(std::cout, std::cerr) ? 0 : 1;
        if (action == "info") return env.info(std::cout, std::cerr) ? 0 : 1;
        if (action == "list") return env.list(std::cout, std::cerr) ? 0 : 1;
        std::cerr << "error: unknown env command '" << action << "'\n";
        NovisEnv::help(std::cerr);
        return 1;
    }

    if (command == "importpy") {
        if (argc < 3) {
            std::fprintf(stderr, "error: importpy expects a Python package name\n");
            std::fprintf(stderr, "help: try `novis importpy pandas` or `novis importpy --list`\n");
            return 1;
        }
        std::string requested = argv[2];
        if (requested == "--list" || requested == "list") {
            ImportPyRegistry::list(std::cout);
            return 0;
        }

        bool ok = true;
        for (int i = 2; i < argc; ++i) {
            std::string package = argv[i];
            if (package == "--help" || package == "-h") {
                std::printf("usage: novis importpy <package> [package...]\n");
                std::printf("example: novis importpy pandas numpy\n");
                return 0;
            }
            ok = ImportPyRegistry::install(package, ".", std::cout, std::cerr) && ok;
        }
        return ok ? 0 : 1;
    }

    bool emit_ir = false;
    bool check_only = false;
    bool run = false;
    bool build_native = false;
    std::string input_path;
    std::string output_path;

    if (command == "check") {
        check_only = true;
        if (argc < 3) { std::fprintf(stderr, "error: check expects a file\n"); return 1; }
        input_path = argv[2];
    } else if (command == "run") {
        run = true;
        if (argc < 3) { std::fprintf(stderr, "error: run expects a file\n"); return 1; }
        input_path = argv[2];
    } else if (command == "build") {
        run = true;
        build_native = true;
        if (argc < 3) { std::fprintf(stderr, "error: build expects a file\n"); return 1; }
        input_path = argv[2];
    } else if (command == "emit-ir" || command == "--emit-ir" || command == "--compile") {
        emit_ir = true;
        if (argc < 3) { std::fprintf(stderr, "error: emit-ir expects a file\n"); return 1; }
        input_path = argv[2];
    } else {
        run = true;
        input_path = command;
    }

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == input_path) continue;
        if (arg == "-o") {
            if (i + 1 >= argc) { std::fprintf(stderr, "error: -o expects output path\n"); return 1; }
            output_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            print_help();
            return 0;
        } else if (input_path.empty()) {
            input_path = arg;
        } else if (arg != input_path) {
            std::fprintf(stderr, "error: unexpected argument '%s'\n", arg.c_str());
            return 1;
        }
    }

    std::string source;
    try {
        source = read_source_file(input_path);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 1;
    }

    if (check_only) {
        return check_source(input_path, source) ? 0 : 1;
    }

    if (emit_ir) {
        std::string ir;
        if (!compile_source_to_ir(input_path, source, ir)) return 1;
        if (!output_path.empty()) {
            std::ofstream out(output_path);
            if (!out) {
                std::fprintf(stderr, "error: cannot write '%s'\n", output_path.c_str());
                return 1;
            }
            out << ir;
        } else {
            std::cout << ir;
        }
        return 0;
    }

    if (build_native) {
        try {
            std::string bin = NativeDriver::build(source, input_path);
            std::string cmd = bin;
            int rc = std::system(cmd.c_str());
            return rc == 0 ? 0 : 1;
        } catch (const std::exception& ex) {
            SourceFile file(input_path, source);
            std::cerr << file.render_error(ex.what());
            return 1;
        }
    }

    if (run) {
        Evaluator eval;
        return run_source(input_path, source, eval) ? 0 : 1;
    }

    print_help();
    return 0;
}
