#include "../src/lexer.hpp"
#include <cassert>
#include <cstdio>

using T = Tok;

static std::vector<Tok> kinds(const std::string& s) {
    std::vector<Tok> k;
    for (const Token& t : Lexer(s).tokenize()) k.push_back(t.kind);
    return k;
}

static bool throws(const std::string& s) {
    try { Lexer(s).tokenize(); } catch (const std::runtime_error&) { return true; }
    return false;
}

int main() {
    assert((kinds("print 2 + 3 * 4") ==
            std::vector<T>{T::Print, T::Number, T::Plus, T::Number, T::Star, T::Number, T::Newline, T::End}));
    assert((kinds("1\n\n\n2") ==
            std::vector<T>{T::Number, T::Newline, T::Number, T::Newline, T::End}));
    assert((kinds("x # a comment\ny") ==
            std::vector<T>{T::Ident, T::Newline, T::Ident, T::Newline, T::End}));
    assert((kinds("== != <= >= < > =") ==
            std::vector<T>{T::Eq, T::Ne, T::Le, T::Ge, T::Lt, T::Gt, T::Assign, T::Newline, T::End}));
    assert((kinds("let while if else print foo") ==
            std::vector<T>{T::Let, T::While, T::If, T::Else, T::Print, T::Ident, T::Newline, T::End}));
    assert((kinds("fn return f , g") ==
            std::vector<T>{T::Fn, T::Return, T::Ident, T::Comma, T::Ident, T::Newline, T::End}));
    assert((kinds("fn f(a: str) -> int") ==
            std::vector<T>{T::Fn, T::Ident, T::LParen, T::Ident, T::Colon, T::Ident, T::RParen, T::Arrow, T::Ident,
                           T::Newline, T::End}));
    assert((kinds("n-1") == std::vector<T>{T::Ident, T::Minus, T::Number, T::Newline, T::End}));
    assert((kinds("") == std::vector<T>{T::End}));

    // strings
    assert((kinds("print \"hi\"") == std::vector<T>{T::Print, T::String, T::Newline, T::End}));
    assert(Lexer("\"hello world\"").tokenize()[0].text == "hello world");
    assert(Lexer("\"\"").tokenize()[0].text == "");
    assert(Lexer("\"a\\nb\"").tokenize()[0].text == "a\nb");
    assert(Lexer("\"a\\tb\"").tokenize()[0].text == "a\tb");
    assert(Lexer("\"say \\\"hi\\\"\"").tokenize()[0].text == "say \"hi\"");
    assert(Lexer("\"back\\\\slash\"").tokenize()[0].text == "back\\slash");
    assert(Lexer("\"# not a comment\"").tokenize()[0].text == "# not a comment");
    assert((kinds("\"a\" \"b\"") == std::vector<T>{T::String, T::String, T::Newline, T::End}));

    auto t = Lexer("let x = 42").tokenize();
    assert(t[3].kind == T::Number && t[3].value == 42 && t[3].line == 1 && t[3].col == 9);
    auto s = Lexer("print \"ab\" + 1").tokenize();
    assert(s[1].kind == T::String && s[1].col == 7 && s[2].kind == T::Plus && s[2].col == 12);

    assert(throws("let x = @"));
    assert(throws("a ! b"));
    assert(throws("12abc"));
    assert(throws("99999999999999999999"));
    assert(throws("\"abc"));            // unterminated
    assert(throws("\"ab\ncd\""));       // newline inside a string
    assert(throws("\"a\\qb\""));        // unknown escape
    assert(throws("\"abc\\"));          // ends after a backslash

    std::puts("all lexer tests passed");
    return 0;
}
