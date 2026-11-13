#include "../src/checker.hpp"
#include "../src/evaluator.hpp"
#include "../src/parser.hpp"
#include <cassert>
#include <cstdio>
#include <sstream>

static Program parse(const std::string& src) { return Parser(Lexer(src).tokenize()).parse_program(); }

// every problem the checker finds, as "line:col: message"
static std::vector<std::string> chk(const std::string& src) {
    Program p = parse(src);
    std::vector<std::string> out;
    for (const Diagnostic& d : Checker().check(p))
        out.push_back(std::to_string(d.line) + ":" + std::to_string(d.col) + ": " + d.message);
    return out;
}

static bool clean(const std::string& src) { return chk(src).empty(); }

// the message of whatever goes wrong first, while parsing or running ("" when nothing does)
static std::string any_error(const std::string& src) {
    try {
        Program p = Parser(Lexer(src).tokenize()).parse_program();
        std::ostringstream os;
        Evaluator(p, os).run();
    } catch (const std::runtime_error& e) { return e.what(); }
    return "";
}

static bool says(const std::string& src, const std::string& needle) {
    for (const std::string& m : chk(src))
        if (m.find(needle) != std::string::npos) return true;
    return false;
}

// what eval says about the same program ("" when it runs)
static std::string eval_error(const std::string& src) {
    Program p = parse(src);
    std::ostringstream os;
    try { Evaluator(p, os).run(); } catch (const std::runtime_error& e) { return e.what(); }
    return "";
}

static const std::string T = "type P {\n  n: int\n  name: str = \"x\"\n}\n";

