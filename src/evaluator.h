#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ast.h"

// =============================================================================
// Novis Tree-walking Evaluator
// =============================================================================
// This is intentionally still a compact interpreter, but the runtime model is
// already shaped around Novis's target domains:
//   * Decimal / Money for banking-grade exact arithmetic.
//   * Tensor for AI/statistical workloads.
//   * Builtins for statistics and lightweight model-style scoring.
//
// Comments marked "// [LLVM-IR-INJECTION-POINT]" mark spots where LLVM IR or a
// future Wasm-first backend would be emitted by a codegen pass.

struct Decimal {
    static constexpr int64_t SCALE_FACTOR = 1000000; // 6 fixed decimal places
    int64_t units = 0;

    Decimal() = default;
    explicit Decimal(int64_t scaled_units) : units(scaled_units) {}
};

inline bool operator==(const Decimal& a, const Decimal& b) {
    return a.units == b.units;
}

struct Money {
    Decimal amount;
    std::string currency = "MXN";
};

inline bool operator==(const Money& a, const Money& b) {
    return a.currency == b.currency && a.amount == b.amount;
}

struct Tensor {
    std::vector<double> data;
    std::vector<std::size_t> shape;
};

inline bool operator==(const Tensor& a, const Tensor& b) {
    return a.shape == b.shape && a.data == b.data;
}

class Environment;

struct UserFunction {
    FunctionDeclStmt* decl = nullptr;
    std::shared_ptr<Environment> closure;
};

using Value = std::variant<int64_t, double, std::string, bool, Decimal, Money, Tensor, std::shared_ptr<UserFunction>>;

// -----------------------------------------------------------------------------
// Environment: chained lexical scopes (parent pointer).
// -----------------------------------------------------------------------------
class Environment {
public:
    explicit Environment(std::shared_ptr<Environment> parent = nullptr)
        : parent_(std::move(parent)) {}

    void define(const std::string& name, Value v) {
        values_[name] = std::move(v);
    }

    Value& get(const std::string& name) {
        auto it = values_.find(name);
        if (it != values_.end()) return it->second;
        if (parent_) return parent_->get(name);
        throw std::runtime_error("Undefined variable '" + name + "'");
    }

    bool isDefined(const std::string& name) const {
        if (values_.count(name) > 0) return true;
        return parent_ ? parent_->isDefined(name) : false;
    }

    void assign(const std::string& name, Value v) {
        auto it = values_.find(name);
        if (it != values_.end()) {
            it->second = std::move(v);
            return;
        }
        if (parent_) {
            parent_->assign(name, std::move(v));
            return;
        }
        throw std::runtime_error("Undefined variable '" + name + "'");
    }

    std::shared_ptr<Environment> parent() const { return parent_; }

private:
    std::shared_ptr<Environment> parent_;
    std::map<std::string, Value> values_;
};

// -----------------------------------------------------------------------------
// ReturnSignal: internal control-flow exception for `return` statements.
// -----------------------------------------------------------------------------
struct ReturnSignal {
    Value value;
};

// -----------------------------------------------------------------------------
// Visitor implementation: actually evaluates the AST.
// -----------------------------------------------------------------------------
class Evaluator : public ExprVisitor, public StmtVisitor {
public:
    Evaluator() : globals_(std::make_shared<Environment>()),
                  current_(globals_) {
        // Builtins are recorded as sentinel string values so visitCallExpr can
        // dispatch by name without needing std::function inside Value.
        define_builtin("print");
        define_builtin("len");

        // Banking / COBOL-replacement primitives.
        define_builtin("decimal");
        define_builtin("money");
        define_builtin("currency");
        define_builtin("round_bankers");
        define_builtin("is_balanced");

        // Statistics / AI primitives.
        define_builtin("tensor");
        define_builtin("Tensor");
        define_builtin("shape");
        define_builtin("sum");
        define_builtin("mean");
        define_builtin("variance");
        define_builtin("stddev");
        define_builtin("min");
        define_builtin("max");
        define_builtin("dot");
        define_builtin("relu");
        define_builtin("sigmoid");
        define_builtin("softmax");
        define_builtin("argmax");
        define_builtin("risk_score");

        // Minimal stdlib-backed host functions.
        define_builtin("sqrt");
        define_builtin("pow");
        define_builtin("read_text");
        define_builtin("write_text");
    }

    // Entry point for a full program. Returns the value of the last
    // expression-like statement (or int64_t 0 if none).
    Value evaluate(const std::vector<std::unique_ptr<Stmt>>& program) {
        Value last = static_cast<int64_t>(0);

        // Register declarations first so functions are callable before their
        // textual definition, which is essential once modules are expanded.
        for (const auto& stmt : program) {
            if (dynamic_cast<FunctionDeclStmt*>(stmt.get()) ||
                dynamic_cast<TypeDeclStmt*>(stmt.get()) ||
                dynamic_cast<InterfaceDeclStmt*>(stmt.get())) {
                last = execute_value(stmt.get());
            }
        }

        for (const auto& stmt : program) {
            if (dynamic_cast<FunctionDeclStmt*>(stmt.get()) ||
                dynamic_cast<TypeDeclStmt*>(stmt.get()) ||
                dynamic_cast<InterfaceDeclStmt*>(stmt.get())) {
                continue;
            }
            last = execute_value(stmt.get());
        }
        return last;
    }

    Value evaluate(Expr* expr) {
        expr->accept(*this);
        return last_expr_value_;
    }

    Value execute_value(Stmt* stmt) {
        stmt->accept(*this);
        return last_expr_value_;
    }

    std::shared_ptr<Environment> current_scope() const { return current_; }

private:
    std::shared_ptr<Environment> globals_;
    std::shared_ptr<Environment> current_;
    Value last_expr_value_{static_cast<int64_t>(0)};

    void define_builtin(const std::string& name) {
        globals_->define(name, std::string("__builtin_" + name + "__"));
    }

    // ----------------------------------------------------------------- SCOPES
    void push_scope() {
        current_ = std::make_shared<Environment>(current_);
    }

