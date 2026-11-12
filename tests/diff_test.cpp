#include "../src/diff.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

// the documents a program emits
static std::vector<Value> docs(const std::string& src) {
    Program p = Parser(Lexer(src).tokenize()).parse_program();
    std::ostringstream sink;
    Evaluator ev(p, sink);
    std::vector<Value> out;
    ev.collect(out);
    ev.run();
    return out;
}

// the diff of two one-document programs, as the printed lines
static std::string d(const std::string& a, const std::string& b, const std::vector<std::string>& ignore = {}) {
    std::vector<Value> x = docs(a), y = docs(b);
    assert(x.size() == 1 && y.size() == 1);
    std::ostringstream os;
    diff_print(diff_pair(x[0], y[0], ignore), os);
    return os.str();
}

static std::string tmp;
static void put(const std::string& rel, const std::string& text) {
    std::filesystem::path p = std::filesystem::path(tmp) / rel;
    std::filesystem::create_directories(p.parent_path());
    std::ofstream(p) << text;
}
static std::string P(const std::string& rel) { return tmp + "/" + rel; }

struct Result { int code; std::string out, err; };
static Result run_diff(const std::vector<std::string>& args) {
    std::ostringstream o, e;
    int c = diff_main(args, o, e);
    return {c, o.str(), e.str()};
}

