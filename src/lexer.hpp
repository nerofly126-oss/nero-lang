#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

enum class Tok {
    Number, String, Ident,
    Let, While, If, Else, Print, Fn, Return,
    True, False, Emit, And, Or, Not, Type, For, In, Interp, Import,
    Plus, Minus, Star, Slash, Percent,
    LParen, RParen, LBrace, RBrace, Comma, Colon, Arrow,
    LBracket, RBracket, Dot,
    Assign, Eq, Ne, Lt, Le, Gt, Ge,
    Newline, End
};

inline const char* tok_name(Tok t) {
    switch (t) {
        case Tok::Number:  return "Number";
        case Tok::String:  return "String";
        case Tok::Ident:   return "Ident";
        case Tok::Let:     return "Let";
        case Tok::While:   return "While";
        case Tok::If:      return "If";
        case Tok::Else:    return "Else";
        case Tok::Print:   return "Print";
        case Tok::Fn:      return "Fn";
        case Tok::Return:  return "Return";
        case Tok::True:    return "True";
        case Tok::False:   return "False";
        case Tok::Emit:    return "Emit";
        case Tok::And:     return "And";
        case Tok::Or:      return "Or";
        case Tok::Not:     return "Not";
        case Tok::Type:    return "Type";
        case Tok::For:     return "For";
        case Tok::In:      return "In";
        case Tok::Interp:  return "Interp";
        case Tok::Import:  return "Import";
        case Tok::Plus:    return "Plus";
        case Tok::Minus:   return "Minus";
        case Tok::Star:    return "Star";
        case Tok::Slash:   return "Slash";
        case Tok::Percent: return "Percent";
        case Tok::LParen:  return "LParen";
        case Tok::RParen:  return "RParen";
        case Tok::LBrace:  return "LBrace";
        case Tok::RBrace:  return "RBrace";
        case Tok::LBracket: return "LBracket";
        case Tok::RBracket: return "RBracket";
        case Tok::Dot:     return "Dot";
        case Tok::Comma:   return "Comma";
        case Tok::Colon:   return "Colon";
        case Tok::Arrow:   return "Arrow";
        case Tok::Assign:  return "Assign";
        case Tok::Eq:      return "Eq";
        case Tok::Ne:      return "Ne";
        case Tok::Lt:      return "Lt";
        case Tok::Le:      return "Le";
        case Tok::Gt:      return "Gt";
        case Tok::Ge:      return "Ge";
        case Tok::Newline: return "Newline";
        case Tok::End:     return "End";
    }
    return "?";
}

// One piece of a string with {expressions} inside: literal text, or the source text of an expression.
struct StrPart {
    bool expr = false;
    std::string text;
    int line = 0, col = 0;     // where an expression's source starts, so errors inside it point at the right place
};

struct Token {
    Tok kind;
    std::string text;   // for String tokens this is the decoded content, escapes already resolved
    int64_t value = 0;
    int line = 0, col = 0;
    std::vector<StrPart> parts;   // Interp tokens only

    Token(Tok k = Tok::End, std::string t = "", int64_t v = 0, int l = 0, int c = 0)
        : kind(k), text(std::move(t)), value(v), line(l), col(c) {}
};

class Lexer {
public:
    explicit Lexer(std::string src, int line = 1, int col = 1) : src_(std::move(src)), line_(line), col_(col) {}

    std::vector<Token> tokenize() {
        std::vector<Token> out;
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            int l = line_, cl = col_;

            if (c == ' ' || c == '\t' || c == '\r') { advance(); continue; }
            if (c == '#') {
                while (pos_ < src_.size() && src_[pos_] != '\n') advance();
                continue;
            }
            if (c == '\n') {
                if (!out.empty() && out.back().kind != Tok::Newline)
                    out.push_back({Tok::Newline, "\\n", 0, l, cl});
                advance();
                continue;
            }

            if (c == '"') {
                std::vector<StrPart> parts;
                std::string text = read_string(l, cl, parts);
                Token t(parts.empty() ? Tok::String : Tok::Interp, parts.empty() ? text : "a string with {...}", 0, l, cl);
                t.parts = std::move(parts);
                out.push_back(std::move(t));
                continue;
            }

            if (is_digit(c)) {
                size_t start = pos_;
                int64_t v = 0;
                while (pos_ < src_.size() && is_digit(src_[pos_])) {
                    int d = src_[pos_] - '0';
                    if (v > (INT64_MAX - d) / 10) fail(l, cl, "number too large");
                    v = v * 10 + d;
                    advance();
                }
                if (pos_ < src_.size() && is_alpha(src_[pos_])) fail(l, cl, "bad number");
                out.push_back({Tok::Number, src_.substr(start, pos_ - start), v, l, cl});
                continue;
            }

            if (is_alpha(c)) {
                static const std::unordered_map<std::string, Tok> keywords = {
                    {"let", Tok::Let}, {"while", Tok::While}, {"if", Tok::If},
                    {"else", Tok::Else}, {"print", Tok::Print},
                    {"fn", Tok::Fn}, {"return", Tok::Return},
                    {"true", Tok::True}, {"false", Tok::False}, {"emit", Tok::Emit},
                    {"and", Tok::And}, {"or", Tok::Or}, {"not", Tok::Not},
                    {"type", Tok::Type}, {"for", Tok::For}, {"in", Tok::In},
                    {"import", Tok::Import},
                };
                size_t start = pos_;
                while (pos_ < src_.size() && (is_alpha(src_[pos_]) || is_digit(src_[pos_]))) advance();
                std::string word = src_.substr(start, pos_ - start);
                auto it = keywords.find(word);
                out.push_back({it == keywords.end() ? Tok::Ident : it->second, word, 0, l, cl});
                continue;
            }

            Tok kind;
            std::string text(1, c);
            switch (c) {
                case '+': kind = Tok::Plus; break;
                case '-':
                    if (peek(1) == '>') { kind = Tok::Arrow; text = "->"; }
                    else kind = Tok::Minus;
                    break;
                case '*': kind = Tok::Star; break;
                case '/': kind = Tok::Slash; break;
                case '%': kind = Tok::Percent; break;
                case '(': kind = Tok::LParen; break;
                case ')': kind = Tok::RParen; break;
                case '{': kind = Tok::LBrace; break;
                case '}': kind = Tok::RBrace; break;
                case ',': kind = Tok::Comma; break;
                case '[': kind = Tok::LBracket; break;
                case ']': kind = Tok::RBracket; break;
                case '.': kind = Tok::Dot; break;
                case ':': kind = Tok::Colon; break;
                case '=':
                    if (peek(1) == '=') { kind = Tok::Eq; text = "=="; }
                    else kind = Tok::Assign;
                    break;
                case '!':
                    if (peek(1) == '=') { kind = Tok::Ne; text = "!="; }
                    else fail(l, cl, "unexpected '!', did you mean '!='?");
                    break;
                case '<':
                    if (peek(1) == '=') { kind = Tok::Le; text = "<="; }
                    else kind = Tok::Lt;
                    break;
                case '>':
                    if (peek(1) == '=') { kind = Tok::Ge; text = ">="; }
                    else kind = Tok::Gt;
                    break;
                default:
                    fail(l, cl, std::string("unexpected character '") + c + "'");
            }
            for (size_t i = 0; i < text.size(); i++) advance();
            out.push_back({kind, text, 0, l, cl});
        }

