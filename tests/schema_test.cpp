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

static const std::string T =
    "type Port {\n  number: int\n  protocol: str = \"tcp\"\n}\n"
    "type Service {\n  name: str\n  replicas: int = 1\n  ports: [Port]\n}\n";

int main() {
    // lexer and parser
    assert(Lexer("type").tokenize()[0].kind == Tok::Type);
    assert(ast("type P {\n  n: int\n  p: str = \"tcp\"\n}\nprint P { n: 1 }") ==
           "type P\n  n: int\n  p: str = \"tcp\"\nprint (record P \"n\" 1)\n");
    assert(ast("type S { x: int }\nfn f(a: S, b: [S]) -> [S] { }") == "type S\n  x: int\nfn f(a: S, b: [S]) -> [S]\n");
    assert(ast("type S { x: int, y: [[int]] }") == "type S\n  x: int\n  y: [[int]]\n");
    assert(ast("type S {}") == "type S\n");
    assert(ast("fn f(a: [int]) -> list { }") == "fn f(a: [int]) -> list\n");
    assert(ast("print P { x: 1 }\ntype P { x: int }") == "type P\n  x: int\nprint (record P \"x\" 1)\n");   // used before declared
    assert(ast("type S { \"app.io/name\": str }") == "type S\n  app.io/name: str\n");
    assert(ast("type S { x: int }\nif x { print 1 }") == "type S\n  x: int\nif x\n  print 1\n");   // 'x {' is not a literal
    assert(has("type", "a type name"));
    assert(has("type S", "'{' after the type name"));
    assert(has("type S { x }", "':' after the field name"));
    assert(has("type S { x: }", "expected a type"));
    assert(has("type S { x: int y: int }", "expected a new line"));
    assert(has("type S { x: int, x: str }", "duplicate field 'x' in type S"));
    assert(has("type S { x: int }\ntype S { }", "type 'S' is already declared"));
    assert(has("type int { }", "'int' is a builtin type"));
    assert(has("if true { type S { } }", "types can only be declared at the top level"));
    assert(has("type S { x: Nope }", "unknown type 'Nope'"));
    assert(has("type Service { }\nfn f(a: Servce) { }", "unknown type 'Servce' (did you mean 'Service'?)"));
    assert(has("fn f(a: float) { }", "unknown type 'float'"));
    assert(has("print S { x: 1 }", "expected end of line"));   // S is not a declared type

    // building values
    assert(ev(T + "emit Service { name: \"web\", ports: [Port { number: 80 }] }") ==
           "{\n  \"name\": \"web\",\n  \"replicas\": 1,\n  \"ports\": [\n    {\n      \"number\": 80,\n      \"protocol\": \"tcp\"\n    }\n  ]\n}\n");
    assert(ev(T + "print Service { ports: [], name: \"x\" }") == "{\"name\": \"x\", \"replicas\": 1, \"ports\": []}\n");   // schema order
    assert(ev(T + "print Service { name: \"x\", replicas: 4, ports: [] }.replicas") == "4\n");
    assert(ev(T + "print Service { name: \"x\", ports: [] }.replicas") == "1\n");
    assert(ev(T + "print Service { name: \"x\", ports: [Port { number: 1 }] }.ports[0].protocol") == "tcp\n");
    assert(ev("type A { x: int, y: str }\nprint A { x: 1, y: \"a\" }") == "{\"x\": 1, \"y\": \"a\"}\n");
    assert(ev("type L { \"app.io/name\": str }\nemit L { \"app.io/name\": \"x\" }") == "{\n  \"app.io/name\": \"x\"\n}\n");
    assert(ev("print P { x: 1 }.x\ntype P { x: int }") == "1\n");
    assert(ev("fn d() { return 7 }\ntype A { x: int = d() }\nprint A {}.x") == "7\n");
    assert(ev("type In { v: int = 1 }\ntype Out { i: In = In {} }\nprint Out {}.i.v") == "1\n");
    assert(ev("type Out { i: In = In {} }\ntype In { v: int = 2 }\nprint Out {}.i.v") == "2\n");       // declaration order is free
    assert(ev("type S { x: int }\nprint S { x: 1 } == S { x: 1 }") == "true\n");
    assert(ev("type S { x: int }\nprint S { x: 1 } == {x: 1}") == "false\n");                          // a schema value is not a plain record
    assert(ev("type S { x: int }\nfn mk(n: int) -> S { return S { x: n } }\nprint mk(3).x") == "3\n");
    assert(ev("type S { x: int }\nfn f(s: S) -> int { return s.x }\nprint f(S { x: 9 })") == "9\n");
    assert(ev("type S { x: int }\nfn f(r: record) -> int { return len([r]) }\nprint f(S { x: 9 })") == "1\n");   // a schema value is a record
    assert(ev("fn f(xs: [int]) -> int { return len(xs) }\nprint f([1, 2, 3])") == "3\n");
    assert(ev("fn f(xs: [[int]]) -> int { return len(xs) }\nprint f([[1], [2, 3]])") == "2\n");

    // errors: unknown field, wrong type, missing field, with line and column
    assert(err("type S { replicas: int }\nprint S { replcas: 3 }").rfind("2:11:", 0) == 0);
    assert(has("type S { replicas: int }\nprint S { replcas: 3 }", "S has no field 'replcas' (did you mean 'replicas'?)"));
    assert(has("type S { a: int }\nprint S { zzzzzz: 3 }", "S has no field 'zzzzzz' (fields: a)"));
    assert(err("type S { port: int }\nprint S { port: \"8080\" }").rfind("2:17:", 0) == 0);
    assert(has("type S { port: int }\nprint S { port: \"8080\" }", "field 'port' of S must be an int, got a str"));
    assert(has("type S { port: int }\nprint S { port: [1] }", "must be an int, got a list"));
    assert(has("type S { port: int, name: str }\nprint S { }", "S is missing fields 'port' (an int), 'name' (a str)"));
    assert(err("type S { port: int, name: str }\nprint S { }").rfind("2:7:", 0) == 0);
    assert(has("type S { port: int, name: str }\nprint S { port: 1 }", "S is missing field 'name' (a str)"));
    assert(has("type S { port: int }\nprint S { port: 1, port: 2 }", "duplicate field 'port'"));
    assert(has("type S { x: [int] }\nprint S { x: [1, \"a\"] }", "must be a list of int, got a list, but element 1 is a str"));
    assert(has("type P { n: int }\ntype S { p: P }\nprint S { p: {n: 1} }", "must be a P, got a record (write P { ... })"));
    assert(has("type P { n: int }\ntype S { p: [P] }\nprint S { p: [P { n: 1 }, {n: 2}] }", "element 1 is a record (write P { ... })"));
    assert(has("type P { n: int }\ntype Q { n: int }\ntype S { p: P }\nprint S { p: Q { n: 1 } }", "must be a P, got a Q"));
    assert(has("type S { replicas: int }\nprint S { replicas: 1 }.replcas", "S has no field 'replcas' (did you mean 'replicas'?)"));
    assert(has("type S { x: int }\nfn f(a: S) { }\nprint f({x: 1})", "argument 1 of 'f' must be a S, got a record (write S { ... })"));
    assert(has("fn f(xs: [int]) { }\nprint f([1, \"a\"])", "argument 1 of 'f' must be a list of int, got a list, but element 1 is a str"));
    assert(has("type Ingress { x: int }\nfn f(a: Ingress) { }\nprint f(1)", "must be an Ingress, got an int"));
    assert(has("type Image { x: int }\ntype Ingress { x: int }\ntype S { i: Image }\nprint S { i: Ingress { x: 1 } }", "must be an Image, got an Ingress"));
    assert(has("type S { x: int }\nfn f() -> S { return {x: 1} }\nprint f()", "this function returns S, but the value is a record (write S { ... })"));
    assert(has("type S { x: int }\nfn f() -> S { }\nprint f()", "function 'f' ended without returning a value of type S"));
    assert(has("type S { x: int }\nfn f() -> S { return }\nprint f()", "a bare 'return' is not allowed in a function that returns S"));

    // defaults are checked before anything runs
    assert(err("type S { x: int = \"a\" }\nprint 1").rfind("1:19:", 0) == 0);
    assert(has("type S { x: int = \"a\" }\nprint 1", "the default of field 'x' must be an int, got a str"));
    assert(has("type A { x: A = A {} }\nprint 1", "refers to itself"));

    // the compiled subset has no schemas
    assert(compile_throws("type S { x: int }"));
    assert(compile_throws("type S { x: int }\nfn f(a: S) { }"));
    assert(compile_throws("fn f(a: [int]) { }"));

    std::puts("all schema tests passed");
    return 0;
}
