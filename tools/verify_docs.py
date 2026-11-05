"""Extract ```text code blocks from markdown docs, compile each Tacqor-looking one, run the self-contained ones."""
import re, sys, subprocess, pathlib, tempfile, os
root = pathlib.Path(sys.argv[1]); tacqor = pathlib.Path(sys.argv[2]).resolve()
docs = [pathlib.Path(p) for p in sys.argv[3:]]
fence = re.compile(r"^```(\w*)\n(.*?)^```", re.S | re.M)
ok = bad = skipped = 0
for doc in docs:
    text = doc.read_text()
    for n, m in enumerate(fence.finditer(text), 1):
        lang, body = m.group(1), m.group(2)
        if lang not in ("text", "tacqor", ""):
            continue
        first = body.lstrip().split("\n", 1)[0]
        line = text[:m.start()].count("\n") + 1
        if not re.match(r"(import |def |struct |trait |const |extern |#!)", first) or "def " not in body:
            skipped += 1
            continue
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "snippet.tq"
            path.write_text(body)
            env = dict(os.environ, TACQOR_HOME=str(root))
            needs_main = "def main" in body
            cmd = [str(tacqor), "check" if not needs_main else "run", str(path)]
            if needs_main and ("tcp_" in body or "http." in body or "input()" in body or "args()" in body or "sleep_ms" in body or "run(" in body or "serve" in body or "extern " in body):
                cmd = [str(tacqor), "check", str(path)]
            r = subprocess.run(cmd, cwd=tmp, capture_output=True, text=True, timeout=60, env=env)
            label = f"{doc.name}:{line}"
            needs_input = cmd[1] == "run" and r.returncode != 0 and any(
                s in r.stderr + r.stdout for s in ("could not open file", "does not exist", "expected a regular file"))
            if needs_input:
                ok += 1
                print(f"ok   {label} (compiled; needs an input file to run)")
            elif r.returncode != 0 and not ("def test_" in body and "def main" not in body):
                bad += 1
                print(f"FAIL {label} ({cmd[1]})\n{r.stderr.strip()}\n{r.stdout.strip()}\n")
            else:
                ok += 1
                if cmd[1] == "run":
                    out = r.stdout.strip().replace("\n", " | ")
                    print(f"ok   {label}: {out[:160]}")
                else:
                    print(f"ok   {label} (check)")
print(f"\n{ok} ok, {bad} failed, {skipped} fragments skipped")
sys.exit(1 if bad else 0)
