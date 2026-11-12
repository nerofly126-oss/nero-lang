# n+ (nero-lang)

A small typed config language, written from scratch in C++17. Write a service once, generate every environment from it, and catch mistakes with a line and column before anything ships.

It started as a JIT compiler that had no reason to exist. The compiler is still in here (`nero run`). Now there is a language on top of it that does have a reason.

## Honest first

Jsonnet, CUE, Dhall, HCL and Pkl exist, and they are better than this. I did not build n+ because the world needed another config language. I built it to learn how one works, using only a terminal, g++ and no libraries. If you need to configure real infrastructure, use one of those.

What you get here is a working language with a Kubernetes demo that actually runs, and every claim below is something you can run yourself.

## Learn it

- [Tutorial](docs/tutorial.md): the language in about fifteen minutes. Every example runs.
- [Reference](docs/reference.md): the exact rules.

Every code block in the docs is run by `python3 tests/docs_test.py`, so the docs cannot drift from the language.

## What it does

YAML will happily ship a port as a string:

```
type Service {
    name: str
    port: int
    replicas: int = 1
}

emit Service {
    name: "web",
    port: "8080",
}
```

n+ refuses, and tells you where:

```
$ ./nero eval examples/bad_config.nero
error: examples/bad_config.nero:11:11: field 'port' of Service must be an int, got a str
```

Schemas also catch unknown fields (with a "did you mean"), missing fields (all of them listed at once), duplicates, and values of the wrong type, including inside nested schemas.

### check: look at code that never runs

`eval` only complains about what it actually executes. `check` is a static pass over every function and every schema default, called or not. `examples/check_demo.nero` has a function that nothing calls:

```
$ ./nero eval examples/check_demo.nero
{
  "number": 80,
  "protocol": "tcp"
}

$ ./nero check examples/check_demo.nero
error: examples/check_demo.nero:14:27: field 'number' of Port must be an int, got a str
error: examples/check_demo.nero:14:35: Port has no field 'protocl' (did you mean 'protocol'?)
2 problems found
```

`check` only reports what it can prove. When it cannot tell a type (a plain `{ }` record, a value that depends on a branch) it stays quiet. So a clean result means "nothing provably wrong", not "correct", and the message says exactly that.

### diff: what changes between environments

`examples/k8s.nero` generates a Deployment and a Service for dev, staging and prod from one loop. To see what differs between two of them:

```
$ ./nero diff examples/k8s.nero --by metadata.name web-staging web-prod
@ Deployment (web-staging -> web-prod)
~ metadata.labels.env: "staging" -> "prod"
~ spec.replicas: 2 -> 5
~ spec.template.metadata.labels.env: "staging" -> "prod"
~ spec.template.spec.containers[name=web].env[name=LOG_LEVEL].value: "debug" -> "info"
~ spec.template.spec.containers[name=web].resources.limits.cpu: "500m" -> "1"
@ Service (web-staging -> web-prod)
~ metadata.labels.env: "staging" -> "prod"
~ spec.selector.env: "staging" -> "prod"
7 changes
```

Lists of records that all have a unique `name` (containers, env vars) are matched by name, so reordering them is not a change. `--ignore PATH` hides a field and everything under it. You can also diff two files: `nero diff a.nero b.nero`. The exit code is 0 for identical, 1 for different and 2 for trouble, so it works in CI.

## A quick tour

```
type Port {
    number: int
    protocol: str = "tcp"          # a default makes the field optional
}

fn web(env: str) -> Port {
    return Port { number: if env == "prod" { 443 } else { 8080 } }
}

for env in ["dev", "staging", "prod"] {
    emit web(env)                  # one document per emit
}
```

- Values: ints, strings (with `"web-{env}"` interpolation), bools, lists, records, and typed records like `Port { ... }`.
- Types: `int`, `str`, `bool`, `list`, `record`, `[T]`, `secret`, and any schema you declare.
- Functions have typed parameters and return types. A parameter with no type written is an int, and that is enforced.
- Control flow: `if` (also as a value), `while`, `for`, and list comprehensions like `[x * 2 for x in xs if x > 1]`.
- Output is JSON by default and YAML with `--yaml`. Several `emit`s become a YAML stream separated by `---`.

