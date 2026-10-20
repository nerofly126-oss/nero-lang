# n+ (the Nero language)

A small programming language, written in C++, that compiles straight to x86-64 machine code at runtime. No VM, no LLVM, no libraries.

Status: just started. First goal is `print 2 + 3 * 4`.

## Plan

- arithmetic and print
- variables
- if and while
- functions
- strings and arrays
- calling C functions
- real programs written in n+

## Layout

- `src/grimoire.hpp`: the x86-64 assembler, carried over from [necromancer](https://github.com/nerofly126-oss/necromancer)
- `examples/`: programs written in n+ (files end in `.nero`)