    void pop_scope() {
        auto p = current_->parent();
        if (p) current_ = p;
    }

    // ----------------------------------------------------------------- EXPR
    void visitIntLiteral(IntLiteral& e) override {
        // [LLVM-IR-INJECTION-POINT] emit constant int
        last_expr_value_ = e.value;
    }

    void visitFloatLiteral(FloatLiteral& e) override {
        // [LLVM-IR-INJECTION-POINT] emit constant double/f32
        last_expr_value_ = e.value;
    }

    void visitStringLiteral(StringLiteral& e) override {
        // [LLVM-IR-INJECTION-POINT] emit constant global string
        last_expr_value_ = e.value;
    }

    void visitBoolLiteral(BoolLiteral& e) override {
        // [LLVM-IR-INJECTION-POINT] emit i1 constant
        last_expr_value_ = e.value;
    }

    void visitTensorLiteral(TensorLiteral& e) override {
        // [LLVM-IR-INJECTION-POINT] lower literal to a static tensor buffer
        std::vector<Value> values;
        values.reserve(e.elements.size());
        for (const auto& element : e.elements) {
            values.push_back(evaluate(element.get()));
        }
        last_expr_value_ = tensor_from_values(values);
    }

    void visitVariableExpr(VariableExpr& e) override {
        // [LLVM-IR-INJECTION-POINT] emit load from alloca/global
        last_expr_value_ = current_->get(e.name);
    }

    void visitUnaryExpr(UnaryExpr& e) override {
        Value v = evaluate(e.operand.get());
        switch (e.op) {
            case UnaryOp::Neg:
                if (std::holds_alternative<int64_t>(v)) {
                    last_expr_value_ = -std::get<int64_t>(v);
                } else if (std::holds_alternative<double>(v)) {
                    last_expr_value_ = -std::get<double>(v);
                } else if (std::holds_alternative<Decimal>(v)) {
                    last_expr_value_ = Decimal{-std::get<Decimal>(v).units};
                } else if (std::holds_alternative<Money>(v)) {
                    Money m = std::get<Money>(v);
                    m.amount.units = -m.amount.units;
                    last_expr_value_ = m;
                } else if (std::holds_alternative<Tensor>(v)) {
                    Tensor t = std::get<Tensor>(v);
                    for (double& x : t.data) x = -x;
                    last_expr_value_ = t;
                } else {
                    throw std::runtime_error("Unary '-' requires numeric operand");
                }
                break;
            case UnaryOp::Not:
                last_expr_value_ = !to_bool(v);
                break;
        }
    }

    void visitBinaryExpr(BinaryExpr& e) override {
        // [LLVM-IR-INJECTION-POINT] emit arithmetic / icmp / fcmp / vector op
        if (e.op == BinOp::And) {
            Value a = evaluate(e.lhs.get());
            if (!to_bool(a)) { last_expr_value_ = false; return; }
            last_expr_value_ = to_bool(evaluate(e.rhs.get()));
            return;
        }
        if (e.op == BinOp::Or) {
            Value a = evaluate(e.lhs.get());
            if (to_bool(a)) { last_expr_value_ = true; return; }
            last_expr_value_ = to_bool(evaluate(e.rhs.get()));
            return;
        }

        Value a = evaluate(e.lhs.get());
        Value b = evaluate(e.rhs.get());

        switch (e.op) {
            case BinOp::Eq:  last_expr_value_ = values_equal(a, b); break;
            case BinOp::Neq: last_expr_value_ = !values_equal(a, b); break;
            case BinOp::Lt:
            case BinOp::Gt:
            case BinOp::Lte:
            case BinOp::Gte: last_expr_value_ = compare(a, b, e.op); break;
            case BinOp::Add: last_expr_value_ = do_add(a, b); break;
            case BinOp::Sub: last_expr_value_ = do_sub(a, b); break;
            case BinOp::Mul: last_expr_value_ = do_mul(a, b); break;
            case BinOp::Div: last_expr_value_ = do_div(a, b); break;
            case BinOp::Mod: last_expr_value_ = do_mod(a, b); break;
            case BinOp::And:
            case BinOp::Or: break; // handled above
        }
    }

    void visitCallExpr(CallExpr& e) override {
        // [LLVM-IR-INJECTION-POINT] resolve callee and emit direct/indirect call
        if (auto var = dynamic_cast<VariableExpr*>(e.callee.get())) {
            if (current_->isDefined(var->name)) {
                Value callee = current_->get(var->name);
                if (auto sv = std::get_if<std::string>(&callee)) {
                    if (*sv == "__builtin_print__")         { call_print(e.args); last_expr_value_ = static_cast<int64_t>(0); return; }
                    if (*sv == "__builtin_len__")           { last_expr_value_ = call_len(e.args); return; }
                    if (*sv == "__builtin_decimal__")       { last_expr_value_ = call_decimal(e.args); return; }
                    if (*sv == "__builtin_money__")         { last_expr_value_ = call_money(e.args); return; }
                    if (*sv == "__builtin_currency__")      { last_expr_value_ = call_currency(e.args); return; }
                    if (*sv == "__builtin_round_bankers__") { last_expr_value_ = call_round_bankers(e.args); return; }
                    if (*sv == "__builtin_is_balanced__")   { last_expr_value_ = call_is_balanced(e.args); return; }
                    if (*sv == "__builtin_tensor__")        { last_expr_value_ = call_tensor(e.args); return; }
                    if (*sv == "__builtin_Tensor__")        { last_expr_value_ = call_tensor(e.args); return; }
                    if (*sv == "__builtin_shape__")         { last_expr_value_ = call_shape(e.args); return; }
                    if (*sv == "__builtin_sum__")           { last_expr_value_ = call_stat(e.args, "sum"); return; }
                    if (*sv == "__builtin_mean__")          { last_expr_value_ = call_stat(e.args, "mean"); return; }
                    if (*sv == "__builtin_variance__")      { last_expr_value_ = call_stat(e.args, "variance"); return; }
                    if (*sv == "__builtin_stddev__")        { last_expr_value_ = call_stat(e.args, "stddev"); return; }
                    if (*sv == "__builtin_min__")           { last_expr_value_ = call_stat(e.args, "min"); return; }
                    if (*sv == "__builtin_max__")           { last_expr_value_ = call_stat(e.args, "max"); return; }
                    if (*sv == "__builtin_dot__")           { last_expr_value_ = call_dot(e.args); return; }
                    if (*sv == "__builtin_relu__")          { last_expr_value_ = call_unary_tensor(e.args, "relu"); return; }
                    if (*sv == "__builtin_sigmoid__")       { last_expr_value_ = call_unary_tensor(e.args, "sigmoid"); return; }
                    if (*sv == "__builtin_softmax__")       { last_expr_value_ = call_softmax(e.args); return; }
                    if (*sv == "__builtin_argmax__")        { last_expr_value_ = call_argmax(e.args); return; }
                    if (*sv == "__builtin_risk_score__")    { last_expr_value_ = call_risk_score(e.args); return; }
                    if (*sv == "__builtin_sqrt__")          { last_expr_value_ = call_sqrt(e.args); return; }
                    if (*sv == "__builtin_pow__")           { last_expr_value_ = call_pow(e.args); return; }
                    if (*sv == "__builtin_read_text__")     { last_expr_value_ = call_read_text(e.args); return; }
                    if (*sv == "__builtin_write_text__")    { last_expr_value_ = call_write_text(e.args); return; }
                }
                if (auto fn = std::get_if<std::shared_ptr<UserFunction>>(&callee)) {
                    last_expr_value_ = call_user_function(*fn, e.args);
                    return;
                }
            }
        }
        throw std::runtime_error("Call to non-function or undefined function");
    }

