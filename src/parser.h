#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ast.h"
#include "token.h"

// =============================================================================
// Novis Parser
// =============================================================================
// Pratt parser for expressions + recursive descent for statements/types.
// Framework-readiness lives here as language surface, not as a framework:
//   * `type` declarations for domain models / DTOs / schemas.
//   * `interface` declarations for service contracts / repository boundaries.
//   * generic type annotations: List<Account>, Result<T, E>, Tensor<f32>.
//   * `pub` visibility marker so packages can later expose stable APIs.
class Parser {
public:
    explicit Parser(std::vector<Token> tokens)
        : tokens_(std::move(tokens)) {}

    // program := statement*
    std::vector<std::unique_ptr<Stmt>> parse_program() {
        std::vector<std::unique_ptr<Stmt>> stmts;
        skip_newlines();
        while (!is_at_end()) {
            stmts.push_back(parse_statement());
            skip_newlines();
        }
        return stmts;
    }

private:
    std::vector<Token> tokens_;
    std::size_t pos_ = 0;

    // --------------------------------------------------------------- helpers
    const Token& current() const { return tokens_[pos_]; }
    const Token& previous() const { return tokens_[pos_ - 1]; }
    bool is_at_end() const {
        return pos_ >= tokens_.size() || current().type == TokenType::EOF_TOKEN;
    }

    bool check(TokenType t) const {
        return !is_at_end() && current().type == t;
    }

    bool match(TokenType t) {
        if (check(t)) { advance(); return true; }
        return false;
    }

    const Token& advance() {
        if (!is_at_end()) pos_++;
        return previous();
    }

    void skip_newlines() {
        while (match(TokenType::NEWLINE)) { /* consume */ }
    }

    const Token& expect(TokenType t, const char* what) {
        if (check(t)) return advance();
        throw error("Expected " + std::string(what) +
                    ", got '" + current().literal + "'");
    }

    std::runtime_error error(const std::string& msg) const {
        return std::runtime_error(
            "Line " + std::to_string(current().line) +
            ", Col " + std::to_string(current().col) +
            ": " + msg);
    }

    // ============================================================ STATEMENTS
    std::unique_ptr<Stmt> parse_statement() {
        bool is_public = match(TokenType::PUB_KW);

        if (check(TokenType::TYPE_KW))      return parse_type_decl(is_public);
        if (check(TokenType::STRUCT_KW))    return parse_struct_decl(is_public);
        if (check(TokenType::INTERFACE_KW)) return parse_interface_decl(is_public);
        if (check(TokenType::FN_KW))        return parse_function_decl(is_public);

        if (is_public) {
            throw error("'pub' can only prefix type, interface, or fn declarations");
        }

        if (check(TokenType::IF_KW))     return parse_if_stmt();
        if (check(TokenType::WHILE_KW))  return parse_while_stmt();
        if (check(TokenType::RETURN_KW)) return parse_return_stmt();
        return parse_expr_or_var_decl();
    }

    std::unique_ptr<Stmt> parse_type_decl(bool is_public) {
        expect(TokenType::TYPE_KW, "'type'");
        const Token& name = expect(TokenType::IDENTIFIER, "type name");
        auto type_params = parse_type_params_if_any();
        expect(TokenType::COLON, "':'");
        expect(TokenType::NEWLINE, "NEWLINE after type header");
        expect(TokenType::INDENT, "INDENT for type body");

        std::vector<FieldDecl> fields;
        skip_newlines();
        while (!check(TokenType::DEDENT) && !is_at_end()) {
            fields.push_back(parse_field_decl());
            skip_newlines();
        }
        expect(TokenType::DEDENT, "DEDENT to close type body");

        return std::make_unique<TypeDeclStmt>(
            is_public, name.literal, std::move(type_params), std::move(fields));
    }

