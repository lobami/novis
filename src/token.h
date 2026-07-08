#pragma once

#include <string>

// =============================================================================
// Novis Token Types
// =============================================================================
// Every distinct token kind produced by the lexer. Kept in a single enum so
// the parser/AST/evaluator can switch on it exhaustively.
enum class TokenType {
    // Special / structural
    EOF_TOKEN,
    UNKNOWN,

    // Indentation-driven structure (Python-like)
    NEWLINE,
    INDENT,
    DEDENT,

    // Literals
    IDENTIFIER,
    INT_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,
    CHAR_LITERAL,

    // Boolean keywords (also act as literal values)
    TRUE_KW,
    FALSE_KW,

    // Type keywords used in annotations
    TYPE_INT,
    TYPE_F32,
    TYPE_STR,
    TYPE_BOOL,
    TYPE_TENSOR,
    TYPE_DECIMAL,
    TYPE_MONEY,

    // Statement keywords
    FN_KW,
    RETURN_KW,
    IF_KW,
    ELSE_KW,
    WHILE_KW,
    IMPORT_KW,
    SPAWN_KW,
    AWAIT_KW,
    AND_KW,
    OR_KW,
    NOT_KW,
    TYPE_KW,
    INTERFACE_KW,
    PUB_KW,
    IMPL_KW,
    AS_KW,

    // Punctuation / operators
    DECLARE_ASSIGN, // :=  declaration / inference assignment
    ASSIGN,         // =   value assignment or typed declaration initializer
    COLON,     // :
    PLUS,      // +
    MINUS,     // -
    STAR,      // *
    SLASH,     // /
    PERCENT,   // %
    EQ,        // ==
    NEQ,       // !=
    GT,        // >
    LT,        // <
    GTE,       // >=
    LTE,       // <=
    LPAREN,    // (
    RPAREN,    // )
    LBRACE,    // {
    RBRACE,    // }
    LBRACKET,  // [
    RBRACKET,  // ]
    COMMA,     // ,
    SEMICOLON, // ;
    DOT,       // .
    ARROW      // ->
};

// A single lexed token. `literal` preserves the raw source text so error
// messages and (later) source mapping stay useful.
struct Token {
    TokenType type;
    std::string literal;
    int line;
    int col;
};