    void visitAssignExpr(AssignExpr& e) override {
        Value v = evaluate(e.value.get());
        current_->assign(e.name, v);
        last_expr_value_ = v;
    }

    // ----------------------------------------------------------------- STMT
    void visitVarDeclStmt(VarDeclStmt& s) override {
        // [LLVM-IR-INJECTION-POINT] emit alloca + optional store
        Value v = s.init ? evaluate(s.init.get())
                         : Value{static_cast<int64_t>(0)};
        if (s.type && !value_matches_type(v, *s.type)) {
            throw std::runtime_error(
                "Type mismatch in declaration of '" + s.name +
                "': expected " + type_to_string(*s.type) +
                ", got " + value_type_name(v));
        }
        current_->define(s.name, v);
        last_expr_value_ = std::move(v);
    }

    void visitExprStmt(ExprStmt& s) override {
        last_expr_value_ = evaluate(s.expr.get());
    }

    void visitBlockStmt(BlockStmt& s) override {
        push_scope();
        try {
            for (const auto& child : s.statements) {
                execute_value(child.get());
            }
        } catch (...) {
            pop_scope();
            throw;
        }
        pop_scope();
    }

    void visitIfStmt(IfStmt& s) override {
        // [LLVM-IR-INJECTION-POINT] emit branch
        bool cond = to_bool(evaluate(s.condition.get()));
        if (cond) {
            execute_value(s.then_branch.get());
        } else if (s.else_branch) {
            execute_value(s.else_branch.get());
        }
    }

    void visitWhileStmt(WhileStmt& s) override {
        // [LLVM-IR-INJECTION-POINT] emit loop header / body / latch
        while (to_bool(evaluate(s.condition.get()))) {
            execute_value(s.body.get());
        }
    }

    void visitReturnStmt(ReturnStmt& s) override {
        // [LLVM-IR-INJECTION-POINT] emit ret
        Value v = s.value ? evaluate(s.value.get())
                          : Value{static_cast<int64_t>(0)};
        throw ReturnSignal{std::move(v)};
    }

    void visitFunctionDeclStmt(FunctionDeclStmt& s) override {
        // [LLVM-IR-INJECTION-POINT] emit function header + body
        auto fn = std::make_shared<UserFunction>();
        fn->decl = &s;
        fn->closure = current_;
        current_->define(s.name, fn);
        last_expr_value_ = static_cast<int64_t>(0);
    }

    void visitTypeDeclStmt(TypeDeclStmt& s) override {
        // Runtime no-op: type declarations are consumed by TypeChecker/IR.
        current_->define(s.name, std::string("__type__" + s.name));
        last_expr_value_ = static_cast<int64_t>(0);
    }

    void visitInterfaceDeclStmt(InterfaceDeclStmt& s) override {
        // Runtime no-op: interfaces describe contracts for static checking.
        current_->define(s.name, std::string("__interface__" + s.name));
        last_expr_value_ = static_cast<int64_t>(0);
    }

    // ----------------------------------------------------------------- BUILTINS
    void call_print(const std::vector<std::unique_ptr<Expr>>& args) {
        std::string out;
        bool first = true;
        for (const auto& a : args) {
            if (!first) out += " ";
            first = false;
            out += value_to_string(evaluate(a.get()));
        }
        std::printf("%s\n", out.c_str());
        std::fflush(stdout);
    }

    Value call_user_function(const std::shared_ptr<UserFunction>& fn,
                             const std::vector<std::unique_ptr<Expr>>& args) {
        if (!fn || !fn->decl) {
            throw std::runtime_error("Invalid function value");
        }
        if (args.size() != fn->decl->params.size()) {
            throw std::runtime_error(
                "Function '" + fn->decl->name + "' expects " +
                std::to_string(fn->decl->params.size()) + " arguments");
        }

        std::vector<Value> evaluated_args;
        evaluated_args.reserve(args.size());
        for (const auto& arg : args) {
            evaluated_args.push_back(evaluate(arg.get()));
        }

        auto previous = current_;
        current_ = std::make_shared<Environment>(fn->closure);
        for (std::size_t i = 0; i < fn->decl->params.size(); ++i) {
            current_->define(fn->decl->params[i].name, evaluated_args[i]);
        }

        try {
            execute_value(fn->decl->body.get());
            current_ = previous;
            return static_cast<int64_t>(0);
        } catch (const ReturnSignal& ret) {
            current_ = previous;
            return ret.value;
        } catch (...) {
            current_ = previous;
            throw;
        }
    }

