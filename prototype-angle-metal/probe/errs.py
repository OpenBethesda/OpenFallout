#!/usr/bin/env python3
# PROTOTYPE - throwaway. errs.py <dump folder>: compiles every dumped shader with ANGLE and prints each distinct error with the source line.
import re, subprocess, sys, os, collections
A = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
dump = sys.argv[1]; chk = os.path.join(A, "tmp", "chk-" + os.path.basename(dump.rstrip("/")))
os.makedirs(chk, exist_ok=True)
files = sorted(f for f in os.listdir(dump) if not f.endswith(".raw"))
for f in files:
    src = open(os.path.join(dump, f)).read()
    open(os.path.join(chk, f), "w").write(re.sub(r"(?m)^[ \t]*#line[^\n]*$", "", src))
out = subprocess.run([os.path.join(A, "probe", "glslcheck")] + [os.path.join(chk, f) for f in files], capture_output=True, text=True).stdout
seen = collections.OrderedDict(); cur = None; lines = None
for l in out.splitlines():
    if l.startswith("== "):
        cur = os.path.basename(l[3:]); lines = open(os.path.join(chk, cur)).read().split("\n")
    m = re.match(r"ERROR: \d+:(\d+): (.*)", l)
    if m and cur:
        n = int(m.group(1)); text = lines[n - 1].strip() if 0 < n <= len(lines) else "?"
        key = (m.group(2)[:110], text[:150])
        seen.setdefault(key, []).append(re.sub(r"^\d+_", "", cur))
for (err, text), where in seen.items():
    print(f"{len(where):3} {err}\n      | {text}\n      in {', '.join(sorted(set(where)))[:160]}")
print(out.splitlines()[-1])