### Imports

```
import "k8s_schemas.nero"
```

at the top of a file. The path is relative to the file that says it, so it works from any directory. An imported file may only hold `import`, `type` and `fn`, so importing can never run anything. There is one flat namespace: a name declared in two files is an error that names both. A cycle is an error that prints the chain. Every error starts with the file it happened in.

### Environment variables and secrets

```
host: env("DB_HOST", "db-{stage}.internal")     # a fallback, or env("DB_HOST") to fail when unset
password: secret("DB_PASSWORD_{stage}")         # a reference, never a value
```

`secret("NAME")` does not read the environment at all. It is a reference, and it is written out as `secretRef: NAME`. You cannot put it in a string, call `str()` on it or compare it, so the value cannot leak into generated output. See `examples/secrets.nero`.

## Commands

```
nero file.nero                    compiled backend: ints and strings only, runs as machine code
nero eval file.nero [--yaml]      the full language, prints JSON or YAML
nero check file.nero              static pass over everything, exit 1 on problems
nero diff a.nero b.nero           what changes between two files
nero diff f.nero --by PATH X Y    what changes between two documents of one file
nero --tokens | --ast file.nero   look at the lexer and parser output
nero --bin file.nero              write the generated machine code to out.bin
```

Errors look like `error: file.nero:line:col: message`.

## Build and test

```
g++ -std=c++17 -O2 -Wall -Wextra -o nero src/main.cpp
for t in lexer parser codegen eval schema flow yaml import check diff; do
    g++ -std=c++17 -O2 -Wall -Wextra -o ${t}_test tests/${t}_test.cpp && ./${t}_test
done
```

Ten assert-based suites, each printing `all ... tests passed`. `python3 tests/docs_test.py` runs every example in the docs. Built and tested with g++ 13.3 on Ubuntu 24.04. `nero run` generates x86-64 machine code, so it needs an x86-64 Linux machine. The evaluator, `check` and `diff` are plain C++ and do not depend on that.

## Layout

```
src/grimoire.hpp   x86-64 assembler (only used by nero run)
src/lexer.hpp      tokens, string interpolation
src/parser.hpp     recursive descent
src/ast.hpp        syntax tree and types
src/loader.hpp     import resolution
src/checker.hpp    nero check
src/codegen.hpp    the compiled backend
src/evaluator.hpp  tree-walking evaluator, schema validation, JSON and YAML writers
src/diff.hpp       nero diff
src/main.cpp       command line
tests/             one suite per part
examples/          small programs, the Kubernetes demo, and the bad ones on purpose
```

## What has and has not been verified

- The YAML output parses back (PyYAML) to the same data as the JSON output for every evaluating example, plus a file of 68 awkward strings and keys.
- The six generated manifests (three Deployments, three Services) pass `kubernetes-validate` in strict mode for 1.28, 1.30 and 1.33, and the validator does reject `replicas: "3"`.
- They have **not** been applied to a real cluster. If you try `./nero eval examples/k8s.nero --yaml | kubectl apply --dry-run=client -f -`, I would like to hear what happens.
- `check` was run over every program in the test suites that evaluates successfully. It stayed quiet on all of them except three that divide by a literal zero inside a branch that never runs.

## Known gaps

- The Kubernetes schemas only cover the fields the demo uses.
- No mutation (no push, no assigning into lists or records). Build new ones with `+` and comprehensions.
- Scope is flat per function: no globals, and functions cannot see the main program's variables.
- YAML output has no comments, anchors or block scalars. Unusual strings are double quoted.
- `check` cannot see runtime-only problems: integer overflow, recursion depth, an index out of range.
- `diff` matches unnamed lists by position, so inserting at the front reports a cascade. `--by` pairs documents in the order they were emitted.
- The evaluator is a tree walker, roughly two orders of magnitude slower than the compiled backend on a recursive `fib`. That is fine for config and not fine for anything else.

## Why the compiler is still here

The compiled backend came first: n+ began as a mini JIT that turns a small language into x86-64 at runtime, built on the assembler from an earlier project, [necromancer](https://github.com/nerofly126-oss/necromancer). It only handles ints and strings. On the eight examples that fit that subset, `eval` and `run` print the same thing.