        if (!out.empty() && out.back().kind != Tok::Newline)
            out.push_back({Tok::Newline, "\\n", 0, line_, col_});
        out.push_back({Tok::End, "", 0, line_, col_});
        return out;
    }

private:
    std::string src_;
    size_t pos_ = 0;
    int line_ = 1, col_ = 1;

    static bool is_digit(char c) { return c >= '0' && c <= '9'; }
    static bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
    char peek(size_t n) const { return pos_ + n < src_.size() ? src_[pos_ + n] : '\0'; }

    void advance() {
        if (src_[pos_] == '\n') { line_++; col_ = 1; }
        else col_++;
        pos_++;
    }

    // Reads a string literal starting at the opening quote, resolving \n \t \" \\ \{ \} escapes.
    // A {expression} inside the quotes becomes a part, and the caller gets an Interp token.
    std::string read_string(int l, int cl, std::vector<StrPart>& parts) {
        advance();  // opening quote
        std::string s;
        while (true) {
            if (pos_ >= src_.size() || src_[pos_] == '\n') fail(l, cl, "unterminated string");
            char d = src_[pos_];
            if (d == '"') { advance(); break; }
            if (d == '{') {
                advance();  // '{'
                int el = line_, ec = col_;
                size_t start = pos_;
                int depth = 1;
                while (true) {
                    if (pos_ >= src_.size() || src_[pos_] == '\n') fail(l, cl, "unterminated '{' in a string (write \\{ for a literal brace)");
                    char x = src_[pos_];
                    if (x == '"') {   // a string inside the expression: skip over it
                        advance();
                        while (pos_ < src_.size() && src_[pos_] != '"' && src_[pos_] != '\n') {
                            if (src_[pos_] == '\\') advance();
                            if (pos_ < src_.size() && src_[pos_] != '\n') advance();
                        }
                        if (pos_ >= src_.size() || src_[pos_] != '"') fail(l, cl, "unterminated '{' in a string (write \\{ for a literal brace)");
                    } else if (x == '{') {
                        depth++;
                    } else if (x == '}' && --depth == 0) {
                        break;
                    }
                    advance();
                }
                std::string code = src_.substr(start, pos_ - start);
                advance();  // '}'
                if (code.find_first_not_of(" \t\r") == std::string::npos)
                    fail(el, ec - 1, "empty {} in a string (write \\{\\} for literal braces)");
                if (!s.empty()) { parts.push_back({false, s, 0, 0}); s.clear(); }
                parts.push_back({true, code, el, ec});
                continue;
            }
            if (d == '\\') {
                advance();
                if (pos_ >= src_.size() || src_[pos_] == '\n') fail(l, cl, "unterminated string");
                char e = src_[pos_];
                switch (e) {
                    case 'n':  s += '\n'; break;
                    case 't':  s += '\t'; break;
                    case '"':  s += '"'; break;
                    case '\\': s += '\\'; break;
                    case '{':  s += '{'; break;
                    case '}':  s += '}'; break;
                    default:   fail(line_, col_, std::string("unknown escape '\\") + e + "'");
                }
                advance();
            } else {
                s += d;
                advance();
            }
        }
        if (!parts.empty() && !s.empty()) parts.push_back({false, s, 0, 0});
        return s;
    }

    [[noreturn]] static void fail(int line, int col, const std::string& msg) {
        throw std::runtime_error(std::to_string(line) + ":" + std::to_string(col) + ": " + msg);
    }
};
