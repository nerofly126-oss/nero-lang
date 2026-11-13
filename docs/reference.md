# n+ reference

The exact rules. For a gentler introduction, read the [tutorial](tutorial.md) first. Every example in this file is run by `tests/docs_test.py`.

## Command line

```
nero file.nero                    run with the compiled backend (a subset, see the end)
nero run file.nero                the same
nero eval file.nero [--json|--yaml]   run with the evaluator: the full language. JSON is the default.
nero check file.nero              static pass over everything, called or not
nero diff a.nero b.nero [--ignore PATH]
nero diff file.nero --by PATH X Y [--ignore PATH]
nero --tokens file.nero           print the tokens
nero --ast file.nero              print the syntax tree (imports already merged in)
nero --bin file.nero              write the generated machine code to out.bin
```

`--json` and `--yaml` only work with `eval`. Errors are printed to standard error as `error: FILE:LINE:COL: message` and exit with status 1. `check` exits 1 when it finds a problem. `diff` exits 0 when the documents are identical, 1 when they differ and 2 on any error.

## Source files

Files end in `.nero`. Statements end at the end of the line. A `#` starts a comment that runs to the end of the line (a `#` inside a string is just a character). Blank lines and indentation mean nothing. Inside `[ ]` and `{ }` a line may be broken after a comma. Inside the parentheses of a call it may not.

```nero
fn add(a: int, b: int) -> int {
    return a + b
}

print add(1,
          2)
```

```error
error: example.nero:5:13: expected a value, got end of line
```

Put the whole call on one line, or compute the arguments first with `let`.

### Names

A name starts with a letter or an underscore and continues with letters, digits and underscores. These words are reserved and cannot be used as variable, function or parameter names:

```
let print emit if else while for in fn return type import true false and or not
```

They can still be used as field names, in schemas, records and after a dot (`{type: 1}.type`).

```nero
let r = {type: "web", import: 1}
print r.type
print r.import
```

```output
web
1
```

### Numbers

Decimal integers. The range is a signed 64-bit integer. A larger literal is an error (`number too large`). There is no unary plus, and `-5` is the unary minus applied to `5`.

There are no decimal numbers. If you need `1.5` as text, write it as a string.

```nero
print 1.5
```

```error
error: example.nero:1:9: there are no decimal numbers in n+, only whole numbers (write "1.5" as a string if you need the text)
```

### Strings

Written in double quotes on a single line. The escapes are `\n`, `\t`, `\"`, `\\`, `\{` and `\}`. Any other backslash sequence is an error. Text is UTF-8 and is passed through unchanged.

`{expression}` inside a string is replaced by the value of the expression, which must be an int, a str or a bool. A string cannot contain a bare `{`: write `\{`. Strings nested inside the braces work, but they cannot themselves contain `{`.

```nero
let n = 2
print "n is {n}, double is {n * 2}, big is {if n > 1 { "yes" } else { "no" }}"
```

```output
n is 2, double is 4, big is yes
```

## Types

| Type | Written | Values |
|---|---|---|
| `int` | `42` | 64-bit signed integers |
| `str` | `"text"` | text |
| `bool` | `true`, `false` | booleans |
| `list` | `[1, 2]` | ordered values of any kinds |
| `record` | `{a: 1}` | named values, in the order written |
| `[T]` | | a list whose elements are all `T`, in a type position |
| schema | `Port { number: 80 }` | a record checked against a `type` declaration |
| `secret` | `secret("NAME")` | a reference to a secret, see below |

In a type position (parameters, return types, schema fields) you can write `int`, `str`, `bool`, `list`, `record`, `secret`, `[T]` and the name of any schema. `record` accepts any record, typed or not. `list` accepts any list. `[T]` checks every element and names the first one that does not fit.

## Statements

| Statement | Meaning |
|---|---|
| `let x = e` | create a variable |
| `x = e` | change a variable. It has to keep its kind: an int stays an int. |
| `print e` | write the value on a line. Strings are written as they are, anything else as compact JSON. |
| `emit e` | produce a document, see Output |
| `if c { ... }` | run the block when `c` holds |
| `if c { ... } else if c2 { ... } else { ... }` | the `else` goes on the same line as the closing brace |
| `while c { ... }` | repeat while `c` holds |
| `for x in list { ... }` | run the block once per element |
| `fn name(a: T, b) -> T { ... }` | declare a function (top level only) |
| `type Name { field: T = default }` | declare a schema (top level only) |
| `return e` / `return` | leave a function |
| `import "path.nero"` | use the types and functions of another file (top level, first lines only) |
| `f(x)` | call a function and drop the result |

Conditions (`if`, `while`, a comprehension filter) accept a bool or an int, where `0` is false and any other int is true. The operators `and`, `or` and `not` are stricter and accept only bools.

```nero
if 0 {
    print "not printed"
} else {
    print "zero is false"
}
```

```output
zero is false
```

### Variables and scope