    Value call_len(const std::vector<std::unique_ptr<Expr>>& args) {
        if (args.size() != 1) throw std::runtime_error("len() expects exactly 1 argument");
        Value v = evaluate(args[0].get());
        if (std::holds_alternative<std::string>(v)) return static_cast<int64_t>(std::get<std::string>(v).size());
        if (std::holds_alternative<Tensor>(v))      return static_cast<int64_t>(std::get<Tensor>(v).data.size());
        if (std::holds_alternative<bool>(v))        return static_cast<int64_t>(1);
        throw std::runtime_error("len() not defined for this type");
    }

    Value call_decimal(const std::vector<std::unique_ptr<Expr>>& args) {
        if (args.size() != 1) throw std::runtime_error("decimal() expects exactly 1 argument");
        return decimal_from_value(evaluate(args[0].get()));
    }

    Value call_money(const std::vector<std::unique_ptr<Expr>>& args) {
        if (args.empty() || args.size() > 2) {
            throw std::runtime_error("money(amount, currency?) expects 1 or 2 arguments");
        }
        Money m;
        m.amount = decimal_from_value(evaluate(args[0].get()));
        if (args.size() == 2) {
            Value currency = evaluate(args[1].get());
            if (!std::holds_alternative<std::string>(currency)) {
                throw std::runtime_error("money(..., currency) expects currency as str");
            }
            m.currency = std::get<std::string>(currency);
        }
        return m;
    }

    Value call_currency(const std::vector<std::unique_ptr<Expr>>& args) {
        if (args.size() != 1) throw std::runtime_error("currency() expects exactly 1 argument");
        Value v = evaluate(args[0].get());
        if (!std::holds_alternative<Money>(v)) throw std::runtime_error("currency() expects money");
        return std::get<Money>(v).currency;
    }

    Value call_round_bankers(const std::vector<std::unique_ptr<Expr>>& args) {
        if (args.empty() || args.size() > 2) {
            throw std::runtime_error("round_bankers(value, places?) expects 1 or 2 arguments");
        }
        int places = 2;
        if (args.size() == 2) {
            places = static_cast<int>(to_int(evaluate(args[1].get())));
        }
        Value v = evaluate(args[0].get());
        if (std::holds_alternative<Money>(v)) {
            Money m = std::get<Money>(v);
            m.amount = round_decimal_bankers(m.amount, places);
            return m;
        }
        return round_decimal_bankers(decimal_from_value(v), places);
    }

    Value call_is_balanced(const std::vector<std::unique_ptr<Expr>>& args) {
        if (args.empty()) throw std::runtime_error("is_balanced() expects money entries");
        std::string currency;
        int64_t total = 0;
        for (const auto& arg : args) {
            Value v = evaluate(arg.get());
            if (!std::holds_alternative<Money>(v)) {
                throw std::runtime_error("is_balanced() expects only money values");
            }
            Money m = std::get<Money>(v);
            if (currency.empty()) currency = m.currency;
            if (m.currency != currency) {
                throw std::runtime_error("is_balanced() cannot mix currencies");
            }
            total += m.amount.units;
        }
        return total == 0;
    }

    Value call_tensor(const std::vector<std::unique_ptr<Expr>>& args) {
        std::vector<Value> values;
        values.reserve(args.size());
        for (const auto& arg : args) values.push_back(evaluate(arg.get()));
        if (values.size() == 1 && std::holds_alternative<Tensor>(values[0])) return std::get<Tensor>(values[0]);
        return tensor_from_values(values);
    }

    Value call_shape(const std::vector<std::unique_ptr<Expr>>& args) {
        if (args.size() != 1) throw std::runtime_error("shape() expects exactly 1 argument");
        Value v = evaluate(args[0].get());
        if (!std::holds_alternative<Tensor>(v)) throw std::runtime_error("shape() expects Tensor");
        Tensor out;
        const Tensor& t = std::get<Tensor>(v);
        out.shape = {t.shape.size()};
        for (std::size_t dim : t.shape) out.data.push_back(static_cast<double>(dim));
        return out;
    }

    Value call_stat(const std::vector<std::unique_ptr<Expr>>& args, const std::string& op) {
        std::vector<double> xs = collect_numbers(args);
        if (xs.empty()) throw std::runtime_error(op + "() expects at least one number");

        if (op == "sum") {
            return std::accumulate(xs.begin(), xs.end(), 0.0);
        }
        if (op == "mean") {
            return std::accumulate(xs.begin(), xs.end(), 0.0) / static_cast<double>(xs.size());
        }
        if (op == "variance" || op == "stddev") {
            double avg = std::accumulate(xs.begin(), xs.end(), 0.0) / static_cast<double>(xs.size());
            double acc = 0.0;
            for (double x : xs) acc += (x - avg) * (x - avg);
            double var = acc / static_cast<double>(xs.size());
            return op == "variance" ? var : std::sqrt(var);
        }
        if (op == "min") return *std::min_element(xs.begin(), xs.end());
        if (op == "max") return *std::max_element(xs.begin(), xs.end());
        throw std::logic_error("unknown stat builtin");
    }

    Value call_dot(const std::vector<std::unique_ptr<Expr>>& args) {
        if (args.size() != 2) throw std::runtime_error("dot(a, b) expects 2 tensors");
        Tensor a = tensor_from_value(evaluate(args[0].get()));
        Tensor b = tensor_from_value(evaluate(args[1].get()));
        if (a.data.size() != b.data.size()) throw std::runtime_error("dot() tensor sizes differ");
        double acc = 0.0;
        for (std::size_t i = 0; i < a.data.size(); ++i) acc += a.data[i] * b.data[i];
        return acc;
    }

    Value call_unary_tensor(const std::vector<std::unique_ptr<Expr>>& args, const std::string& op) {
        if (args.size() != 1) throw std::runtime_error(op + "() expects exactly 1 argument");
        Value v = evaluate(args[0].get());
        if (op == "sigmoid" && !std::holds_alternative<Tensor>(v)) {
            return sigmoid(to_double(v));
        }
        Tensor t = tensor_from_value(v);
        for (double& x : t.data) {
            if (op == "relu") x = std::max(0.0, x);
            else if (op == "sigmoid") x = sigmoid(x);
        }
        return t;
    }

