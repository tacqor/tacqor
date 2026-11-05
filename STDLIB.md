# Tacqor 1.2 standard library

Two layers: **built-in functions** (implemented in the runtime, available everywhere without import) and **std modules** (written in Tacqor, in the `std/` folder, imported with `import std.name`). Any built-in or module function can also be called in method form on its first argument: `items.push(x)`, `text.upper()`, `value.unwrap()`.

## Built-in functions

### Output, input, process
| Function | Description |
|---|---|
| `print(x)` | Prints any value and a newline. |
| `write(x)` | Prints without a newline. `flush()` flushes stdout. |
| `eprint(x)` | Prints to stderr. |
| `input() -> str` | Reads one line from stdin (`""` at end of input). |
| `args() -> list[str]`, `argc() -> i64`, `arg(i) -> str` | Program arguments (after `--` with `tacqor run`). |
| `env(name) -> Option[str]` | Environment variable. |
| `run(command) -> i64` | Runs a shell command, returns its exit status. `run_capture(command) -> str` returns its standard output instead. |
| `exit(code)` | Ends the program with status `code` (0–255). |
| `fail(message)` | Raises a runtime error. |
| `time_ms() -> i64` | Monotonic milliseconds. `sleep_ms(n)` pauses. `now_text() -> str` is the UTC time as `YYYY-MM-DDTHH:MM:SSZ`; `unix_time() -> i64` is seconds since 1970. |
| `attempt(f) -> Result[T, str]` | Runs `f: fn() -> T` and turns a runtime error into `Err(message)`. |

### Files
| Function | Description |
|---|---|
| `read_text(path) -> str`, `read_lines(path) -> list[str]` | Read a whole file (error if missing). |
| `write_text(path, text)`, `append_text(path, text)` | Write / append. |
| `file_exists(path) -> bool`, `delete_file(path) -> bool`, `make_dir(path) -> bool`, `list_dir(path) -> list[str]` | File system helpers. |

### Conversion and text
| Function | Description |
|---|---|
| `str(x) -> str` | Any value to text (same format as `print`). |
| `int(x) -> i64` | From `f64` (truncating), `str` (error if invalid), or `bool`. `parse_int(s) -> Option[i64]` never fails. |
| `float(x) -> f64` | From `i64` or `str`. `parse_float(s) -> Option[f64]`. |
| `len(s)`, `char_count(s)` | Bytes / Unicode code points. `chars(s) -> list[str]` splits into code points. |
| `upper`, `lower`, `strip`, `capitalize`* | Case and whitespace. |
| `split(s, sep) -> list[str]`, `join(list, sep) -> str` | |
| `find(s, sub) -> i64` (`-1` if absent), `sub in s`, `starts_with(s, p)`, `ends_with(s, p)`, `replace(s, old, new)`, `repeat(s, n)` | |
| `chr(code) -> str`, `ord(s) -> i64` | Code point ↔ one-character string. |
| `fixed(f, digits) -> str` | Fixed-point formatting: `fixed(3.14159, 2)` is `"3.14"`. |
| `to_hex(i) -> str`, `from_hex(s) -> Option[i64]` | Hexadecimal (`"ff"`, `"0xFF"`). |
| `hash(s) -> i64` | 64-bit FNV-1a hash of the bytes, always non-negative. |
| `bytes(s) -> list[i64]`, `from_bytes(list) -> str` | Raw bytes. |

### Lists (`list[T]`)
`len`, `push(l, x)`, `pop(l) -> T`, `insert(l, i, x)`, `remove_at(l, i) -> T`, `index_of(l, x) -> i64`, `x in l`, `sort(l)` (i64/f64/str/bool), `reverse(l)`, `clear(l)`, `extend(l, other)`, `fill(n, x) -> list[T]`, `sum(l)` (i64/f64), `min_of(l)`, `max_of(l)`, slicing `l[a:b]`.

### Dicts (`dict[K, V]`)
`len`, `d[k]` (error if missing), `d[k] = v`, `k in d`, `lookup(d, k) -> Option[V]`, `keys(d) -> list[K]`, `values(d) -> list[V]`, `remove_key(d, k) -> bool`, `clear(d)`.