    // `struct X:` is a Pydantic-style data record. The body is the same
    // field syntax as `type X:`, so we just delegate to parse_type_decl's
    // inner machinery. We don't allow methods in struct bodies — the field
    // parser already rejects them because it only looks for `name: type`
    // followed by NEWLINE.
    std::unique_ptr<Stmt> parse_struct_decl(bool is_public) {
        expect(TokenType::STRUCT_KW, "'struct'");
        const Token& name = expect(TokenType::IDENTIFIER, "struct name");
        auto type_params = parse_type_params_if_any();
        expect(TokenType::COLON, "':'");
        expect(TokenType::NEWLINE, "NEWLINE after struct header");
        expect(TokenType::INDENT, "INDENT for struct body");

        std::vector<FieldDecl> fields;
        skip_newlines();
        while (!check(TokenType::DEDENT) && !is_at_end()) {
            fields.push_back(parse_field_decl());
            skip_newlines();
        }
        expect(TokenType::DEDENT, "DEDENT to close struct body");

        return std::make_unique<StructDeclStmt>(
            is_public, name.literal, std::move(type_params), std::move(fields));
    }

    FieldDecl parse_field_decl() {
        bool is_public = match(TokenType::PUB_KW);
        const Token& name = expect(TokenType::IDENTIFIER, "field name");
        expect(TokenType::COLON, "':' after field name");
        auto type = parse_type_annotation();
        expect(TokenType::NEWLINE, "NEWLINE after field declaration");
        return FieldDecl(is_public, name.literal, std::move(type));
    }

    std::unique_ptr<Stmt> parse_interface_decl(bool is_public) {
        expect(TokenType::INTERFACE_KW, "'interface'");
        const Token& name = expect(TokenType::IDENTIFIER, "interface name");
        auto type_params = parse_type_params_if_any();
        expect(TokenType::COLON, "':'");
        expect(TokenType::NEWLINE, "NEWLINE after interface header");
        expect(TokenType::INDENT, "INDENT for interface body");

        std::vector<MethodSignature> methods;
        skip_newlines();
        while (!check(TokenType::DEDENT) && !is_at_end()) {
            methods.push_back(parse_method_signature());
            skip_newlines();
        }
        expect(TokenType::DEDENT, "DEDENT to close interface body");

        return std::make_unique<InterfaceDeclStmt>(
            is_public, name.literal, std::move(type_params), std::move(methods));
    }

    MethodSignature parse_method_signature() {
        bool is_public = match(TokenType::PUB_KW);
        expect(TokenType::FN_KW, "'fn' in interface method");
        const Token& name = expect(TokenType::IDENTIFIER, "method name");
        expect(TokenType::LPAREN, "'('");
        std::vector<Param> params;
        if (!check(TokenType::RPAREN)) {
            params.push_back(parse_param());
            while (match(TokenType::COMMA)) {
                params.push_back(parse_param());
            }
        }
        expect(TokenType::RPAREN, "')'");
        std::unique_ptr<TypeAnnotation> ret_type;
        if (match(TokenType::ARROW)) ret_type = parse_type_annotation();
        expect(TokenType::NEWLINE, "NEWLINE after interface method signature");
        return MethodSignature(is_public, name.literal, std::move(params), std::move(ret_type));
    }

    std::unique_ptr<Stmt> parse_function_decl(bool is_public) {
        expect(TokenType::FN_KW, "'fn'");
        const Token& name = expect(TokenType::IDENTIFIER, "function name");
        auto type_params = parse_type_params_if_any();
        expect(TokenType::LPAREN, "'('");
        std::vector<Param> params;
        if (!check(TokenType::RPAREN)) {
            params.push_back(parse_param());
            while (match(TokenType::COMMA)) {
                params.push_back(parse_param());
            }
        }
        expect(TokenType::RPAREN, "')'");

        std::unique_ptr<TypeAnnotation> ret_type;
        if (match(TokenType::ARROW)) {
            ret_type = parse_type_annotation();
        }

        expect(TokenType::COLON, "':'");
        expect(TokenType::NEWLINE, "NEWLINE after function header");
        expect(TokenType::INDENT, "INDENT for function body");
        auto body = std::make_unique<BlockStmt>(parse_block());
        expect(TokenType::DEDENT, "DEDENT to close function body");
        return std::make_unique<FunctionDeclStmt>(
            is_public, name.literal, std::move(type_params), std::move(params),
            std::move(ret_type), std::move(body));
    }

