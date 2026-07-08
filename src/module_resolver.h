#pragma once

#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// =============================================================================
// Novis module resolver
// =============================================================================
// A deliberately small resolver: it expands `import x.y` and
// `from x.y import Foo` into source text before lexing. This gives Novis a real
// multi-file story without forcing the early AST/IR layers to own module graphs
// yet. The expanded source keeps comments with import boundaries for diagnostics.

struct ResolvedSource {
    std::string source;
    std::vector<std::string> loaded_modules;
};

class ModuleResolver {
public:
    ResolvedSource resolve_file(const std::string& path) {
        seen_.clear();
        loaded_.clear();
        std::string source = read_file(path);
        std::string base = dirname(path);
        return {resolve_source(path, base, source), loaded_};
    }

    ResolvedSource resolve_virtual(const std::string& path, const std::string& source) {
        seen_.clear();
        loaded_.clear();
        std::string base = dirname(path);
        return {resolve_source(path, base, source), loaded_};
    }

private:
    std::set<std::string> seen_;
    std::vector<std::string> loaded_;

    static std::string read_file(const std::string& path) {
        std::ifstream in(path);
        if (!in) throw std::runtime_error("cannot open '" + path + "'");
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    static bool file_exists(const std::string& path) {
        std::ifstream in(path);
        return static_cast<bool>(in);
    }

    static std::string dirname(const std::string& path) {
        std::size_t slash = path.find_last_of("/");
        if (slash == std::string::npos) return ".";
        if (slash == 0) return "/";
        return path.substr(0, slash);
    }

    static std::string trim(std::string s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.erase(s.begin());
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
        return s;
    }

    static bool starts_with(const std::string& s, const std::string& prefix) {
        return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
    }

    static std::string strip_inline_comment(std::string s) {
        std::size_t hash = s.find('#');
        if (hash != std::string::npos) s = s.substr(0, hash);
        return trim(s);
    }

    static std::string module_to_relative_path(std::string module) {
        for (char& c : module) if (c == '.') c = '/';
        return module;
    }

    static std::string parse_import_module(const std::string& trimmed_line) {
        if (starts_with(trimmed_line, "import ")) {
            std::string rest = strip_inline_comment(trimmed_line.substr(7));
            std::size_t as_pos = rest.find(" as ");
            if (as_pos != std::string::npos) rest = trim(rest.substr(0, as_pos));
            std::size_t comma = rest.find(',');
            if (comma != std::string::npos) rest = trim(rest.substr(0, comma));
            return rest;
        }
        if (starts_with(trimmed_line, "from ")) {
            std::string rest = strip_inline_comment(trimmed_line.substr(5));
            std::size_t import_pos = rest.find(" import ");
            if (import_pos == std::string::npos) return "";
            return trim(rest.substr(0, import_pos));
        }
        return "";
    }

    std::string resolve_source(const std::string& path,
                               const std::string& base_dir,
                               const std::string& source) {
        std::ostringstream out;
        out << "# <module " << path << ">\n";

        std::stringstream ss(source);
        std::string line;
        while (std::getline(ss, line)) {
            std::string trimmed_line = trim(line);
            std::string module = parse_import_module(trimmed_line);
            if (!module.empty()) {
                out << expand_module(module, base_dir);
                out << "# import " << module << "\n";
            } else {
                out << line << '\n';
            }
        }

        out << "# </module " << path << ">\n";
        return out.str();
    }

    std::string expand_module(const std::string& module, const std::string& base_dir) {
        if (seen_.count(module)) return "# <module " + module + " already loaded>\n";
        seen_.insert(module);
        loaded_.push_back(module);

        std::string std_source = stdlib_source(module);
        if (!std_source.empty()) {
            return resolve_source("stdlib:" + module, base_dir, std_source);
        }

        std::string rel = module_to_relative_path(module);
        std::vector<std::string> candidates = {
            base_dir + "/" + rel + ".novis",
            base_dir + "/" + rel + "/mod.novis",
            base_dir + "/.novis/packages/" + rel + ".novis",
            base_dir + "/.novis/packages/" + rel + "/mod.novis",
            ".novis/packages/" + rel + ".novis",
            ".novis/packages/" + rel + "/mod.novis",
            rel + ".novis",
            rel + "/mod.novis"
        };
        for (const auto& candidate : candidates) {
            if (file_exists(candidate)) {
                return resolve_source(candidate, dirname(candidate), read_file(candidate));
            }
        }

        throw std::runtime_error("module not found: '" + module + "'");
    }

    static std::string stdlib_source(const std::string& module) {
        if (module == "std" || module == "std.core") {
            return R"NOVIS(
pub type Option<T>:
    has_value: bool

pub type Result<T, E>:
    ok: bool

pub fn identity<T>(value: T) -> T:
    return value
)NOVIS";
        }

        if (module == "std.bank") {
            return R"NOVIS(
import std.core

pub type Account:
    pub id: str
    balance: money
    risk_features: Tensor<f32>

pub type LedgerEntry:
    account_id: str
    amount: money
    description: str

pub fn credit(amount: money) -> money:
    return amount

pub fn debit(amount: money) -> money:
    return -amount

pub fn balanced(a: money, b: money) -> bool:
    return is_balanced(a, b)
)NOVIS";
        }

        if (module == "std.ai") {
            return R"NOVIS(
pub type FeatureVector:
    values: Tensor<f32>

pub fn linear_score(features: Tensor<f32>, weights: Tensor<f32>) -> f32:
    return dot(features, weights)

pub fn classify(features: Tensor<f32>, weights: Tensor<f32>) -> f32:
    return sigmoid(dot(features, weights))
)NOVIS";
        }

        if (module == "std.math") {
            return R"NOVIS(
pub fn clamp(x: f32, lo: f32, hi: f32) -> f32:
    if x < lo:
        return lo
    if x > hi:
        return hi
    return x

pub fn square(x: f32) -> f32:
    return x * x
)NOVIS";
        }

        if (module == "std.http") {
            return R"NOVIS(
import std.core

pub type Request<T>:
    body: T

pub type Response<T>:
    status: int
    body: T
)NOVIS";
        }

        if (module == "std.fs") {
            return R"NOVIS(
pub fn read_file(path: str) -> str:
    return read_text(path)

pub fn write_file(path: str, content: str) -> int:
    return write_text(path, content)
)NOVIS";
        }

        if (module == "std.string") {
            return R"NOVIS(
pub fn length(value: str) -> int:
    return len(value)
)NOVIS";
        }

        return "";
    }
};