Scope is flat per function, and the main program is like a function. There are no global variables, and a function cannot see the variables of the main program. A function sees its parameters and what it declares itself.

A variable made with `let` inside a block stays visible after the block, if the block ran. A loop variable exists only inside its loop, and it cannot reuse a name that is already taken.

```nero
if true {
    let inside = 1
}
print inside
```

```output
1
```

Declaring the same name twice with `let` in one function is an error, unless it is the very same `let` running again (a `let` in a loop body is fine). Two `let`s of one name in different branches of an `if` are also fine, since only one of them runs.

```nero
let x = 1
let x = 2
```

```error
error: example.nero:2:1: 'x' is already declared
```

## Expressions

### Precedence

From loosest to tightest:

| Level | Operators |
|---|---|
| 1 | `or` |
| 2 | `and` |
| 3 | `not` |
| 4 | `==` `!=` `<` `<=` `>` `>=` |
| 5 | `+` `-` |
| 6 | `*` `/` `%` |
| 7 | unary `-` |
| 8 | `.name`, `[index]`, calls, and everything below |

Binary operators associate to the left. Note that `not` binds *looser* than comparison: `not a == b` means `not (a == b)`. Use parentheses to be explicit.

```nero
print not 1 == 2
print 2 + 3 * 4
print (2 + 3) * 4
print 10 - 2 - 3
```

```output
true
14
20
5
```

`and` and `or` short-circuit: the right side only runs when it is needed.

### Arithmetic and comparison

`+ - * / %` work on ints. `/` truncates toward zero and `%` takes the sign of the left side, as in C. Dividing by zero is an error. An overflow is an error.

`+` also joins two strings and two lists. `<`, `<=`, `>` and `>=` work on ints only: comparing strings with `<` is an error.

`==` and `!=` compare deeply, lists and records included, but only values of the same kind: `1 == "1"` is an error, not `false`. A schema value is never equal to a plain record, even with the same fields.

```nero
type A {
    n: int
}

print A { n: 1 } == A { n: 1 }
print A { n: 1 } == {n: 1}
print [1, 2] == [1, 2]
```

```output
true
false
true
```

### if as a value

`if c { a } else { b }` is an expression, and the `else` is required. It binds like a single value, so `if c { 1 } else { 2 } + 10` is `(if ...) + 10`. The branches may be of different kinds.

### Lists, records, access

| Expression | Meaning |
|---|---|
| `[a, b, c]` | a list |
| `{k: v, "odd key": v}` | a record. A key is a name or a string. A duplicate key is an error. |
| `Name { k: v }` | a value of schema `Name` (the name has to be a declared schema) |
| `x[i]` | list element, counting from 0. Out of range is an error. |
| `r.k` or `r["k"]` | record field. A missing field is an error that lists the fields. |
| `[e for x in list]` | a new list from `e` for each element |
| `[e for x in list if c]` | the same, only for elements where `c` holds |

A comprehension has one `for`. Lists and records cannot be changed in place. Build a new one with `+`, a comprehension, or a record literal.

A variable named like a schema and followed by `{` is read as a typed literal, so avoid naming variables after schemas.

### Builtins

| Call | Result |
|---|---|
| `len(x)` | the length of a list, or the number of **bytes** of a string (`len("é")` is 2) |
| `str(x)` | an int or a bool as text |
| `range(n)` | the list `[0, 1, ..., n-1]`. `n` cannot be negative or above 1,000,000. |
| `env("NAME")` | the environment variable, an error when it is not set |
| `env("NAME", "fallback")` | the environment variable, or the fallback |
| `secret("NAME")` | a reference to a secret |

A function cannot be given one of these names. In `eval` the name of a variable may still be `env`.

### Secrets

`secret("NAME")` never reads anything. It makes a value of type `secret`, and written out it is the record `{secretRef: "NAME"}`. Because the value is never read it cannot leak. A secret cannot be put in a string, passed to `str()`, joined, or compared (even inside a list or record). `nero diff` compares secrets by name.

## Schemas

```
type Name {
    field: Type
    other: Type = default
}
```

Fields end at a newline or a comma. A field with `= default` is optional. The default is evaluated once, before the program runs, and checked against the field's type. A default that refers to itself is an error.

A value `Name { ... }` is checked completely, and each of these is an error with the position of the culprit:

- a field the schema does not have (with a "did you mean" when a name is close)
- a field of the wrong type
- required fields that are missing (all of them are listed at once)
- a field given twice

The output of a schema value always follows the order of the schema, whatever order you wrote the fields in.

A field whose type is a schema must be given a value of that schema, written with its name. A bare `{ }` is rejected with a hint.

```nero
type Port {
    number: int
}

type Service {
    port: Port
}

print Service { port: { number: 80 } }
```

```error
error: example.nero:9:23: field 'port' of Service must be a Port, got a record (write Port { ... })
```

## Functions

```
fn name(a: T, b: T) -> T {
    ...
    return value
}
```

