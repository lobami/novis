#pragma once

#include <cctype>
#include <sstream>
#include <string>
#include <vector>

// =============================================================================
// Novis diagnostics
// =============================================================================
// Lightweight source manager + diagnostic renderer. The compiler still throws
// exceptions internally for speed of implementation, but the CLI translates
// those exceptions into Rust/Elm-style source snippets whenever line/column are
// available in the error message.

struct SourceLocation {
    int line = 1;
    int col = 1;
};

class SourceFile {
public:
    SourceFile(std::string path, std::string source)
        : path_(std::move(path)), source_(std::move(source)) {
        split_lines();
    }

    const std::string& path() const { return path_; }
    const std::string& source() const { return source_; }

    std::string line_text(int line) const {
        if (line <= 0 || static_cast<std::size_t>(line) > lines_.size()) return "";
        return lines_[static_cast<std::size_t>(line - 1)];
    }

    std::string render_error(const std::string& message) const {
        SourceLocation loc;
        std::string clean_message = message;
        bool has_location = extract_line_col(message, loc, clean_message);
        if (!has_location) {
            has_location = infer_location(clean_message, loc);
        }

        std::ostringstream out;
        out << "error: " << clean_message << '\n';
        if (!has_location) return out.str();

        std::string text = line_text(loc.line);
        out << " --> " << path_ << ':' << loc.line << ':' << loc.col << '\n';
        out << "  |\n";
        out << loc.line << " | " << text << '\n';
        out << "  | ";
        int marker_col = loc.col < 1 ? 1 : loc.col;
        for (int i = 1; i < marker_col; ++i) out << ' ';
        out << '^' << '\n';
        maybe_add_help(clean_message, out);
        return out.str();
    }

private:
    std::string path_;
    std::string source_;
    std::vector<std::string> lines_;

    void split_lines() {
        std::stringstream ss(source_);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines_.push_back(line);
        }
        if (lines_.empty()) lines_.push_back("");
    }

    static bool read_int_after(const std::string& s, std::size_t start, int& value, std::size_t& end) {
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
        if (start >= s.size() || !std::isdigit(static_cast<unsigned char>(s[start]))) return false;
        int result = 0;
        while (start < s.size() && std::isdigit(static_cast<unsigned char>(s[start]))) {
            result = result * 10 + (s[start] - '0');
            ++start;
        }
        value = result;
        end = start;
        return true;
    }

    static bool extract_line_col(const std::string& message, SourceLocation& loc, std::string& clean) {
        std::size_t line_pos = message.find("Line ");
        std::size_t col_pos = message.find(", Col ");
        if (line_pos == std::string::npos || col_pos == std::string::npos || col_pos <= line_pos) return false;

        int line = 0;
        int col = 0;
        std::size_t after_line = 0;
        if (!read_int_after(message, line_pos + 5, line, after_line)) return false;
        std::size_t after_col = 0;
        if (!read_int_after(message, col_pos + 6, col, after_col)) return false;

        std::size_t colon = message.find(':', after_col);
        if (colon != std::string::npos && colon + 1 < message.size()) {
            clean = message.substr(colon + 1);
            while (!clean.empty() && std::isspace(static_cast<unsigned char>(clean.front()))) clean.erase(clean.begin());
        }
        loc.line = line;
        loc.col = col;
        return true;
    }

    bool infer_location(const std::string& message, SourceLocation& loc) const {
        std::string needle;
        auto extract_quoted_after = [&](const std::string& marker) -> std::string {
            std::size_t pos = message.find(marker);
            if (pos == std::string::npos) return "";
            pos += marker.size();
            std::size_t end = message.find('\'', pos);
            if (end == std::string::npos || end <= pos) return "";
            return message.substr(pos, end - pos);
        };

        needle = extract_quoted_after("Cannot initialize '");
        if (needle.empty()) needle = extract_quoted_after("Undefined symbol '");
        if (needle.empty()) needle = extract_quoted_after("Cannot assign undefined variable '");
        if (needle.empty()) needle = extract_quoted_after("Duplicate function '");
        if (needle.empty()) needle = extract_quoted_after("Duplicate type/interface name '");
        if (needle.empty() && message.find("Return type mismatch") != std::string::npos) needle = "return";
        if (needle.empty()) return false;

        for (std::size_t i = 0; i < lines_.size(); ++i) {
            std::size_t found = lines_[i].find(needle);
            if (found != std::string::npos) {
                loc.line = static_cast<int>(i + 1);
                loc.col = static_cast<int>(found + 1);
                return true;
            }
        }
        return false;
    }

    static void maybe_add_help(const std::string& message, std::ostringstream& out) {
        if (message.find("Cannot initialize") != std::string::npos &&
            message.find("money") != std::string::npos &&
            message.find("decimal") != std::string::npos) {
            out << "help: money values need an explicit currency, e.g. money(\"10.00\", \"MXN\")\n";
        } else if (message.find("Expected NEWLINE") != std::string::npos) {
            out << "help: Novis is indentation-sensitive; finish one statement per line or wrap expressions in parentheses.\n";
        } else if (message.find("Indentation") != std::string::npos) {
            out << "help: use spaces only; Novis currently requires 4 spaces per indentation level.\n";
        } else if (message.find("Undefined symbol") != std::string::npos) {
            out << "help: check the name or import/define the symbol before using it.\n";
        }
    }
};
