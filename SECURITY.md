# Security Policy

## Supported versions

| Version | Supported |
|---|---|
| 1.2.x | yes |
| 1.1.x and earlier | no — upgrade; programs run unchanged |

## Reporting a vulnerability

Report security issues privately to the maintainer — **security contact: _add your email or private issue form here before publishing_** — not in the public issue tracker. Include the Tacqor version, platform, a reproducing program or input, and what the impact is. You will receive an acknowledgement within a few days and a fix or a mitigation plan before any public disclosure; credit is given in `CHANGELOG.md` unless you ask otherwise.

## What counts as a security issue

Tacqor's security goal is: **no input to the toolchain can make it execute code or corrupt memory**, whatever the source of that input — a source file, a `.tqb` bytecode file, an executable with an embedded program, data read from a file, a socket or standard input by a running program, or a package fetched with `tacqor add`.

In scope:

- Memory-unsafe behaviour (crash, out-of-bounds access, use-after-free) in the compiler, VM, bytecode loader, native-code generator, or runtime library on any input.
- A `.tqb` file or embedded program that passes the verifier but performs an operation the verifier is meant to exclude (type confusion, stack underflow, wild jumps).
- Generated C++ (`emit`, `build --native`) that is unsafe for a program that the VM runs safely.
- Unbounded resource use from bounded input (a small file that makes the compiler allocate without limit or recurse without limit).
- Ways to make `tacqor add` write outside `packages/`, or to make a module import read a file outside the documented search path.

Out of scope:

- Programs doing what they say: a Tacqor program can read and write files, run commands with `run`, open sockets, and exit with any status. Running an untrusted program is running untrusted code.
- Denial of service by a program that simply loops or allocates (`while True:`, growing a list forever); the limits protect the toolchain, not the host.
- The security of third-party packages. `tacqor add` fetches code; review it as you would any dependency.
- `std.http` is a small HTTP/1.1 implementation intended for tools and internal services; it does not implement TLS, request size limits beyond the runtime's text limit, or protection against slow clients. Put it behind a reverse proxy for anything exposed to the internet.

## Design measures already in place

- Every container access in the VM and runtime is bounds-checked; integer arithmetic is overflow-checked; there is no unchecked pointer arithmetic on data from outside.
- Bytecode is verified on load: opcode range, operand ranges, jump targets, stack depth at every instruction, type indices, function and struct references, trailer length and magic (`docs/BYTECODE.md` §4). Verification happens before any instruction runs, including for embedded programs.
- Source size (1 MB), bytecode size, expression and block nesting, call depth, value nesting and text size are bounded, with a diagnostic at the bound (`docs/SPECIFICATION.md` §10).
- Deep structures are destroyed iteratively, so no input can overflow the C++ stack through destructors.
- The toolchain reads packages only from `packages/`, the importing directory, the standard library directory and `TACQOR_PATH`; module names are identifiers, so `..` cannot appear in an import.
- The complete test suite runs under AddressSanitizer and UndefinedBehaviorSanitizer; 5,500 mutated source programs and 600 corrupted bytecode files were fuzzed with no findings (`docs/WHITEPAPER.md` §12). The fuzzing harness is rerun before each release.

## Hardening advice for deployments

- Run native executables rather than scripts where the program is fixed; there is no compiler in the executable to attack.
- Set `TACQOR_HOME` explicitly in services so the standard library location cannot be influenced by the working directory.
- Treat `packages/` as part of your source tree: commit `tacqor.pkg`, review what `tacqor install` fetched, and pin to a commit by vendoring if the source repository is not under your control.
- Validate all data from sockets and files in the program; `parse_int`, `std.json.parse` and `std.http.parse_request` return `Option`/`Result` and never trust their input.
