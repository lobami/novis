#pragma once

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ast.h"

// =============================================================================
// Novis IR Compiler (first backend milestone)
// =============================================================================
// Textual NovisIR is the staging backend before real LLVM/Wasm. It keeps the
// language ready for frameworks without shipping a framework: public models,
// interfaces, generic signatures and function contracts are preserved in IR.

class IRCompiler : public ExprVisitor, public StmtVisitor {
public:
    std::string compile(const std::vector<std::unique_ptr<Stmt>>& program) {
        out_.str("");
        out_.clear();
        temp_id_ = 0;
        label_id_ = 0;
        indent_ = 0;

        emit("; NovisIR v1.0");
        emit("; Created by Loth Mejía Martínez · México · 2026");
        emit("; target: wasm-first | llvm-ready | banking-ai-runtime | framework-ready-types | module-aware");
        emit("module @main {");
        indent_++;
        for (const auto& stmt : program) {
            stmt->accept(*this);
        }
        indent_--;
        emit("}");
        return out_.str();
    }

private:
    std::ostringstream out_;
    int temp_id_ = 0;
    int label_id_ = 0;
    int indent_ = 0;
    std::string last_value_;

    std::string temp() { return "%t" + std::to_string(temp_id_++); }
    std::string label(const std::string& base) { return base + std::to_string(label_id_++); }

    void emit(const std::string& line) {
        for (int i = 0; i < indent_; ++i) out_ << "  ";
        out_ << line << '\n';
    }

    static std::string quote(const std::string& s) {
        std::string out = "\"";
        for (char c : s) {
            switch (c) {
                case '\n': out += "\\n"; break;
                case '\t': out += "\\t"; break;
                case '\\': out += "\\\\"; break;
                case '"':  out += "\\\""; break;
                default:   out += c; break;
            }
        }
        out += "\"";
        return out;
    }

    static std::string join_generics(const std::vector<std::string>& params) {
        if (params.empty()) return "";
        std::string out = "<";
        for (std::size_t i = 0; i < params.size(); ++i) {
            if (i) out += ", ";
            out += params[i];
        }
        out += ">";
        return out;
    }

    static std::string type_name(const TypeAnnotation* type) {
        if (!type) return "infer";
        std::string base;
        switch (type->kind) {
            case TypeAnnotation::Kind::Int:     base = "i64"; break;
            case TypeAnnotation::Kind::Float32: base = "f32"; break;
            case TypeAnnotation::Kind::Str:     base = "str"; break;
            case TypeAnnotation::Kind::Bool:    base = "bool"; break;
            case TypeAnnotation::Kind::Tensor:  base = "Tensor"; break;
            case TypeAnnotation::Kind::Decimal: base = "decimal(scale=6)"; break;
            case TypeAnnotation::Kind::Money:   base = "money(decimal(scale=6), currency)"; break;
            case TypeAnnotation::Kind::Custom:  base = type->custom_name; break;
        }
        if (!type->args.empty()) {
            base += "<";
            for (std::size_t i = 0; i < type->args.size(); ++i) {
                if (i) base += ", ";
                base += type_name(type->args[i].get());
            }
            base += ">";
        }
        return base;
    }

    static std::string op_name(BinOp op) {
        switch (op) {
            case BinOp::Add: return "add";
            case BinOp::Sub: return "sub";
            case BinOp::Mul: return "mul";
            case BinOp::Div: return "div";
            case BinOp::Mod: return "mod";
            case BinOp::Eq:  return "eq";
            case BinOp::Neq: return "neq";
            case BinOp::Lt:  return "lt";
            case BinOp::Gt:  return "gt";
            case BinOp::Lte: return "lte";
            case BinOp::Gte: return "gte";
            case BinOp::And: return "and";
            case BinOp::Or:  return "or";
        }
        return "op";
    }

    // ----------------------------------------------------------------- EXPR
    void visitIntLiteral(IntLiteral& e) override {
        last_value_ = temp();
        emit(last_value_ + " = const.i64 " + std::to_string(e.value));
    }

    void visitFloatLiteral(FloatLiteral& e) override {
        last_value_ = temp();
        emit(last_value_ + " = const.f64 " + std::to_string(e.value));
    }

    void visitStringLiteral(StringLiteral& e) override {
        last_value_ = temp();
        emit(last_value_ + " = const.str " + quote(e.value));
    }

    void visitBoolLiteral(BoolLiteral& e) override {
        last_value_ = temp();
        emit(last_value_ + std::string(" = const.bool ") + (e.value ? "true" : "false"));
    }

