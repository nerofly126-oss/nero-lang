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
    assert((kinds("") == std::vector<T>{T::End}));

    auto t = Lexer("let x = 42").tokenize();
    assert(t[3].kind == T::Number && t[3].value == 42 && t[3].line == 1 && t[3].col == 9);

    assert(throws("let x = @"));
    assert(throws("a ! b"));
    assert(throws("12abc"));
    assert(throws("99999999999999999999"));

    std::puts("all lexer tests passed");
    return 0;
}
