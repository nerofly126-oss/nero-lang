# Learn n+

This is a tour of the language, in the order you would want to learn it. It takes about fifteen minutes and every example runs. In fact `tests/docs_test.py` runs every code block in these docs, so if an example here is wrong, the tests fail.

You need the `nero` binary. Build it once:

```
g++ -std=c++17 -O2 -Wall -Wextra -o nero src/main.cpp
```

To try an example, save it in a file ending in `.nero` and run `./nero eval file.nero`. The `output` blocks below show what you should see. When the language is quiet about something, the [reference](reference.md) has the exact rules.

## 1. Hello

```nero
print "hello"
print 2 + 3 * 4
```

```output
hello
14
```

`print` writes one line. A `#` starts a comment that runs to the end of the line. Statements end at the end of the line, so there are no semicolons.

## 2. Values and variables

There are four basic values: integers, strings, booleans, and (see section 4) lists and records.

```nero
let name = "web"
let replicas = 3
let debug = true

print "service {name} runs {replicas} copies, debug is {debug}"
```

```output
service web runs 3 copies, debug is true
```

`let` creates a variable. Inside a string, `{...}` is replaced by the value of the expression in it, and that works for integers, strings and booleans. To write a literal brace, put a backslash before it: `\{` and `\}`.

```nero
print "a literal \{brace\}"
```

```output
a literal {brace}
```

Change a variable with `=`. It has to stay the same kind of thing: you can change a number to another number, but not to a string.

```nero
let count = 1
count = count + 1
print count
```

```output
2
```

```nero
let count = 1
count = "two"
```

```error
error: example.nero:2:1: 'count' is an int, cannot assign a str
```

Every error carries the file, the line and the column. You will see a lot of these in this tutorial, because telling you early and precisely is the whole point of the language.

Numbers are 64-bit integers. There are no decimals. Division throws away the remainder, and overflowing is an error, not a wraparound.

```nero
print 7 / 2
print 7 % 2
print 10 - 2 - 3
print (1 + 2) * 3
```

```output
3
1
5
9
```

## 3. Making decisions

```nero
let env = "prod"

if env == "prod" {
    print "careful"
} else if env == "staging" {
    print "almost careful"
} else {
    print "go wild"
}
```

```output
careful
```

The `else` has to be on the same line as the closing brace. Combine conditions with `and`, `or` and `not`, which only accept booleans (so `1 and 2` is an error, on purpose).

An `if` can also be a value. This is how you pick a number per environment:

```nero
let env = "staging"
let replicas = if env == "prod" { 5 } else if env == "staging" { 2 } else { 1 }
print replicas
```

```output
2
```

When `if` is a value, the `else` is required, since the value has to come from somewhere.

`while` repeats while its condition holds:

```nero
let n = 3
while n > 0 {
    print n
    n = n - 1
}
```

```output
3
2
1
```

## 4. Lists and records

A list is written with brackets, a record with braces. They can nest, and both may span several lines with a trailing comma.

```nero
let ports = [80, 443]
let service = {
    name: "web",
    ports: ports,
}

print ports[0]
print service.name
print service["name"]
print len(ports)
```

```output
80
web
web
2
```

Lists are indexed from 0. Reading past the end is an error that says how long the list was:

```nero
let ports = [80, 443]
print ports[2]
```

```error
error: example.nero:2:12: index 2 is out of range for a list of length 2
```

There is no way to change a list or record in place. You build a new one instead: `+` joins lists, and a comprehension transforms one.

```nero
let base = [1, 2, 3]
let all = base + [4]
let doubled = [x * 2 for x in all]
let big = [x for x in doubled if x > 4]
print all
print doubled
print big
```

```output
[1, 2, 3, 4]
[2, 4, 6, 8]
[6, 8]
```

`for` loops over a list, and `range(n)` is the list `[0, 1, ..., n-1]`:

```nero
for i in range(3) {
    print "item {i}"
}
```

```output
item 0
item 1
item 2
```

## 5. Functions

```nero
fn double(x: int) -> int {
    return x * 2
}

print double(21)
```

```output
42
```

Parameters and return values are typed. If you leave a type off, it is an `int`, and that is enforced:

```nero
fn greet(name: str) -> str {
    return "hello {name}"
}

print greet(5)
```

```error
error: example.nero:5:13: argument 1 of 'greet' must be a str, got an int
```

