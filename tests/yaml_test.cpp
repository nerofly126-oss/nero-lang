#include "../src/evaluator.hpp"
#include "../src/parser.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <sstream>
#include <utility>

static Value I(int64_t n) { return Value::integer(n); }
static Value S(const std::string& s) { return Value::text(s); }
static Value B(bool b) { return Value::boolean(b); }
static Value L(std::initializer_list<Value> items) {
    Value v;
    v.kind = Value::List;
    v.items = items;
    return v;
}
static Value R(std::initializer_list<std::pair<std::string, Value>> fields) {
    Value v = Value::record();
    for (const auto& f : fields) { v.keys.push_back(f.first); v.items.push_back(f.second); }
    return v;
}

static std::string run(const std::string& src, OutFormat fmt) {
    Program p = Parser(Lexer(src).tokenize()).parse_program();
    std::ostringstream os;
    Evaluator(p, os, fmt).run();
    return os.str();
}

static std::string ast(const std::string& src) {
    return dump_program(Parser(Lexer(src).tokenize()).parse_program());
}

int main() {
    // structure
    assert(to_yaml(S("web")) == "web\n");
    assert(to_yaml(I(-3)) == "-3\n");
    assert(to_yaml(R({{"name", S("web")}, {"replicas", I(3)}, {"debug", B(true)}})) == "name: web\nreplicas: 3\ndebug: true\n");
    assert(to_yaml(R({{"metadata", R({{"name", S("x")}})}})) == "metadata:\n  name: x\n");
    assert(to_yaml(R({{"ports", L({R({{"port", I(80)}, {"protocol", S("tcp")}}), R({{"port", I(443)}})})}})) ==
           "ports:\n  - port: 80\n    protocol: tcp\n  - port: 443\n");
    assert(to_yaml(R({{"args", L({S("a"), I(1)})}})) == "args:\n  - a\n  - 1\n");
    assert(to_yaml(R({{"a", L({})}, {"b", R({})}})) == "a: []\nb: {}\n");
    assert(to_yaml(L({I(1), I(2)})) == "- 1\n- 2\n");
    assert(to_yaml(L({L({I(1), I(2)}), L({I(3)})})) == "- - 1\n  - 2\n- - 3\n");
    assert(to_yaml(L({R({{"a", R({{"b", I(1)}})}})})) == "- a:\n    b: 1\n");
    assert(to_yaml(L({})) == "[]\n");
    assert(to_yaml(R({})) == "{}\n");
    assert(to_yaml(R({{"outer", R({{"list", L({R({{"k", S("v")}})})}})}})) == "outer:\n  list:\n    - k: v\n");

    // strings that YAML would read as something else get quotes
    assert(to_yaml(S("true")) == "\"true\"\n");
    assert(to_yaml(S("False")) == "\"False\"\n");
    assert(to_yaml(S("no")) == "\"no\"\n");
    assert(to_yaml(S("Yes")) == "\"Yes\"\n");
    assert(to_yaml(S("on")) == "\"on\"\n");
    assert(to_yaml(S("OFF")) == "\"OFF\"\n");
    assert(to_yaml(S("y")) == "\"y\"\n");
    assert(to_yaml(S("N")) == "\"N\"\n");
    assert(to_yaml(S("null")) == "\"null\"\n");
    assert(to_yaml(S("~")) == "\"~\"\n");
    assert(to_yaml(S("")) == "\"\"\n");
    assert(to_yaml(S("1")) == "\"1\"\n");
    assert(to_yaml(S("1.5")) == "\"1.5\"\n");
    assert(to_yaml(S("1.4.2")) == "\"1.4.2\"\n");
    assert(to_yaml(S("0x1F")) == "\"0x1F\"\n");
    assert(to_yaml(S("100m")) == "\"100m\"\n");
    assert(to_yaml(S("-x")) == "\"-x\"\n");
    assert(to_yaml(S("*star")) == "\"*star\"\n");
    assert(to_yaml(S("a: b")) == "\"a: b\"\n");
    assert(to_yaml(S("foo:")) == "\"foo:\"\n");
    assert(to_yaml(S("a #b")) == "\"a #b\"\n");
    assert(to_yaml(S(" a")) == "\" a\"\n");
    assert(to_yaml(S("a ")) == "\"a \"\n");
    assert(to_yaml(S("a  b")) == "\"a  b\"\n");
    assert(to_yaml(S("line1\nline2")) == "\"line1\\nline2\"\n");
    assert(to_yaml(S("tab\there")) == "\"tab\\there\"\n");
    assert(to_yaml(S("say \"hi\"")) == "\"say \\\"hi\\\"\"\n");
    assert(to_yaml(S("back\\slash")) == "\"back\\\\slash\"\n");

    // strings that are safe stay plain
    assert(to_yaml(S("hello world")) == "hello world\n");
    assert(to_yaml(S("yes sir")) == "yes sir\n");
    assert(to_yaml(S("ghcr.io/nero/web:1.4.2")) == "ghcr.io/nero/web:1.4.2\n");
    assert(to_yaml(S("app.kubernetes.io/name")) == "app.kubernetes.io/name\n");
    assert(to_yaml(S("/health")) == "/health\n");
    assert(to_yaml(S("apps/v1")) == "apps/v1\n");
    assert(to_yaml(S("http://example.com/x")) == "http://example.com/x\n");
    assert(to_yaml(S("ClusterIP")) == "ClusterIP\n");

    // keys follow the same rules
    assert(to_yaml(R({{"on", I(1)}, {"app.kubernetes.io/name", S("w")}, {"has space", I(2)}, {"", I(3)}})) ==
           "\"on\": 1\napp.kubernetes.io/name: w\nhas space: 2\n\"\": 3\n");

    // emit in YAML mode: documents are separated by ---
    assert(run("emit {a: 1}", OutFormat::Yaml) == "a: 1\n");
    assert(run("emit {a: 1}\nemit {a: 2}", OutFormat::Yaml) == "a: 1\n---\na: 2\n");
    assert(run("emit 1\nemit \"x\"\nemit [1, 2]", OutFormat::Yaml) == "1\n---\nx\n---\n- 1\n- 2\n");
    assert(run("type P { count: int }\nemit P { count: 1 }", OutFormat::Yaml) == "count: 1\n");
    assert(run("print {a: 1}\nemit {a: 1}", OutFormat::Yaml) == "{\"a\": 1}\na: 1\n");       // print is unchanged
    assert(run("emit {a: 1}", OutFormat::Json) == "{\n  \"a\": 1\n}\n");                      // JSON is still the default
    assert(run("for e in [\"a\", \"b\"] { emit {name: \"web-{e}\"} }", OutFormat::Yaml) == "name: web-a\n---\nname: web-b\n");

    // keywords work as field names
    assert(ast("type S { type: str, in: int }") == "type S\n  type: str\n  in: int\n");
    assert(run("type S { type: str, in: int }\nprint S { type: \"a\", in: 1 }.type", OutFormat::Json) == "a\n");
    assert(run("print {type: 1, for: 2}.for", OutFormat::Json) == "2\n");
    assert(run("emit {type: \"x\"}", OutFormat::Yaml) == "type: x\n");
    assert(ast("print s.type") == "print (. s type)\n");
    bool threw = false;
    try { ast("print s."); } catch (const std::runtime_error&) { threw = true; }
    assert(threw);
    threw = false;
    try { ast("print {1: 2}"); } catch (const std::runtime_error&) { threw = true; }
    assert(threw);

    std::puts("all yaml tests passed");
    return 0;
}
