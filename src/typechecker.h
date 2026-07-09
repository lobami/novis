#pragma once

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ast.h"

// =============================================================================
// Novis Type Checker
// =============================================================================
// This is the missing layer that makes Novis useful for future frameworks:
// framework authors need stable contracts (models, interfaces, generic types,
// public functions), while application code needs inference for fast iteration.
//
// Scope of this pass:
//   * Registers user `type` and `interface` declarations.
//   * Supports generic annotations: List<T>, Result<T, E>, Option<T>, Tensor<f32>.
//   * Checks function signatures, returns, variable declarations and assignment.
//   * Checks core banking/AI builtins enough to catch domain mistakes early.
//
// It is intentionally conservative. Unknown/dynamic edges stay possible for the
// prototype, but public API surfaces become explicit and inspectable.

struct CheckedType {
    enum class Kind {
        Unknown,
        Void,
        Int,
        Float32,
        Str,
        Bool,
        Tensor,
        Decimal,
        Money,
        Custom,
        GenericParam
    } kind = Kind::Unknown;

    std::string name;
    std::vector<CheckedType> args;

    CheckedType() = default;
    explicit CheckedType(Kind k) : kind(k) {}
    CheckedType(Kind k, std::string n) : kind(k), name(std::move(n)) {}
    CheckedType(Kind k, std::string n, std::vector<CheckedType> a)
        : kind(k), name(std::move(n)), args(std::move(a)) {}

    static CheckedType unknown() { return CheckedType(Kind::Unknown); }
    static CheckedType void_() { return CheckedType(Kind::Void); }
};

inline bool operator==(const CheckedType& a, const CheckedType& b) {
    return a.kind == b.kind && a.name == b.name && a.args == b.args;
}

inline bool operator!=(const CheckedType& a, const CheckedType& b) {
    return !(a == b);
}

struct FunctionTypeInfo {
    std::string name;
    std::vector<std::string> type_params;
    std::vector<CheckedType> params;
    CheckedType return_type = CheckedType::void_();
    bool is_public = false;
};

class TypeChecker : public ExprVisitor, public StmtVisitor {
public:
    TypeChecker() {
        push_scope();
    }

    void check(const std::vector<std::unique_ptr<Stmt>>& program) {
        register_top_level(program);
        for (const auto& stmt : program) {
            stmt->accept(*this);
        }
    }

private:
    std::vector<std::map<std::string, CheckedType>> scopes_;
    std::map<std::string, TypeDeclStmt*> user_types_;
    std::map<std::string, InterfaceDeclStmt*> interfaces_;
    std::map<std::string, FunctionTypeInfo> functions_;
    std::vector<std::set<std::string>> generic_scopes_;
    std::vector<CheckedType> return_stack_;
    CheckedType last_type_ = CheckedType::unknown();

    // --------------------------------------------------------------- registry
    void register_top_level(const std::vector<std::unique_ptr<Stmt>>& program) {
        // First register nominal surfaces so functions can refer to types that
        // appear later in the file.
        for (const auto& stmt : program) {
            if (auto type = dynamic_cast<TypeDeclStmt*>(stmt.get())) {
                if (user_types_.count(type->name) || interfaces_.count(type->name)) {
                    fail("Duplicate type/interface name '" + type->name + "'");
                }
                user_types_[type->name] = type;
            } else if (auto str = dynamic_cast<StructDeclStmt*>(stmt.get())) {
                if (user_types_.count(str->name) || interfaces_.count(str->name)) {
                    fail("Duplicate type/struct/interface name '" + str->name + "'");
                }
                user_types_[str->name] = nullptr;  // struct: nominal name only
            } else if (auto iface = dynamic_cast<InterfaceDeclStmt*>(stmt.get())) {
                if (user_types_.count(iface->name) || interfaces_.count(iface->name)) {
                    fail("Duplicate type/interface name '" + iface->name + "'");
                }
                interfaces_[iface->name] = iface;
            }
        }

        for (const auto& stmt : program) {
            if (auto fn = dynamic_cast<FunctionDeclStmt*>(stmt.get())) {
                if (functions_.count(fn->name)) {
                    fail("Duplicate function '" + fn->name + "'");
                }
                functions_[fn->name] = function_info(*fn);
            }
        }
    }