    Value call_softmax(const std::vector<std::unique_ptr<Expr>>& args) {
        if (args.size() != 1) throw std::runtime_error("softmax() expects exactly 1 Tensor");
        Tensor t = tensor_from_value(evaluate(args[0].get()));
        if (t.data.empty()) return t;
        double maxv = *std::max_element(t.data.begin(), t.data.end());
        double sumv = 0.0;
        for (double& x : t.data) { x = std::exp(x - maxv); sumv += x; }
        for (double& x : t.data) x /= sumv;
        return t;
    }

    Value call_argmax(const std::vector<std::unique_ptr<Expr>>& args) {
        if (args.size() != 1) throw std::runtime_error("argmax() expects exactly 1 Tensor");
        Tensor t = tensor_from_value(evaluate(args[0].get()));
        if (t.data.empty()) throw std::runtime_error("argmax() expects non-empty Tensor");
        auto it = std::max_element(t.data.begin(), t.data.end());
        return static_cast<int64_t>(std::distance(t.data.begin(), it));
    }

    Value call_risk_score(const std::vector<std::unique_ptr<Expr>>& args) {
        // Toy logistic scoring primitive. Real Novis should replace this with a
        // compiled model/tensor graph backend, but this proves the language can
        // host banking + AI semantics in the same runtime.
        std::vector<double> xs = collect_numbers(args);
        double income = xs.size() > 0 ? xs[0] : 0.0;
        double debt = xs.size() > 1 ? xs[1] : 0.0;
        double late = xs.size() > 2 ? xs[2] : 0.0;
        double ratio = income <= 0.0 ? 1.0 : debt / income;
        return sigmoid(-2.0 + 3.0 * ratio + 0.8 * late);
    }

    Value call_sqrt(const std::vector<std::unique_ptr<Expr>>& args) {
        if (args.size() != 1) throw std::runtime_error("sqrt() expects exactly 1 argument");
        return std::sqrt(to_double(evaluate(args[0].get())));
    }

    Value call_pow(const std::vector<std::unique_ptr<Expr>>& args) {
        if (args.size() != 2) throw std::runtime_error("pow() expects exactly 2 arguments");
        return std::pow(to_double(evaluate(args[0].get())), to_double(evaluate(args[1].get())));
    }