int main() {
    char templ[] = "/tmp/nero_diff_XXXXXX";
    assert(mkdtemp(templ));
    tmp = templ;

    // ---- the core: what counts as a change ----
    assert(d("emit {a: 1}", "emit {a: 1}") == "");
    assert(d("emit {a: 1}", "emit {a: 2}") == "~ a: 1 -> 2\n");
    assert(d("emit {a: \"x\"}", "emit {a: \"y\"}") == "~ a: \"x\" -> \"y\"\n");
    assert(d("emit {a: true}", "emit {a: false}") == "~ a: true -> false\n");
    assert(d("emit {a: 1}", "emit {a: \"1\"}") == "~ a: 1 -> \"1\"\n");                       // a different kind is a change
    assert(d("emit {a: 1}", "emit {a: 1, b: 2}") == "+ b: 2\n");
    assert(d("emit {a: 1, b: 2}", "emit {a: 1}") == "- b: 2\n");
    assert(d("emit {x: {y: {z: 1}}}", "emit {x: {y: {z: 2}}}") == "~ x.y.z: 1 -> 2\n");
    assert(d("emit {a: 1, b: 2}", "emit {b: 2, a: 1}") == "");                              // field order is not a change
    assert(d("emit 1", "emit 2") == "~ (value): 1 -> 2\n");
    assert(d("emit {l: {\"app.io/name\": \"a\"}}", "emit {l: {\"app.io/name\": \"b\"}}") == "~ l[\"app.io/name\"]: \"a\" -> \"b\"\n");
    assert(d("emit {a: {b: 1}}", "emit {a: 5}") == "~ a: {\"b\": 1} -> 5\n");

    // ---- lists: by position, unless every item has its own name ----
    assert(d("emit {l: [1, 2]}", "emit {l: [1, 3, 4]}") == "~ l[1]: 2 -> 3\n+ l[2]: 4\n");
    assert(d("emit {l: [1, 2, 3]}", "emit {l: [1]}") == "- l[1]: 2\n- l[2]: 3\n");
    assert(d("emit [1, 2]", "emit [1, 9]") == "~ [1]: 2 -> 9\n");
    std::string cs = "emit {c: [{name: \"web\", image: \"a\"}, {name: \"log\", image: \"b\"}]}";
    std::string cs_reordered = "emit {c: [{name: \"log\", image: \"b\"}, {name: \"web\", image: \"a\"}]}";
    assert(d(cs, cs_reordered) == "");                                                      // reordering named items is not a change
    assert(d(cs, "emit {c: [{name: \"web\", image: \"z\"}, {name: \"log\", image: \"b\"}]}") == "~ c[name=web].image: \"a\" -> \"z\"\n");
    assert(d(cs, "emit {c: [{name: \"web\", image: \"a\"}]}") == "- c[name=log]: {\"name\": \"log\", \"image\": \"b\"}\n");
    assert(d("emit {c: [{name: \"web\"}]}", "emit {c: [{name: \"web\"}, {name: \"db\"}]}") == "+ c[name=db]: {\"name\": \"db\"}\n");
    // two items with the same name: no way to match them by name, so it falls back to position
    assert(d("emit {c: [{name: \"a\", v: 1}, {name: \"a\", v: 2}]}", "emit {c: [{name: \"a\", v: 1}, {name: \"a\", v: 3}]}") == "~ c[1].v: 2 -> 3\n");
    assert(d("emit {c: [{v: 1}]}", "emit {c: [{v: 2}]}") == "~ c[0].v: 1 -> 2\n");          // no name at all: by position

    // ---- secrets are compared by name ----
    assert(d("emit {p: secret(\"A\")}", "emit {p: secret(\"A\")}") == "");
    assert(d("emit {p: secret(\"A\")}", "emit {p: secret(\"B\")}") == "~ p: secret(A) -> secret(B)\n");
    assert(d("emit {p: secret(\"A\")}", "emit {p: \"A\"}") == "~ p: secret(A) -> \"A\"\n");

    // ---- schema values diff like records ----
    assert(d("type P {\n  n: int\n  s: str = \"x\"\n}\nemit P { n: 1 }", "type P {\n  n: int\n  s: str = \"x\"\n}\nemit P { n: 2 }") == "~ n: 1 -> 2\n");

    // ---- ignore ----
    assert(d("emit {a: {b: 1, c: 1}}", "emit {a: {b: 2, c: 2}}", {"a.b"}) == "~ a.c: 1 -> 2\n");
    assert(d("emit {a: {b: 1, c: 1}}", "emit {a: {b: 2, c: 2}}", {"a"}) == "");              // everything under it too
    assert(d("emit {ab: 1}", "emit {ab: 2}", {"a"}) == "~ ab: 1 -> 2\n");                    // a prefix of the name is not the same field
    assert(d("emit {l: [{name: \"x\", v: 1}]}", "emit {l: [{name: \"x\", v: 2}]}", {"l"}) == "");

    // ---- two files ----
    put("a.nero", "emit {kind: \"Deployment\", replicas: 2}\nemit {kind: \"Service\", port: 80}\n");
    put("b.nero", "emit {kind: \"Deployment\", replicas: 5}\nemit {kind: \"Service\", port: 80}\n");
    Result r = run_diff({P("a.nero"), P("b.nero")});
    assert(r.code == 1);
    assert(r.out == "@ Deployment\n~ replicas: 2 -> 5\n1 change\n");
    r = run_diff({P("a.nero"), P("a.nero")});
    assert(r.code == 0 && r.out == "no differences\n");
    put("one.nero", "emit {a: 1}\n");
    put("two.nero", "emit {a: 1}\nemit {kind: \"Extra\"}\n");
    r = run_diff({P("one.nero"), P("two.nero")});
    assert(r.code == 1 && r.out == "+ Extra (only on the right)\n1 change\n");
    r = run_diff({P("two.nero"), P("one.nero")});
    assert(r.code == 1 && r.out == "- Extra (only on the left)\n1 change\n");
    put("single_a.nero", "emit {a: 1}\n");
    put("single_b.nero", "emit {a: 2}\n");
    r = run_diff({P("single_a.nero"), P("single_b.nero")});
    assert(r.out == "~ a: 1 -> 2\n1 change\n");                                              // no header when there is one document each
    put("printy.nero", "print \"noise\"\nemit {a: 1}\n");
    r = run_diff({P("printy.nero"), P("single_a.nero")});
    assert(r.code == 0 && r.out == "no differences\n");                                      // print output is not part of a diff

    // ---- --by, inside one file ----
    put("envs.nero",
        "fn app(env: str, n: int) -> record {\n  return {kind: \"Deployment\", metadata: {name: \"web-{env}\"}, spec: {replicas: n}}\n}\n"
        "fn svc(env: str) -> record {\n  return {kind: \"Service\", metadata: {name: \"web-{env}\"}, port: 80}\n}\n"
        "for e in [\"staging\", \"prod\"] {\n  emit app(e, if e == \"prod\" { 5 } else { 2 })\n  emit svc(e)\n}\n");
    r = run_diff({P("envs.nero"), "--by", "metadata.name", "web-staging", "web-prod"});
    assert(r.code == 1);
    assert(r.out == "@ Deployment (web-staging -> web-prod)\n~ spec.replicas: 2 -> 5\n1 change\n");   // the Service is identical, the name is hidden
    r = run_diff({P("envs.nero"), "--by", "metadata.name", "web-prod", "web-prod"});
    assert(r.code == 0);
    r = run_diff({P("envs.nero"), "--by", "metadata.name", "web-staging", "web-prod", "--ignore", "spec"});
    assert(r.code == 0);
    r = run_diff({P("envs.nero"), "--by", "metadata.name", "web-qa", "web-prod"});
    assert(r.code == 2 && r.err == "error: no document has metadata.name equal to \"web-qa\" (found: web-staging, web-prod)\n");
    r = run_diff({P("envs.nero"), "--by", "nothing.here", "a", "b"});
    assert(r.code == 2 && r.err.find("no document has nothing.here equal to \"a\"") != std::string::npos);

    // ---- mistakes ----
    assert(run_diff({}).code == 2);
    assert(run_diff({P("a.nero")}).code == 2);
    assert(run_diff({P("a.nero"), P("b.nero"), P("a.nero")}).code == 2);
    assert(run_diff({P("a.nero"), "--by"}).code == 2);
    assert(run_diff({P("a.nero"), P("b.nero"), "--nope"}).err == "error: unknown option --nope\n");
    r = run_diff({P("missing.nero"), P("a.nero")});
    assert(r.code == 2 && r.err.find("cannot open") != std::string::npos);
    put("broken.nero", "emit 1 / 0\n");
    r = run_diff({P("broken.nero"), P("a.nero")});
    assert(r.code == 2 && r.err.find("division by zero") != std::string::npos && r.err.find("broken.nero:1:") != std::string::npos);

    std::filesystem::remove_all(tmp);
    std::puts("all diff tests passed");
    return 0;
}