    FunctionTypeInfo function_info(FunctionDeclStmt& fn) {
        FunctionTypeInfo info;
        info.name = fn.name;
        info.type_params = fn.type_params;
        info.is_public = fn.is_public;
        push_generic_scope(fn.type_params);
        for (const auto& param : fn.params) {
            if (!param.type) fail("Function parameter '" + param.name + "' needs a type annotation");
            info.params.push_back(from_annotation(param.type.get()));
        }
        info.return_type = fn.return_type ? from_annotation(fn.return_type.get())
                                          : CheckedType::void_();
        pop_generic_scope();
        return info;
    }

    // ---------------------------------------------------------------- scopes
    void push_scope() { scopes_.push_back({}); }
    void pop_scope() { scopes_.pop_back(); }

    void define_var(const std::string& name, CheckedType type) {
        scopes_.back()[name] = std::move(type);
    }

    CheckedType lookup_var(const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return found->second;
        }
        if (functions_.count(name)) return CheckedType(CheckedType::Kind::Custom, "fn");
        if (is_builtin(name)) return CheckedType(CheckedType::Kind::Custom, "builtin");
        fail("Undefined symbol '" + name + "'");
        return CheckedType::unknown();
    }

    void assign_var(const std::string& name, const CheckedType& value) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                if (!is_assignable(value, found->second)) {
                    fail("Cannot assign " + type_to_string(value) + " to '" +
                         name + "' of type " + type_to_string(found->second));
                }
                return;
            }
        }
        fail("Cannot assign undefined variable '" + name + "'");
    }

    void push_generic_scope(const std::vector<std::string>& params) {
        std::set<std::string> seen;
        for (const auto& p : params) {
            if (!seen.insert(p).second) fail("Duplicate generic parameter '" + p + "'");
        }
        generic_scopes_.push_back(std::move(seen));
    }

    void pop_generic_scope() { generic_scopes_.pop_back(); }

    bool is_generic_param(const std::string& name) const {
        for (auto it = generic_scopes_.rbegin(); it != generic_scopes_.rend(); ++it) {
            if (it->count(name)) return true;
        }
        return false;
    }

    // ------------------------------------------------------------- type utils
    CheckedType from_annotation(const TypeAnnotation* ann) const {
        if (!ann) return CheckedType::unknown();
        std::vector<CheckedType> args;
        args.reserve(ann->args.size());
        for (const auto& arg : ann->args) args.push_back(from_annotation(arg.get()));

        CheckedType out;
        switch (ann->kind) {
            case TypeAnnotation::Kind::Int:     out = CheckedType(CheckedType::Kind::Int); break;
            case TypeAnnotation::Kind::Float32: out = CheckedType(CheckedType::Kind::Float32); break;
            case TypeAnnotation::Kind::Str:     out = CheckedType(CheckedType::Kind::Str); break;
            case TypeAnnotation::Kind::Bool:    out = CheckedType(CheckedType::Kind::Bool); break;
            case TypeAnnotation::Kind::Tensor:  out = CheckedType(CheckedType::Kind::Tensor); break;
            case TypeAnnotation::Kind::Decimal: out = CheckedType(CheckedType::Kind::Decimal); break;
            case TypeAnnotation::Kind::Money:   out = CheckedType(CheckedType::Kind::Money); break;
            case TypeAnnotation::Kind::Custom:
                if (is_generic_param(ann->custom_name)) {
                    out = CheckedType(CheckedType::Kind::GenericParam, ann->custom_name);
                } else {
                    out = CheckedType(CheckedType::Kind::Custom, ann->custom_name);
                }
                break;
        }
        out.args = std::move(args);
        validate_known_type(out);
        return out;
    }

    void validate_known_type(const CheckedType& type) const {
        if (type.kind == CheckedType::Kind::Unknown || type.kind == CheckedType::Kind::Void) return;
        if (type.kind == CheckedType::Kind::Custom) {
            if (!is_known_custom_type(type.name)) {
                fail("Unknown type '" + type.name + "'");
            }
        }
        if (type.kind == CheckedType::Kind::GenericParam && !type.args.empty()) {
            fail("Generic parameter '" + type.name + "' cannot have type arguments");
        }
        if (type.kind != CheckedType::Kind::Custom &&
            type.kind != CheckedType::Kind::Tensor &&
            !type.args.empty()) {
            fail("Type '" + type_to_string(type) + "' cannot take generic arguments");
        }
        if (type.kind == CheckedType::Kind::Tensor && type.args.size() > 1) {
            fail("Tensor accepts at most one element type argument");
        }
        for (const auto& arg : type.args) validate_known_type(arg);
    }

    bool is_known_custom_type(const std::string& name) const {
        return user_types_.count(name) || interfaces_.count(name) ||
               is_builtin_generic_family(name) || name == "Error" ||
               name == "dict" || name == "Dict";
    }

    static bool is_builtin_generic_family(const std::string& name) {
        return name == "List" || name == "Map" || name == "Option" ||
               name == "Result" || name == "Future" || name == "Stream" ||
               name == "Request" || name == "Response" || name == "Schema" ||
               name == "Event" || name == "Service" || name == "Repository";
    }

    static bool is_numeric(const CheckedType& t) {
        return t.kind == CheckedType::Kind::Int ||
               t.kind == CheckedType::Kind::Float32 ||
               t.kind == CheckedType::Kind::Decimal;
    }

    static bool is_boolish(const CheckedType& t) {
        return t.kind == CheckedType::Kind::Bool || is_numeric(t);
    }

    static bool is_assignable(const CheckedType& from, const CheckedType& to) {
        if (to.kind == CheckedType::Kind::Unknown || from.kind == CheckedType::Kind::Unknown) return true;
        if (from.kind == CheckedType::Kind::Tensor && to.kind == CheckedType::Kind::Tensor) return true;
        if (from == to) return true;
        if (to.kind == CheckedType::Kind::Float32 && from.kind == CheckedType::Kind::Int) return true;
        if (to.kind == CheckedType::Kind::Decimal &&
            (from.kind == CheckedType::Kind::Int || from.kind == CheckedType::Kind::Float32)) return true;
        if (to.kind == CheckedType::Kind::GenericParam || from.kind == CheckedType::Kind::GenericParam) return true;
        return false;
    }

    static CheckedType common_numeric(const CheckedType& a, const CheckedType& b) {
        if (a.kind == CheckedType::Kind::Float32 || b.kind == CheckedType::Kind::Float32) {
            return CheckedType(CheckedType::Kind::Float32);
        }
        if (a.kind == CheckedType::Kind::Decimal || b.kind == CheckedType::Kind::Decimal) {
            return CheckedType(CheckedType::Kind::Decimal);
        }
        return CheckedType(CheckedType::Kind::Int);
    }

    static std::string type_to_string(const CheckedType& t) {
        std::string base;
        switch (t.kind) {
            case CheckedType::Kind::Unknown:      base = "unknown"; break;
            case CheckedType::Kind::Void:         base = "void"; break;
            case CheckedType::Kind::Int:          base = "int"; break;
            case CheckedType::Kind::Float32:      base = "f32"; break;
            case CheckedType::Kind::Str:          base = "str"; break;
            case CheckedType::Kind::Bool:         base = "bool"; break;
            case CheckedType::Kind::Tensor:       base = "Tensor"; break;
            case CheckedType::Kind::Decimal:      base = "decimal"; break;
            case CheckedType::Kind::Money:        base = "money"; break;
            case CheckedType::Kind::Custom:
            case CheckedType::Kind::GenericParam: base = t.name; break;
        }
        if (!t.args.empty()) {
            base += "<";
            for (std::size_t i = 0; i < t.args.size(); ++i) {
                if (i) base += ", ";
                base += type_to_string(t.args[i]);
            }
            base += ">";
        }
        return base;
    }

    // ------------------------------------------------------------- builtins
    static bool is_builtin(const std::string& name) {
        static const std::set<std::string> builtins = {
            "print", "len", "decimal", "money", "currency", "round_bankers",
            "is_balanced", "tensor", "shape", "sum", "mean", "variance",
            "stddev", "min", "max", "dot", "relu", "sigmoid", "softmax",
            "argmax", "risk_score", "Tensor", "sqrt", "pow", "read_text",
            "write_text", "__spawn", "dict", "Dict",
            "zynta_app_new", "zynta_route", "zynta_run",
            "zynta_json_parse", "zynta_json_stringify"
        };
        return builtins.count(name) > 0;
    }

    CheckedType type_builtin_call(const std::string& name, const std::vector<CheckedType>& args) const {
        if (name == "print") return CheckedType(CheckedType::Kind::Int);
        if (name == "len") {
            require_arity(name, args, 1);
            if (args[0].kind != CheckedType::Kind::Str && args[0].kind != CheckedType::Kind::Tensor) {
                fail("len() expects str or Tensor, got " + type_to_string(args[0]));
            }
            return CheckedType(CheckedType::Kind::Int);
        }
        if (name == "decimal") {
            require_arity(name, args, 1);
            if (!is_numeric(args[0]) && args[0].kind != CheckedType::Kind::Str &&
                args[0].kind != CheckedType::Kind::Money) {
                fail("decimal() expects numeric, money, or str");
            }
            return CheckedType(CheckedType::Kind::Decimal);
        }
        if (name == "money") {
            require_arity_range(name, args, 1, 2);
            if (!is_numeric(args[0]) && args[0].kind != CheckedType::Kind::Str) {
                fail("money(amount, currency?) expects numeric or str amount");
            }
            if (args.size() == 2 && args[1].kind != CheckedType::Kind::Str) {
                fail("money(amount, currency) expects currency as str");
            }
            return CheckedType(CheckedType::Kind::Money);
        }
        if (name == "currency") {
            require_arity(name, args, 1);
            require_type(name, args[0], CheckedType::Kind::Money);
            return CheckedType(CheckedType::Kind::Str);
        }
        if (name == "round_bankers") {
            require_arity_range(name, args, 1, 2);
            if (args[0].kind != CheckedType::Kind::Money && !is_numeric(args[0])) {
                fail("round_bankers() expects money or numeric value");
            }
            if (args.size() == 2) require_type(name, args[1], CheckedType::Kind::Int);
            return args[0].kind == CheckedType::Kind::Money
                ? CheckedType(CheckedType::Kind::Money)
                : CheckedType(CheckedType::Kind::Decimal);
        }
        if (name == "is_balanced") {
            if (args.empty()) fail("is_balanced() expects at least one money argument");
            for (const auto& arg : args) require_type(name, arg, CheckedType::Kind::Money);
            return CheckedType(CheckedType::Kind::Bool);
        }
        if (name == "tensor" || name == "Tensor") {
            for (const auto& arg : args) {
                if (!is_numeric(arg) && arg.kind != CheckedType::Kind::Tensor && arg.kind != CheckedType::Kind::Money) {
                    fail("tensor() expects numbers, money, or tensors");
                }
            }
            return CheckedType(CheckedType::Kind::Tensor);
        }
        if (name == "shape") {
            require_arity(name, args, 1);
            require_type(name, args[0], CheckedType::Kind::Tensor);
            return CheckedType(CheckedType::Kind::Tensor);
        }
        if (name == "sum" || name == "mean" || name == "variance" || name == "stddev" ||
            name == "min" || name == "max") {
            if (args.empty()) fail(name + "() expects at least one argument");
            for (const auto& arg : args) {
                if (!is_numeric(arg) && arg.kind != CheckedType::Kind::Tensor && arg.kind != CheckedType::Kind::Money) {
                    fail(name + "() expects numbers, money, or Tensor");
                }
            }
            return CheckedType(CheckedType::Kind::Float32);
        }
        if (name == "dot") {
            require_arity(name, args, 2);
            require_type(name, args[0], CheckedType::Kind::Tensor);
            require_type(name, args[1], CheckedType::Kind::Tensor);
            return CheckedType(CheckedType::Kind::Float32);
        }
        if (name == "relu" || name == "softmax") {
            require_arity(name, args, 1);
            require_type(name, args[0], CheckedType::Kind::Tensor);
            return CheckedType(CheckedType::Kind::Tensor);
        }
        if (name == "sigmoid") {
            require_arity(name, args, 1);
            if (args[0].kind == CheckedType::Kind::Tensor) return CheckedType(CheckedType::Kind::Tensor);
            if (!is_numeric(args[0])) fail("sigmoid() expects numeric or Tensor");
            return CheckedType(CheckedType::Kind::Float32);
        }
        if (name == "argmax") {
            require_arity(name, args, 1);
            require_type(name, args[0], CheckedType::Kind::Tensor);
            return CheckedType(CheckedType::Kind::Int);
        }
        if (name == "risk_score") {
            if (args.empty()) fail("risk_score() expects features");
            for (const auto& arg : args) {
                if (!is_numeric(arg) && arg.kind != CheckedType::Kind::Tensor && arg.kind != CheckedType::Kind::Money) {
                    fail("risk_score() expects numbers, money, or Tensor");
                }
            }
            return CheckedType(CheckedType::Kind::Float32);
        }
        if (name == "sqrt") {
            require_arity(name, args, 1);
            if (!is_numeric(args[0])) fail("sqrt() expects numeric argument");
            return CheckedType(CheckedType::Kind::Float32);
        }
        if (name == "pow") {
            require_arity(name, args, 2);
            if (!is_numeric(args[0]) || !is_numeric(args[1])) fail("pow() expects numeric arguments");
            return CheckedType(CheckedType::Kind::Float32);
        }
        if (name == "read_text") {
            require_arity(name, args, 1);
            require_type(name, args[0], CheckedType::Kind::Str);
            return CheckedType(CheckedType::Kind::Str);
        }
        if (name == "write_text") {
            require_arity(name, args, 2);
            require_type(name, args[0], CheckedType::Kind::Str);
            require_type(name, args[1], CheckedType::Kind::Str);
            return CheckedType(CheckedType::Kind::Int);
        }
        if (name == "__spawn") {
            // spawn(fn, arg1, arg2, ...) — at least 1 arg (the callable)
            if (args.empty()) fail("__spawn() expects at least 1 argument");
            return CheckedType(CheckedType::Kind::Custom, "Task");
        }
        if (name == "zynta_app_new") {
            require_arity(name, args, 0);
            return CheckedType(CheckedType::Kind::Int);
        }
        if (name == "zynta_route") {
            require_arity(name, args, 4);
            return CheckedType(CheckedType::Kind::Int);
        }
        if (name == "zynta_run") {
            require_arity(name, args, 3);
            return CheckedType(CheckedType::Kind::Int);
        }
        if (name == "zynta_json_parse") {
            require_arity(name, args, 1);
            return CheckedType(CheckedType::Kind::Custom, "dict");
        }
        if (name == "zynta_json_stringify") {
            require_arity(name, args, 1);
            return CheckedType(CheckedType::Kind::Str);
        }
        fail("Unknown builtin '" + name + "'");
        return CheckedType::unknown();
    }

    static void require_arity(const std::string& name, const std::vector<CheckedType>& args, std::size_t expected) {
        if (args.size() != expected) {
            fail(name + "() expects " + std::to_string(expected) + " argument(s), got " + std::to_string(args.size()));
        }
    }

    static void require_arity_range(const std::string& name,
                                    const std::vector<CheckedType>& args,
                                    std::size_t min,
                                    std::size_t max) {
        if (args.size() < min || args.size() > max) {
            fail(name + "() expects " + std::to_string(min) + ".." + std::to_string(max) +
                 " arguments, got " + std::to_string(args.size()));
        }
    }

    static void require_type(const std::string& name, const CheckedType& got, CheckedType::Kind expected) {
        CheckedType expected_type(expected);
        if (got.kind != expected) {
            fail(name + "() expected " + type_to_string(expected_type) + ", got " + type_to_string(got));
        }
    }

    // --------------------------------------------------------------- visitors
    void visitIntLiteral(IntLiteral&) override { last_type_ = CheckedType(CheckedType::Kind::Int); }
    void visitFloatLiteral(FloatLiteral&) override { last_type_ = CheckedType(CheckedType::Kind::Float32); }
    void visitStringLiteral(StringLiteral&) override { last_type_ = CheckedType(CheckedType::Kind::Str); }
    void visitBoolLiteral(BoolLiteral&) override { last_type_ = CheckedType(CheckedType::Kind::Bool); }

    void visitTensorLiteral(TensorLiteral& e) override {
        for (const auto& element : e.elements) {
            CheckedType item = type_expr(element.get());
            if (!is_numeric(item) && item.kind != CheckedType::Kind::Tensor && item.kind != CheckedType::Kind::Money) {
                fail("Tensor literal elements must be numeric, money, or Tensor, got " + type_to_string(item));
            }
        }
        last_type_ = CheckedType(CheckedType::Kind::Tensor);
    }

    void visitVariableExpr(VariableExpr& e) override { last_type_ = lookup_var(e.name); }

    void visitBinaryExpr(BinaryExpr& e) override {
        CheckedType a = type_expr(e.lhs.get());
        CheckedType b = type_expr(e.rhs.get());

        switch (e.op) {
            case BinOp::And:
            case BinOp::Or:
                if (!is_boolish(a) || !is_boolish(b)) fail("Logical operators expect bool/numeric operands");
                last_type_ = CheckedType(CheckedType::Kind::Bool);
                return;
            case BinOp::Eq:
            case BinOp::Neq:
                if (!is_assignable(a, b) && !is_assignable(b, a)) {
                    fail("Cannot compare " + type_to_string(a) + " with " + type_to_string(b));
                }
                last_type_ = CheckedType(CheckedType::Kind::Bool);
                return;
            case BinOp::Lt:
            case BinOp::Gt:
            case BinOp::Lte:
            case BinOp::Gte:
                if (!(is_numeric(a) && is_numeric(b)) && !(a.kind == CheckedType::Kind::Money && b.kind == CheckedType::Kind::Money)) {
                    fail("Ordering expects numeric or money operands");
                }
                last_type_ = CheckedType(CheckedType::Kind::Bool);
                return;
            case BinOp::Add:
                if (a.kind == CheckedType::Kind::Str && b.kind == CheckedType::Kind::Str) {
                    last_type_ = CheckedType(CheckedType::Kind::Str); return;
                }
                if (a.kind == CheckedType::Kind::Tensor || b.kind == CheckedType::Kind::Tensor) {
                    last_type_ = CheckedType(CheckedType::Kind::Tensor); return;
                }
                if (a.kind == CheckedType::Kind::Money && b.kind == CheckedType::Kind::Money) {
                    last_type_ = CheckedType(CheckedType::Kind::Money); return;
                }
                if (is_numeric(a) && is_numeric(b)) { last_type_ = common_numeric(a, b); return; }
                fail("Operator + not defined for " + type_to_string(a) + " and " + type_to_string(b));
                return;
            case BinOp::Sub:
                if (a.kind == CheckedType::Kind::Tensor || b.kind == CheckedType::Kind::Tensor) {
                    last_type_ = CheckedType(CheckedType::Kind::Tensor); return;
                }
                if (a.kind == CheckedType::Kind::Money && b.kind == CheckedType::Kind::Money) {
                    last_type_ = CheckedType(CheckedType::Kind::Money); return;
                }
                if (is_numeric(a) && is_numeric(b)) { last_type_ = common_numeric(a, b); return; }
                fail("Operator - not defined for " + type_to_string(a) + " and " + type_to_string(b));
                return;
            case BinOp::Mul:
                if (a.kind == CheckedType::Kind::Tensor || b.kind == CheckedType::Kind::Tensor) {
                    last_type_ = CheckedType(CheckedType::Kind::Tensor); return;
                }
                if ((a.kind == CheckedType::Kind::Money && is_numeric(b)) ||
                    (b.kind == CheckedType::Kind::Money && is_numeric(a))) {
                    last_type_ = CheckedType(CheckedType::Kind::Money); return;
                }
                if (is_numeric(a) && is_numeric(b)) { last_type_ = common_numeric(a, b); return; }
                fail("Operator * not defined for " + type_to_string(a) + " and " + type_to_string(b));
                return;
            case BinOp::Div:
                if (a.kind == CheckedType::Kind::Tensor || b.kind == CheckedType::Kind::Tensor) {
                    last_type_ = CheckedType(CheckedType::Kind::Tensor); return;
                }
                if (a.kind == CheckedType::Kind::Money && is_numeric(b)) {
                    last_type_ = CheckedType(CheckedType::Kind::Money); return;
                }
                if (is_numeric(a) && is_numeric(b)) { last_type_ = common_numeric(a, b); return; }
                fail("Operator / not defined for " + type_to_string(a) + " and " + type_to_string(b));
                return;
            case BinOp::Mod:
                if (is_numeric(a) && is_numeric(b)) { last_type_ = common_numeric(a, b); return; }
                fail("Operator % not defined for " + type_to_string(a) + " and " + type_to_string(b));
                return;
        }
    }

    void visitUnaryExpr(UnaryExpr& e) override {
        CheckedType t = type_expr(e.operand.get());
        if (e.op == UnaryOp::Not) {
            if (!is_boolish(t)) fail("not expects bool/numeric operand");
            last_type_ = CheckedType(CheckedType::Kind::Bool);
            return;
        }
        if (!is_numeric(t) && t.kind != CheckedType::Kind::Money && t.kind != CheckedType::Kind::Tensor) {
            fail("Unary - expects numeric, money, or Tensor");
        }
        last_type_ = t;
    }

    void visitCallExpr(CallExpr& e) override {
        auto callee_var = dynamic_cast<VariableExpr*>(e.callee.get());
        if (!callee_var) {
            fail("Only named function calls are supported in the type checker for now");
        }
        std::vector<CheckedType> args;
        for (const auto& arg : e.args) args.push_back(type_expr(arg.get()));

        const std::string& name = callee_var->name;
        if (is_builtin(name)) {
            last_type_ = type_builtin_call(name, args);
            return;
        }
        auto fn_it = functions_.find(name);
        if (fn_it == functions_.end()) fail("Call to undefined function '" + name + "'");

        const FunctionTypeInfo& fn = fn_it->second;
        if (fn.params.size() != args.size()) {
            fail("Function '" + name + "' expects " + std::to_string(fn.params.size()) +
                 " args, got " + std::to_string(args.size()));
        }

        std::map<std::string, CheckedType> substitutions;
        for (std::size_t i = 0; i < args.size(); ++i) {
            unify_or_check(fn.params[i], args[i], substitutions, "argument " + std::to_string(i + 1) + " of " + name);
        }
        last_type_ = substitute(fn.return_type, substitutions);
    }

    void visitAssignExpr(AssignExpr& e) override {
        CheckedType value = type_expr(e.value.get());
        assign_var(e.name, value);
        last_type_ = value;
    }

    void visitDictLiteral(DictLiteral& e) override {
        // Type-check each entry. All keys are string literals at the parser
        // level, so the resulting type is `dict` (Custom "dict").
        for (const auto& entry : e.entries) {
            type_expr(entry.value.get());
        }
        last_type_ = CheckedType(CheckedType::Kind::Custom, "dict");
    }

    void visitStructDeclStmt(StructDeclStmt& s) override {
        // Structs are type declarations with field types. They're registered
        // the same way as `type X:` declarations but with the additional
        // semantic that all fields are required (no method bodies allowed).
        user_types_[s.name] = nullptr;
        push_scope();
        for (const auto& f : s.fields) {
            CheckedType ft = f.type ? from_annotation(f.type.get())
                                    : CheckedType::unknown();
            define_var(f.name, ft);
        }
        pop_scope();
        last_type_ = CheckedType::void_();
    }

    void visitSpawnExpr(SpawnExpr& e) override {
        // The inner expression has already been type-checked by the call to
        // visitCallExpr when we walked e.callee. We expose it as a Task<...>
        // carrying the same payload type as the inner callable's return.
        // We don't model generic Task<T> yet, so the spawn expression simply
        // has its payload type at this layer; the await then unwraps it.
        CheckedType inner = type_expr(e.callee.get());
        if (auto* c = dynamic_cast<CallExpr*>(e.callee.get())) {
            if (!c->args.empty()) {
                CheckedType arg_t = type_expr(c->args[0].get());
                if (arg_t.kind == CheckedType::Kind::Custom && arg_t.name == "fn") {
                    auto fn_it = functions_.find(/*name*/ "");
                    // The synthetic name is "__spawn" so we look it up via the
                    // call's callee. Simpler: drill to functions_ by name.
                }
            }
        }
        last_type_ = inner;
        // Note: last_type_ ends up as `Task` (Custom "Task") from visitCallExpr
        // for the __spawn call. visitAwaitExpr re-resolves the inner return.
    }

    void visitAwaitExpr(AwaitExpr& e) override {
        // await(spawn(fn, ...)) — the result type is fn's return type.
        last_type_ = type_expr(e.task.get());
        if (auto* spawn = dynamic_cast<SpawnExpr*>(e.task.get())) {
            if (auto* c = dynamic_cast<CallExpr*>(spawn->callee.get())) {
                if (!c->args.empty() && c->args[0]) {
                    // We need to look up the named function. The first arg is
                    // a VariableExpr carrying the function name.
                    if (auto* v = dynamic_cast<VariableExpr*>(c->args[0].get())) {
                        auto fn_it = functions_.find(v->name);
                        if (fn_it != functions_.end()) {
                            last_type_ = fn_it->second.return_type;
                        }
                    }
                }
            }
        }
    }

    void visitVarDeclStmt(VarDeclStmt& s) override {
        CheckedType init = s.init ? type_expr(s.init.get()) : CheckedType::unknown();
        CheckedType declared = s.type ? from_annotation(s.type.get()) : init;
        if (s.type && !is_assignable(init, declared)) {
            fail("Cannot initialize '" + s.name + "' of type " + type_to_string(declared) +
                 " with " + type_to_string(init));
        }
        define_var(s.name, declared);
        last_type_ = declared;
    }

    void visitExprStmt(ExprStmt& s) override { last_type_ = type_expr(s.expr.get()); }

    void visitBlockStmt(BlockStmt& s) override {
        push_scope();
        for (const auto& child : s.statements) child->accept(*this);
        pop_scope();
        last_type_ = CheckedType::void_();
    }

    void visitIfStmt(IfStmt& s) override {
        CheckedType cond = type_expr(s.condition.get());
        if (!is_boolish(cond)) fail("if condition must be bool/numeric, got " + type_to_string(cond));
        s.then_branch->accept(*this);
        if (s.else_branch) s.else_branch->accept(*this);
        last_type_ = CheckedType::void_();
    }

    void visitWhileStmt(WhileStmt& s) override {
        CheckedType cond = type_expr(s.condition.get());
        if (!is_boolish(cond)) fail("while condition must be bool/numeric, got " + type_to_string(cond));
        s.body->accept(*this);
        last_type_ = CheckedType::void_();
    }

    void visitReturnStmt(ReturnStmt& s) override {
        if (return_stack_.empty()) fail("return outside function");
        CheckedType expected = return_stack_.back();
        CheckedType actual = s.value ? type_expr(s.value.get()) : CheckedType::void_();
        if (!is_assignable(actual, expected)) {
            fail("Return type mismatch: expected " + type_to_string(expected) +
                 ", got " + type_to_string(actual));
        }
        last_type_ = actual;
    }

    void visitFunctionDeclStmt(FunctionDeclStmt& s) override {
        push_generic_scope(s.type_params);
        for (const auto& param : s.params) {
            if (!param.type) fail("Function parameter '" + param.name + "' needs a type annotation");
        }
        CheckedType ret = s.return_type ? from_annotation(s.return_type.get()) : CheckedType::void_();

        push_scope();
        for (const auto& param : s.params) {
            define_var(param.name, from_annotation(param.type.get()));
        }
        return_stack_.push_back(ret);
        if (s.body) s.body->accept(*this);
        return_stack_.pop_back();
        pop_scope();
        pop_generic_scope();
        last_type_ = CheckedType::void_();
    }

    void visitTypeDeclStmt(TypeDeclStmt& s) override {
        push_generic_scope(s.type_params);
        std::set<std::string> names;
        for (const auto& field : s.fields) {
            if (!names.insert(field.name).second) fail("Duplicate field '" + field.name + "' in type '" + s.name + "'");
            if (!field.type) fail("Field '" + field.name + "' needs a type annotation");
            (void)from_annotation(field.type.get());
        }
        pop_generic_scope();
        last_type_ = CheckedType::void_();
    }

    void visitInterfaceDeclStmt(InterfaceDeclStmt& s) override {
        push_generic_scope(s.type_params);
        std::set<std::string> names;
        for (const auto& method : s.methods) {
            if (!names.insert(method.name).second) fail("Duplicate method '" + method.name + "' in interface '" + s.name + "'");
            for (const auto& param : method.params) {
                if (!param.type) fail("Interface method parameter '" + param.name + "' needs a type annotation");
                (void)from_annotation(param.type.get());
            }
            if (method.return_type) (void)from_annotation(method.return_type.get());
        }
        pop_generic_scope();
        last_type_ = CheckedType::void_();
    }

    CheckedType type_expr(Expr* expr) {
        expr->accept(*this);
        return last_type_;
    }

    static CheckedType substitute(const CheckedType& type, const std::map<std::string, CheckedType>& substitutions) {
        if (type.kind == CheckedType::Kind::GenericParam) {
            auto it = substitutions.find(type.name);
            if (it != substitutions.end()) return it->second;
        }
        CheckedType out = type;
        out.args.clear();
        for (const auto& arg : type.args) out.args.push_back(substitute(arg, substitutions));
        return out;
    }

    static void unify_or_check(const CheckedType& expected,
                               const CheckedType& actual,
                               std::map<std::string, CheckedType>& substitutions,
                               const std::string& context) {
        if (expected.kind == CheckedType::Kind::GenericParam) {
            auto it = substitutions.find(expected.name);
            if (it == substitutions.end()) {
                substitutions[expected.name] = actual;
                return;
            }
            if (!is_assignable(actual, it->second)) {
                fail("Generic argument mismatch for " + context + ": expected " +
                     type_to_string(it->second) + ", got " + type_to_string(actual));
            }
            return;
        }
        if (!is_assignable(actual, expected)) {
            fail("Type mismatch for " + context + ": expected " + type_to_string(expected) +
                 ", got " + type_to_string(actual));
        }
    }

    [[noreturn]] static void fail(const std::string& message) {
        throw std::runtime_error("type error: " + message);
    }
};