    Value call_read_text(const std::vector<std::unique_ptr<Expr>>& args) {
        if (args.size() != 1) throw std::runtime_error("read_text() expects exactly 1 path argument");
        Value path = evaluate(args[0].get());
        if (!std::holds_alternative<std::string>(path)) throw std::runtime_error("read_text() expects path as str");
        std::ifstream in(std::get<std::string>(path));
        if (!in) throw std::runtime_error("read_text() cannot open file");
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    Value call_write_text(const std::vector<std::unique_ptr<Expr>>& args) {
        if (args.size() != 2) throw std::runtime_error("write_text() expects path and content");
        Value path = evaluate(args[0].get());
        Value content = evaluate(args[1].get());
        if (!std::holds_alternative<std::string>(path) || !std::holds_alternative<std::string>(content)) {
            throw std::runtime_error("write_text() expects path and content as str");
        }
        std::ofstream out(std::get<std::string>(path));
        if (!out) throw std::runtime_error("write_text() cannot open file for writing");
        out << std::get<std::string>(content);
        return static_cast<int64_t>(0);
    }

    // ----------------------------------------------------------------- UTIL
    static bool to_bool(const Value& v) {
        if (std::holds_alternative<bool>(v))        return std::get<bool>(v);
        if (std::holds_alternative<int64_t>(v))     return std::get<int64_t>(v) != 0;
        if (std::holds_alternative<double>(v))      return std::get<double>(v) != 0.0;
        if (std::holds_alternative<std::string>(v)) return !std::get<std::string>(v).empty();
        if (std::holds_alternative<Decimal>(v))     return std::get<Decimal>(v).units != 0;
        if (std::holds_alternative<Money>(v))       return std::get<Money>(v).amount.units != 0;
        if (std::holds_alternative<Tensor>(v))      return !std::get<Tensor>(v).data.empty();
        return false;
    }

    static int64_t to_int(const Value& v) {
        if (std::holds_alternative<int64_t>(v)) return std::get<int64_t>(v);
        if (std::holds_alternative<double>(v))  return static_cast<int64_t>(std::get<double>(v));
        if (std::holds_alternative<Decimal>(v)) return std::get<Decimal>(v).units / Decimal::SCALE_FACTOR;
        throw std::runtime_error("Expected integer-compatible value");
    }

    static bool values_equal(const Value& a, const Value& b) {
        if (a.index() == b.index()) return a == b;
        if (is_decimal_numeric(a) && is_decimal_numeric(b)) {
            return decimal_from_value(a).units == decimal_from_value(b).units;
        }
        if (is_numeric(a) && is_numeric(b)) return to_double(a) == to_double(b);
        return false;
    }

    static bool compare(const Value& a, const Value& b, BinOp op) {
        if (std::holds_alternative<Money>(a) || std::holds_alternative<Money>(b)) {
            Money x = money_from_value(a);
            Money y = money_from_value(b);
            require_same_currency(x, y);
            return compare_scaled(x.amount.units, y.amount.units, op);
        }
        if (is_decimal_numeric(a) && is_decimal_numeric(b) &&
            !std::holds_alternative<double>(a) && !std::holds_alternative<double>(b)) {
            return compare_scaled(decimal_from_value(a).units, decimal_from_value(b).units, op);
        }
        double x = to_double(a);
        double y = to_double(b);
        switch (op) {
            case BinOp::Lt:  return x <  y;
            case BinOp::Gt:  return x >  y;
            case BinOp::Lte: return x <= y;
            case BinOp::Gte: return x >= y;
            default: return false;
        }
    }

    static bool compare_scaled(int64_t x, int64_t y, BinOp op) {
        switch (op) {
            case BinOp::Lt:  return x <  y;
            case BinOp::Gt:  return x >  y;
            case BinOp::Lte: return x <= y;
            case BinOp::Gte: return x >= y;
            default: return false;
        }
    }

    static bool is_numeric(const Value& v) {
        return std::holds_alternative<int64_t>(v) ||
               std::holds_alternative<double>(v) ||
               std::holds_alternative<bool>(v) ||
               std::holds_alternative<Decimal>(v);
    }

    static bool is_decimal_numeric(const Value& v) {
        return std::holds_alternative<int64_t>(v) ||
               std::holds_alternative<bool>(v) ||
               std::holds_alternative<Decimal>(v);
    }

    static double to_double(const Value& v) {
        if (std::holds_alternative<int64_t>(v)) return static_cast<double>(std::get<int64_t>(v));
        if (std::holds_alternative<double>(v))  return std::get<double>(v);
        if (std::holds_alternative<bool>(v))    return std::get<bool>(v) ? 1.0 : 0.0;
        if (std::holds_alternative<Decimal>(v)) return static_cast<double>(std::get<Decimal>(v).units) / Decimal::SCALE_FACTOR;
        if (std::holds_alternative<Money>(v))   return static_cast<double>(std::get<Money>(v).amount.units) / Decimal::SCALE_FACTOR;
        throw std::runtime_error("Expected numeric value");
    }

    static double sigmoid(double x) {
        return 1.0 / (1.0 + std::exp(-x));
    }

    static Value do_add(const Value& a, const Value& b) {
        if (std::holds_alternative<Tensor>(a) || std::holds_alternative<Tensor>(b)) {
            return tensor_binary(a, b, [](double x, double y) { return x + y; });
        }
        if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
            return std::get<std::string>(a) + std::get<std::string>(b);
        }
        if (std::holds_alternative<Money>(a) || std::holds_alternative<Money>(b)) {
            Money x = money_from_value(a);
            Money y = money_from_value(b);
            require_same_currency(x, y);
            return Money{Decimal{x.amount.units + y.amount.units}, x.currency};
        }
        if (std::holds_alternative<Decimal>(a) || std::holds_alternative<Decimal>(b)) {
            return Decimal{decimal_from_value(a).units + decimal_from_value(b).units};
        }
        if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b)) {
            return to_double(a) + to_double(b);
        }
        return static_cast<int64_t>(std::get<int64_t>(a) + std::get<int64_t>(b));
    }

    static Value do_sub(const Value& a, const Value& b) {
        if (std::holds_alternative<Tensor>(a) || std::holds_alternative<Tensor>(b)) {
            return tensor_binary(a, b, [](double x, double y) { return x - y; });
        }
        if (std::holds_alternative<Money>(a) || std::holds_alternative<Money>(b)) {
            Money x = money_from_value(a);
            Money y = money_from_value(b);
            require_same_currency(x, y);
            return Money{Decimal{x.amount.units - y.amount.units}, x.currency};
        }
        if (std::holds_alternative<Decimal>(a) || std::holds_alternative<Decimal>(b)) {
            return Decimal{decimal_from_value(a).units - decimal_from_value(b).units};
        }
        if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b)) {
            return to_double(a) - to_double(b);
        }
        return static_cast<int64_t>(std::get<int64_t>(a) - std::get<int64_t>(b));
    }

    static Value do_mul(const Value& a, const Value& b) {
        if (std::holds_alternative<Tensor>(a) || std::holds_alternative<Tensor>(b)) {
            return tensor_binary(a, b, [](double x, double y) { return x * y; });
        }
        if (std::holds_alternative<Money>(a) || std::holds_alternative<Money>(b)) {
            if (std::holds_alternative<Money>(a) && std::holds_alternative<Money>(b)) {
                throw std::runtime_error("money * money is not a valid banking operation");
            }
            Money m = std::holds_alternative<Money>(a) ? std::get<Money>(a) : std::get<Money>(b);
            Value factor = std::holds_alternative<Money>(a) ? b : a;
            Decimal d = decimal_from_value(factor);
            return Money{Decimal{scaled_mul(m.amount.units, d.units)}, m.currency};
        }
        if (std::holds_alternative<Decimal>(a) || std::holds_alternative<Decimal>(b)) {
            return Decimal{scaled_mul(decimal_from_value(a).units, decimal_from_value(b).units)};
        }
        if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b)) {
            return to_double(a) * to_double(b);
        }
        return static_cast<int64_t>(std::get<int64_t>(a) * std::get<int64_t>(b));
    }

    static Value do_div(const Value& a, const Value& b) {
        if (std::holds_alternative<Tensor>(a) || std::holds_alternative<Tensor>(b)) {
            return tensor_binary(a, b, [](double x, double y) {
                if (y == 0.0) throw std::runtime_error("Division by zero");
                return x / y;
            });
        }
        if (std::holds_alternative<Money>(a)) {
            Money m = std::get<Money>(a);
            Decimal d = decimal_from_value(b);
            if (d.units == 0) throw std::runtime_error("Division by zero");
            return Money{Decimal{scaled_div(m.amount.units, d.units)}, m.currency};
        }
        if (std::holds_alternative<Money>(b)) {
            throw std::runtime_error("numeric / money is not a valid banking operation");
        }
        if (std::holds_alternative<Decimal>(a) || std::holds_alternative<Decimal>(b)) {
            Decimal y = decimal_from_value(b);
            if (y.units == 0) throw std::runtime_error("Division by zero");
            return Decimal{scaled_div(decimal_from_value(a).units, y.units)};
        }
        if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b)) {
            double y = to_double(b);
            if (y == 0.0) throw std::runtime_error("Division by zero");
            return to_double(a) / y;
        }
        int64_t y = std::get<int64_t>(b);
        if (y == 0) throw std::runtime_error("Division by zero");
        return static_cast<int64_t>(std::get<int64_t>(a) / y);
    }

    static Value do_mod(const Value& a, const Value& b) {
        if (std::holds_alternative<Money>(a) || std::holds_alternative<Money>(b)) {
            throw std::runtime_error("Modulo is not defined for money");
        }
        if (std::holds_alternative<Decimal>(a) || std::holds_alternative<Decimal>(b)) {
            int64_t y = decimal_from_value(b).units;
            if (y == 0) throw std::runtime_error("Modulo by zero");
            return Decimal{decimal_from_value(a).units % y};
        }
        if (std::holds_alternative<double>(a) || std::holds_alternative<double>(b)) {
            double y = to_double(b);
            if (y == 0.0) throw std::runtime_error("Modulo by zero");
            return std::fmod(to_double(a), y);
        }
        int64_t y = std::get<int64_t>(b);
        if (y == 0) throw std::runtime_error("Modulo by zero");
        return static_cast<int64_t>(std::get<int64_t>(a) % y);
    }

    static int64_t scaled_mul(int64_t a, int64_t b) {
        return static_cast<int64_t>((static_cast<__int128>(a) * b) / Decimal::SCALE_FACTOR);
    }

    static int64_t scaled_div(int64_t a, int64_t b) {
        return static_cast<int64_t>((static_cast<__int128>(a) * Decimal::SCALE_FACTOR) / b);
    }

    template <typename Fn>
    static Value tensor_binary(const Value& a, const Value& b, Fn fn) {
        bool a_tensor = std::holds_alternative<Tensor>(a);
        bool b_tensor = std::holds_alternative<Tensor>(b);

        if (a_tensor && b_tensor) {
            Tensor x = std::get<Tensor>(a);
            const Tensor& y = std::get<Tensor>(b);
            if (x.shape != y.shape) throw std::runtime_error("Tensor shapes differ");
            for (std::size_t i = 0; i < x.data.size(); ++i) x.data[i] = fn(x.data[i], y.data[i]);
            return x;
        }
        if (a_tensor) {
            Tensor x = std::get<Tensor>(a);
            double y = to_double(b);
            for (double& item : x.data) item = fn(item, y);
            return x;
        }
        if (b_tensor) {
            Tensor y = std::get<Tensor>(b);
            double x = to_double(a);
            for (double& item : y.data) item = fn(x, item);
            return y;
        }
        throw std::runtime_error("Expected Tensor operation");
    }

    static Tensor tensor_from_value(const Value& v) {
        if (std::holds_alternative<Tensor>(v)) return std::get<Tensor>(v);
        if (is_numeric(v) || std::holds_alternative<Money>(v)) {
            return Tensor{{to_double(v)}, {1}};
        }
        throw std::runtime_error("Expected numeric value or Tensor");
    }

    static Tensor tensor_from_values(const std::vector<Value>& values) {
        if (values.empty()) return Tensor{{}, {0}};

        bool all_tensor = true;
        bool all_scalar = true;
        for (const auto& v : values) {
            all_tensor = all_tensor && std::holds_alternative<Tensor>(v);
            all_scalar = all_scalar && (is_numeric(v) || std::holds_alternative<Money>(v));
        }

        if (all_scalar) {
            Tensor t;
            t.shape = {values.size()};
            for (const auto& v : values) t.data.push_back(to_double(v));
            return t;
        }

        if (all_tensor) {
            const Tensor& first = std::get<Tensor>(values[0]);
            Tensor out;
            out.shape.push_back(values.size());
            out.shape.insert(out.shape.end(), first.shape.begin(), first.shape.end());
            for (const auto& v : values) {
                const Tensor& child = std::get<Tensor>(v);
                if (child.shape != first.shape) {
                    throw std::runtime_error("Nested tensor literal has inconsistent shapes");
                }
                out.data.insert(out.data.end(), child.data.begin(), child.data.end());
            }
            return out;
        }

        throw std::runtime_error("Tensor literals must contain all numbers or all tensors");
    }

    std::vector<double> collect_numbers(const std::vector<std::unique_ptr<Expr>>& args) {
        std::vector<double> xs;
        for (const auto& arg : args) {
            Value v = evaluate(arg.get());
            if (std::holds_alternative<Tensor>(v)) {
                const Tensor& t = std::get<Tensor>(v);
                xs.insert(xs.end(), t.data.begin(), t.data.end());
            } else if (is_numeric(v) || std::holds_alternative<Money>(v)) {
                xs.push_back(to_double(v));
            } else {
                throw std::runtime_error("Expected number or Tensor");
            }
        }
        return xs;
    }

    static Decimal decimal_from_value(const Value& v) {
        if (std::holds_alternative<Decimal>(v)) return std::get<Decimal>(v);
        if (std::holds_alternative<Money>(v))   return std::get<Money>(v).amount;
        if (std::holds_alternative<int64_t>(v)) return Decimal{std::get<int64_t>(v) * Decimal::SCALE_FACTOR};
        if (std::holds_alternative<bool>(v))    return Decimal{(std::get<bool>(v) ? 1 : 0) * Decimal::SCALE_FACTOR};
        if (std::holds_alternative<double>(v))  return Decimal{static_cast<int64_t>(std::llround(std::get<double>(v) * Decimal::SCALE_FACTOR))};
        if (std::holds_alternative<std::string>(v)) return parse_decimal(std::get<std::string>(v));
        throw std::runtime_error("Cannot convert value to decimal");
    }

    static Money money_from_value(const Value& v) {
        if (!std::holds_alternative<Money>(v)) throw std::runtime_error("Expected money value");
        return std::get<Money>(v);
    }

    static void require_same_currency(const Money& a, const Money& b) {
        if (a.currency != b.currency) throw std::runtime_error("Money currency mismatch");
    }

    static Decimal parse_decimal(const std::string& text) {
        if (text.empty()) throw std::runtime_error("Invalid decimal literal");
        bool neg = false;
        std::size_t i = 0;
        if (text[i] == '+' || text[i] == '-') {
            neg = text[i] == '-';
            ++i;
        }

        int64_t whole = 0;
        bool any_digit = false;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            any_digit = true;
            whole = whole * 10 + (text[i] - '0');
            ++i;
        }

        int64_t frac = 0;
        int frac_digits = 0;
        if (i < text.size() && text[i] == '.') {
            ++i;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
                if (frac_digits < 6) {
                    frac = frac * 10 + (text[i] - '0');
                    ++frac_digits;
                } else if (text[i] != '0') {
                    throw std::runtime_error("decimal() supports up to 6 fractional digits");
                }
                any_digit = true;
                ++i;
            }
        }
        if (!any_digit || i != text.size()) throw std::runtime_error("Invalid decimal literal '" + text + "'");
        while (frac_digits < 6) { frac *= 10; ++frac_digits; }

        int64_t units = whole * Decimal::SCALE_FACTOR + frac;
        return Decimal{neg ? -units : units};
    }

    static Decimal round_decimal_bankers(Decimal d, int places) {
        if (places < 0 || places > 6) throw std::runtime_error("round_bankers places must be 0..6");
        int64_t divisor = 1;
        for (int i = 0; i < 6 - places; ++i) divisor *= 10;
        if (divisor == 1) return d;

        int64_t sign = d.units < 0 ? -1 : 1;
        int64_t abs_units = d.units < 0 ? -d.units : d.units;
        int64_t q = abs_units / divisor;
        int64_t r = abs_units % divisor;
        int64_t half = divisor / 2;

        if (r > half || (r == half && (q % 2 != 0))) ++q;
        return Decimal{sign * q * divisor};
    }

    static bool value_matches_type(const Value& v, const TypeAnnotation& type) {
        switch (type.kind) {
            case TypeAnnotation::Kind::Int:
                return std::holds_alternative<int64_t>(v);
            case TypeAnnotation::Kind::Float32:
                return std::holds_alternative<int64_t>(v) ||
                       std::holds_alternative<double>(v) ||
                       std::holds_alternative<Decimal>(v);
            case TypeAnnotation::Kind::Str:
                return std::holds_alternative<std::string>(v);
            case TypeAnnotation::Kind::Bool:
                return std::holds_alternative<bool>(v);
            case TypeAnnotation::Kind::Tensor:
                return std::holds_alternative<Tensor>(v);
            case TypeAnnotation::Kind::Decimal:
                return std::holds_alternative<Decimal>(v);
            case TypeAnnotation::Kind::Money:
                return std::holds_alternative<Money>(v);
            case TypeAnnotation::Kind::Custom:
                return true; // future nominal/struct type checker hook
        }
        return false;
    }

    static std::string type_to_string(const TypeAnnotation& type) {
        std::string base;
        switch (type.kind) {
            case TypeAnnotation::Kind::Int:     base = "int"; break;
            case TypeAnnotation::Kind::Float32: base = "f32"; break;
            case TypeAnnotation::Kind::Str:     base = "str"; break;
            case TypeAnnotation::Kind::Bool:    base = "bool"; break;
            case TypeAnnotation::Kind::Tensor:  base = "Tensor"; break;
            case TypeAnnotation::Kind::Decimal: base = "decimal"; break;
            case TypeAnnotation::Kind::Money:   base = "money"; break;
            case TypeAnnotation::Kind::Custom:  base = type.custom_name; break;
        }
        if (!type.args.empty()) {
            base += "<";
            for (std::size_t i = 0; i < type.args.size(); ++i) {
                if (i) base += ", ";
                base += type_to_string(*type.args[i]);
            }
            base += ">";
        }
        return base;
    }

    static std::string value_type_name(const Value& v) {
        if (std::holds_alternative<int64_t>(v))     return "int";
        if (std::holds_alternative<double>(v))      return "f32";
        if (std::holds_alternative<std::string>(v)) return "str";
        if (std::holds_alternative<bool>(v))        return "bool";
        if (std::holds_alternative<Decimal>(v))     return "decimal";
        if (std::holds_alternative<Money>(v))       return "money";
        if (std::holds_alternative<Tensor>(v))      return "Tensor";
        if (std::holds_alternative<std::shared_ptr<UserFunction>>(v)) return "function";
        return "<unknown>";
    }

    static std::string double_to_string(double value) {
        std::ostringstream ss;
        ss << std::setprecision(12) << value;
        return ss.str();
    }

    static std::string decimal_to_string(Decimal d, int min_frac = 0) {
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
            while (static_cast<int>(frac_text.size()) > min_frac && !frac_text.empty() && frac_text.back() == '0') {
                frac_text.pop_back();
            }
            ss << '.' << frac_text;
        }
        return ss.str();
    }

    static std::string tensor_to_string(const Tensor& t) {
        std::ostringstream ss;
        if (t.shape.size() <= 1) {
            ss << '[';
            for (std::size_t i = 0; i < t.data.size(); ++i) {
                if (i) ss << ", ";
                ss << double_to_string(t.data[i]);
            }
            ss << ']';
            return ss.str();
        }
        ss << "Tensor(shape=[";
        for (std::size_t i = 0; i < t.shape.size(); ++i) {
            if (i) ss << ", ";
            ss << t.shape[i];
        }
        ss << "], data=[";
        for (std::size_t i = 0; i < t.data.size(); ++i) {
            if (i) ss << ", ";
            ss << double_to_string(t.data[i]);
        }
        ss << "])";
        return ss.str();
    }

    static std::string value_to_string(const Value& v) {
        if (std::holds_alternative<int64_t>(v))     return std::to_string(std::get<int64_t>(v));
        if (std::holds_alternative<double>(v))      return double_to_string(std::get<double>(v));
        if (std::holds_alternative<bool>(v))        return std::get<bool>(v) ? "true" : "false";
        if (std::holds_alternative<Decimal>(v))     return decimal_to_string(std::get<Decimal>(v));
        if (std::holds_alternative<Money>(v)) {
            const Money& m = std::get<Money>(v);
            return m.currency + " " + decimal_to_string(m.amount, 2);
        }
        if (std::holds_alternative<Tensor>(v))      return tensor_to_string(std::get<Tensor>(v));
        if (std::holds_alternative<std::shared_ptr<UserFunction>>(v)) return "<function>";
        return std::get<std::string>(v);
    }
};
