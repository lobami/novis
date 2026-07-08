#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// =============================================================================
// Novis AST
// =============================================================================
// Header-only AST. The design is intentionally boring and explicit: frontend
// passes (type checker, interpreter, NovisIR/LLVM/Wasm codegen) walk the same
// tree through visitors. This keeps compile times low and avoids hidden runtime
// reflection.

struct Expr;
struct Stmt;

class ExprVisitor {
public:
    virtual ~ExprVisitor() = default;
    virtual void visitIntLiteral(struct IntLiteral&) = 0;
    virtual void visitFloatLiteral(struct FloatLiteral&) = 0;
    virtual void visitStringLiteral(struct StringLiteral&) = 0;
    virtual void visitBoolLiteral(struct BoolLiteral&) = 0;
    virtual void visitTensorLiteral(struct TensorLiteral&) = 0;
    virtual void visitVariableExpr(struct VariableExpr&) = 0;
    virtual void visitBinaryExpr(struct BinaryExpr&) = 0;
    virtual void visitUnaryExpr(struct UnaryExpr&) = 0;
    virtual void visitCallExpr(struct CallExpr&) = 0;
    virtual void visitAssignExpr(struct AssignExpr&) = 0;
    virtual void visitSpawnExpr(struct SpawnExpr&) = 0;
    virtual void visitAwaitExpr(struct AwaitExpr&) = 0;
};

class StmtVisitor {
public:
    virtual ~StmtVisitor() = default;
    virtual void visitVarDeclStmt(struct VarDeclStmt&) = 0;
    virtual void visitExprStmt(struct ExprStmt&) = 0;
    virtual void visitBlockStmt(struct BlockStmt&) = 0;
    virtual void visitIfStmt(struct IfStmt&) = 0;
    virtual void visitWhileStmt(struct WhileStmt&) = 0;
    virtual void visitReturnStmt(struct ReturnStmt&) = 0;
    virtual void visitFunctionDeclStmt(struct FunctionDeclStmt&) = 0;
    virtual void visitTypeDeclStmt(struct TypeDeclStmt&) = 0;
    virtual void visitInterfaceDeclStmt(struct InterfaceDeclStmt&) = 0;
};

// =============================================================================
// Operators & type annotations
// =============================================================================
enum class BinOp { Add, Sub, Mul, Div, Mod, Eq, Neq, Lt, Gt, Lte, Gte, And, Or };
enum class UnaryOp { Neg, Not };

struct TypeAnnotation {
    enum class Kind { Int, Float32, Str, Bool, Tensor, Decimal, Money, Custom } kind;
    std::string custom_name; // populated when kind == Custom
    std::vector<std::unique_ptr<TypeAnnotation>> args; // generic arguments

    TypeAnnotation() : kind(Kind::Int), custom_name(), args() {}
    explicit TypeAnnotation(Kind k) : kind(k), custom_name(), args() {}
    TypeAnnotation(Kind k, std::vector<std::unique_ptr<TypeAnnotation>> a)
        : kind(k), custom_name(), args(std::move(a)) {}
    explicit TypeAnnotation(std::string name)
        : kind(Kind::Custom), custom_name(std::move(name)), args() {}
    TypeAnnotation(std::string name, std::vector<std::unique_ptr<TypeAnnotation>> a)
        : kind(Kind::Custom), custom_name(std::move(name)), args(std::move(a)) {}
};

struct Param {
    std::string name;
    std::unique_ptr<TypeAnnotation> type; // optional at parse level; typechecker may require it
    Param(std::string n, std::unique_ptr<TypeAnnotation> t)
        : name(std::move(n)), type(std::move(t)) {}
};

struct FieldDecl {
    bool is_public = false;
    std::string name;
    std::unique_ptr<TypeAnnotation> type;
    FieldDecl(bool pub, std::string n, std::unique_ptr<TypeAnnotation> t)
        : is_public(pub), name(std::move(n)), type(std::move(t)) {}
};