    std::vector<std::string> parse_type_params_if_any() {
        std::vector<std::string> params;
        if (!match(TokenType::LT)) return params;
        const Token& first = expect(TokenType::IDENTIFIER, "generic type parameter");
        params.push_back(first.literal);
        while (match(TokenType::COMMA)) {
            const Token& next = expect(TokenType::IDENTIFIER, "generic type parameter");
            params.push_back(next.literal);
        }
        expect(TokenType::GT, "'>' after generic parameters");
        return params;
    }

    Param parse_param() {
        const Token& name = expect(TokenType::IDENTIFIER, "parameter name");
        std::unique_ptr<TypeAnnotation> type;
        if (match(TokenType::COLON)) {
            type = parse_type_annotation();
        }
        return Param(name.literal, std::move(type));
    }

    std::unique_ptr<Stmt> parse_if_stmt() {
        expect(TokenType::IF_KW, "'if'");
        auto cond = parse_expression();
        expect(TokenType::COLON, "':'");
        expect(TokenType::NEWLINE, "NEWLINE after if condition");
        expect(TokenType::INDENT, "INDENT for if body");
        auto then_body = std::make_unique<BlockStmt>(parse_block());
        expect(TokenType::DEDENT, "DEDENT to close if body");

        std::unique_ptr<Stmt> else_body;
        if (match(TokenType::ELSE_KW)) {
            expect(TokenType::COLON, "':'");
            expect(TokenType::NEWLINE, "NEWLINE after 'else'");
            expect(TokenType::INDENT, "INDENT for else body");
            else_body = std::make_unique<BlockStmt>(parse_block());
            expect(TokenType::DEDENT, "DEDENT to close else body");
        }

        return std::make_unique<IfStmt>(
            std::move(cond), std::move(then_body), std::move(else_body));
    }

    std::unique_ptr<Stmt> parse_while_stmt() {
        expect(TokenType::WHILE_KW, "'while'");
        auto cond = parse_expression();
        expect(TokenType::COLON, "':'");
        expect(TokenType::NEWLINE, "NEWLINE after while condition");
        expect(TokenType::INDENT, "INDENT for while body");
        auto body = std::make_unique<BlockStmt>(parse_block());
        expect(TokenType::DEDENT, "DEDENT to close while body");
        return std::make_unique<WhileStmt>(std::move(cond), std::move(body));
    }

    std::unique_ptr<Stmt> parse_return_stmt() {
        expect(TokenType::RETURN_KW, "'return'");
        std::unique_ptr<Expr> value;
        if (!check(TokenType::NEWLINE) && !is_at_end()) {
            value = parse_expression();
        }
        expect(TokenType::NEWLINE, "NEWLINE after return");
        return std::make_unique<ReturnStmt>(std::move(value));
    }

    // Either a bare expression or an inline variable declaration.
    // varDecl := IDENT DECLARE_ASSIGN expr | IDENT COLON typeAnnotation ASSIGN expr
    // exprStmt := expression NEWLINE
    std::unique_ptr<Stmt> parse_expr_or_var_decl() {
        if (looks_like_var_decl()) {
            return parse_var_decl();
        }

        auto expr = parse_expression();
        expect(TokenType::NEWLINE, "NEWLINE after expression");
        return std::make_unique<ExprStmt>(std::move(expr));
    }

    bool looks_like_var_decl() {
        if (!check(TokenType::IDENTIFIER)) return false;
        std::size_t saved = pos_;
        advance(); // IDENTIFIER

        bool is_decl = false;
        if (check(TokenType::DECLARE_ASSIGN)) {
            is_decl = true;
        } else if (check(TokenType::COLON)) {
            advance(); // ':'
            int generic_depth = 0;
            while (!is_at_end() && !check(TokenType::NEWLINE)) {
                if (check(TokenType::LT)) generic_depth++;
                else if (check(TokenType::GT) && generic_depth > 0) generic_depth--;
                else if (check(TokenType::ASSIGN) && generic_depth == 0) {
                    is_decl = true;
                    break;
                }
                advance();
            }
        }

        pos_ = saved;
        return is_decl;
    }