int main() {
    // ---- the point of the whole thing: code that nothing calls still gets checked ----
    std::string forgotten = T + "fn used() -> P {\n  return P { n: 1 }\n}\nfn forgotten() -> P {\n  return P { n: \"8080\" }\n}\nemit used()\n";
    assert(eval_error(forgotten) == "");                                   // eval is happy, it never runs forgotten()
    assert(chk(forgotten).size() == 1);
    assert(says(forgotten, "field 'n' of P must be an int, got a str"));

    // ---- silent on correct code ----
    assert(clean(T + "fn mk(n: int) -> P {\n  return P { n: n, name: \"{n}\" }\n}\nemit mk(1)\n"));
    assert(clean("let xs = [1, 2, 3]\nlet ys = [x * 2 for x in xs if x > 1]\nprint len(ys)\n"));
    assert(clean("fn f(a: str, b: [int]) -> record {\n  return {a: a, b: b}\n}\nprint f(\"x\", [1]).a\n"));
    assert(clean("print {a: 1}.whatever\n"));                              // a plain record: nothing is known, so nothing is claimed
    assert(clean("fn f(x: int) -> int {\n  return x\n}\nprint 1 + f(2)\nprint \"a\" + \"b\"\nprint [1] + [2]\n"));
    assert(clean("let a = 1\nif true {\n  let b = 1\n} else {\n  let b = 2\n}\n"));         // the same name in two branches is fine
    assert(clean("for i in [1, 2] {\n  let a = i\n}\nfor i in [3] {\n  print i\n}\n"));       // a let in a loop, a loop variable reused later
    assert(clean("let i = 0\nwhile i < 2 {\n  if i > 0 {\n    print x\n  }\n  let x = i\n  i = i + 1\n}\n"));   // used above its let, valid on the second turn
    assert(clean(T + "fn f(n: int) -> P {\n  while true {\n    return P { n: n }\n  }\n}\nprint 1\n"));
    assert(clean("print if 1 < 2 { 1 } else { \"a\" }\n"));                 // branches of different types: nothing claimed
    assert(says("fn f(x: bool) -> int {\n  return 1\n}\nprint f(if true { 1 } else { \"a\" })\n",
                "argument 1 of 'f' must be a bool"));
    assert(says("fn f(x: bool) -> int {\n  return 1\n}\nlet value = if true { 1 } else { \"a\" }\nprint f(value)\n",
                "argument 1 of 'f' must be a bool"));
    assert(clean("fn f(x: int) -> int {\n  return x\n}\nprint f(if true { 1 } else { \"a\" })\n"));
    assert(clean("fn f(x: int) -> int {\n  return x\n}\nlet value = if true { 1 } else { \"a\" }\nprint f(value)\n"));
    assert(says("fn f(x: bool) -> int {\n  return 1\n}\nlet value = if true { 1 } else { \"a\" }\nvalue = true\nprint f(value)\n",
                "argument 1 of 'f' must be a bool"));
    assert(says("type P {\n  n: bool\n}\nprint P { n: if true { 1 } else { \"a\" } }\n",
                "field 'n' of P must be a bool"));
    assert(says("fn f() -> bool {\n  return if true { 1 } else { \"a\" }\n}\nprint f()\n",
                "this function returns bool"));
    assert(clean("print secret(\"X\")\nprint env(\"A\", \"b\")\n"));
    // a variable that gets assigned again may change what is inside it, so only its kind is trusted
    assert(clean("type S {\n  n: [str]\n}\nlet x = [1]\nx = [\"a\"]\nprint S { n: x }\n"));
    assert(clean("type A {\n  a: int\n}\ntype B {\n  b: int\n}\nlet x = A { a: 1 }\nx = B { b: 2 }\nprint x.b\n"));
    // and without the assignment the same code is wrong
    assert(says("type S {\n  n: [str]\n}\nlet x = [1]\nprint S { n: x }\n", "field 'n' of S must be a list of str, got a list, but element 0 is an int"));
    assert(says("type A {\n  a: int\n}\ntype B {\n  b: int\n}\nlet x = A { a: 1 }\nprint x.b\n", "A has no field 'b'"));

    // ---- schemas ----
    assert(says(T + "print P { n: 1, nam: \"x\" }", "P has no field 'nam' (did you mean 'name'?)"));
    assert(says(T + "print P { name: \"x\" }", "P is missing field 'n' (an int)"));
    assert(says(T + "print P { n: 1, n: 2 }", "duplicate field 'n'"));
    assert(says("type Q {\n  p: P\n}\n" + T + "print Q { p: { n: 1 } }", "must be a P, got a record (write P { ... })"));
    assert(says("type D {\n  n: int = \"x\"\n}\nprint 1", "the default of field 'n' must be an int, got a str"));
    assert(says(T + "print [P { n: 1 }, { n: 2 }][0].n\ntype W {\n  ps: [P]\n}\nprint W { ps: [P { n: 1 }, { n: 2 }] }",
                "a list, but element 1 is a record (write P { ... })"));
    assert(says(T + "let p = P { n: 1 }\nprint p.zzz", "P has no field 'zzz'"));
    assert(says("let x = 1\nprint x.a", "cannot read field 'a' of an int"));

    // ---- functions ----
    assert(says("print nope(1)", "undefined function 'nope'"));
    assert(says("fn f(a: int) -> int {\n  return a\n}\nprint f(1, 2)", "function 'f' takes 1 argument(s), got 2"));
    assert(says("fn f(a: int) -> int {\n  return a\n}\nprint f(\"x\")", "argument 1 of 'f' must be an int, got a str"));
    assert(says("fn f(a) -> int {\n  return a\n}\nprint f(\"x\")", "parameter 'a' has no type written, so it is an int"));
    assert(says("fn f() -> int {\n  return \"x\"\n}", "this function returns int, but the value is a str"));
    assert(says("fn f() {\n  return \"x\"\n}", "no return type is written, so it is an int"));
    assert(says(T + "fn f(c: bool) -> P {\n  if c {\n    return P { n: 1 }\n  }\n}\nprint 1", "function 'f' can end without returning a value of type P"));
    assert(clean(T + "fn f(c: bool) -> P {\n  if c {\n    return P { n: 1 }\n  } else {\n    return P { n: 2 }\n  }\n}\nprint 1"));
    assert(says(T + "fn f() -> P {\n  return\n}", "a bare 'return' is not allowed in a function that returns P"));
    assert(says("return 1", "'return' outside a function"));
    assert(says("fn len() -> int {\n  return 1\n}", "'len' is a builtin and cannot be redefined"));
    assert(says("fn f() -> int {\n  return 1\n}\nfn f() -> int {\n  return 2\n}", "function 'f' is already defined"));
    assert(says("fn f(a: int, a: int) -> int {\n  return a\n}", "duplicate parameter 'a'"));

    // ---- variables ----
    assert(says("print x", "undefined variable 'x'"));
    assert(says("for i in [1] {\n  print i\n}\nprint i", "undefined variable 'i'"));             // a loop variable ends with its loop
    assert(says("let x = 1\nx = \"a\"", "'x' is an int, cannot assign a str"));
    assert(says("let x = 1\nlet x = 2", "'x' is already declared"));
    assert(says("let x = 1\nif true {\n  let x = 2\n}", "'x' is already declared"));
    assert(says("fn f(a: int) -> int {\n  let a = 2\n  return a\n}", "'a' is already declared"));
    assert(says("let x = 1\nfor x in [1] {\n  print x\n}", "'x' is already declared"));

    // ---- operators and builtins ----
    assert(says("if 1 + 1 == 2 and \"a\" {\n  print 1\n}", "both sides of 'and' must be bools, got a str"));
    assert(says("if \"a\" {\n  print 1\n}", "an if condition must be a bool, got a str"));
    assert(says("while [1] {\n  print 1\n}", "a while condition must be a bool, got a list"));
    assert(says("print not 1", "the operand of 'not' must be a bool, got an int"));
    assert(says("print -\"a\"", "the operand of '-' must be an int, got a str"));
    assert(says("print \"a\" + 1", "cannot use '+' on a str and an int (use str(...) to turn a number into text)"));
    assert(says("print 1 - \"a\"", "cannot use '-' on an int and a str"));
    assert(says("print 1 == \"a\"", "cannot compare an int with a str"));
    assert(says("let s = secret(\"X\")\nprint s == s", "secrets cannot be compared"));
    assert(says("print 1 / 0", "division by zero"));
    assert(says("print 1 / -0", "division by zero"));
    assert(says("print 1 % -(0)", "division by zero"));
    assert(says("for x in 5 {\n  print x\n}", "can only loop over a list, got an int"));
    assert(says("print [x for x in \"ab\"]", "can only loop over a list, got a str"));
    assert(says("print len(5)", "'len' needs a str or a list, got an int"));
    assert(says("print str([1])", "'str' needs an int or a bool, got a list"));
    assert(says("print range(\"a\")", "'range' needs an int, got a str"));
    assert(says("print len(1, 2)", "'len' takes 1 argument, got 2"));
    assert(says("print \"{[1]}\"", "cannot put a list of int inside a string"));
    assert(says("print [1, 2][\"a\"]", "a list index must be an int, got a str"));
    assert(says("print {a: 1}[1]", "a record key must be a str, got an int"));
    assert(says("print 5[0]", "cannot index an int"));
    assert(says("print env(1)", "'env' needs the name as a str, got an int"));
    assert(says("print env(\"A\", 1)", "the fallback of 'env' must be a str, got an int"));
    assert(says("print secret()", "'secret' takes 1 argument, got 0"));
    assert(says("print secret(\"\")", "needs a name that is not empty"));

    // ---- it reports everything, in order of position ----
    std::vector<std::string> many = chk("print x\nprint y\nprint nope()\n");
    assert(many.size() == 3);
    assert(many[0].rfind("1:7:", 0) == 0 && many[1].rfind("2:7:", 0) == 0 && many[2].rfind("3:7:", 0) == 0);

    // ---- check and eval say the same thing, in the same place ----
    const char* broken[] = {
        "type P {\n  n: int\n}\nprint P { n: \"x\" }",
        "type P {\n  n: int\n}\nprint P { n: 1, m: 2 }",
        "type P {\n  n: int\n}\nprint P { }",
        "type P {\n  n: int\n}\nprint P { n: 1, n: 2 }",
        "type P {\n  n: int = \"x\"\n}\nprint 1",
        "fn f(a: int) -> int {\n  return a\n}\nprint f(\"x\")",
        "fn f(a: int) -> int {\n  return a\n}\nprint f(1, 2)",
        "fn f() -> int {\n  return \"x\"\n}\nprint f()",
        "print nope(1)",
        "print x",
        "let x = 1\nx = \"a\"",
        "let x = 1\nlet x = 2",
        "if \"a\" {\n  print 1\n}",
        "print \"a\" + 1",
        "print 1 == \"a\"",
        "print not 1",
        "print len(5)",
        "print str([1])",
        "print 1 / 0",
        "print 1 / -0",
        "print 1 % -(0)",
        "for x in 5 {\n  print x\n}",
        "print 5[0]",
        "print [1][\"a\"]",
        "print secret(1)",
        "let s = secret(\"X\")\nprint s == s",
        "type P {\n  n: int\n}\nlet p = P { n: 1 }\nprint p.zzz",
    };
    for (const char* src : broken) {
        std::string want = eval_error(src);
        std::vector<std::string> got = chk(src);
        assert(!want.empty());
        assert(!got.empty());
        if (got[0] != want) std::printf("DISAGREE on %s\n  eval:  %s\n  check: %s\n", src, want.c_str(), got[0].c_str());
        assert(got[0] == want);
    }

    // a function that returns a secret has to return one, as a function that returns a schema does
    assert(any_error("fn f() -> secret {\n}\nprint f()").find("function 'f' ended without returning a value of type secret") != std::string::npos);
    assert(says("fn f() -> secret {\n}", "function 'f' can end without returning a value of type secret"));
    assert(says("fn f() -> secret {\n  return\n}", "a bare 'return' is not allowed in a function that returns secret"));
    assert(clean("fn f() -> secret {\n  return secret(\"A\")\n}\nprint f()"));
    // a decimal number says it is not supported, instead of complaining about a field name
    assert(any_error("print 1.5") == "1:9: there are no decimal numbers in n+, only whole numbers (write \"1.5\" as a string if you need the text)");
    assert(any_error("print 7.25").find("write \"7.25\" as a string") != std::string::npos);
    assert(any_error("let r = {a: 1}\nprint r.a") == "");
    assert(any_error("print [1][0].x").find("cannot read field") != std::string::npos);

    std::puts("all check tests passed");
    return 0;
}