struct MethodSignature {
    bool is_public = false;
    std::string name;
    std::vector<Param> params;
    std::unique_ptr<TypeAnnotation> return_type;
    MethodSignature(bool pub,
                    std::string n,
                    std::vector<Param> p,
                    std::unique_ptr<TypeAnnotation> r)
        : is_public(pub), name(std::move(n)), params(std::move(p)),
          return_type(std::move(r)) {}
};

// =============================================================================
// Expression nodes
// =============================================================================
struct Expr {
    virtual ~Expr() = default;
    virtual void accept(ExprVisitor& v) = 0;
};

struct IntLiteral : Expr {
    int64_t value;
    explicit IntLiteral(int64_t v) : value(v) {}
    void accept(ExprVisitor& v) override { v.visitIntLiteral(*this); }
};

struct FloatLiteral : Expr {
    double value;
    explicit FloatLiteral(double v) : value(v) {}
    void accept(ExprVisitor& v) override { v.visitFloatLiteral(*this); }
};

struct StringLiteral : Expr {
    std::string value;
    explicit StringLiteral(std::string v) : value(std::move(v)) {}
    void accept(ExprVisitor& v) override { v.visitStringLiteral(*this); }
};

struct BoolLiteral : Expr {
    bool value;
    explicit BoolLiteral(bool v) : value(v) {}
    void accept(ExprVisitor& v) override { v.visitBoolLiteral(*this); }
};

struct TensorLiteral : Expr {
    std::vector<std::unique_ptr<Expr>> elements;
    explicit TensorLiteral(std::vector<std::unique_ptr<Expr>> elems)
        : elements(std::move(elems)) {}
    void accept(ExprVisitor& v) override { v.visitTensorLiteral(*this); }
};

struct VariableExpr : Expr {
    std::string name;
    explicit VariableExpr(std::string n) : name(std::move(n)) {}
    void accept(ExprVisitor& v) override { v.visitVariableExpr(*this); }
};

struct BinaryExpr : Expr {
    BinOp op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
    BinaryExpr(BinOp o, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r)
        : op(o), lhs(std::move(l)), rhs(std::move(r)) {}
    void accept(ExprVisitor& v) override { v.visitBinaryExpr(*this); }
};

struct UnaryExpr : Expr {
    UnaryOp op;
    std::unique_ptr<Expr> operand;
    UnaryExpr(UnaryOp o, std::unique_ptr<Expr> e)
        : op(o), operand(std::move(e)) {}
    void accept(ExprVisitor& v) override { v.visitUnaryExpr(*this); }
};

struct CallExpr : Expr {
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> args;
    CallExpr(std::unique_ptr<Expr> c, std::vector<std::unique_ptr<Expr>> a)
        : callee(std::move(c)), args(std::move(a)) {}
    void accept(ExprVisitor& v) override { v.visitCallExpr(*this); }
};

// `spawn` expr: a zero-arg lambda call that runs asynchronously and yields a
// `Task<T>`. The result is a first-class value you can `await` later.
struct SpawnExpr : Expr {
    std::unique_ptr<Expr> callee;
    SpawnExpr(std::unique_ptr<Expr> c) : callee(std::move(c)) {}
    void accept(ExprVisitor& v) override { v.visitSpawnExpr(*this); }
};

// `await` expr: blocks the current fiber until a `Task<T>` resolves and
// produces the underlying value. Errors are rethrown as runtime errors.
struct AwaitExpr : Expr {
    std::unique_ptr<Expr> task;
    AwaitExpr(std::unique_ptr<Expr> t) : task(std::move(t)) {}
    void accept(ExprVisitor& v) override { v.visitAwaitExpr(*this); }
};

struct AssignExpr : Expr {
    std::string name;
    std::unique_ptr<Expr> value;
    AssignExpr(std::string n, std::unique_ptr<Expr> v)
        : name(std::move(n)), value(std::move(v)) {}
    void accept(ExprVisitor& v) override { v.visitAssignExpr(*this); }
};

// =============================================================================
// Statement nodes
// =============================================================================
struct Stmt {
    virtual ~Stmt() = default;
    virtual void accept(StmtVisitor& v) = 0;
};