    std::unique_ptr<Stmt> parse_var_decl() {
        const Token& name_tok = expect(TokenType::IDENTIFIER, "variable name");
        std::unique_ptr<TypeAnnotation> type;
        if (match(TokenType::COLON)) {
            type = parse_type_annotation();
            expect(TokenType::ASSIGN, "'=' after type annotation");
        } else {
            expect(TokenType::DECLARE_ASSIGN, "':=' for inferred declaration");
        }
        auto init = parse_expression();
        expect(TokenType::NEWLINE, "NEWLINE after var declaration");
        return std::make_unique<VarDeclStmt>(
            name_tok.literal, std::move(type), std::move(init));
    }

    // block := statement*  (caller has already consumed the INDENT)
    std::vector<std::unique_ptr<Stmt>> parse_block() {
        std::vector<std::unique_ptr<Stmt>> stmts;
        skip_newlines();
        while (!check(TokenType::DEDENT) && !is_at_end()) {
            stmts.push_back(parse_statement());
            skip_newlines();
        }
        return stmts;
    }

    // ============================================================== EXPRESSIONS
    std::unique_ptr<Expr> parse_expression() {
        return parse_binary(1);
    }

    std::unique_ptr<Expr> parse_binary(int min_prec) {
        auto left = parse_unary();

        while (true) {
            int prec = binary_precedence(current().type);
            if (prec < min_prec) break;

            BinOp op = token_to_binop(current().type);
            advance();
            auto right = parse_binary(prec + 1);
            left = std::make_unique<BinaryExpr>(
                op, std::move(left), std::move(right));
        }

        return left;
    }

    std::unique_ptr<Expr> parse_unary() {
        if (check(TokenType::MINUS)) {
            advance();
            auto operand = parse_unary();
            return std::make_unique<UnaryExpr>(UnaryOp::Neg, std::move(operand));
        }
        if (check(TokenType::NOT_KW)) {
            advance();
            auto operand = parse_unary();
            return std::make_unique<UnaryExpr>(UnaryOp::Not, std::move(operand));
        }
        if (check(TokenType::AWAIT_KW)) {
            advance();
            auto task = parse_unary();
            return std::make_unique<AwaitExpr>(std::move(task));
        }
        return parse_spawn_or_call();
    }

    std::unique_ptr<Expr> parse_spawn_or_call() {
        if (check(TokenType::SPAWN_KW)) {
            advance();
            expect(TokenType::LPAREN, "'('");
            std::vector<std::unique_ptr<Expr>> args;
            if (!check(TokenType::RPAREN)) {
                args.push_back(parse_expression());
                while (match(TokenType::COMMA)) {
                    if (check(TokenType::RPAREN)) break;
                    args.push_back(parse_expression());
                }
            }
            expect(TokenType::RPAREN, "')'");
            // spawn(fn) lowers to: __spawn(fn) — visitSpawnExpr will look up
            // the UserFunction in the env and submit it to the worker pool.
            auto callable = std::make_unique<CallExpr>(
                std::make_unique<VariableExpr>("__spawn"), std::move(args));
            return std::make_unique<SpawnExpr>(std::move(callable));
        }
        return parse_call();
    }

    std::unique_ptr<Expr> parse_call() {
        auto expr = parse_primary();

        while (true) {
            if (match(TokenType::LPAREN)) {
                std::vector<std::unique_ptr<Expr>> args;
                if (!check(TokenType::RPAREN)) {
                    args.push_back(parse_expression());
                    while (match(TokenType::COMMA)) {
                        if (check(TokenType::RPAREN)) break;
                        args.push_back(parse_expression());
                    }
                }
                expect(TokenType::RPAREN, "')'");
                expr = std::make_unique<CallExpr>(std::move(expr), std::move(args));
            } else if (match(TokenType::DOT)) {
                const Token& field = expect(TokenType::IDENTIFIER, "field name");
                std::vector<std::unique_ptr<Expr>> none;
                expr = std::make_unique<CallExpr>(
                    std::make_unique<VariableExpr>(field.literal), std::move(none));
            } else if (match(TokenType::LBRACKET)) {
                auto idx = parse_expression();
                expect(TokenType::RBRACKET, "']'");
                std::vector<std::unique_ptr<Expr>> args;
                args.push_back(std::move(idx));
                expr = std::make_unique<CallExpr>(std::move(expr), std::move(args));
            } else {
                break;
            }
        }
        return expr;
    }

