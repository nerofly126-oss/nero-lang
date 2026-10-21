#include "../src/codegen.hpp"
#include "../src/parser.hpp"
#include <cassert>
#include <cstdio>

using V = std::vector<int64_t>;

static V run(const std::string& src) {
    Program p = Parser(Lexer(src).tokenize()).parse_program();
    Grimoire g;
    NeroFn fn = CodeGen(g).compile(p);
    V out;
    nero_sink = &out;
    fn();
    nero_sink = nullptr;
    return out;
}

static bool throws(const std::string& src) {
    try { run(src); } catch (const std::runtime_error&) { return true; }
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

    // scope quirks: one flat scope, slots start at zero
    assert(run("if 1 { let y = 7 }\nprint y") == V{7});
    assert(run("if 0 { let y = 7 }\nprint y") == V{0});

    // compile errors
    assert(throws("print x"));
    assert(throws("x = 1"));
    assert(throws("let x = 1\nlet x = 2"));
    assert(throws("let x = x"));

    std::puts("all codegen tests passed");
    return 0;
}