### Option and Result
| Function | Description |
|---|---|
| `is_some(o)`, `is_none(o)`, `is_ok(r)`, `is_err(r)` | Tests. |
| `unwrap(v) -> T` | The payload; runtime error on `None`/`Err` (the error is included in the message). |
| `unwrap_or(v, default) -> T` | |
| `expect(v, message) -> T` | Like `unwrap` with your own message. |
| `v?` | Operator: unwrap or return the failure from the enclosing function. |

### Math
`abs`, `min(a, b)`, `max(a, b)` (i64/f64/str), `sqrt`, `pow`, `floor`, `ceil`, `round` (f64 → i64, half away from zero), `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `exp`, `log`, `log2`, `log10`, `random_float() -> f64` in [0, 1), `random_int(lo, hi) -> i64` inclusive, `seed(n)`.

### Parallelism
`parallel_sum(count, workers, mapper) -> i64` and `parallel_map(count, workers, mapper) -> list[i64]` — mapper is any `fn(i64) -> i64` (named function or closure capturing only numbers/bools); see the language reference.

`parallel_run(workers, task) -> i64` — runs `task(worker_index)` on `workers` threads at once (1–64), each thread with its own VM, and returns once every task has returned (for a server loop, that is never). This is the share-nothing building block behind `http.serve_concurrent`: a worker owns everything it allocates, so the task may capture only scalars and function values (enforced at compile time for an inline task, at runtime otherwise), and any shared state goes through a channel, a file, or a database. See the language reference.

Channels pass messages between workers: `channel() -> i64` makes one (the handle is a scalar, so a task may capture it); `channel_send(ch, message: str)` enqueues a copy; `channel_recv(ch) -> Option[str]` blocks for a message (`Some`) or returns `None` once the channel is closed and drained; `channel_try_recv(ch) -> Option[str]` never blocks; `channel_close(ch)` ends the stream. Messages cross as copies, so nothing on the heap is shared. The `std.channels` module wraps these.

### Memory
`gc_collect() -> i64` runs the cycle collector and returns the number of objects freed; `gc_count() -> i64` is the number of tracked containers.

### Networking (blocking TCP)
| Function | Description |
|---|---|
| `tcp_listen(port) -> Result[i64, str]` | Listening socket on all interfaces. |
| `tcp_accept(server) -> Result[i64, str]` | Waits for a connection. |
| `tcp_connect(host, port) -> Result[i64, str]` | Connects (DNS names allowed). |
| `tcp_read(socket, max_bytes) -> Result[str, str]` | Reads what is available, up to the limit; `""` at end of stream. |
| `tcp_write(socket, data) -> Result[i64, str]` | Sends everything; returns the byte count. |
| `tcp_close(socket)` | |

## std modules

### `std.lists`
Generic list algorithms, all taking closures:

`map(items, f)`, `filter(items, keep)`, `reduce(items, initial, step)`, `any(items, test)`, `all(items, test)`, `find_first(items, test) -> Option[T]`, `position(items, test) -> Option[i64]`, `count_if(items, test)`, `take(items, n)`, `drop(items, n)`, `flatten(nested)`, `unique(items)`, `range_list(a, b)`, `sorted_by(items, key: fn(T) -> i64)`, `max_by(items, key) -> Option[T]`, `each(items, action)`, `enumerate(items) -> list[Pair[i64, T]]`, `zip(a, b) -> list[Pair[A, B]]`, and `struct Pair[A, B]` with `first`/`second`.

### `std.strings`
`pad_left(s, width, fill)`, `pad_right`, `center`, `is_digit`, `is_alpha`, `is_space`, `lines(text)`, `words(text)`, `count(text, part)`, `capitalize`, `reverse_text`, `strip_prefix(s, p) -> Option[str]`, `strip_suffix`.

### `std.math`
Constants `PI`, `TAU`, `E`, `INT_MAX`, `INT_MIN`; `clamp`, `clamp_f`, `gcd`, `lcm`, `is_prime`, `factorial(n) -> Result[i64, str]`, `isqrt`, `lerp`, `to_degrees`, `to_radians`, `mean(list[f64]) -> Option[f64]`.

### `std.collections`
`Stack[T]` (`push`, `pop -> Option[T]`, `peek`, `size`), `Queue[T]` (`push`, `pop`, `size`), `Counter` over strings (`add`, `get`, `most_common -> Option[str]`), and constructors `new_stack[T]()`, `new_queue[T]()`, `new_counter()`.

### `std.json`
A JSON parser and encoder written in Tacqor. `parse(text) -> Result[Json, str]`, `encode(doc) -> str`; build documents with `null()`, `boolean(b)`, `number(f)`, `integer(i)`, `string(s)`, `array(items)`, `object()` + `set(obj, key, value)`; read them with the `Json` methods `get(key) -> Option[Json]`, `at(index) -> Option[Json]`, `as_text()`, `as_number()`, `as_int()`, `as_bool()`, and the fields `kind`, `items`, `keys`.

### `std.http`
An HTTP/1.1 server and client. Server: `serve(port, handler)` (forever, one connection at a time), `serve_count(port, n, handler)` (exactly `n`, then stop), and `serve_concurrent(port, workers, handler)` (a pool of `workers` threads serving at once), where `handler: fn(Request) -> Response`; a handler that fails answers 500 and the server continues. For `serve_concurrent` the handler must be a plain top-level function (no captures) and any state shared between requests lives in a file or a database — see the concurrency notes in the language reference. `Request` has `method`, `path`, `query`, `headers`, `body` and `header(name) -> Option[str]`; build responses with `text(status, body)`, `html`, `json`, `response(status, content_type, body)`, `not_found()`. Client: `get(url)`, `post(url, body)`, `request(method, url, body)` return `Result[Response, str]`; `Response` has `status`, `headers`, `body`, `header(name)`. Plain `http://` only, request bodies up to 8 MB.