    std::unique_ptr<Expr> parse_primary() {
        const Token& tok = current();
        switch (tok.type) {
            case TokenType::INT_LITERAL:
                advance();
                return std::make_unique<IntLiteral>(static_cast<int64_t>(std::stoll(tok.literal)));

            case TokenType::FLOAT_LITERAL:
                advance();
                return std::make_unique<FloatLiteral>(std::stod(tok.literal));

            case TokenType::STRING_LITERAL:
                advance();
                return std::make_unique<StringLiteral>(unescape_string(tok.literal));

            case TokenType::CHAR_LITERAL:
                advance();
                return std::make_unique<IntLiteral>(static_cast<int64_t>(unescape_char(tok.literal)));

            case TokenType::TRUE_KW:
                advance();
                return std::make_unique<BoolLiteral>(true);

            case TokenType::FALSE_KW:
                advance();
                return std::make_unique<BoolLiteral>(false);

            case TokenType::IDENTIFIER:
            case TokenType::TYPE_DECIMAL:
            case TokenType::TYPE_MONEY:
            case TokenType::TYPE_TENSOR: {
                advance();
                if (check(TokenType::ASSIGN)) {
                    advance();
                    auto value = parse_expression();
                    return std::make_unique<AssignExpr>(tok.literal, std::move(value));
                }
                return std::make_unique<VariableExpr>(tok.literal);
            }

            case TokenType::LPAREN: {
                advance();
                auto inner = parse_expression();
                expect(TokenType::RPAREN, "')'");
                return inner;
            }

            case TokenType::LBRACKET: {
                advance();
                std::vector<std::unique_ptr<Expr>> elements;
                if (!check(TokenType::RBRACKET)) {
                    elements.push_back(parse_expression());
                    while (match(TokenType::COMMA)) {
                        if (check(TokenType::RBRACKET)) break;
                        elements.push_back(parse_expression());
                    }
                }
                expect(TokenType::RBRACKET, "']'");
                return std::make_unique<TensorLiteral>(std::move(elements));
            }

            case TokenType::LBRACE: {
                // Dict literal: { "k1": v1, "k2": v2, ... }
                advance();
                std::vector<DictLiteralEntry> entries;
                if (!check(TokenType::RBRACE)) {
                    auto k = parse_dict_key();
                    expect(TokenType::COLON, "':' after dict key");
                    auto v = parse_expression();
                    entries.emplace_back(std::move(k), std::move(v));
                    while (match(TokenType::COMMA)) {
                        if (check(TokenType::RBRACE)) break;
                        auto k2 = parse_dict_key();
                        expect(TokenType::COLON, "':' after dict key");
                        auto v2 = parse_expression();
                        entries.emplace_back(std::move(k2), std::move(v2));
                    }
                }
                expect(TokenType::RBRACE, "'}' to close dict literal");
                return std::make_unique<DictLiteral>(std::move(entries));
            }

            default:
                throw error("Unexpected token '" + tok.literal + "' in expression");
        }
    }

    // Dict keys can be string literals or identifier-style names. We accept
    // bare identifiers as a convenience (e.g. {name: "x", age: 7}) so the
    // JSON-style "key": value is the verbose form, the bareword form is
    // ergonomic sugar.
    std::unique_ptr<Expr> parse_dict_key() {
        if (check(TokenType::STRING_LITERAL)) {
            std::string lit = current().literal;
            advance();
            return std::make_unique<StringLiteral>(unescape_string(lit));
        }
        if (check(TokenType::IDENTIFIER)) {
            std::string lit = current().literal;
            advance();
            return std::make_unique<StringLiteral>(lit);
        }
        throw error("dict key must be a string or identifier");
    }

