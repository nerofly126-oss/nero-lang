#include "../src/codegen.hpp"
#include "../src/evaluator.hpp"
#include "../src/loader.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

// ---- helpers for source strings ----
static std::string ev(const std::string& src, OutFormat fmt = OutFormat::Json) {
    Program p = Parser(Lexer(src).tokenize()).parse_program();
    std::ostringstream os;
    Evaluator(p, os, fmt).run();
    return os.str();
}
static std::string err(const std::string& src) {
    try { ev(src); } catch (const std::runtime_error& e) { return e.what(); }
    return "";
}
static bool has(const std::string& src, const std::string& needle) { return err(src).find(needle) != std::string::npos; }
static std::string ast(const std::string& src) { return dump_program(Parser(Lexer(src).tokenize()).parse_program()); }
static std::string compile_err(const std::string& src) {
    try {
        Program p = Parser(Lexer(src).tokenize()).parse_program();
        Grimoire g;
        CodeGen(g).compile(p);
    } catch (const std::runtime_error& e) { return e.what(); }
    return "";
}

// ---- helpers for real files in a temporary folder ----
static std::string tmp;
static void put(const std::string& rel, const std::string& text) {
    std::filesystem::path p = std::filesystem::path(tmp) / rel;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p) << text;
}
static std::string P(const std::string& rel) { return tmp + "/" + rel; }
static std::string run(const std::string& rel, OutFormat fmt = OutFormat::Json) {
    Program p = Loader().load(P(rel));
    std::ostringstream os;
    Evaluator(p, os, fmt).run();
    return os.str();
}
static std::string ferr(const std::string& rel) {
    try { run(rel); } catch (const std::runtime_error& e) { return e.what(); }
    return "";
}
static bool starts(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }
static bool contains(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

int main() {
    auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    tmp = (std::filesystem::temp_directory_path() / ("nero_import_" + std::to_string(nonce))).string();
    assert(std::filesystem::create_directory(tmp));

    // ---- syntax ----
    assert(ast("import \"a.nero\"\nimport \"b.nero\"\nprint 1") == "import \"a.nero\"\nimport \"b.nero\"\nprint 1\n");
    assert(has("print 1\nimport \"a.nero\"", "must come before"));
    assert(has("fn f() -> int {\n  import \"a.nero\"\n  return 1\n}", "top level"));
    assert(has("import \"a{x}.nero\"", "plain string"));
    assert(has("import \"\"", "empty"));
    assert(has("import 5", "expected a file path"));
    assert(ev("print {import: 1}.import") == "1\n");                       // still fine as a field name
    assert(ev("type S { import: int }\nprint S { import: 2 }.import") == "2\n");

    // ---- importing ----
    put("lib.nero", "type Port {\n    number: int\n    protocol: str = \"tcp\"\n}\nfn web(n: int) -> Port {\n    return Port { number: n }\n}\n");
    put("main.nero", "import \"lib.nero\"\nemit web(80)\nemit Port { number: 1, protocol: \"udp\" }\nlet ps = [Port { number: 2 }]\nprint len(ps)\n");
    assert(run("main.nero") ==
           "{\n  \"number\": 80,\n  \"protocol\": \"tcp\"\n}\n{\n  \"number\": 1,\n  \"protocol\": \"udp\"\n}\n1\n");

    // the type is usable in annotations of the importing file, and the path is relative to the importing file
    put("deep/main.nero", "import \"../lib.nero\"\nfn first(ps: [Port]) -> Port {\n    return ps[0]\n}\nemit first([web(7)]).number\n");
    assert(run("deep/main.nero") == "7\n");

    // schemas are visible through a chain of imports, and a diamond loads the shared file once
    put("d.nero", "type D {\n    x: int\n}\n");
    put("b.nero", "import \"d.nero\"\nfn mk_b() -> D {\n    return D { x: 1 }\n}\n");
    put("c.nero", "import \"d.nero\"\nfn mk_c() -> D {\n    return D { x: 2 }\n}\n");
    put("top.nero", "import \"b.nero\"\nimport \"c.nero\"\nprint mk_b().x + mk_c().x\nprint D { x: 5 }.x\n");
    assert(run("top.nero") == "3\n5\n");

    // ---- import errors, each names the file ----
    put("cyc_a.nero", "import \"cyc_b.nero\"\nprint 1\n");
    put("cyc_b.nero", "import \"cyc_a.nero\"\ntype X {\n    a: int\n}\n");
    std::string e = ferr("cyc_a.nero");
    assert(starts(e, P("cyc_b.nero") + ":1:1: import cycle: "));
    assert(contains(e, P("cyc_a.nero") + " -> " + P("cyc_b.nero") + " -> " + P("cyc_a.nero")));
    put("self.nero", "import \"self.nero\"\n");
    assert(contains(ferr("self.nero"), "import cycle: " + P("self.nero") + " -> " + P("self.nero")));

    put("x.nero", "type Same {\n    a: int\n}\n");
    put("y.nero", "\ntype Same {\n    b: int\n}\n");
    put("clash.nero", "import \"x.nero\"\nimport \"y.nero\"\n");
    e = ferr("clash.nero");
    assert(starts(e, P("y.nero") + ":2:6: type 'Same' is already declared in " + P("x.nero") + " (line 1)"));
    put("fx.nero", "fn same() -> int {\n    return 1\n}\n");
    put("fy.nero", "fn same() -> int {\n    return 2\n}\n");
    put("fclash.nero", "import \"fx.nero\"\nimport \"fy.nero\"\n");
    assert(contains(ferr("fclash.nero"), "function 'same' is already defined in " + P("fx.nero")));

    put("runs.nero", "type T {\n    a: int\n}\nprint 1\n");
    put("imports_runs.nero", "import \"runs.nero\"\n");
    e = ferr("imports_runs.nero");
    assert(starts(e, P("runs.nero") + ":4:1: an imported file can only hold"));

    put("missing.nero", "print 1\nimport \"nope.nero\"\n");
    put("missing2.nero", "import \"nope.nero\"\n");
    assert(starts(ferr("missing2.nero"), P("missing2.nero") + ":1:1: cannot open '" + P("nope.nero") + "'"));
    assert(starts(ferr("missing.nero"), P("missing.nero") + ":2:1: 'import' must come before"));

    // parse errors inside an imported file name that file
    put("syntax.nero", "type Oops {\n    a int\n}\n");
    put("imports_syntax.nero", "import \"syntax.nero\"\n");
    assert(starts(ferr("imports_syntax.nero"), P("syntax.nero") + ":2:"));

    // ---- runtime errors name the file that holds the code ----
    put("bad_fn.nero", "type Port {\n    number: int\n}\nfn bad() -> Port {\n    return Port { number: \"x\" }\n}\n");
    put("calls_bad.nero", "import \"bad_fn.nero\"\nprint 1\nprint bad()\n");
    e = ferr("calls_bad.nero");
    assert(starts(e, P("bad_fn.nero") + ":5:27: field 'number' of Port must be an int, got a str"));

    put("bad_default.nero", "type T {\n    n: int = \"x\"\n}\n");
    put("uses_bad_default.nero", "import \"bad_default.nero\"\nprint 1\n");
    e = ferr("uses_bad_default.nero");
    assert(starts(e, P("bad_default.nero") + ":2:14: the default of field 'n' must be an int, got a str"));

    // after a call into another file the errors point back at the caller
    put("twice.nero", "fn twice(n: int) -> int {\n    return n * 2\n}\n");
    put("after_call.nero", "import \"twice.nero\"\nprint twice(2)\nprint 1 / 0\n");
    assert(starts(ferr("after_call.nero"), P("after_call.nero") + ":3:9: division by zero"));
    put("bad_arg.nero", "import \"twice.nero\"\nprint twice(\"x\")\n");
    assert(starts(ferr("bad_arg.nero"), P("bad_arg.nero") + ":2:13: argument 1 of 'twice' must be an int"));

    // source from a string has no file, so its errors stay "line:col: message"
    assert(err("print 1 / 0") == "1:9: division by zero");

    // import is not allowed in the compiled backend, and neither are env and secret
    assert(contains(compile_err("import \"a.nero\"\nprint 1"), "'import' only works in 'nero eval'"));
    assert(contains(compile_err("print env(\"A\")"), "'env' only works in 'nero eval'"));
    assert(contains(compile_err("print secret(\"A\")"), "'secret' only works in 'nero eval'"));

    // ---- env ----
    setenv("NERO_T_SET", "hello", 1);
    unsetenv("NERO_T_UNSET");
    assert(ev("print env(\"NERO_T_SET\")") == "hello\n");
    assert(ev("print env(\"NERO_T_UNSET\", \"fallback\")") == "fallback\n");
    assert(ev("print env(\"NERO_T_SET\", \"fallback\")") == "hello\n");
    assert(ev("let k = \"SET\"\nprint env(\"NERO_T_{k}\")") == "hello\n");
    assert(has("print env(\"NERO_T_UNSET\")", "environment variable 'NERO_T_UNSET' is not set"));
    assert(has("print env()", "takes 1 or 2 arguments"));
    assert(has("print env(\"a\", \"b\", \"c\")", "takes 1 or 2 arguments"));
    assert(has("print env(1)", "needs the name as a str"));
    assert(has("print env(\"NERO_T_SET\", 1)", "fallback of 'env' must be a str"));   // checked even though the variable is set
    assert(has("print env(\"\")", "not empty"));
    assert(has("fn env() -> int {\n  return 1\n}", "builtin"));
    assert(ev("let env = \"prod\"\nprint env") == "prod\n");                                // a variable may still be called env
    assert(ev("fn f(env: str) -> str {\n  return env(\"NERO_T_SET\")\n}\nprint f(\"x\")") == "hello\n");

    // ---- secret ----
    assert(ev("emit secret(\"DB_PASS\")") == "{\n  \"secretRef\": \"DB_PASS\"\n}\n");
    assert(ev("print secret(\"DB_PASS\")") == "{\"secretRef\": \"DB_PASS\"}\n");
    assert(ev("emit {password: secret(\"X\")}", OutFormat::Yaml) == "password:\n  secretRef: X\n");
    assert(ev("emit [secret(\"A\"), secret(\"B\")]", OutFormat::Yaml) == "- secretRef: A\n- secretRef: B\n");
    assert(ev("let s = \"prod\"\nprint secret(\"DB_{s}\")") == "{\"secretRef\": \"DB_prod\"}\n");
    assert(!contains(ev("print secret(\"NERO_T_SET\")"), "hello"));                           // the value is never read
    assert(ev("type D { password: secret }\nemit D { password: secret(\"X\") }", OutFormat::Yaml) == "password:\n  secretRef: X\n");
    assert(has("type D { password: secret }\nprint D { password: \"plain\" }", "must be a secret, got a str"));
    assert(has("type D { name: str }\nprint D { name: secret(\"X\") }", "must be a str, got a secret"));
    assert(has("let s = secret(\"X\")\nprint \"{s}\"", "cannot put a secret inside a string"));
    assert(has("print str(secret(\"X\"))", "got a secret"));
    assert(has("print len(secret(\"X\"))", "got a secret"));
    assert(has("let s = secret(\"X\")\nprint s + s", "cannot use '+' on a secret and a secret"));
    assert(has("let s = secret(\"X\")\nprint s == s", "secrets cannot be compared"));
    assert(has("let s = secret(\"X\")\nprint [s] != [s]", "secrets cannot be compared"));
    assert(has("let s = secret(\"X\")\nprint {a: s} == {a: 1}", "secrets cannot be compared"));
    assert(has("print secret()", "takes 1 argument"));
    assert(has("print secret(\"a\", \"b\")", "takes 1 argument"));
    assert(has("print secret(1)", "needs the name as a str"));
    assert(has("type secret { a: int }", "builtin type"));
    assert(has("fn secret() -> int {\n  return 1\n}", "builtin"));

    std::filesystem::remove_all(tmp);
    std::puts("all import tests passed");
    return 0;
}
