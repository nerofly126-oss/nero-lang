#!/usr/bin/env python3
"""Runs every example in docs/*.md against ./nero, so the docs cannot drift from the language.

Run it from the repository root, after building ./nero:   python3 tests/docs_test.py

Markdown conventions:
  ```nero            a program. It is run with 'nero eval' and has to succeed.
  ```nero yaml       the same, with --yaml
  ```nero check      run with 'nero check' instead
  ```nero file=a.nero  a helper file for the next program (imports). Not run itself.
  ```nero fragment   a snippet that is not a whole program. Not run.
  ```nero nocheck    a program that is wrong on purpose in a way only 'check' sees: skip the check
  ```nero save=a.nero  a program that is also kept as a.nero for later examples in the same file
  <!-- docs-test: diff a.nero --by name X Y -->   (invisible) runs that nero command in the folder of saved
                     files and compares its stdout with the code block that follows
  ```output          directly after a program: its stdout has to match exactly
  ```error           directly after a program (or after its output): it has to fail, and stderr has to match
Programs are saved as example.nero, so errors read "example.nero:3:7: ...".
Every program that runs fine under eval also has to pass 'nero check'.
"""
import glob
import os
import re
import subprocess
import sys
import tempfile

NERO = os.path.abspath("./nero")
FENCE = re.compile(r"^```([^\n]*)\n(.*?)^```[ \t]*$", re.S | re.M)


ITEM = re.compile(r"^```([^\n]*)\n(.*?)^```[ \t]*$|^<!-- docs-test: (.*?) -->[ \t]*$", re.S | re.M)


def items(text):
    out = []
    for m in ITEM.finditer(text):
        line = text.count("\n", 0, m.start()) + 1
        if m.group(3) is not None:
            out.append(("directive", m.group(3).strip(), "", line))
        else:
            out.append(("fence", m.group(1).strip(), m.group(2), line))
    return out


def run(args, cwd):
    env = {k: v for k, v in os.environ.items() if not k.startswith("NERO_DOCS_")}
    return subprocess.run([NERO] + args, cwd=cwd, capture_output=True, text=True, env=env)


def main():
    if not os.path.exists(NERO):
        print("build ./nero first (run this from the repository root)")
        return 2
    failures, checked = [], 0
    for path in sorted(glob.glob("docs/*.md")):
        it = items(open(path).read())
        helpers, saved = {}, {}
        i = 0
        while i < len(it):
            kind, info, code, line = it[i]
            i += 1
            where = "%s:%d" % (path, line)

            if kind == "directive":
                # the next fence holds the expected stdout of a nero command, run where the saved files are
                if i >= len(it) or it[i][0] != "fence":
                    failures.append("%s: a docs-test directive has to be followed by a code block" % where)
                    continue
                expected = it[i][2]
                i += 1
                with tempfile.TemporaryDirectory() as tmp:
                    for name, text in saved.items():
                        open(os.path.join(tmp, name), "w").write(text)
                    r = run(info.split(), tmp)
                checked += 1
                if r.stdout.strip() != expected.strip():
                    failures.append("%s: wrong output of 'nero %s'\n  expected: %r\n  got:      %r" % (where, info, expected.strip(), r.stdout.strip()))
                continue

            words = info.split()
            if not words or words[0] != "nero":
                continue
            flags = words[1:]
            helper = [f[5:] for f in flags if f.startswith("file=")]
            if helper:
                helpers[helper[0]] = code
                continue
            if "fragment" in flags:
                continue
            keep = [f[5:] for f in flags if f.startswith("save=")]
            want_out = want_err = None
            if i < len(it) and it[i][0] == "fence" and it[i][1] == "output":
                want_out = it[i][2]
                i += 1
            if i < len(it) and it[i][0] == "fence" and it[i][1] == "error":
                want_err = it[i][2]
                i += 1
            with tempfile.TemporaryDirectory() as tmp:
                for name, text in list(saved.items()) + list(helpers.items()):
                    open(os.path.join(tmp, name), "w").write(text)
                open(os.path.join(tmp, "example.nero"), "w").write(code)
                if "check" in flags:
                    r = run(["check", "example.nero"], tmp)
                elif "yaml" in flags:
                    r = run(["eval", "example.nero", "--yaml"], tmp)
                else:
                    r = run(["eval", "example.nero"], tmp)
                checked += 1
                if want_err is not None:
                    if r.returncode == 0:
                        failures.append("%s: expected an error, but it ran fine and printed %r" % (where, r.stdout))
                    elif r.stderr.strip() != want_err.strip():
                        failures.append("%s: wrong error\n  expected: %s\n  got:      %s" % (where, want_err.strip(), r.stderr.strip()))
                    if want_out is not None and r.stdout.strip() != want_out.strip():
                        failures.append("%s: wrong output before the error\n  expected: %r\n  got:      %r" % (where, want_out.strip(), r.stdout.strip()))
                else:
                    if r.returncode != 0:
                        failures.append("%s: it failed: %s" % (where, r.stderr.strip()))
                    elif want_out is not None and r.stdout.strip() != want_out.strip():
                        failures.append("%s: wrong output\n  expected: %r\n  got:      %r" % (where, want_out.strip(), r.stdout.strip()))
                    elif "check" not in flags and "nocheck" not in flags:
                        c = run(["check", "example.nero"], tmp)
                        if c.returncode != 0:
                            failures.append("%s: runs fine but 'nero check' objects: %s" % (where, c.stderr.strip()))
            if keep:
                saved[keep[0]] = code
            helpers = {}
    for f in failures:
        print("FAIL " + f)
    if failures:
        print("%d of %d examples failed" % (len(failures), checked))
        return 1
    print("all docs tests passed (%d examples)" % checked)
    return 0


sys.exit(main())