struct VarDeclStmt : Stmt {
    std::string name;
    std::unique_ptr<TypeAnnotation> type; // may be null for inference
    std::unique_ptr<Expr> init;
    VarDeclStmt(std::string n,
                std::unique_ptr<TypeAnnotation> t,
                std::unique_ptr<Expr> i)
        : name(std::move(n)), type(std::move(t)), init(std::move(i)) {}
    void accept(StmtVisitor& v) override { v.visitVarDeclStmt(*this); }
};

struct ExprStmt : Stmt {
    std::unique_ptr<Expr> expr;
    explicit ExprStmt(std::unique_ptr<Expr> e) : expr(std::move(e)) {}
    void accept(StmtVisitor& v) override { v.visitExprStmt(*this); }
};

struct BlockStmt : Stmt {
    std::vector<std::unique_ptr<Stmt>> statements;
    explicit BlockStmt(std::vector<std::unique_ptr<Stmt>> s)
        : statements(std::move(s)) {}
    void accept(StmtVisitor& v) override { v.visitBlockStmt(*this); }
};

struct IfStmt : Stmt {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> then_branch;
    std::unique_ptr<Stmt> else_branch; // may be null
    IfStmt(std::unique_ptr<Expr> cond,
           std::unique_ptr<Stmt> t,
           std::unique_ptr<Stmt> e)
        : condition(std::move(cond)),
          then_branch(std::move(t)),
          else_branch(std::move(e)) {}
    void accept(StmtVisitor& v) override { v.visitIfStmt(*this); }
};

struct WhileStmt : Stmt {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> body;
    WhileStmt(std::unique_ptr<Expr> c, std::unique_ptr<Stmt> b)
        : condition(std::move(c)), body(std::move(b)) {}
    void accept(StmtVisitor& v) override { v.visitWhileStmt(*this); }
};

struct ReturnStmt : Stmt {
    std::unique_ptr<Expr> value; // may be null for bare `return`
    explicit ReturnStmt(std::unique_ptr<Expr> v) : value(std::move(v)) {}
    void accept(StmtVisitor& v) override { v.visitReturnStmt(*this); }
};

struct FunctionDeclStmt : Stmt {
    bool is_public = false;
    std::string name;
    std::vector<std::string> type_params;
    std::vector<Param> params;
    std::unique_ptr<TypeAnnotation> return_type; // optional
    std::unique_ptr<Stmt> body;
    FunctionDeclStmt(bool pub,
                     std::string n,
                     std::vector<std::string> generics,
                     std::vector<Param> p,
                     std::unique_ptr<TypeAnnotation> r,
                     std::unique_ptr<Stmt> b)
        : is_public(pub),
          name(std::move(n)),
          type_params(std::move(generics)),
          params(std::move(p)),
          return_type(std::move(r)),
          body(std::move(b)) {}
    void accept(StmtVisitor& v) override { v.visitFunctionDeclStmt(*this); }
};

struct TypeDeclStmt : Stmt {
    bool is_public = false;
    std::string name;
    std::vector<std::string> type_params;
    std::vector<FieldDecl> fields;
    TypeDeclStmt(bool pub,
                 std::string n,
                 std::vector<std::string> generics,
                 std::vector<FieldDecl> f)
        : is_public(pub), name(std::move(n)), type_params(std::move(generics)),
          fields(std::move(f)) {}
    void accept(StmtVisitor& v) override { v.visitTypeDeclStmt(*this); }
};

struct InterfaceDeclStmt : Stmt {
    bool is_public = false;
    std::string name;
    std::vector<std::string> type_params;
    std::vector<MethodSignature> methods;
    InterfaceDeclStmt(bool pub,
                      std::string n,
                      std::vector<std::string> generics,
                      std::vector<MethodSignature> m)
        : is_public(pub), name(std::move(n)), type_params(std::move(generics)),
          methods(std::move(m)) {}
    void accept(StmtVisitor& v) override { v.visitInterfaceDeclStmt(*this); }
};
