#pragma once

#include <string>
#include <vector>
#include <cctype>
#include <cstddef>
#include <stdexcept>

#include "token.h"

// =============================================================================
// Novis Lexer
// =============================================================================
// Header-only, indentation-sensitive lexer. Emits NEWLINE / INDENT / DEDENT
// tokens the parser can treat as statement delimiters, mirroring Python's
// significant-whitespace model.
//
// Indentation rules (strict, per design spec):
//   * 4 spaces == 1 level (INDENT_SIZE).
//   * Tabs are rejected outright.
//   * Lines that are blank or comment-only produce no NEWLINE.
//   * A NEWLINE is emitted only when an actual '\n' is consumed after real
//     tokens, OR when the file ends on a non-empty logical line.
//
// Two-character operators are matched before single-character ones to avoid
// greedy mistakes (e.g. ':' vs ':=', '=' vs '==', '<' vs '<=', '>' vs '>=',
// '-' vs '->').
class Lexer {
public:
    explicit Lexer(const std::string& source) : input_(source) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        indent_stack_.push_back(0);
        at_line_start_ = true;

        while (pos_ < input_.size()) {
            if (at_line_start_) {
                if (!handle_line_start(tokens)) {
                    // line was blank / comment / consumed; loop continues
                    continue;
                }
                // Now positioned at first real char of the line.
            }

            char c = peek();

            // Newline terminates the current logical line.
            if (c == '\n') {
                advance();
                push_token(tokens,{TokenType::NEWLINE, "\\n", line_, col_});
                line_++;
                col_ = 1;
                at_line_start_ = true;
                continue;
            }

            // Any whitespace inside a line is insignificant (only spaces and
            // tabs can appear; tabs were already rejected at line start).
            if (c == ' ' || c == '\t' || c == '\r') {
                advance();
                continue;
            }

            // Line comment: '#' to end of line.
            if (c == '#') {
                while (pos_ < input_.size() && peek() != '\n') {
                    advance();
                }
                continue;
            }

            // String literal: "..." or triple-quoted """..."""
            if (c == '"') {
                tokenize_string(tokens);
                continue;
            }

            // Char literal: 'x'
            if (c == '\'') {
                tokenize_char(tokens);
                continue;
            }

            // Number literal.
            if (std::isdigit(static_cast<unsigned char>(c))) {
                tokenize_number(tokens);
                continue;
            }

            // Identifier or keyword.
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                tokenize_identifier(tokens);
                continue;
            }

            // Two-character operators first.
            if (match_two(':', '=')) { push_token(tokens,{TokenType::DECLARE_ASSIGN, ":=", line_, col_before(2)}); continue; }
            if (match_two('=', '=')) { push_token(tokens,{TokenType::EQ,    "==",   line_, col_before(2)}); continue; }
            if (match_two('!', '=')) { push_token(tokens,{TokenType::NEQ,   "!=",   line_, col_before(2)}); continue; }
            if (match_two('>', '=')) { push_token(tokens,{TokenType::GTE,   ">=",   line_, col_before(2)}); continue; }
            if (match_two('<', '=')) { push_token(tokens,{TokenType::LTE,   "<=",   line_, col_before(2)}); continue; }
            if (match_two('-', '>')) { push_token(tokens,{TokenType::ARROW, "->",   line_, col_before(2)}); continue; }

            // Single-character punctuation / operators.
            char single = advance();
            TokenType tt = single_char_type(single);
            push_token(tokens,{tt, std::string(1, single), line_, col_before(1)});
        }

        // If the source ended on a real token without a trailing newline, inject
        // one so the parser can treat each statement as NEWLINE-terminated.
        // This is purely a courtesy for files that omit the final '\n'.
        if (last_was_real_) {
            push_token(tokens,{TokenType::NEWLINE, "", line_, col_});
        }

        // Drain any still-open indentation so the parser sees balanced blocks.
        while (indent_stack_.size() > 1) {
            indent_stack_.pop_back();
            push_token(tokens,{TokenType::DEDENT, "", line_, col_});
        }

        push_token(tokens,{TokenType::EOF_TOKEN, "", line_, col_});
        return tokens;
    }