Functions are declared at the top level and can be used before their declaration. A parameter or return type that is not written is `int`, and that is enforced in `eval`. Arguments and return values are checked against the types.

```nero
fn half(n) {
    return n / 2
}

print half("ten")
```

```error
error: example.nero:5:12: argument 1 of 'half' must be an int, got a str (parameter 'n' has no type written, so it is an int)
```

A function that ends without a `return` gives the default of its return type: `0` for an int, `""`, `false`, `[]` or `{}`. A function that returns a schema or a `secret` has no sensible default, so there that is an error:

```nero
fn token() -> secret {
}

print token()
```

```error
error: example.nero:4:7: function 'token' ended without returning a value of type secret
```

Recursion is allowed up to a depth of 1000.

## Imports

```
import "relative/path.nero"
```

- The `import` lines come first in the file, and the path is a plain string, relative to the file that contains the `import`.
- An imported file can only hold `import`, `type` and `fn`.
- There is one flat namespace across all the files. The same type or function name in two files is an error that names both. All the types of the files you import, also through other files, are visible.
- A file imported several times is loaded once. An import cycle is an error that prints the chain.
- Errors name the file they happened in: `lib.nero:3:5: message`.
- `import` only works in `eval`, `check` and `diff`, not in `nero run`.

## Output

`emit e` produces one document. With `eval` the format is JSON (default) or YAML (`--yaml`).

JSON is pretty-printed with two spaces. YAML writes every document one after another, separated by `---`. A plain record is written in the order of its fields, and a schema value in the order of its schema.

The YAML writer is strict on purpose. A string is written plain only if it starts with a letter, an underscore or a slash, has only safe characters, and is not a word YAML would read as something else (`no`, `on`, `null`, `y`, `n` and the like). Numbers-as-strings and everything else are double-quoted.

```nero yaml
emit {a: "plain", b: "no", c: "1.0", d: "has: colon", e: [1, 2], f: {}}
```

```output
a: plain
b: "no"
c: "1.0"
d: "has: colon"
e:
  - 1
  - 2
f: {}
```

There is no YAML comment, anchor or block-scalar output.

## Checks before running: `nero check`

`check` goes over every function and every schema default, whether it is called or not, and reports everything it can prove wrong, with positions, sorted:

- fields of a schema value that are unknown, missing, duplicated or of the wrong type
- calls to undefined functions, wrong argument counts and argument types
- return values of the wrong type, and a function returning a schema or a secret that can end without a `return`
- variables that are never declared, assignments of a different kind, a second `let` of one name
- operators and conditions applied to the wrong kinds, builtins given the wrong kind, a division by a literal zero (including `-0`)
- a field read on a schema that does not have it

When a value depends on a branch, `check` keeps the possible types: it reports a mismatch only when every branch violates the expected type. It stays quiet about values from plain records and runtime failures (overflow, recursion depth, an index out of range, `range` limits). A clean result means *nothing provably wrong*, which is less than *correct*.

```nero check
fn f(n: int) -> int {
    return n + "1"
}
```

```error
error: example.nero:2:14: cannot use '+' on an int and a str (use str(...) to turn a number into text)
1 problem found
```

## Comparing: `nero diff`

Evaluates the files and compares the documents they `emit`:

- `nero diff a.nero b.nero` pairs the documents of both files by position.
- `nero diff file.nero --by PATH X Y` takes the documents of one file whose field `PATH` (a dotted path such as `metadata.name`) equals `X` and the ones where it equals `Y`, and pairs those. The `--by` field itself is not reported.
- `--ignore PATH` hides a field and everything below it. It can be repeated.

One line per change: `~ path: before -> after`, `+ path: value`, `- path: value`. A list whose items are all records with a unique string `name` is compared by that name, other lists by position. `print` output is not part of a diff. The exit code is 0 for identical, 1 for different, 2 for an error.

## The compiled backend

`nero file.nero` and `nero run` compile to x86-64 machine code and run it. It supports a small subset: integers and strings, `let`, assignment, `print`, `if` / `else if` / `else`, `while`, functions, arithmetic and comparison, `len` and `str`. Everything else is rejected up front with `only works in 'nero eval'`: booleans, lists, records, schemas, `for`, `emit`, `and` / `or` / `not`, `if` as a value, string interpolation, `import`, `env` and `secret`.

Differences from `eval`: an integer overflow wraps instead of failing, a comparison produces the int `1` or `0` (so `print 1 < 2` prints `1`), and interpolation is not available (so write `\{` for a literal brace).

On the programs in `examples/` that fit the subset, `eval` and `run` print the same thing.

## Limits

- A recursion depth of 1000 in `eval`.
- `range` is limited to 1,000,000 elements.
- The evaluator is a tree walker, about a hundred times slower than the compiled backend.
- `nero run` requires an x86-64 process. It works natively on x86-64 Linux and Intel Macs, and may work under Rosetta on Apple Silicon. The other commands do not use the compiled backend.
