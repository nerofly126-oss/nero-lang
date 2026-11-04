#include "../src/evaluator.hpp"
#include "../src/parser.hpp"
#include <cassert>
#include <cstdio>
#include <sstream>

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

int main() {
    // scalars
    assert(ev("print 2 + 3 * 4") == "14\n");
    assert(ev("print 10 / 3") == "3\n");
    assert(ev("print -7 % 3") == "-1\n");
    assert(ev("print \"a\" + \"b\"") == "ab\n");
    assert(ev("print true") == "true\n");
    assert(ev("print 1 < 2") == "true\n");
    assert(ev("print \"n=\" + str(5)") == "n=5\n");
    assert(ev("print str(true)") == "true\n");
    assert(ev("print len(\"hello\")") == "5\n");

    // lists and records print as compact JSON
    assert(ev("print [1, 2, 3]") == "[1, 2, 3]\n");
    assert(ev("print {a: 1, b: \"x\"}") == "{\"a\": 1, \"b\": \"x\"}\n");
    assert(ev("print []") == "[]\n");
    assert(ev("print {}") == "{}\n");
    assert(ev("print [1] + [2, 3]") == "[1, 2, 3]\n");
    assert(ev("print {\n  a: 1,\n  b: [\n    2,\n    3,\n  ],\n}\n") == "{\"a\": 1, \"b\": [2, 3]}\n");

    // emit prints pretty JSON
    assert(ev("emit {name: \"web\", ports: [80, 443], meta: {on: true, tags: []}}") ==
           "{\n  \"name\": \"web\",\n  \"ports\": [\n    80,\n    443\n  ],\n  \"meta\": {\n    \"on\": true,\n    \"tags\": []\n  }\n}\n");
    assert(ev("emit 1\nemit \"x\"") == "1\n\"x\"\n");
    assert(ev("emit \"a\\\"b\\nc\"") == "\"a\\\"b\\nc\"\n");
    assert(ev("emit {\"app.io/name\": 1}") == "{\n  \"app.io/name\": 1\n}\n");

    // logic
    assert(ev("print true and false") == "false\n");
    assert(ev("print true or false") == "true\n");
    assert(ev("print not false") == "true\n");
    assert(ev("print not 1 == 2") == "true\n");
    assert(ev("print false and 1 / 0 == 0") == "false\n");   // short circuit
    assert(ev("print true or 1 / 0 == 0") == "true\n");
    assert(ev("print 1 == 1 and 2 < 3 or false") == "true\n");

    // equality is deep, and field order does not matter
    assert(ev("print [1, 2] == [1, 2]") == "true\n");
    assert(ev("print [1] == [2]") == "false\n");
    assert(ev("print {a: 1, b: 2} == {b: 2, a: 1}") == "true\n");
    assert(ev("print {a: 1} != {a: 2}") == "true\n");
    assert(ev("print \"a\" != \"b\"") == "true\n");

    // access
    assert(ev("let s = {name: \"web\", ports: [80, 443], meta: {tier: 2}}\n"
              "print s.name\nprint s.ports[1]\nprint s.meta.tier\nprint len(s.ports)") == "web\n443\n2\n2\n");
    assert(ev("let r = {\"app.io/name\": \"x\"}\nprint r[\"app.io/name\"]") == "x\n");
    assert(ev("fn mk() -> record { return {a: [1, 2]} }\nprint mk().a[0]") == "1\n");

    // statements
    assert(ev("let xs = [3, 4, 5]\nlet i = 0\nlet sum = 0\nwhile i < len(xs) {\n  sum = sum + xs[i]\n  i = i + 1\n}\nprint sum") == "12\n");
    assert(ev("if 1 == 2 { print 1 } else if true { print 2 } else { print 3 }") == "2\n");
    assert(ev("if 5 { print \"int conditions still work\" }") == "int conditions still work\n");

    // functions
    assert(ev("fn fib(n) {\n  if n < 2 { return n }\n  return fib(n - 1) + fib(n - 2)\n}\nprint fib(20)") == "6765\n");
    assert(ev("print later(2)\nfn later(x) { return x * 2 }") == "4\n");
    assert(ev("fn f() { }\nprint f()") == "0\n");
    assert(ev("fn g() -> str { }\nprint g()") == "\n");
    assert(ev("fn r() -> record { }\nemit r()") == "{}\n");
    assert(has("fn f(x) { return x }\nprint f(\"a\")", "argument 1 of 'f' must be an int, got a str"));   // unannotated means int
    assert(ev("fn f(r: record) -> list { return [r.a] }\nprint f({a: 1})") == "[1]\n");
    assert(ev("fn find(xs: list, v: int) -> int {\n  let i = 0\n  while i < len(xs) {\n    if xs[i] == v { return i }\n    i = i + 1\n  }\n  return -1\n}\n"
              "print find([5, 6, 7], 7)\nprint find([5, 6, 7], 9)") == "2\n-1\n");

    // errors carry line:col and a useful message
    assert(err("let y = 1\nlet z = y + \"a\"").rfind("2:11:", 0) == 0);
    assert(has("let y = 1\nlet z = y + \"a\"", "use str("));
    assert(has("print x", "undefined variable 'x'"));
    assert(has("print 1 + \"a\"", "cannot use '+' on an int and a str"));
    assert(has("print [1] + 1", "cannot use '+' on a list and an int"));
    assert(has("print 1 / 0", "division by zero"));
    assert(has("print 5 % 0", "division by zero"));
    assert(has("print 9223372036854775807 + 1", "integer overflow"));
    assert(has("print -9223372036854775807 - 2", "integer overflow"));
    assert(has("print 4611686018427387904 * 2", "integer overflow"));
    assert(has("print 1 == \"a\"", "cannot compare an int with a str"));
    assert(has("print \"a\" < \"b\"", "cannot use '<' on a str and a str"));
    assert(has("print not 1", "must be a bool"));
    assert(has("print 1 and true", "must be bools"));
    assert(has("print -true", "must be an int"));
    assert(has("if \"a\" { print 1 }", "an if condition must be a bool"));
    assert(has("while [] { }", "a while condition"));
    assert(has("let a = 1\na = \"x\"", "'a' is an int, cannot assign a str"));
    assert(has("x = 1", "undefined variable 'x'"));
    assert(has("let a = 1\nlet a = 2", "already declared"));
    assert(has("return 1", "'return' outside a function"));
    assert(has("print {a: 1}.b", "record has no field 'b' (fields: a)"));
    assert(has("print {}.b", "record has no field 'b'"));
    assert(has("print [1, 2][5]", "index 5 is out of range for a list of length 2"));
    assert(has("print [1][-1]", "out of range"));
    assert(has("print [1][\"a\"]", "a list index must be an int"));
    assert(has("print {a: 1}[0]", "a record key must be a str"));
    assert(has("print 5[0]", "cannot index an int"));
    assert(has("print 5.x", "cannot read field 'x' of an int"));
    assert(has("print {a: 1, a: 2}", "duplicate field 'a'"));
    assert(has("fn f(a: str) { }\nprint f(1)", "argument 1 of 'f' must be a str, got an int"));
    assert(has("fn f() -> str { return 1 }\nprint f()", "this function returns str, but the value is an int"));
    assert(has("fn f(a) { }\nprint f()", "takes 1 argument(s), got 0"));
    assert(has("print nope(1)", "undefined function 'nope'"));
    assert(has("print len(1)", "'len' needs a str or a list"));
    assert(has("print len()", "takes 1 argument"));
    assert(has("fn len(x) { }", "builtin"));
    assert(has("fn f() { }\nfn f() { }", "already defined"));
    assert(has("fn f(a, a) { }", "duplicate parameter"));
    assert(has("let x = 1\nfn f() { return x }\nprint f()", "undefined variable 'x'"));   // no globals
    assert(has("fn f(n) { return f(n + 1) }\nprint f(0)", "recursion too deep"));
    assert(has("emit {a: 1 / 0}", "division by zero"));

    std::puts("all eval tests passed");
    return 0;
}