A function only sees its own parameters and variables. It cannot read variables from the main program, so everything it needs has to be passed in. That is less convenient than it sounds and much easier to reason about than it sounds.

A function can call itself:

```nero
fn fib(n: int) -> int {
    return if n < 2 { n } else { fib(n - 1) + fib(n - 2) }
}

print fib(10)
```

```output
55
```

## 6. Schemas: the point of the language

A schema says what a record is supposed to look like.

```nero
type Port {
    number: int
    protocol: str = "tcp"
}

print Port { number: 80 }
```

```output
{"number": 80, "protocol": "tcp"}
```

A field with `= value` is optional and falls back to its default. A field without one is required. Write `Port { ... }` to build a value of that type, and n+ checks every field.

A field of the wrong type:

```nero
type Port {
    number: int
    protocol: str = "tcp"
}

print Port { number: "80" }
```

```error
error: example.nero:6:22: field 'number' of Port must be an int, got a str
```

A typo:

```nero
type Port {
    number: int
    protocol: str = "tcp"
}

print Port { number: 80, protocl: "udp" }
```

```error
error: example.nero:6:26: Port has no field 'protocl' (did you mean 'protocol'?)
```

Missing fields, all of them at once:

```nero
type Service {
    name: str
    image: str
    replicas: int = 1
}

print Service { }
```

```error
error: example.nero:7:7: Service is missing fields 'name' (a str), 'image' (a str)
```

Schemas nest, and a list can be typed with `[Port]`. When a field expects a schema, you must write the schema's name, even for the inner value:

```nero
type Port {
    number: int
    protocol: str = "tcp"
}

type Service {
    name: str
    ports: [Port]
}

print Service { name: "web", ports: [Port { number: 80 }, Port { number: 443 }] }
```

```output
{"name": "web", "ports": [{"number": 80, "protocol": "tcp"}, {"number": 443, "protocol": "tcp"}]}
```

Leaving the name off the inner record is the classic mistake, and the error tells you the fix:

```nero
type Port {
    number: int
}

type Service {
    ports: [Port]
}

print Service { ports: [{ number: 80 }] }
```

```error
error: example.nero:9:24: field 'ports' of Service must be a list of Port, got a list, but element 0 is a record (write Port { ... })
```

The types you can write are `int`, `str`, `bool`, `list`, `record`, `[T]` for a list of `T`, `secret` (section 10) and the name of any schema. `record` accepts any record at all, which is handy for things like labels where you do not want to fix the keys.

## 7. Emit: producing the output

`print` is for looking at things. `emit` is for producing the result. By default `emit` writes JSON:

```nero
type Port {
    number: int
    protocol: str = "tcp"
}

emit Port { number: 80 }
```

```output
{
  "number": 80,
  "protocol": "tcp"
}
```

Run with `--yaml` to get YAML instead. Each `emit` is one document, and several documents are separated by `---`, which is exactly what `kubectl apply -f -` wants.

```nero yaml
type Port {
    number: int
    protocol: str = "tcp"
}

emit Port { number: 80 }
emit Port { number: 53, protocol: "udp" }
```

```output
number: 80
protocol: tcp
---
number: 53
protocol: udp
```

The output follows the order of the fields in the schema, not the order you wrote them in, so the generated files are stable. The YAML writer is deliberately strict: it quotes anything that could be misread (`"no"`, `"on"`, `"1.0"` and the like), so a string stays a string when something else reads it back.

## 8. One definition, many environments

Now the idea the whole thing is built for. Describe the service once as a function of the environment, then loop:

```nero yaml save=web.nero
type Service {
    name: str
    image: str
    replicas: int = 1
    debug: bool
}

fn web(env: str, version: str) -> Service {
    return Service {
        name: "web-{env}",
        image: "myapp:{version}",
        replicas: if env == "prod" { 5 } else if env == "staging" { 2 } else { 1 },
        debug: env != "prod",
    }
}

for env in ["dev", "prod"] {
    emit web(env, "1.4.2")
}
```

```output
name: web-dev
image: myapp:1.4.2
replicas: 1
debug: true
---
name: web-prod
image: myapp:1.4.2
replicas: 5
debug: false
```

Add a staging environment by adding a word to a list. Every environment is generated from the same definition, so they cannot quietly drift apart. And since every value is checked against the schema, a typo in one of them is an error with a line and column, not a failed deploy.