    void visitTensorLiteral(TensorLiteral& e) override {
        std::vector<std::string> values;
        for (const auto& element : e.elements) {
            element->accept(*this);
            values.push_back(last_value_);
        }
        last_value_ = temp();
        std::ostringstream ss;
        ss << last_value_ << " = tensor.literal [";
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i) ss << ", ";
            ss << values[i];
        }
        ss << "]";
        emit(ss.str());
    }

    void visitVariableExpr(VariableExpr& e) override {
        last_value_ = temp();
        emit(last_value_ + " = load @" + e.name);
    }

    void visitBinaryExpr(BinaryExpr& e) override {
        e.lhs->accept(*this);
        std::string lhs = last_value_;
        e.rhs->accept(*this);
        std::string rhs = last_value_;
        last_value_ = temp();
        emit(last_value_ + " = " + op_name(e.op) + " " + lhs + ", " + rhs);
    }

    void visitUnaryExpr(UnaryExpr& e) override {
        e.operand->accept(*this);
        std::string operand = last_value_;
        last_value_ = temp();
        emit(last_value_ + std::string(" = ") + (e.op == UnaryOp::Neg ? "neg " : "not ") + operand);
    }

    void visitCallExpr(CallExpr& e) override {
        std::string callee;
        if (auto var = dynamic_cast<VariableExpr*>(e.callee.get())) {
            callee = "@" + var->name;
        } else {
            e.callee->accept(*this);
            callee = last_value_;
        }

        std::vector<std::string> args;
        for (const auto& arg : e.args) {
            arg->accept(*this);
            args.push_back(last_value_);
        }

        last_value_ = temp();
        std::ostringstream ss;
        ss << last_value_ << " = call " << callee << "(";
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i) ss << ", ";
            ss << args[i];
        }
        ss << ")";
        emit(ss.str());
    }

    void visitAssignExpr(AssignExpr& e) override {
        e.value->accept(*this);
        emit("store " + last_value_ + ", @" + e.name);
    }

    // ----------------------------------------------------------------- STMT
    void visitVarDeclStmt(VarDeclStmt& s) override {
        emit("decl @" + s.name + " : " + type_name(s.type.get()));
        if (s.init) {
            s.init->accept(*this);
            emit("store " + last_value_ + ", @" + s.name);
        }
    }

    void visitExprStmt(ExprStmt& s) override {
        s.expr->accept(*this);
        emit("eval " + last_value_);
    }

    void visitBlockStmt(BlockStmt& s) override {
        indent_++;
        for (const auto& stmt : s.statements) stmt->accept(*this);
        indent_--;
    }

    void visitIfStmt(IfStmt& s) override {
        s.condition->accept(*this);
        std::string then_label = label("if.then.");
        std::string else_label = label("if.else.");
        std::string end_label = label("if.end.");
        emit("br " + last_value_ + ", ^" + then_label + ", ^" + else_label);

        emit("^" + then_label + ":");
        indent_++;
        s.then_branch->accept(*this);
        emit("jump ^" + end_label);
        indent_--;

        emit("^" + else_label + ":");
        indent_++;
        if (s.else_branch) s.else_branch->accept(*this);
        emit("jump ^" + end_label);
        indent_--;

        emit("^" + end_label + ":");
    }

    void visitWhileStmt(WhileStmt& s) override {
        std::string cond_label = label("while.cond.");
        std::string body_label = label("while.body.");
        std::string end_label = label("while.end.");
        emit("jump ^" + cond_label);
        emit("^" + cond_label + ":");
        indent_++;
        s.condition->accept(*this);
        emit("br " + last_value_ + ", ^" + body_label + ", ^" + end_label);
        indent_--;

        emit("^" + body_label + ":");
        indent_++;
        s.body->accept(*this);
        emit("jump ^" + cond_label);
        indent_--;

        emit("^" + end_label + ":");
    }

    void visitReturnStmt(ReturnStmt& s) override {
        if (s.value) {
            s.value->accept(*this);
            emit("ret " + last_value_);
        } else {
            emit("ret");
        }
    }

    void visitFunctionDeclStmt(FunctionDeclStmt& s) override {
        std::ostringstream header;
        header << (s.is_public ? "pub " : "") << "fn @" << s.name
               << join_generics(s.type_params) << "(";
        for (std::size_t i = 0; i < s.params.size(); ++i) {
            if (i) header << ", ";
            header << "%" << s.params[i].name << ": " << type_name(s.params[i].type.get());
        }
        header << ") -> " << type_name(s.return_type.get()) << " {";
        emit(header.str());
        if (s.body) s.body->accept(*this);
        emit("}");
    }

    void visitTypeDeclStmt(TypeDeclStmt& s) override {
        emit(std::string(s.is_public ? "pub " : "") + "type @" + s.name + join_generics(s.type_params) + " {");
        indent_++;
        for (const auto& field : s.fields) {
            emit(std::string(field.is_public ? "pub " : "") + field.name + ": " + type_name(field.type.get()));
        }
        indent_--;
        emit("}");
    }

    void visitInterfaceDeclStmt(InterfaceDeclStmt& s) override {
        emit(std::string(s.is_public ? "pub " : "") + "interface @" + s.name + join_generics(s.type_params) + " {");
        indent_++;
        for (const auto& method : s.methods) {
            std::ostringstream sig;
            sig << (method.is_public ? "pub " : "") << "fn @" << method.name << "(";
            for (std::size_t i = 0; i < method.params.size(); ++i) {
                if (i) sig << ", ";
                sig << "%" << method.params[i].name << ": " << type_name(method.params[i].type.get());
            }
            sig << ") -> " << type_name(method.return_type.get());
            emit(sig.str());
        }
        indent_--;
        emit("}");
    }
};
