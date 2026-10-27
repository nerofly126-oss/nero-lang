#include "../src/codegen.hpp"
#include "../src/parser.hpp"
#include <cassert>
#include <cstdio>

using V = std::vector<int64_t>;
using S = std::vector<std::string>;

static NeroFn build(const std::string& src, Grimoire& g) {
    Program p = Parser(Lexer(src).tokenize()).parse_program();
    return CodeGen(g).compile(p);
}

static V run(const std::string& src) {          // ints printed by the program
    Grimoire g;
    NeroFn fn = build(src, g);
    V out;
    nero_sink = &out;
    fn();
    nero_sink = nullptr;
    return out;
}

static S text(const std::string& src) {         // everything printed, as text
    Grimoire g;
    NeroFn fn = build(src, g);
    S out;
    nero_text = &out;
    fn();
    nero_text = nullptr;
    return out;
}

static bool throws(const std::string& src) {
    try { Grimoire g; build(src, g); } catch (const std::runtime_error&) { return true; }
    return false;
}

int main() {
    // arithmetic
    assert(run("print 2 + 3 * 4") == V{14});
    assert(run("print (2 + 3) * 4") == V{20});
    assert(run("print 10 - 4 - 3") == V{3});
    assert(run("print -5 + 2") == V{-3});
    assert(run("print 7 / 2") == V{3});
    assert(run("print -7 / 2") == V{-3});
    assert(run("print 7 % 3") == V{1});
    assert(run("print -7 % 3") == V{-1});
    assert(run("print 1 + (2 + (3 + (4 + 5)))") == V{15});
    assert(run("print 2 * (3 + 4) * (5 - 1)") == V{56});
    assert(run("let a = 5\nprint -a") == V{-5});

    // comparisons
    assert(run("print 1 < 2\nprint 2 < 1\nprint 3 == 3\nprint 3 != 3\nprint 3 >= 3\nprint 2 > 3\nprint 2 <= 2")
           == (V{1, 0, 1, 0, 1, 0, 1}));

    // control flow
    assert(run("let total = 0\nlet i = 1\nwhile i <= 10 {\n total = total + i\n i = i + 1\n}\nprint total") == V{55});
    assert(run("let x = 5\nif x > 3 { print 1 } else { print 2 }\nif x > 9 { print 3 } else { print 4 }") == (V{1, 4}));
    assert(run("let x = 2\nif x == 1 { print 10 } else if x == 2 { print 20 } else { print 30 }") == V{20});
    assert(run("let i = 1\nlet c = 0\nwhile i <= 100 {\n if i % 3 == 0 { c = c + 1 } else if i % 5 == 0 { c = c + 1 }\n i = i + 1\n}\nprint c") == V{47});

    // bigger programs
    assert(run("let n = 20\nlet f = 1\nwhile n > 1 {\n f = f * n\n n = n - 1\n}\nprint f") == V{2432902008176640000LL});
    assert(run("let n = 27\nlet steps = 0\nwhile n != 1 {\n if n % 2 == 0 {\n n = n / 2\n } else {\n n = 3 * n + 1\n }\n steps = steps + 1\n}\nprint steps") == V{111});

    // scope quirks: one flat scope per function, slots start at zero
    assert(run("if 1 { let y = 7 }\nprint y") == V{7});
    assert(run("if 0 { let y = 7 }\nprint y") == V{0});

    // functions
    assert(run("fn add(a, b) { return a + b }\nprint add(2, 3)") == V{5});
    assert(run("fn sub3(a, b, c) { return a - b - c }\nprint sub3(10, 3, 2)") == V{5});
    assert(run("fn s(a, b, c, d, e, f) { return a * 100000 + b * 10000 + c * 1000 + d * 100 + e * 10 + f }\n"
               "print s(1, 2, 3, 4, 5, 6)") == V{123456});
    assert(run("fn id(x) { return x }\nprint id(1) + id(2) * id(3)") == V{7});
    assert(run("fn id(x) { return x }\nprint id(id(id(9)))") == V{9});
    assert(run("print twice(4)\nfn twice(x) { return x * 2 }") == V{8});                  // call before definition
    assert(run("fn nothing() { }\nprint nothing()") == V{0});                              // falling off the end
    assert(run("fn f() { return }\nprint f()") == V{0});                                   // bare return
    assert(run("fn f() { print 7 }\nf()\nf()") == (V{7, 7}));                              // call as a statement
    assert(run("fn a() { let x = 1\n return x }\nfn b() { let x = 2\n return x }\nprint a() + b()") == V{3});
    assert(run("fn fact(n) {\n if n <= 1 { return 1 }\n return n * fact(n - 1)\n}\nprint fact(10)") == V{3628800});
    assert(run("fn fib(n) {\n if n < 2 { return n }\n return fib(n - 1) + fib(n - 2)\n}\nprint fib(20)") == V{6765});
    assert(run("fn sum(n) {\n if n == 0 { return 0 }\n return n + sum(n - 1)\n}\nprint sum(1000)") == V{500500});
    assert(run("fn even(n) {\n if n == 0 { return 1 }\n return odd(n - 1)\n}\nfn odd(n) {\n if n == 0 { return 0 }\n return even(n - 1)\n}\nprint even(10)\nprint odd(10)") == (V{1, 0}));
    assert(run("fn loop_sum(n) {\n let t = 0\n let i = 1\n while i <= n {\n t = t + i\n i = i + 1\n }\n return t\n}\nprint loop_sum(100)") == V{5050});
    assert(run("fn shout(x) { print x\n return x }\nprint 1 + shout(5) * 2") == (V{5, 11}));  // call inside an expression
    assert(run("fn f(a) { a = a + 1\n return a }\nlet x = 1\nprint f(x)\nprint x") == (V{2, 1})); // arguments are copies

    // strings: literals and printing
    assert(text("print \"hi\"") == S{"hi"});
    assert(text("print \"\"") == S{""});
    assert(text("print \"one\"\nprint \"two\"") == (S{"one", "two"}));
    assert(text("print \"a\\tb\"") == S{"a\tb"});
    assert(text("print \"line1\\nline2\"") == S{"line1\nline2"});
    assert(text("print \"say \\\"hi\\\"\"") == S{"say \"hi\""});
    assert(text("print \"back\\\\slash\"") == S{"back\\slash"});
    assert(text("print \"# not a comment\"") == S{"# not a comment"});
    assert(text("print \"same\"\nprint \"same\"") == (S{"same", "same"}));            // identical literals share storage
    assert(text("print 1\nprint \"x\"\nprint 2") == (S{"1", "x", "2"}));             // ints and strings interleave

    // strings: variables, concatenation
    assert(text("let s = \"a\"\nprint s") == S{"a"});
    assert(text("let s = \"a\"\ns = s + \"b\"\nprint s") == S{"ab"});
    assert(text("print \"a\" + \"b\" + \"c\"") == S{"abc"});
    assert(text("print \"\" + \"\"") == S{""});
    assert(text("let a = \"foo\"\nlet b = \"bar\"\nprint a + b + a") == S{"foobarfoo"});
    assert(text("let s = \"ab\"\nlet i = 0\nlet out = \"\"\nwhile i < 3 {\n out = out + s\n i = i + 1\n}\nprint out") == S{"ababab"});

    // strings: len, str, comparisons
    assert(text("print len(\"hello\")") == S{"5"});
    assert(text("print len(\"\")") == S{"0"});
    assert(text("print len(\"ab\" + \"cde\")") == S{"5"});
    assert(text("print str(42)") == S{"42"});
    assert(text("print str(-7)") == S{"-7"});
    assert(text("print str(0)") == S{"0"});
    assert(text("print \"n = \" + str(2 + 3)") == S{"n = 5"});
    assert(text("print \"len of hello is \" + str(len(\"hello\"))") == S{"len of hello is 5"});
    assert(text("print \"a\" == \"a\"\nprint \"a\" == \"b\"\nprint \"a\" != \"b\"\nprint \"a\" != \"a\"") == (S{"1", "0", "1", "0"}));
    assert(text("print \"\" == \"\"\nprint \"ab\" == \"a\" + \"b\"") == (S{"1", "1"}));
    assert(text("let n = \"nero\"\nif n == \"nero\" { print \"yes\" } else { print \"no\" }") == S{"yes"});
    assert(text("if \"a\" != \"a\" { print 1 } else { print 2 }") == S{"2"});
    assert(text("print len(\"abc\") + 1") == S{"4"});

    // strings in functions
    assert(text("fn greet(name: str) -> str { return \"Hello, \" + name + \"!\" }\nprint greet(\"Nero\")") == S{"Hello, Nero!"});
    assert(text("fn show(label: str, n: int) -> str { return label + str(n) }\nprint show(\"n=\", 5)") == S{"n=5"});
    assert(text("fn rep(s: str, n: int) -> str {\n if n == 0 { return \"\" }\n return s + rep(s, n - 1)\n}\nprint rep(\"ab\", 3)") == S{"ababab"});
    assert(text("fn a() -> str { return \"A\" }\nprint \"<\" + a() + \">\"") == S{"<A>"});          // call inside an expression
    assert(text("fn f() -> str { }\nprint f()\nprint len(f())") == (S{"", "0"}));                    // falling off returns ""
    assert(text("fn f(a: str, b: int, c: str) -> int { return len(a) + b + len(c) }\nprint f(\"xx\", 10, \"y\")") == S{"13"});
    assert(text("fn id(s: str) -> str { return s }\nprint id(id(\"deep\"))") == S{"deep"});
    assert(text("fn shout(s: str) -> str { print s\n return s + \"!\" }\nprint \"<\" + shout(\"hey\") + \">\"") == (S{"hey", "<hey!>"}));
    assert(text("fn f(n: int) -> str {\n if n == 0 { return \"zero\" }\n return \"nonzero\"\n}\nprint f(0)\nprint f(5)") == (S{"zero", "nonzero"}));
    assert(text("if 0 { let s = \"x\" }\nprint s\nprint len(s)") == (S{"", "0"}));                    // never assigned counts as ""

    // compile errors
    assert(throws("print x"));
    assert(throws("x = 1"));
    assert(throws("let x = 1\nlet x = 2"));
    assert(throws("let x = x"));
    assert(throws("print foo(1)"));
    assert(throws("fn f(a) { return a }\nprint f(1, 2)"));
    assert(throws("fn f(a) { return a }\nprint f()"));
    assert(throws("fn f() { }\nfn f() { }"));
    assert(throws("return 1"));
    assert(throws("fn f(a, a) { }"));
    assert(throws("fn f(a, b, c, d, e, f, g) { }"));
    assert(throws("let g = 5\nfn f() { return g }\nprint f()"));       // functions cannot see main's variables
    assert(throws("fn f(a) { let a = 1 }"));

    // type errors
    assert(throws("print 1 + \"a\""));
    assert(throws("print \"a\" + 1"));
    assert(throws("print \"a\" == 1"));
    assert(throws("let x = 1\nx = \"a\""));
    assert(throws("let s = \"a\"\ns = 1"));
    assert(throws("print -\"a\""));
    assert(throws("print \"a\" < \"b\""));
    assert(throws("print \"a\" * 2"));
    assert(throws("print \"a\" - \"b\""));
    assert(throws("if \"a\" { print 1 }"));
    assert(throws("while \"a\" { print 1 }"));
    assert(throws("fn f(a: str) { }\nprint f(1)"));
    assert(throws("fn f(a: int) { }\nprint f(\"x\")"));
    assert(throws("fn f() -> str { return 1 }"));
    assert(throws("fn f() -> int { return \"x\" }"));
    assert(throws("fn f() -> str { return }"));
    assert(throws("print len(1)"));
    assert(throws("print str(\"a\")"));
    assert(throws("print len()"));
    assert(throws("print len(\"a\", \"b\")"));
    assert(throws("fn len(x) { }"));
    assert(throws("fn str(x) { }"));
    assert(throws("fn f() -> str { return \"a\" }\nprint 1 + f()"));

    std::puts("all codegen tests passed");
    return 0;
}