### `std.testing`
For `tacqor test`: `assert_eq(actual, expected)`, `assert_ne`, `assert_true(condition, message)`, `assert_some(option) -> T`, `assert_ok(result) -> T`. Failures report the values involved.

### `std.time`
Dates and times in UTC, built on `unix_time()`. `from_unix(seconds) -> DateTime` and `now() -> DateTime` convert an epoch time to a `DateTime { year, month, day, hour, minute, second, weekday }` (weekday 0=Sunday); `to_unix(dt)` is the inverse. Formatting: `iso(dt)` (`"2026-11-04T20:27:05Z"`), `date_string(dt)`, `time_string(dt)`, `weekday_name(dt)`, `month_name(dt)`. Calendar helpers: `is_leap(year)`, `days_in_month(year, month)`, `days_from_civil(year, month, day)`. No time-zone database — everything is UTC.

### `std.random`
Typed conveniences over the built-in generator (call `seed(n)` first for a reproducible stream). `fraction() -> f64` in [0, 1); `integer(lo, hi)` inclusive; `below(n)` in [0, n-1]; `dice(sides)` in [1, sides]; `boolean(p)` true with probability `p`. On lists: `choice(items) -> Option[T]` (None if empty), `shuffle(items) -> list[T]` (a new list, Fisher-Yates), `sample(items, k) -> list[T]` (k distinct elements).

### `std.channels`
Message channels between the workers of a `parallel_run` pool (see the Parallelism built-ins above). `make() -> i64`, `send(ch, message)`, `recv(ch) -> Option[str]` (blocks; `None` when closed and drained), `try_recv(ch) -> Option[str]` (non-blocking), `close(ch)`. Messages are strings — use `std.json` for structure — and cross as copies, so no object is shared between threads.

### `std.os`
The environment and path names (file and process I/O is already in the built-ins). `env_or(name, fallback)` reads an environment variable with a default. Path helpers treat `/`-separated strings: `join(a, b)` (absolute `b` wins), `basename(path)`, `dirname(path)`, `extname(path)` (e.g. `".gz"`, `""` when none, and a leading dot is not an extension), `stem(path)` (basename without extension).

\* `capitalize` lives in `std.strings`.
