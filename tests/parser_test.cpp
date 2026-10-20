#include "../src/parser.hpp"
#include <cassert>
#include <cstdio>

static std::string ast(const std::string& src) {
    return dump_program(Parser(Lexer(src).tokenize()).parse_program());
}

static bool throws(const std::string& src) {
    try { ast(src); } catch (const std::runtime_error&) { return true; }
    return false;
}

int main() {
    // precedence and associativity
    assert(ast("print 2 + 3 * 4") == "print (+ 2 (* 3 4))\n");
    assert(ast("print (2 + 3) * 4") == "print (* (+ 2 3) 4)\n");
    assert(ast("print 10 - 4 - 3") == "print (- (- 10 4) 3)\n");
    assert(ast("print -2 * 3") == "print (* (- 2) 3)\n");
    assert(ast("print - -2") == "print (- (- 2))\n");
    assert(ast("print 1 + 2 < 4") == "print (< (+ 1 2) 4)\n");

    // statements
    assert(ast("let x = 5\nx = x + 1") == "let x := 5\nassign x := (+ x 1)\n");
    assert(ast("while i < 3 {\n    i = i + 1\n}") == "while (< i 3)\n  assign i := (+ i 1)\n");
    assert(ast("if x == 1 { print 1 } else { print 2 }") ==
           "if (== x 1)\n  print 1\nelse\n  print 2\n");
    assert(ast("if a { print 1 } else if b { print 2 } else { print 3 }") ==
           "if a\n  print 1\nelse\n  if b\n    print 2\n  else\n    print 3\n");

    // blank lines, comments, empty program
    assert(ast("\n\n# hi\nprint 1\n\n\nprint 2\n") == "print 1\nprint 2\n");
    assert(ast("") == "");

    // errors
    assert(throws("let = 5"));
    assert(throws("print"));
    assert(throws("print 2 +"));
    assert(throws("while x { print 1"));
    assert(throws("x 5"));
    assert(throws("print 1 2"));
    assert(throws("else"));
    assert(throws("print (1 + 2"));
    assert(throws("print 1 +* 2"));
    assert(throws("}"));
    assert(throws("if x { print 1 }\nelse { print 2 }"));

    std::puts("all parser tests passed");
    return 0;
}
