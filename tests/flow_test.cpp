#include "../src/codegen.hpp"
#include "../src/evaluator.hpp"
#include "../src/parser.hpp"
#include <cassert>
#include <cstdio>
#include <sstream>

static std::string ast(const std::string& src) {
    return dump_program(Parser(Lexer(src).tokenize()).parse_program());
}

static std::string ev(const std::string& src) {
    Program p = Parser(Lexer(src).tokenize()).parse_program();
    std::ostringstream os;
    Evaluator(p, os).run();
    return os.str();
}

static std::string err(const std::string& src) {   // the error message, or "" when nothing failed
    try { ev(src); } catch (const std::runtime_error& e) { return e.what(); }
    return "";
}

static bool has(const std::string& src, const std::string& needle) {
    return err(src).find(needle) != std::string::npos;
}

static bool compile_throws(const std::string& src) {
    try {
        Program p = Parser(Lexer(src).tokenize()).parse_program();
        Grimoire g;
        CodeGen(g).compile(p);
    } catch (const std::runtime_error&) { return true; }
    return false;
}

int main() {
    // lexer: strings with {expressions}
    {
        std::vector<Token> t = Lexer("\"a{b}c\"").tokenize();
        assert(t[0].kind == Tok::Interp);
        assert(t[0].parts.size() == 3);
        assert(!t[0].parts[0].expr && t[0].parts[0].text == "a");
        assert(t[0].parts[1].expr && t[0].parts[1].text == "b" && t[0].parts[1].line == 1 && t[0].parts[1].col == 4);
        assert(!t[0].parts[2].expr && t[0].parts[2].text == "c");
        assert(Lexer("\"abc\"").tokenize()[0].kind == Tok::String);
        assert(Lexer("\"\\{x\\}\"").tokenize()[0].kind == Tok::String);
        assert(Lexer("\"\\{x\\}\"").tokenize()[0].text == "{x}");
        assert(Lexer("for in").tokenize()[0].kind == Tok::For);
        assert(Lexer("for in").tokenize()[1].kind == Tok::In);
    }

    // parser
    assert(ast("print \"web-{env}\"") == "print (interp \"web-\" env)\n");
    assert(ast("print \"{a}{b}\"") == "print (interp a b)\n");
    assert(ast("print \"x{1 + 2}y\"") == "print (interp \"x\" (+ 1 2) \"y\")\n");
    assert(ast("print \"{f(\"q\")}\"") == "print (interp (call f \"q\"))\n");
    assert(ast("let x = if a { 1 } else { 2 }") == "let x := (if a 1 2)\n");
    assert(ast("print if a { 1 } else if b { 2 } else { 3 }") == "print (if a 1 (if b 2 3))\n");
    assert(ast("let n = if a {\n  1\n} else {\n  2\n}") == "let n := (if a 1 2)\n");
    assert(ast("if a { print 1 }") == "if a\n  print 1\n");   // as a statement it is still a statement
    assert(ast("for x in xs { print x }") == "for x in xs\n  print x\n");
    assert(ast("print [x * 2 for x in xs]") == "print (comp (* x 2) x xs)\n");
    assert(ast("print [x for x in xs if x > 1]") == "print (comp x x xs (> x 1))\n");
    assert(ast("print [x, 2]") == "print (list x 2)\n");
    assert(has("print if a { 1 }", "needs an 'else'"));
    assert(has("for x xs { }", "'in' after the loop variable"));
    assert(has("for in xs { }", "a loop variable after 'for'"));
    assert(has("print [x for x in]", "expected a value"));
    assert(has("print \"{}\"", "empty {} in a string"));
    assert(has("print \"{a\"", "unterminated"));
    assert(has("print \"{1 +}\"", "expected a value"));
    assert(has("print \"{a b}\"", "unexpected 'b' inside {} in a string"));
    assert(err("print \"{1 +}\"").rfind("1:12:", 0) == 0);   // errors inside {} point at the real position

    // interpolation
    assert(ev("let env = \"dev\"\nprint \"web-{env}\"") == "web-dev\n");
    assert(ev("print \"n={1 + 2} ok={1 < 2}\"") == "n=3 ok=true\n");
    assert(ev("fn up(s: str) -> str { return s + \"!\" }\nprint \"{up(\"hi\")}\"") == "hi!\n");
    assert(ev("print \"\\{not interpolated\\}\"") == "{not interpolated}\n");
    assert(ev("let r = {a: \"x\"}\nprint \"v={r.a}\"") == "v=x\n");
    assert(has("print \"{[1]}\"", "cannot put a list inside a string"));
    assert(err("let x = [1]\nprint \"ab{x}\"").rfind("2:11:", 0) == 0);
    assert(err("print \"{nope}\"").rfind("1:9:", 0) == 0);
    assert(has("print \"{nope}\"", "undefined variable 'nope'"));

    // if as a value
    assert(ev("print if 1 < 2 { \"a\" } else { \"b\" }") == "a\n");
    assert(ev("print if 1 > 2 { 1 } else if 2 > 3 { 2 } else { 3 }") == "3\n");
    assert(ev("print if true { 1 } else { 1 / 0 }") == "1\n");      // only the chosen branch runs
    assert(ev("emit {r: if 1 == 1 { 5 } else { 1 }}") == "{\n  \"r\": 5\n}\n");
    assert(ev("fn pick(env: str) -> int { return if env == \"prod\" { 5 } else { 1 } }\nprint pick(\"prod\")\nprint pick(\"dev\")") == "5\n1\n");
    assert(ev("let n = if 1 > 2 {\n  10\n} else {\n  20\n}\nprint n") == "20\n");
    assert(has("print if \"x\" { 1 } else { 2 }", "an if condition must be a bool"));

    // for loops
    assert(ev("for x in [1, 2, 3] { print x * x }") == "1\n4\n9\n");
    assert(ev("let s = 0\nfor x in [1, 2, 3] {\n  s = s + x\n}\nprint s") == "6\n");
    assert(ev("for i in range(3) { print i }") == "0\n1\n2\n");
    assert(ev("print range(4)") == "[0, 1, 2, 3]\n");
    assert(ev("print range(0)") == "[]\n");
    assert(ev("for x in [1] { print x }\nfor x in [2] { print x }") == "1\n2\n");   // a loop variable is gone after its loop
    assert(ev("fn first_big(xs: list) -> int {\n  for x in xs {\n    if x > 2 { return x }\n  }\n  return -1\n}\nprint first_big([1, 5, 3])\nprint first_big([1])") == "5\n-1\n");
    assert(ev("type S { name: str, n: int }\nfor e in [\"a\", \"b\"] { print S { name: \"x-{e}\", n: 1 } }") ==
           "{\"name\": \"x-a\", \"n\": 1}\n{\"name\": \"x-b\", \"n\": 1}\n");
    assert(has("for x in [1] { }\nprint x", "undefined variable 'x'"));
    assert(has("for x in [1] { for x in [2] { } }", "'x' is already declared"));
    assert(has("let x = 1\nfor x in [1] { }", "'x' is already declared"));
    assert(has("for x in [1] { let x = 2 }", "'x' is already declared"));
    assert(has("for x in 5 { }", "can only loop over a list, got an int"));
    assert(has("print range(-1)", "not negative"));
    assert(has("print range(\"a\")", "'range' needs an int, got a str"));
    assert(has("print range(2000000)", "limited to 1000000"));
    assert(has("fn range(a) { }", "builtin"));

    // list comprehensions
    assert(ev("print [x * 2 for x in [1, 2, 3]]") == "[2, 4, 6]\n");
    assert(ev("print [x for x in range(6) if x % 2 == 0]") == "[0, 2, 4]\n");
    assert(ev("print [\"a{i}\" for i in range(2)]") == "[\"a0\", \"a1\"]\n");
    assert(ev("print [[y for y in [1, 2]] for x in [1, 2]]") == "[[1, 2], [1, 2]]\n");
    assert(ev("type P { n: int }\nprint len([P { n: p } for p in [80, 443]])") == "2\n");
    assert(ev("fn double(xs: list) -> list { return [x * 2 for x in xs] }\nprint double([1, 2])") == "[2, 4]\n");
    assert(has("print [x for x in [1]]\nprint x", "undefined variable 'x'"));
    assert(has("let x = 1\nprint [x for x in [1]]", "'x' is already declared"));
    assert(has("print [x for x in 5]", "can only loop over a list, got an int"));
    assert(has("print [x for x in [1] if 5 + \"a\" == 1]", "cannot use '+'"));

    // unannotated parameters and returns are int, in eval just like in run
    assert(has("fn f(x) { return x }\nprint f(\"a\")",
               "argument 1 of 'f' must be an int, got a str (parameter 'x' has no type written, so it is an int)"));
    assert(has("fn f() { return \"a\" }\nprint f()",
               "this function returns int, but the value is a str (no return type is written, so it is an int)"));
    assert(has("fn f(x: str) { }\nprint f(1)", "must be a str, got an int"));
    assert(!has("fn f(x: str) { }\nprint f(1)", "no type written"));
    assert(ev("fn f(x, y) { return x + y }\nprint f(1, 2)") == "3\n");

    // the compiled subset has none of this
    assert(compile_throws("let x = if 1 { 2 } else { 3 }"));
    assert(compile_throws("print \"a{1}\""));
    assert(compile_throws("for x in [1] { }"));
    assert(compile_throws("print [x for x in y]"));
    assert(compile_throws("fn range(a) { }"));
    assert(!compile_throws("print \"\\{ok\\}\""));

    std::puts("all flow tests passed");
    return 0;
}