    // ============================================================ ANNOTATIONS
    std::unique_ptr<TypeAnnotation> parse_type_annotation() {
        std::unique_ptr<TypeAnnotation> base;
        if (match(TokenType::TYPE_INT))         base = std::make_unique<TypeAnnotation>(TypeAnnotation::Kind::Int);
        else if (match(TokenType::TYPE_F32))    base = std::make_unique<TypeAnnotation>(TypeAnnotation::Kind::Float32);
        else if (match(TokenType::TYPE_STR))    base = std::make_unique<TypeAnnotation>(TypeAnnotation::Kind::Str);
        else if (match(TokenType::TYPE_BOOL))   base = std::make_unique<TypeAnnotation>(TypeAnnotation::Kind::Bool);
        else if (match(TokenType::TYPE_TENSOR)) base = std::make_unique<TypeAnnotation>(TypeAnnotation::Kind::Tensor);
        else if (match(TokenType::TYPE_DECIMAL)) base = std::make_unique<TypeAnnotation>(TypeAnnotation::Kind::Decimal);
        else if (match(TokenType::TYPE_MONEY))   base = std::make_unique<TypeAnnotation>(TypeAnnotation::Kind::Money);
        else if (check(TokenType::IDENTIFIER)) {
            std::string name = current().literal;
            advance();
            base = std::make_unique<TypeAnnotation>(std::move(name));
        } else {
            throw error("Expected type annotation");
        }

        if (match(TokenType::LT)) {
            std::vector<std::unique_ptr<TypeAnnotation>> args;
            args.push_back(parse_type_annotation());
            while (match(TokenType::COMMA)) {
                args.push_back(parse_type_annotation());
            }
            expect(TokenType::GT, "'>' after generic type arguments");
            base->args = std::move(args);
        }
        return base;
    }

    // ============================================================ PRECEDENCE
    static int binary_precedence(TokenType t) {
        switch (t) {
            case TokenType::OR_KW:  return 1;
            case TokenType::AND_KW: return 2;
            case TokenType::EQ:    case TokenType::NEQ: return 3;
            case TokenType::GT:    case TokenType::LT:
            case TokenType::GTE:   case TokenType::LTE: return 4;
            case TokenType::PLUS:  case TokenType::MINUS: return 5;
            case TokenType::STAR:  case TokenType::SLASH:
            case TokenType::PERCENT: return 6;
            default: return -1;
        }
    }

    static BinOp token_to_binop(TokenType t) {
        switch (t) {
            case TokenType::AND_KW:  return BinOp::And;
            case TokenType::OR_KW:   return BinOp::Or;
            case TokenType::PLUS:    return BinOp::Add;
            case TokenType::MINUS:   return BinOp::Sub;
            case TokenType::STAR:    return BinOp::Mul;
            case TokenType::SLASH:   return BinOp::Div;
            case TokenType::PERCENT: return BinOp::Mod;
            case TokenType::EQ:      return BinOp::Eq;
            case TokenType::NEQ:     return BinOp::Neq;
            case TokenType::LT:      return BinOp::Lt;
            case TokenType::GT:      return BinOp::Gt;
            case TokenType::LTE:     return BinOp::Lte;
            case TokenType::GTE:     return BinOp::Gte;
            default: throw std::logic_error("non-binary token passed to token_to_binop");
        }
    }

    // ============================================================ STRING UTIL
    static std::string unescape_string(const std::string& raw) {
        std::size_t start = 0;
        std::size_t end = raw.size();
        if (raw.size() >= 6 && raw.compare(0, 3, "\"\"\"") == 0 &&
            raw.compare(raw.size() - 3, 3, "\"\"\"") == 0) {
            start = 3;
            end = raw.size() - 3;
        } else if (raw.size() >= 2 && raw[0] == '"' && raw.back() == '"') {
            start = 1;
            end = raw.size() - 1;
        }
        std::string out;
        for (std::size_t i = start; i < end; ++i) {
            if (raw[i] == '\\' && i + 1 < end) {
                char c = raw[++i];
                switch (c) {
                    case 'n':  out += '\n'; break;
                    case 't':  out += '\t'; break;
                    case '\\': out += '\\'; break;
                    case '"':  out += '"';  break;
                    case '\'': out += '\''; break;
                    case '0':  out += '\0'; break;
                    default:   out += c;    break;
                }
            } else {
                out += raw[i];
            }
        }
        return out;
    }

    static int64_t unescape_char(const std::string& raw) {
        if (raw.size() < 3) return 0;
        if (raw[1] == '\\' && raw.size() >= 4) {
            switch (raw[2]) {
                case 'n':  return '\n';
                case 't':  return '\t';
                case '\\': return '\\';
                case '\'': return '\'';
                case '0':  return '\0';
                default:   return static_cast<int64_t>(raw[2]);
            }
        }
        return static_cast<int64_t>(raw[1]);
    }
};
