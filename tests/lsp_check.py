#!/usr/bin/env python3
# Drives `tacqor lsp` over stdio: initialize, open a file with an error (expect a
# diagnostic), change it to valid text (expect the diagnostic cleared), then shut down.
import json, subprocess, sys, os

TACQOR = sys.argv[1] if len(sys.argv) > 1 else "./tacqor-dev"

proc = subprocess.Popen([TACQOR, "lsp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)

def send(obj):
    body = json.dumps(obj).encode()
    proc.stdin.write(b"Content-Length: %d\r\n\r\n%s" % (len(body), body))
    proc.stdin.flush()

def recv():
    headers = {}
    while True:
        line = proc.stdout.readline()
        if not line:
            return None
        line = line.rstrip(b"\r\n")
        if line == b"":
            break
        k, _, v = line.partition(b":")
        headers[k.strip().decode()] = v.strip().decode()
    n = int(headers["Content-Length"])
    return json.loads(proc.stdout.read(n))

def recv_method(method, tries=5):
    for _ in range(tries):
        msg = recv()
        if msg is None:
            return None
        if msg.get("method") == method:
            return msg
    return None

uri = "file:///tmp/lsp_demo.tq"

send({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"capabilities": {}}})
init = recv()
assert init and init.get("id") == 1, f"bad initialize reply: {init}"
assert init["result"]["capabilities"]["textDocumentSync"] == 1, "missing textDocumentSync"
print("initialize: ok")

# Open a file with a type error: 1 + True.
send({"jsonrpc": "2.0", "method": "textDocument/didOpen",
      "params": {"textDocument": {"uri": uri, "languageId": "tacqor", "version": 1,
                                   "text": "def main():\n    print(1 + True)\n"}}})
d = recv_method("textDocument/publishDiagnostics")
assert d and d["params"]["uri"] == uri, f"no diagnostics for open: {d}"
diags = d["params"]["diagnostics"]
assert len(diags) >= 1, f"expected an error diagnostic, got {diags}"
assert diags[0]["severity"] == 1, "expected error severity"
assert diags[0]["range"]["start"]["line"] == 1, f"error should be on line index 1, got {diags[0]['range']}"
print(f"didOpen error diagnostic: ok ({diags[0]['message'][:40]}...)")

# Fix it: now the document is valid.
send({"jsonrpc": "2.0", "method": "textDocument/didChange",
      "params": {"textDocument": {"uri": uri, "version": 2},
                 "contentChanges": [{"text": "def main():\n    print(1 + 2)\n"}]}})
d = recv_method("textDocument/publishDiagnostics")
assert d and d["params"]["diagnostics"] == [], f"expected cleared diagnostics, got {d['params']['diagnostics']}"
print("didChange cleared diagnostics: ok")

send({"jsonrpc": "2.0", "id": 2, "method": "shutdown"})
assert recv().get("id") == 2, "bad shutdown reply"
send({"jsonrpc": "2.0", "method": "exit"})
proc.wait(timeout=5)
assert proc.returncode == 0, f"exit code {proc.returncode}"
print("shutdown/exit: ok")
print("LSP PASS")