private:
    static constexpr int INDENT_SIZE = 4;

    std::string input_;
    std::size_t pos_ = 0;
    int line_ = 1;
    int col_ = 1;

    // Indentation state. Always contains at least one element (0).
    std::vector<int> indent_stack_;

    // True when we are positioned at the start of a (potential) logical line.
    bool at_line_start_ = true;

    // Tracks whether the most recently emitted token was a "real" statement
    // token (i.e. not NEWLINE/INDENT/DEDENT/EOF). Used at EOF to decide
    // whether to inject a final NEWLINE for the parser.
    bool last_was_real_ = false;

    // ------------------------------------------------------------------ utils
    char peek() const {
        return pos_ < input_.size() ? input_[pos_] : '\0';
    }

    char peek_at(std::size_t offset) const {
        return (pos_ + offset) < input_.size() ? input_[pos_ + offset] : '\0';
    }

    char advance() {
        char c = input_[pos_++];
        if (c == '\n') {
            // callers handle newline bookkeeping themselves
        } else {
            col_++;
        }
        return c;
    }

    // Return a column pointing to where a token of length `len` started,
    // given that we are about to emit and `col_` already advanced past it.
    int col_before(int len) const {
        return col_ - len;
    }

    bool match_two(char a, char b) {
        if (peek() == a && peek_at(1) == b) {
            advance(); // a
            advance(); // b
            return true;
        }
        return false;
    }

    // ---------------------------------------------------- line-start handling
    // Returns true if the line had real content (caller should tokenize it).
    // Returns false if the line was blank / comment-only and was consumed.
    bool handle_line_start(std::vector<Token>& tokens) {
        int space_count = 0;
        while (peek() == ' ') {
            advance();
            space_count++;
        }

        if (peek() == '\t') {
            throw std::runtime_error(
                "Line " + std::to_string(line_) +
                ", Col " + std::to_string(col_) +
                ": Tab character not allowed in Novis source");
        }

        // Blank line or comment-only line: skip entirely.
        if (peek() == '\n') {
            advance();
            line_++;
            col_ = 1;
            return false;
        }
        if (peek() == '#') {
            while (pos_ < input_.size() && peek() != '\n') {
                advance();
            }
            return false;
        }
        if (peek() == '\0') {
            return false; // EOF before any content
        }

        // Real content ahead: validate indentation and adjust stack.
        if (space_count % INDENT_SIZE != 0) {
            throw std::runtime_error(
                "Line " + std::to_string(line_) +
                ", Col 1: Indentation not a multiple of " +
                std::to_string(INDENT_SIZE));
        }

        int level = space_count / INDENT_SIZE;
        int top = indent_stack_.back();

        if (level > top) {
            // Always emit exactly one INDENT when entering a new block (a
            // single jump can only increase by one logical level in well-
            // formed code, but we accept the general case).
            while (level > indent_stack_.back()) {
                indent_stack_.push_back(indent_stack_.back() + 1);
                push_token(tokens,{TokenType::INDENT, "", line_, 1});
            }
        } else {
            while (level < indent_stack_.back()) {
                indent_stack_.pop_back();
                push_token(tokens,{TokenType::DEDENT, "", line_, 1});
            }
        }

        at_line_start_ = false;
        return true;
    }

    // ----------------------------------------------------- identifier/keyword
    void tokenize_identifier(std::vector<Token>& tokens) {
        int start_col = col_;
        std::size_t start = pos_;
        while (pos_ < input_.size()) {
            char c = input_[pos_];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                advance();
            } else {
                break;
            }
        }
        std::string text = input_.substr(start, pos_ - start);
        push_token(tokens,{keyword_type(text), text, line_, start_col});
    }

    static TokenType keyword_type(const std::string& text) {
        if (text == "fn")     return TokenType::FN_KW;
        if (text == "return") return TokenType::RETURN_KW;
        if (text == "if")     return TokenType::IF_KW;
        if (text == "else")   return TokenType::ELSE_KW;
        if (text == "while")  return TokenType::WHILE_KW;
        if (text == "import") return TokenType::IMPORT_KW;
        if (text == "spawn")  return TokenType::SPAWN_KW;
        if (text == "await")  return TokenType::AWAIT_KW;
        if (text == "and")    return TokenType::AND_KW;
        if (text == "or")     return TokenType::OR_KW;
        if (text == "not")    return TokenType::NOT_KW;
        if (text == "type")   return TokenType::TYPE_KW;
        if (text == "struct") return TokenType::STRUCT_KW;
        if (text == "interface") return TokenType::INTERFACE_KW;
        if (text == "pub")    return TokenType::PUB_KW;
        if (text == "impl")   return TokenType::IMPL_KW;
        if (text == "as")     return TokenType::AS_KW;
        if (text == "true")   return TokenType::TRUE_KW;
        if (text == "false")  return TokenType::FALSE_KW;
        if (text == "int")    return TokenType::TYPE_INT;
        if (text == "f32")    return TokenType::TYPE_F32;
        if (text == "str")    return TokenType::TYPE_STR;
        if (text == "bool")   return TokenType::TYPE_BOOL;
        if (text == "Tensor") return TokenType::TYPE_TENSOR;
        if (text == "decimal") return TokenType::TYPE_DECIMAL;
        if (text == "money")   return TokenType::TYPE_MONEY;
        return TokenType::IDENTIFIER;
    }

    // ------------------------------------------------------------- numbers
    void tokenize_number(std::vector<Token>& tokens) {
        int start_col = col_;
        std::size_t start = pos_;
        bool is_float = false;

        while (pos_ < input_.size() &&
               std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            advance();
        }

        if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek_at(1)))) {
            is_float = true;
            advance(); // '.'
            while (pos_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                advance();
            }
        }

        if (peek() == 'e' || peek() == 'E') {
            is_float = true;
            advance(); // 'e' / 'E'
            if (peek() == '+' || peek() == '-') advance();
            while (pos_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                advance();
            }
        }

        std::string text = input_.substr(start, pos_ - start);
        push_token(tokens,{
            is_float ? TokenType::FLOAT_LITERAL : TokenType::INT_LITERAL,
            text, line_, start_col
        });
    }

    // -------------------------------------------------------------- string
    void tokenize_string(std::vector<Token>& tokens) {
        int start_line = line_;
        int start_col = col_;
        std::size_t start = pos_;
        advance(); // opening "

        // Triple-quoted string: """...""". This is useful for future docstrings
        // and multi-line AI prompt literals.
        if (peek() == '"' && peek_at(1) == '"') {
            advance(); advance(); // consume the next two "
            bool closed = false;
            while (pos_ < input_.size()) {
                if (peek() == '"' && peek_at(1) == '"' && peek_at(2) == '"') {
                    advance(); advance(); advance();
                    closed = true;
                    break;
                }
                if (peek() == '\n') {
                    line_++;
                    col_ = 1;
                    advance();
                } else {
                    advance();
                }
            }
            if (!closed) {
                throw std::runtime_error(
                    "Line " + std::to_string(start_line) +
                    ", Col " + std::to_string(start_col) +
                    ": Unterminated triple-quoted string literal");
            }
            std::string text = input_.substr(start, pos_ - start);
            push_token(tokens,{TokenType::STRING_LITERAL, text, start_line, start_col});
            return;
        }

        // Single-line string with escape handling. We keep the raw literal in
        // the token and cook it in the parser, so later diagnostics can still
        // report the exact source spelling.
        while (pos_ < input_.size() && peek() != '"' && peek() != '\n') {
            if (peek() == '\\') {
                advance();
                if (pos_ >= input_.size() || peek() == '\n') {
                    throw std::runtime_error(
                        "Line " + std::to_string(start_line) +
                        ", Col " + std::to_string(start_col) +
                        ": Unterminated escape sequence in string literal");
                }
                advance();
            } else {
                advance();
            }
        }
        if (peek() != '"') {
            throw std::runtime_error(
                "Line " + std::to_string(start_line) +
                ", Col " + std::to_string(start_col) +
                ": Unterminated string literal");
        }
        advance(); // closing quote

        std::string literal = input_.substr(start, pos_ - start);
        push_token(tokens,{TokenType::STRING_LITERAL, literal, start_line, start_col});
    }

    // --------------------------------------------------------------- char
    void tokenize_char(std::vector<Token>& tokens) {
        int start_line = line_;
        int start_col = col_;
        std::size_t start = pos_;
        advance(); // opening '

        if (pos_ >= input_.size() || peek() == '\n' || peek() == '\'') {
            throw std::runtime_error(
                "Line " + std::to_string(start_line) +
                ", Col " + std::to_string(start_col) +
                ": Empty or unterminated char literal");
        }

        if (peek() == '\\') {
            advance(); // consume backslash
            if (pos_ >= input_.size() || peek() == '\n') {
                throw std::runtime_error(
                    "Line " + std::to_string(start_line) +
                    ", Col " + std::to_string(start_col) +
                    ": Unterminated escape sequence in char literal");
            }
            advance(); // escaped payload
        } else {
            advance(); // one raw byte for now; UTF-8 char literals can land later
        }

        if (peek() != '\'') {
            throw std::runtime_error(
                "Line " + std::to_string(start_line) +
                ", Col " + std::to_string(start_col) +
                ": Char literal must contain exactly one character");
        }
        advance(); // closing '

        std::string text = input_.substr(start, pos_ - start);
        push_token(tokens,{TokenType::CHAR_LITERAL, text, start_line, start_col});
    }

    // ---------------------------------------------------- single-char kind
    // Emit a token and update `last_was_real_` so the EOF logic can decide
    // whether to inject a trailing NEWLINE.
    void push_token(std::vector<Token>& tokens, Token t) {
        tokens.push_back(t);
        last_was_real_ = (t.type != TokenType::NEWLINE &&
                          t.type != TokenType::INDENT &&
                          t.type != TokenType::DEDENT &&
                          t.type != TokenType::EOF_TOKEN);
    }

    static TokenType single_char_type(char c) {
        switch (c) {
            case '+': return TokenType::PLUS;
            case '-': return TokenType::MINUS;
            case '*': return TokenType::STAR;
            case '/': return TokenType::SLASH;
            case '%': return TokenType::PERCENT;
            case '(': return TokenType::LPAREN;
            case ')': return TokenType::RPAREN;
            case '{': return TokenType::LBRACE;
            case '}': return TokenType::RBRACE;
            case '[': return TokenType::LBRACKET;
            case ']': return TokenType::RBRACKET;
            case ',': return TokenType::COMMA;
            case ';': return TokenType::SEMICOLON;
            case '.': return TokenType::DOT;
            case ':': return TokenType::COLON;
            case '>': return TokenType::GT;
            case '<': return TokenType::LT;
            case '=': return TokenType::ASSIGN; // standalone '=' reused as ASSIGN
            default:  return TokenType::UNKNOWN;
        }
    }
};