## 9. Splitting into files

Put shared schemas in their own file and import them. The path is relative to the file that imports it.

```nero file=schemas.nero
type Port {
    number: int
    protocol: str = "tcp"
}

fn web_port() -> Port {
    return Port { number: 80 }
}
```

```nero
import "schemas.nero"

emit web_port()
emit Port { number: 9090, protocol: "udp" }
```

```output
{
  "number": 80,
  "protocol": "tcp"
}
{
  "number": 9090,
  "protocol": "udp"
}
```

Some rules, all designed so that importing is safe:

- `import` lines come first in the file.
- An imported file can only contain `import`, `type` and `fn`. Nothing runs when you import it.
- There is one namespace. Declaring the same name in two files is an error that names both files.
- Import cycles are an error, and the message shows the chain.
- An error inside an imported file names that file.

## 10. Environment variables and secrets

`env("NAME")` reads an environment variable. If it is not set, that is an error. Give a second argument to say what to use instead:

```nero
let host = env("NERO_DOCS_UNSET", "localhost")
print host
```

```output
localhost
```

```nero
print env("NERO_DOCS_UNSET")
```

```error
error: example.nero:1:7: environment variable 'NERO_DOCS_UNSET' is not set (env("NERO_DOCS_UNSET", "...") gives it a fallback)
```

Failing when a variable is missing is deliberate. The other option is quietly deploying with an empty string.

Secrets are different. `secret("NAME")` does not read anything. It makes a reference, and the reference is all that ends up in the output:

```nero yaml
type Database {
    host: str
    password: secret
}

emit Database { host: "db.internal", password: secret("DB_PASSWORD") }
```

```output
host: db.internal
password:
  secretRef: DB_PASSWORD
```

Because the value is never read, it cannot leak. You also cannot put a secret in a string, convert it with `str()` or compare it, and each of those is an error:

```nero
let s = secret("DB_PASSWORD")
print "the password is {s}"
```

```error
error: example.nero:2:25: cannot put a secret inside a string (only int, str and bool)
```

What consumes `secretRef` is up to you. For Kubernetes you would write a schema that turns it into whatever shape you need.

## 11. Catching mistakes early: check and diff

`eval` only complains about code that actually runs. Here is a program with a bug in a function that nothing calls, so `eval` is perfectly happy:

```nero nocheck
type Port {
    number: int
}

fn forgotten() -> Port {
    return Port { number: "8080" }
}

emit Port { number: 80 }
```

```output
{
  "number": 80
}
```

`nero check` looks at all of it, called or not:

```nero check
type Port {
    number: int
}

fn forgotten() -> Port {
    return Port { number: "8080" }
}

emit Port { number: 80 }
```

```error
error: example.nero:6:27: field 'number' of Port must be an int, got a str
1 problem found
```

`check` only reports what it can prove is wrong, so a clean result means "nothing provably wrong", not "correct". When it cannot tell what type something has, it keeps quiet instead of guessing. It exits with 1 when it finds problems, so you can put it in CI.

The other tool is `nero diff`, which shows what changes between two environments. Given the loop from section 8 in a file, you compare two of the documents it emits:

```
nero diff web.nero --by name web-dev web-prod
```

<!-- docs-test: diff web.nero --by name web-dev web-prod -->
```
@ document 1 (web-dev -> web-prod)
~ replicas: 1 -> 5
~ debug: true -> false
2 changes
```

`--by name` means: pick the documents whose `name` field is `web-dev` and `web-prod` and compare them. The `name` field itself is not reported, since it always differs. Use a dotted path like `metadata.name` for nested fields, and `--ignore PATH` to hide a field you do not care about. You can also compare two files with `nero diff a.nero b.nero`. The exit code is 0 for identical, 1 for different and 2 when something went wrong.

Lists of records that all have a unique `name` field (containers, environment variables) are matched by that name, so reordering them is not a change.

## Where next

- [`reference.md`](reference.md) has the exact rules: every statement, operator, precedence, builtin and error.
- `examples/k8s.nero` is the whole idea at full size: Kubernetes Deployments and Services for three environments from one definition, with its schemas in `examples/k8s_schemas.nero`.
- `examples/k8s_bad.nero` is the same file with one quoted number. See what happens: `./nero eval examples/k8s_bad.nero`.
