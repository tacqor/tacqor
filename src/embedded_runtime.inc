R"tacqor_runtime(
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace tacqor {

constexpr std::string_view version = "1.3.0";
constexpr std::size_t source_limit = 4'000'000;
constexpr std::size_t bytecode_limit = 64'000'000;
constexpr std::size_t value_limit = 65'536;        // locals per function, list literal size
constexpr std::size_t stack_limit = 4'000'000;     // values on the shared VM stack
constexpr std::size_t frame_limit = 100'000;       // nested calls
constexpr std::size_t text_limit = 256'000'000;    // bytes in one string or file

struct Location {
    std::uint32_t line = 1;
    std::uint32_t column = 1;
    std::uint32_t length = 1;
};

struct Error : std::runtime_error {
    Location at;
    std::string file;  // empty for the root source
    Error(Location location, const std::string& message, std::string in = {})
        : std::runtime_error(message), at(location), file(std::move(in)) {}
};

struct Finding {
    Location at;
    bool warning;
    std::string code;
    std::string message;
    std::string file;  // empty for the root source
};

// Types are interned in Program::types; a TypeId indexes that table.
using TypeId = std::uint32_t;

enum class Kind : std::uint8_t { unit, integer, floating, boolean, text, list, dict, object, function, option, result, trait_object };

struct TypeInfo {
    Kind kind = Kind::unit;
    TypeId first = 0;               // list element, dict key, struct index, function result, option/result payload, trait index
    TypeId second = 0;              // dict value, result error
    std::vector<TypeId> params;     // function parameters
    bool operator==(const TypeInfo& other) const {
        return kind == other.kind && first == other.first && second == other.second && params == other.params;
    }
};

struct StructInfo {
    std::string name;
    std::vector<std::string> field_names;
    std::vector<TypeId> field_types;
    // Dynamic dispatch tables: for each trait the struct implements, the function id of every trait method, in trait order.
    std::vector<std::pair<std::uint32_t, std::vector<std::uint32_t>>> vtables;
};

struct TraitInfo {
    std::string name;
    std::vector<std::string> method_names;
    std::vector<TypeId> method_types;   // function types without the self parameter
};

enum class Op : std::uint8_t {
    push_integer, push_float, push_boolean, push_text, push_unit, load, store, discard,
    make_list, make_dict, make_object, get_field, set_field,
    index, set_index, slice, length, contains,
    add, subtract, multiply, divide, modulo, negate,
    fadd, fsubtract, fmultiply, fdivide, fmodulo, fnegate, concat,
    equal, not_equal, less, less_equal, greater, greater_equal, invert,
    jump, jump_false, jump_true,
    jump_unless,             // operand = (comparison Op << 32) | target; pops two values, jumps unless comparison holds
    // Superinstructions produced by the peephole pass (never emitted directly by the code generator):
    load2,                   // operand = a | (b << 32): push locals[a], push locals[b]
    load_integer,            // operand = slot | (int32 constant << 32): push locals[slot], push constant
    add_store, subtract_store, multiply_store,  // pop two i64, apply checked op, store into locals[operand]
    jump_unless_locals,      // operand = (cmp << 56) | (a << 44) | (b << 32) | target: compare locals[a] ? locals[b]
    loop_init, loop_next,    // fused for-range: loop_init stores (limit) and starts; loop_next steps
    iter_next,               // fused for-in over a list
    call, reduce, builtin, return_value, finish,
    // 1.0: first-class functions and variants
    push_function,           // operand = function id: push a closure with no captures
    make_closure,            // operand = id | (capture count << 32): pop captures, push closure
    call_value,              // operand = argument count: stack [args..., closure] -> call
    make_variant,            // operand = which | (has payload << 32): Some/Ok (which 1) or None/Err (which 0)
    variant_which,           // pop a variant, push true when it is Some/Ok
    variant_payload,         // pop variant, push its payload
    // 1.1
    call_trait,              // operand = trait | (slot << 32): stack [self, args...] -> dynamic dispatch on self's struct
    parallel,                // operand = 0 (sum) or 1 (map): stack [count, workers, fn] -> i64 or list[i64]
    // 1.2
    bit_and, bit_or, bit_xor, shift_left, shift_right, bit_not,
    try_call,                // operand = 1 when the fn returns nothing: stack [fn] -> Result[T, str]
    count                    // number of opcodes; not an instruction
};

enum class Builtin : std::uint8_t {
    print, argc, arg, read_text, write_text, input, to_text, to_integer, to_float,
    abs, min, max, sqrt, pow, floor, ceil, round,
    push, pop, insert, remove_at, index_of, sort, reverse, clear,
    keys, values, remove_key,
    split, join, find, upper, lower, strip, replace, starts_with, ends_with, chr, ord,
    exit, time_ms, fail,
    // 1.0
    is_some, is_none, is_ok, is_err, unwrap, unwrap_or, expect,
    lookup, parse_int, parse_float, env,
    random_float, random_int, seed,
    file_exists, read_lines, append_text, delete_file, list_dir, make_dir,
    sleep_ms, now_text, args, run, write, eprint, flush,
    chars, char_count, repeat, bytes, from_bytes,
    sin, cos, tan, asin, acos, atan, atan2, exp, log, log2, log10,
    sum, min_of, max_of, fill, extend,
    // 1.1
    fixed, to_hex, from_hex, gc_collect, gc_count,
    // 1.2
    tcp_listen, tcp_accept, tcp_connect, tcp_read, tcp_write, tcp_close,
    run_capture, unix_time, hash,
    // 1.3 — value channels between workers
    channel, channel_send, channel_recv, channel_try_recv, channel_close,
    count,  // sentinel
};

struct Instruction {
    Op op;
    std::int64_t operand;
    Location at;
};

struct Function {
    std::string name;
    std::vector<TypeId> params;     // declared parameters first, then captured values
    std::uint32_t captures = 0;     // how many trailing params are captures (lambdas)
    TypeId result = 0;
    std::uint32_t locals = 0;
    std::vector<Instruction> code;
};

// A function that calls into a C symbol. The native backend emits a direct call; the bytecode
// VM runs the function's synthesised body, which reports that a native build is required. Not
// serialised into .tqb (foreign calls are a native-build feature), so build --native works from
// source, which is the documented path.
struct ForeignFunction {
    std::uint32_t function;      // index into Program::functions
    std::string symbol;          // C symbol to call
    std::string library;         // library to link (-l<name>); empty means none
};

struct Program {
    std::vector<TypeInfo> types;      // index 0 is always unit
    std::vector<StructInfo> structs;
    std::vector<TraitInfo> traits;
    std::vector<Function> functions;
    std::vector<std::string> texts;
    std::vector<ForeignFunction> foreign;
    std::uint32_t entry = 0;
};

struct CompileOptions {
    std::filesystem::path source_path;          // for resolving relative imports; may be empty
    std::vector<std::filesystem::path> search;  // extra module directories (the std library)
    bool require_main = true;
};

std::string type_name(const Program& program, TypeId type);
Program compile(std::string_view source, std::vector<Finding>& findings, const CompileOptions& options = {});
void run(const Program& program, const std::vector<std::string>& args, std::ostream& output);
int run_tests(const Program& program, std::ostream& output);
std::string encode(const Program& program);
Program decode(std::string_view bytes);
void verify(const Program& program);
bool embedded_program(const std::filesystem::path& executable, Program& program);
void write_artifact(const std::filesystem::path& output, std::string_view bytes,
                    const std::filesystem::path& executable = {});
std::filesystem::path current_executable(const char* argv0);
std::string generate_native(const Program& program);
void build_native(const Program& program, const std::filesystem::path& output);
int run_lsp(const std::vector<std::filesystem::path>& search);

}  // namespace tacqor
// Shared value runtime: used by the bytecode VM (runtime.cpp) and embedded verbatim into
// programs produced by the native backend (native.cpp). Everything here is header-only.


#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <fstream>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <cstdlib>
#include <ctime>
#include <new>
#include <random>
#include <optional>
#include <thread>
#if defined(_WIN32)
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#if defined(_MSC_VER)
#pragma comment(lib, "ws2_32.lib")  // Winsock, for the TCP builtins; also covers natively compiled programs
#endif
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <unordered_map>
#include <utility>
#include <vector>

namespace tacqor {

using Int = std::int64_t;
constexpr auto low = std::numeric_limits<Int>::min();
constexpr auto high = std::numeric_limits<Int>::max();

#if defined(__GNUC__) || defined(__clang__)
#define TACQOR_BUILTIN_OVERFLOW 1
#if !defined(TACQOR_NO_THREADED)
#define TACQOR_THREADED 1
#pragma GCC diagnostic ignored "-Wpedantic"  // computed goto is a GCC/Clang extension used on purpose
#endif
#endif

inline Int arithmetic(Op op, Int a, Int b) {
    Int result;
    switch (op) {
        case Op::add:
#ifdef TACQOR_BUILTIN_OVERFLOW
            if (__builtin_add_overflow(a, b, &result)) throw std::overflow_error("i64 overflow in +");
            return result;
#else
            if ((b > 0 && a > high - b) || (b < 0 && a < low - b)) throw std::overflow_error("i64 overflow in +");
            return a + b;
#endif
        case Op::subtract:
#ifdef TACQOR_BUILTIN_OVERFLOW
            if (__builtin_sub_overflow(a, b, &result)) throw std::overflow_error("i64 overflow in -");
            return result;
#else
            if ((b > 0 && a < low + b) || (b < 0 && a > high + b)) throw std::overflow_error("i64 overflow in -");
            return a - b;
#endif
        case Op::multiply:
#ifdef TACQOR_BUILTIN_OVERFLOW
            if (__builtin_mul_overflow(a, b, &result)) throw std::overflow_error("i64 overflow in *");
            return result;
#else
            if ((a > 0 && (b > high / a || b < low / a)) ||
                (a < 0 && ((b > 0 && a < low / b) || (b < 0 && a < high / b)))) {
                throw std::overflow_error("i64 overflow in *");
            }
            return a * b;
#endif
        case Op::negate:
            if (a == low) throw std::overflow_error("i64 overflow in -");
            return -a;
        case Op::divide:
            if (b == 0) throw std::domain_error("division by zero");
            if (a == low && b == -1) throw std::overflow_error("i64 overflow in //");
            return a / b - ((a % b != 0) && ((a < 0) != (b < 0)));
        case Op::modulo: {
            if (b == 0) throw std::domain_error("modulo by zero");
            if (b == -1) return 0;
            const auto remainder = a % b;
            return remainder && ((remainder < 0) != (b < 0)) ? remainder + b : remainder;
        }
        default: throw std::logic_error("invalid arithmetic opcode");
    }
}


inline Int shift_left(Int a, Int b) {
    if (b < 0 || b > 63) throw std::runtime_error("shift count must be between 0 and 63");
    return static_cast<Int>(static_cast<std::uint64_t>(a) << b);  // wrapping, like Rust's wrapping_shl
}
inline Int shift_right(Int a, Int b) {
    if ()tacqor_runtime",
R"tacqor_runtime(b < 0 || b > 63) throw std::runtime_error("shift count must be between 0 and 63");
    return a >> b;  // arithmetic shift: the sign is kept
}

inline std::string read_text(std::istream& input, std::size_t limit) {
    std::string result;
    char buffer[65536];
    while (input) {
        input.read(buffer, sizeof(buffer));
        const auto count = static_cast<std::size_t>(input.gcount());
        if (count > limit - result.size()) throw std::runtime_error("input exceeds the size limit");
        result.append(buffer, count);
    }
    if (!input.eof() || input.bad()) throw std::runtime_error("input could not be read");
    return result;
}

// ---------------------------------------------------------------------------
// Values: 16 bytes, immediates inline, heap objects intrusively reference counted.
// ---------------------------------------------------------------------------

enum class Tag : std::uint8_t { unit, integer, floating, boolean, text, list, dict, object, function, variant };

// Reference counts are not atomic: heap values are never shared between threads. parallel_sum
// workers receive only an i64 and return an i64, and the string constants they can reach are
// immortal (never counted, never freed), so each VM owns every counted object it touches.
class Value;

// ---------------------------------------------------------------------------
// Cycle collection. Reference counting frees everything except cycles (a struct that refers to
// itself through a field or a list). Containers that can hold references are kept on a global
// list; periodically the collector runs CPython's "trial deletion": for every tracked object,
// subtract the references that come from other tracked objects. Whatever is left with a positive
// count is referenced from outside (the stack, C++ locals in native code, ...) and is a root;
// everything not reachable from a root is a cycle of garbage and is broken up. No root
// enumeration is needed, so the same code serves the VM and native executables.
// ---------------------------------------------------------------------------

struct Obj;

struct GcState {
    std::mutex lock;
    Obj* head = nullptr;
    std::size_t tracked = 0;
    std::size_t since_collection = 0;
    std::size_t threshold = 200'000;
    bool parallel = false;      // set while worker threads run; collection is deferred
    std::size_t collected = 0;  // objects freed by the collector so far
};

inline GcState& gc_state() {
    static GcState state;
    return state;
}

inline thread_local bool gc_main_thread = false;

struct Obj {
    std::uint32_t refs = 1;
    bool immortal = false;
    bool tracked = false;
    bool gc_reachable = false;
    std::uint32_t gc_refs = 0;
    Obj* gc_prev = nullptr;
    Obj* gc_next = nullptr;

    virtual ~Obj() {
        if (!tracked) return;
        auto& state = gc_state();
        std::lock_guard<std::mutex> guard(state.lock);
        if (gc_prev) gc_prev->gc_next = gc_next; else state.head = gc_next;
        if (gc_next) gc_next->gc_prev = gc_prev;
        --state.tracked;
    }
    // Visits every Value this object holds that may refer to another object.
    virtual void each_child(const std::function<void(Value&)>&) {}
    // Drops every child reference (used to break garbage cycles).
    virtual void clear_children() {}
};

std::size_t collect_cycles();

inline void gc_track(Obj* object) {
    auto& state = gc_state();
    {
        std::lock_guard<std::mutex> guard(state.lock);
        object->tracked = true;
        object->gc_next = state.head;
        if (state.head) state.head->gc_prev = object;
        state.head = object;
        ++state.tracked;
        ++state.since_collection;
    }
    if (gc_main_thread && !state.parallel && state.since_collection > state.threshold) collect_cycles();
}

class Value {
public:
    Tag tag;
    union {
        Int i;
        double f;
        bool b;
        Obj* o;
    };

    Value() : tag(Tag::unit), i(0) {}
    Value(Int value) : tag(Tag::integer), i(value) {}
    Value(double value) : tag(Tag::floating), f(value) {}
    Value(bool value) : tag(Tag::boolean), b(value) {}
    Value(Tag kind, Obj* object) : tag(kind), o(object) {}  // takes ownership of one reference
    Value(const Value& other) : tag(other.tag), i(other.i) { retain(); }
    Value(Value&& other) noexcept : tag(other.tag), i(other.i) { other.tag = Tag::unit; }
    Value& operator=(const Value& other) {
        if (this != &other) {
            other.retain();
            release();
            tag = other.tag;
            i = other.i;
        }
        return *this;
    }
    Value& operator=(Value&& other) noexcept {
        if (this != &other) {
            release();
            tag = other.tag;
            i = other.i;
            other.tag = Tag::unit;
        }
        return *this;
    }
    ~Value() { release(); }

    bool heap() const { return tag >= Tag::text; }

private:
    void retain() const {
        if (heap() && !o->immortal) ++o->refs;
    }
    void release() {
        if (heap() && !o->immortal && --o->refs == 0) destroy(o);
    }
    // Deleting an object releases its children; a long chain (a linked list of a million nodes)
    // would otherwise recurse once per node. Pending deletions are drained from a loop instead.
    static void destroy(Obj* object) {
        thread_local std::vector<Obj*> pending;
        thread_local bool draining = false;
        pending.push_back(object);
        if (draining) return;
        draining = true;
        while (!pending.empty()) {
            Obj* next = pending.back();
            pending.pop_back();
            delete next;
        }
        draining = false;
    }
};

struct TextObj : Obj {
    std::string s;               // immutable after construction
    std::size_t cached_hash = 0;
    bool hashed = false;
    explicit TextObj(std::string value) : s(std::move(value)) {}
    std::size_t hash() {
        if (!hashed) {
            cached_hash = std::hash<std::string>()(s);
            hashed = true;
        }
        return cached_hash;
    }
};

// Lists store immediates (i64, f64, bool) in flat native arrays, like Vec<T>, and everything
// else as Values. The element kind is fixed when the list is created.
struct ListObj : Obj {
    enum class Elem : std::uint8_t { value, integer, floating, boolean };
    Elem elem = Elem::value;
    std::vector<Value> values;
    std::vector<Int> ints;
    std::vector<double> floats;
    std::vector<unsigned char> bools;

    explicit ListObj(Elem kind = Elem::value) : elem(kind) {
        if (kind == Elem::value) gc_track(this);  // lists of immediates cannot form cycles
    }
    void each_child(const std::function<void(Value&)>& visit) override { for (auto& item : values) visit(item); }
    void clear_children() override { values.clear(); }

    static Elem elem_for(Tag tag) {
        return tag == Tag::integer ? Elem::integer : tag == Tag::floating ? Elem::floating
             : tag == Tag::boolean ? Elem::boolean : Elem::value;
    }
    std::size_t size() const {
        switch (elem) {
            case Elem::integer: return ints.size();
            case Elem::floating: return floats.size();
            case Elem::boolean: return bools.size();
            default: return values.size();
        }
    }
    void reserve(std::size_t count) {
        switch (elem) {
            case Elem::integer: ints.reserve(count); break;
            case Elem::floating: floats.reserve(count); break;
            case Elem::boolean: bools.reserve(count); break;
            default: values.reserve(count); break;
        }
    }
    Value get(std::size_t i) const {
        switch (elem) {
            case Elem::integer: return ints[i];
            case Elem::floating: return floats[i];
            case Elem::boolean: return static_cast<bool>(bools[i]);
            default: return values[i];
        }
    }
    void set(std::size_t i, Value value) {
        switch (elem) {
            case Elem::integer: ints[i] = value.i; break;
            case Elem::floating: floats[i] = value.f; break;
            case Elem::boolean: bools[i] = value.b; break;
            default: values[i] = std::move(value); break;
        }
    }
    void push(Value value) {
        if (size() >= text_limit) throw std::runtime_error("list exceeds the size limit");
        switch (elem) {
            case Elem::integer: ints.push_back(value.i); break;
            case Elem::floating: floats.push_back(value.f); break;
            case Elem::boolean: bools.push_back(value.b); break;
            default: values.push_back(std::move(value)); break;
        }
    }
    Value pop() {
        if (size() == 0) throw std::runtime_error("pop from an empty list");
        Value value = get(size() - 1);
        switch (elem) {
            case Elem::integer: ints.pop_back(); break;
            case Elem::floating: floats.pop_back(); break;
            case Elem::boolean: bools.pop_back(); break;
            default: values.pop_back(); break;
        }
        return value;
    }
    void insert(std::size_t i, Value value) {
        if (size() >= text_limit) throw std::runtime_error("list exceeds the size limit");
        switch (elem) {
            case Elem::integer: ints.insert(ints.begin() + static_cast<std::ptrdiff_t>(i), value.i); break;
            case Elem::floating: floats.insert(floats.begin() + static_cast<std::ptrdiff_t>(i), value.f); break;
            case Elem::boolean: bools.insert(bools.begin() + static_cast<std::ptrdiff_t>(i), value.b); break;
            default: values.insert(values.begin() + static_cast<std::ptrdiff_t>(i), std::move(value)); break;
        }
    }
    Value erase(std::size_t i) {
        Value value = get(i);
        switch (elem) {
            case Elem::integer: ints.erase(ints.begin() + static_cast<std::ptrdiff_t>(i)); break;
            case Elem::floating: floats.erase(floats.begin() + static_cast<std::ptrdiff_t>(i)); break;
            case Elem::boolean: bools.erase(bools.begin() + static_cast<std::ptrdiff_t>(i)); break;
            default: values.erase(values.begin() + static_cast<std::ptrdiff_t>(i)); break;
        }
        return value;
    }
    void clear() { values.clear(); ints.clear(); floats.clear(); bools.clear(); }
    void reverse() {
        switch (elem) {
            case Elem::integer: std::reverse(ints.begin(), ints.end()); break;
            case Elem::floating: std::reverse(floats.begin(), floats.end()); break;
            case Elem::boolean: std::reverse(bools.begin(), bools.end()); break;
            default: std::reverse(values.begin(), values.end()); break;
        }
    }
    void sort();  // defined after compare()
    ListObj* slice(std::size_t a, std::size_t b) const {
        auto* result = new ListObj(elem);
        switch (elem) {
            case Elem::integer: result->ints.assign(ints.begin() + static_cast<std::ptrdiff_t>(a), ints.begin() + static_cast<std::ptrdiff_t>(b)); break;
            case Elem::floating: result->floats.assign(floats.begin() + static_cast<std::ptrdiff_t>(a), floats.begin() + static_cast<std::ptrdiff_t>(b)); break;
            case Elem::boolean: result->bools.assign(bools.begin() + static_cast<std::ptrdiff_t>(a), bools.begin() + static_cast<std::ptrdiff_t>(b)); break;
            default: result->values.assign(values.begin() + static_cast<std::ptrdiff_t>(a), values.begin() + static_cast<std::ptrdiff_t>(b)); break;
        }
        return result;
    }
};

struct ObjectObj : Obj {
    std::uint32_t type = 0;
    std::vector<Value> fields;
    ObjectObj() { gc_track(this); }
    void each_child(const std::function<void(Value&)>& visit) override { for (auto& item : fields) visit(item); }
    void clear_children() override { fields.clear(); }
};

// A function value: a program function plus the values it captured when it was created.
struct ClosureObj : Obj {
    std::uint32_t function = 0;
    std::vector<Value> captures;
    ClosureObj() { gc_track(th)tacqor_runtime",
R"tacqor_runtime(is); }
    void each_child(const std::function<void(Value&)>& visit) override { for (auto& item : captures) visit(item); }
    void clear_children() override { captures.clear(); }
};

// Option and Result values: `which` is 1 for Some/Ok and 0 for None/Err.
struct VariantObj : Obj {
    std::uint8_t which = 0;
    bool is_result = false;
    Value payload;
    VariantObj() { gc_track(this); }
    void each_child(const std::function<void(Value&)>& visit) override { visit(payload); }
    void clear_children() override { payload = Value(); }
};

inline ClosureObj& closure_of(const Value& v) {
    if (v.tag != Tag::function) throw std::runtime_error("invalid bytecode value type (expected fn)");
    return *static_cast<ClosureObj*>(v.o);
}
inline VariantObj& variant_of(const Value& v) {
    if (v.tag != Tag::variant) throw std::runtime_error("invalid bytecode value type (expected Option or Result)");
    return *static_cast<VariantObj*>(v.o);
}
inline Value make_variant(bool which, Value payload, bool is_result = false) {
    auto* variant = new VariantObj;
    variant->which = which;
    variant->is_result = is_result;
    variant->payload = std::move(payload);
    return {Tag::variant, variant};
}
inline Value make_closure(std::uint32_t function, std::vector<Value> captures) {
    auto* closure = new ClosureObj;
    closure->function = function;
    closure->captures = std::move(captures);
    return {Tag::function, closure};
}

// A parallel_run task is shared, read-only, by every worker thread, so it may capture only
// values that need no cross-thread reference counting: scalars (never counted) and function
// values (made immortal before the pool starts). Any other heap capture — a list, dict,
// object, string, or variant — would have its non-atomic refcount touched by two threads at
// once, so it is refused here. Share that kind of state through a file or a database instead.
// The compiler enforces this for an inline lambda; this is the backstop for a task bound to a
// name first (where the lambda's captures are no longer visible at the call site).
inline void check_shareable(const Value& value) {
    if (!value.heap()) return;
    if (value.tag != Tag::function) {
        throw std::runtime_error("a parallel_run task may only capture scalars and functions; share other state through a file or a database");
    }
    for (const auto& captured : static_cast<ClosureObj*>(value.o)->captures) check_shareable(captured);
}

// Mark a function value, and any function values it captures, immortal — so copies made on
// other threads never touch a non-atomic refcount. parallel_run uses this after check_shareable
// has confirmed the only heap captures are functions. Immortal objects are never freed, which
// is the intended trade for a pool that lives for the life of the process.
inline void immortalize_shared(const Value& value) {
    if (!value.heap() || value.o->immortal) return;
    value.o->immortal = true;
    if (value.tag == Tag::function) {
        for (const auto& captured : static_cast<ClosureObj*>(value.o)->captures) immortalize_shared(captured);
    }
}

struct KeyHash {
    std::size_t operator()(const Value& v) const {
        if (v.tag == Tag::text) return static_cast<TextObj*>(v.o)->hash();
        return std::hash<Int>()(v.i);
    }
};
struct KeyEqual {
    bool operator()(const Value& a, const Value& b) const {
        if (a.tag != b.tag) return false;
        if (a.tag == Tag::text) return static_cast<TextObj*>(a.o)->s == static_cast<TextObj*>(b.o)->s;
        return a.i == b.i;
    }
};

struct DictObj : Obj {
    std::vector<std::pair<Value, Value>> entries;  // insertion order
    std::unordered_map<Value, std::size_t, KeyHash, KeyEqual> index;

    DictObj() { gc_track(this); }
    void each_child(const std::function<void(Value&)>& visit) override { for (auto& entry : entries) visit(entry.second); }
    void clear_children() override { entries.clear(); index.clear(); }

    Value* find(const Value& key) {
        const auto it = index.find(key);
        return it == index.end() ? nullptr : &entries[it->second].second;
    }
    void set(const Value& key, Value value) {
        const auto it = index.find(key);
        if (it != index.end()) {
            entries[it->second].second = std::move(value);
            return;
        }
        index.emplace(key, entries.size());
        entries.emplace_back(key, std::move(value));
    }
    bool remove(const Value& key) {
        const auto it = index.find(key);
        if (it == index.end()) return false;
        const auto position = it->second;
        index.erase(it);
        entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(position));
        for (auto& entry : index) {
            if (entry.second > position) --entry.second;
        }
        return true;
    }
};

inline std::size_t collect_cycles() {
    auto& state = gc_state();
    std::vector<Obj*> objects;
    {
        std::lock_guard<std::mutex> guard(state.lock);
        objects.reserve(state.tracked);
        for (Obj* o = state.head; o; o = o->gc_next) {
            o->gc_refs = o->refs;
            o->gc_reachable = false;
            objects.push_back(o);
        }
        state.since_collection = 0;
    }
    // Subtract references that originate inside the tracked set.
    for (Obj* o : objects) {
        o->each_child([](Value& child) {
            if (child.heap() && child.o->tracked) --child.o->gc_refs;
        });
    }
    // Everything still positive is held from outside; mark what those reach.
    std::vector<Obj*> work;
    for (Obj* o : objects) {
        if (o->gc_refs > 0) {
            o->gc_reachable = true;
            work.push_back(o);
        }
    }
    while (!work.empty()) {
        Obj* o = work.back();
        work.pop_back();
        o->each_child([&](Value& child) {
            if (child.heap() && child.o->tracked && !child.o->gc_reachable) {
                child.o->gc_reachable = true;
                work.push_back(child.o);
            }
        });
    }
    std::vector<Obj*> garbage;
    for (Obj* o : objects) if (!o->gc_reachable) garbage.push_back(o);
    if (garbage.empty()) {
        state.threshold = std::max<std::size_t>(200'000, 2 * state.tracked);
        return 0;
    }
    for (Obj* o : garbage) ++o->refs;                  // keep them alive while the cycle is broken
    for (Obj* o : garbage) o->clear_children();        // releases everything they held
    std::size_t freed = 0;
    for (Obj* o : garbage) {
        if (--o->refs == 0) {
            delete o;
            ++freed;
        }
    }
    state.collected += freed;
    state.threshold = std::max<std::size_t>(200'000, 2 * state.tracked);
    return freed;
}

inline Value make_text(std::string s) {
    if (s.size() > text_limit) throw std::runtime_error("text exceeds the size limit");
    return {Tag::text, new TextObj(std::move(s))};
}
// Builds a list from Values. Without an explicit element kind, the first element decides.
inline Value make_list(std::vector<Value> items, std::optional<ListObj::Elem> elem = std::nullopt) {
    const auto kind = elem ? *elem : items.empty() ? ListObj::Elem::value : ListObj::elem_for(items.front().tag);
    auto* list = new ListObj(kind);
    if (kind == ListObj::Elem::value) {
        list->values = std::move(items);
    } else {
        list->reserve(items.size());
        for (auto& item : items) list->push(std::move(item));
    }
    return {Tag::list, list};
}
inline ListObj::Elem elem_for_kind(Kind kind) {
    return kind == Kind::integer ? ListObj::Elem::integer : kind == Kind::floating ? ListObj::Elem::floating
         : kind == Kind::boolean ? ListObj::Elem::boolean : ListObj::Elem::value;
}

// Cold error paths live out of line so the hot accessors below stay small enough to inline.
#if defined(__GNUC__) || defined(__clang__)
#define TACQOR_COLD __attribute__((noinline, cold))
#define TACQOR_LIKELY(x) __builtin_expect(!!(x), 1)
#else
#define TACQOR_COLD
#define TACQOR_LIKELY(x) (x)
#endif

[[noreturn]] TACQOR_COLD inline void fail(const char* message) { throw std::runtime_error(message); }

inline const std::string& text_of(const Value& v) {
    if (!TACQOR_LIKELY(v.tag == Tag::text)) fail("invalid bytecode value type (expected str)");
    return static_cast<TextObj*>(v.o)->s;
}
inline ListObj& list_of(const Value& v) {
    if (!TACQOR_LIKELY(v.tag == Tag::list)) fail("invalid bytecode value type (expected list)");
    return *static_cast<ListObj*>(v.o);
}
inline DictObj& dict_of(const Value& v) {
    if (!TACQOR_LIKELY(v.tag == Tag::dict)) fail("invalid bytecode value type (expected dict)");
    return *static_cast<DictObj*>(v.o);
}
inline ObjectObj& object_of(const Value& v) {
    if (!TACQOR_LIKELY(v.tag == Tag::object)) fail("invalid bytecode value type (expected struct)");
    return *static_cast<ObjectObj*>(v.o);
}
inline Int int_of(const Value& v) {
    if (!TACQOR_LIKELY(v.tag == Tag::integer)) fail("invalid bytecode value type (expected i64)");
    return v.i;
}
inline double float_of(const Value& v) {
    if (!TACQOR_LIKELY(v.tag == Tag::floating)) fail("invalid bytecode value type (expected f64)");
    return v.f;
}
inline bool bool_of(const Value& v) {
    if (!TACQOR_LIKELY(v.tag == Tag::boolean)) fail("invalid bytecode value type (expected bool)");
    return v.b;
}

constexpr int nesting_limit = 2000;

inline bool equal(const Value& a, const Value& b, int depth = 0) {
    if (a.tag != b.tag) throw std::runtime_error("invalid comparison types");
    if (depth > nesting_limit) throw std::runtime_error("value nesting exceeds the runtime limit");
    switch (a.tag) {
        case Tag::unit: return true;
        case Tag::integer: return a.i == b.i;
        case Tag::floating: return a.f == b.f;
        case Tag::boolean: return a.b == b.b;
        case Tag::text: return text_of(a) == text_of(b);
        case Tag::list: {
            const auto& x = list_of(a);
            const auto& y = list_of(b);
            if (x.size() != y.size()) return false;
            if (x.elem == ListObj::Elem::integer && y.elem == ListObj::Elem::integer) return x.ints == y.ints;
            for (std::size_t i = 0; i < x.size(); ++i) if (!equal(x.get(i), y.get(i), depth + 1)) return false;
            return true;
        }
        case Tag::dict: {
            auto& x = dict_of(a);
            auto& y = dict_of(b);
            if (x.entries.size() != y.entries.size()) return false;
            for (const auto& [key, value] : x.entries) {
                const auto* other = y.find(key);
                if (!other || !equal(value, *other, depth + 1)) return false;
            }
            return true;
        }
        case Tag::object: {
            const auto& x = object_of(a);
            const auto& y = object_of(b);
            if (x.type != y.type || x.fields.size() != y.fields.size()) return false;
            for (std::size_t i = 0; i < x.fields.size(); ++i) if (!equal(x.fields[i], y.fields[i], depth + 1)) return false;
            return true;
        }
        case Tag::function: {
            const auto& x = closure_of(a);
            const auto& y = closure_of(b);
            if (x.function != y.function || x.captures.size() != y.captures.size()) return false;
            for (std::size_t i = 0; i < x.captures.size(); ++i) if (!equal(x.captures[i], y.captures[i], depth + 1)) return false;
            return true;
        }
        case Tag::variant: {
            const auto& x = variant_of(a);
            const auto& y = variant_of(b);
            if (x.which != y.which) return false;
            if (x.payload.tag != y.payload.tag) return false;
            return x.payload.tag == Tag::unit || equal(x.payload, y.payload, depth + 1);
        }
    }
    return false;
}

inline int compare(const Value& a, const Value& b) {
    if (a.tag != b.tag) throw std::runtime_error("invalid comparison types");
    switch (a.tag) {
        case Tag::integer: return a.i < b.i ? -1 : a.i > b.)tacqor_runtime",
R"tacqor_runtime(i;
        case Tag::floating: return a.f < b.f ? -1 : a.f > b.f;
        case Tag::boolean: return a.b < b.b ? -1 : a.b > b.b;
        case Tag::text: {
            const int order = text_of(a).compare(text_of(b));
            return order < 0 ? -1 : order > 0;
        }
        default: throw std::runtime_error("values of this type cannot be ordered");
    }
}

inline void ListObj::sort() {
    switch (elem) {
        case Elem::integer: std::sort(ints.begin(), ints.end()); break;
        case Elem::floating: std::stable_sort(floats.begin(), floats.end()); break;
        case Elem::boolean: std::sort(bools.begin(), bools.end()); break;
        default: std::stable_sort(values.begin(), values.end(), [](const Value& a, const Value& b) { return compare(a, b) < 0; }); break;
    }
}

inline std::string format_float(double value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value < 0 ? "-inf" : "inf";
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    std::string text(buffer, result.ptr);
    if (text.find_first_of(".e") == std::string::npos) text += ".0";
    return text;
}

inline void format(std::string& out, const Value& value, const std::vector<StructInfo>& structs, bool quoted, int depth = 0) {
    if (depth > nesting_limit) throw std::runtime_error("value nesting exceeds the runtime limit");
    switch (value.tag) {
        case Tag::unit: out += "None"; break;
        case Tag::integer: out += std::to_string(value.i); break;
        case Tag::floating: out += format_float(value.f); break;
        case Tag::boolean: out += value.b ? "True" : "False"; break;
        case Tag::text:
            if (!quoted) {
                out += text_of(value);
            } else {
                out += '"';
                for (const char ch : text_of(value)) {
                    switch (ch) {
                        case '"': out += "\\\""; break;
                        case '\\': out += "\\\\"; break;
                        case '\n': out += "\\n"; break;
                        case '\r': out += "\\r"; break;
                        case '\t': out += "\\t"; break;
                        default: out += ch;
                    }
                }
                out += '"';
            }
            break;
        case Tag::list: {
            out += '[';
            bool first = true;
            const auto& list = list_of(value);
            for (std::size_t i = 0; i < list.size(); ++i) {
                if (!first) out += ", ";
                first = false;
                format(out, list.get(i), structs, true, depth + 1);
            }
            out += ']';
            break;
        }
        case Tag::dict: {
            out += '{';
            bool first = true;
            for (const auto& [key, item] : dict_of(value).entries) {
                if (!first) out += ", ";
                first = false;
                format(out, key, structs, true, depth + 1);
                out += ": ";
                format(out, item, structs, true, depth + 1);
            }
            out += '}';
            break;
        }
        case Tag::object: {
            const auto& object = object_of(value);
            const auto& info = structs.at(object.type);
            out += info.name;
            out += '(';
            for (std::size_t i = 0; i < object.fields.size(); ++i) {
                if (i) out += ", ";
                out += info.field_names.at(i);
                out += '=';
                format(out, object.fields[i], structs, true, depth + 1);
            }
            out += ')';
            break;
        }
        case Tag::function: out += "<fn #" + std::to_string(closure_of(value).function) + ">"; break;
        case Tag::variant: {
            const auto& variant = variant_of(value);
            if (!variant.is_result && !variant.which) {
                out += "None";
            } else {
                out += variant.is_result ? (variant.which ? "Ok(" : "Err(") : "Some(";
                format(out, variant.payload, structs, true, depth + 1);
                out += ')';
            }
            break;
        }
    }
    if (out.size() > text_limit) throw std::runtime_error("text exceeds the size limit");
}

inline Int checked_size(std::size_t size) { return static_cast<Int>(size); }

[[noreturn]] TACQOR_COLD inline void fail_index(const char* what) {
    throw std::runtime_error(std::string(what) + " index out of bounds");
}
inline std::size_t checked_index(Int offset, std::size_t size, const char* what) {
    if (!TACQOR_LIKELY(static_cast<std::uint64_t>(offset) < size)) fail_index(what);
    return static_cast<std::size_t>(offset);
}

inline std::pair<std::size_t, std::size_t> clamp_slice(Int lo, Int hi, std::size_t size) {
    const auto n = static_cast<Int>(size);
    if (lo < 0) lo = 0;
    if (hi > n) hi = n;
    if (lo > hi) hi = lo;
    if (lo > n) lo = hi = n;
    return {static_cast<std::size_t>(lo), static_cast<std::size_t>(hi)};
}

inline Int float_to_int(double value) {
    if (std::isnan(value)) throw std::runtime_error("cannot convert nan to i64");
    if (value >= 9223372036854775808.0 || value < -9223372036854775808.0) {
        throw std::runtime_error("float is outside the i64 range");
    }
    return static_cast<Int>(value);
}

inline std::string_view trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\n' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\n' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

inline Int parse_int(const std::string& text) {
    const auto view = trimmed(text);
    Int value{};
    const auto begin = view.data() + (view.size() > 0 && view.front() == '+');
    const auto result = std::from_chars(begin, view.data() + view.size(), value);
    if (view.empty() || result.ptr != view.data() + view.size() || result.ec != std::errc{}) {
        throw std::runtime_error("cannot convert \"" + text + "\" to i64");
    }
    return value;
}

inline double parse_float(const std::string& text) {
    const auto view = trimmed(text);
    if (view == "inf" || view == "+inf") return std::numeric_limits<double>::infinity();
    if (view == "-inf") return -std::numeric_limits<double>::infinity();
    if (view == "nan") return std::numeric_limits<double>::quiet_NaN();
    double value{};
    const auto begin = view.data() + (view.size() > 0 && view.front() == '+');
    const auto result = std::from_chars(begin, view.data() + view.size(), value);
    if (view.empty() || result.ptr != view.data() + view.size() || result.ec != std::errc{}) {
        throw std::runtime_error("cannot convert \"" + text + "\" to f64");
    }
    return value;
}

inline std::string utf8_encode(Int code) {
    if (code < 0 || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
        throw std::runtime_error("chr expects a Unicode scalar value");
    }
    std::string out;
    const auto c = static_cast<std::uint32_t>(code);
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
    return out;
}

inline Int utf8_decode(const std::string& text) {
    if (text.empty()) throw std::runtime_error("ord expects a non-empty string");
    const auto byte = [&](std::size_t i) -> std::uint32_t {
        if (i >= text.size() || (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
            throw std::runtime_error("ord expects valid UTF-8");
        }
        return static_cast<unsigned char>(text[i]) & 0x3F;
    };
    const auto lead = static_cast<unsigned char>(text[0]);
    if (lead < 0x80) return lead;
    if ((lead & 0xE0) == 0xC0) return ((lead & 0x1F) << 6) | byte(1);
    if ((lead & 0xF0) == 0xE0) return ((lead & 0x0F) << 12) | (byte(1) << 6) | byte(2);
    if ((lead & 0xF8) == 0xF0) return ((lead & 0x07) << 18) | (byte(1) << 12) | (byte(2) << 6) | byte(3);
    throw std::runtime_error("ord expects valid UTF-8");
}

// With at most INT64_MAX signed 64-bit terms, the exact sum fits in a signed 128-bit value.
struct WideSum {
    std::uint64_t lower = 0;
    std::uint64_t upper = 0;

    void add(WideSum term) {
        const auto previous = lower;
        lower += term.lower;
        upper += term.upper + (lower < previous);
    }
    void add(Int term) {
        add({static_cast<std::uint64_t>(term), term < 0 ? UINT64_MAX : 0});
    }
    Int narrow() const {
        constexpr auto sign = std::uint64_t{1} << 63;
        if (upper == 0 && lower < sign) return static_cast<Int>(lower);
        if (upper == UINT64_MAX && lower >= sign) return -1 - static_cast<Int>(~lower);
        throw std::overflow_error("i64 overflow in parallel_sum result");
    }
};

struct ExitRequest {
    int code;
};


// Container helpers shared by the VM and generated native code.
inline Value list_at(const Value& list, Int index) {
    const auto& items = list_of(list);
    return items.get(checked_index(index, items.size(), "list"));
}
inline Int list_get_int(const Value& list, Int index) {
    const auto& items = list_of(list);
    const auto i = checked_index(index, items.size(), "list");
    return items.elem == ListObj::Elem::integer ? items.ints[i] : items.values[i].i;
}
inline double list_get_float(const Value& list, Int index) {
    const auto& items = list_of(list);
    const auto i = checked_index(index, items.size(), "list");
    return items.elem == ListObj::Elem::floating ? items.floats[i] : items.values[i].f;
}
inline bool list_get_bool(const Value& list, Int index) {
    const auto& items = list_of(list);
    const auto i = checked_index(index, items.size(), "list");
    return items.elem == ListObj::Elem::boolean ? items.bools[i] != 0 : items.values[i].b;
}
inline void list_set(const Value& list, Int index, Value value) {
    auto& items = list_of(list);
    items.set(checked_index(index, items.size(), "list"), std::move(value));
}
inline void list_set_int(const Value& list, Int index, Int value) {
    auto& items = list_of(list);
    const auto i = checked_index(index, items.size(), "list");
    if (items.elem == ListObj::Elem::integer) items.ints[i] = value; else items.values[i].i = value;
}
inline void list_set_float(const Value& list, Int index, double value) {
    auto& items = list_of(list);
    const auto i = checked_index(index, items.size(), "list");
    if (items.elem == ListObj::Elem::floating) items.floats[i] = value; else items.values[i].f = value;
}
inline void list_set_bool(const Value& list, Int index, bool value) {
    auto& items = list_of(list);
    const auto i = checked_index(index, items.size(), "list");
    if (items.elem == ListObj::Elem::boolean) items.bools[i] = value; else items.values[i].b = value;
}
inline void list_push(const Value& list, Value value) { list_of(list).push(std::move(value)); }
inline Value list_pop(const Value& list) { return list_of(list).pop(); }
inline Value text_at(const Value& text, Int index) {
    const auto& s = text_of(text);
    return make_text(std::string(1, s[checked_index(index, s.size(), "string")]));
}
struct Context;
inline const Value& dict_at(const Value& dict, const Value& key, const Context& ctx);
inline Value slice_value(const Value& container, Int lo, Int hi) {
    if (container.tag == Tag::list) {
        const auto& items = list_of)tacqor_runtime",
R"tacqor_runtime((container);
        const auto [a, b] = clamp_slice(lo, hi, items.size());
        return {Tag::list, items.slice(a, b)};
    }
    const auto& text = text_of(container);
    const auto [a, b] = clamp_slice(lo, hi, text.size());
    return make_text(text.substr(a, b - a));
}
inline Int length_of(const Value& value) {
    if (value.tag == Tag::text) return checked_size(text_of(value).size());
    if (value.tag == Tag::list) return checked_size(list_of(value).size());
    return checked_size(dict_of(value).entries.size());
}
inline bool contains_value(const Value& item, const Value& container) {
    if (container.tag == Tag::list) {
        const auto& items = list_of(container);
        if (items.elem == ListObj::Elem::integer) return std::find(items.ints.begin(), items.ints.end(), item.i) != items.ints.end();
        for (std::size_t i = 0; i < items.size(); ++i) if (equal(items.get(i), item)) return true;
        return false;
    }
    if (container.tag == Tag::text) return text_of(container).find(text_of(item)) != std::string::npos;
    return dict_of(container).find(item) != nullptr;
}
inline Value concat_text(const Value& left, const Value& right) {
    const auto& a = text_of(left);
    const auto& b = text_of(right);
    if (a.size() + b.size() > text_limit) throw std::runtime_error("text exceeds the size limit");
    std::string joined;
    joined.reserve(a.size() + b.size());
    joined += a;
    joined += b;
    return make_text(std::move(joined));
}

// Everything a program needs from its environment while it runs.
struct Context {
    const std::vector<std::string>& arguments;
    std::ostream& output;
    const std::vector<StructInfo>& structs;
    std::mutex output_lock;
    std::mutex input_lock;
};

inline const Value& dict_at(const Value& dict, const Value& key, const Context& ctx) {
    const auto* found = dict_of(dict).find(key);
    if (!found) {
        std::string shown;
        format(shown, key, ctx.structs, true);
        throw std::runtime_error("missing dictionary key " + shown);
    }
    return *found;
}

// Argument counts, indexed by Builtin.
constexpr int builtin_arity[] = {
    1, 0, 1, 1, 2, 0, 1, 1, 1,
    1, 2, 2, 1, 2, 1, 1, 1,
    2, 1, 3, 2, 2, 1, 1, 1,
    1, 1, 2,
    2, 2, 2, 1, 1, 1, 3, 2, 2, 1, 1,
    1, 0, 1,
    // 1.0
    1, 1, 1, 1, 1, 2, 2,
    2, 1, 1, 1,
    0, 2, 1,
    1, 1, 2, 1, 1, 1,
    1, 0, 0, 1, 1, 1, 0,
    1, 1, 2, 1, 1,
    1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1,
    1, 1, 1, 2, 2,
    // 1.1
    2, 1, 1, 0, 0,
    // 1.2
    1, 1, 2, 2, 2, 1, 1, 0, 1,
    // 1.3 — channel, channel_send, channel_recv, channel_try_recv, channel_close
    0, 2, 1, 1, 1,
};

inline std::mt19937_64& random_engine() {
    static std::mt19937_64 engine(std::random_device{}());
    return engine;
}

inline std::string value_text(const Value& value, const Context& ctx) {
    std::string out;
    format(out, value, ctx.structs, false);
    return out;
}

inline std::vector<std::string> utf8_chars(const std::string& text) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t width = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 1;
        if (i + width > text.size()) width = text.size() - i;
        out.push_back(text.substr(i, width));
        i += width;
    }
    return out;
}
static_assert(sizeof(builtin_arity) / sizeof(builtin_arity[0]) == static_cast<std::size_t>(Builtin::count),
              "builtin_arity must list every builtin");

// --- sockets (blocking TCP; enough for servers, clients, and std.http) ---------------------------
#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket invalid_socket = INVALID_SOCKET;
inline void socket_close(Socket s) { closesocket(s); }
inline std::string socket_error() { return "socket error " + std::to_string(WSAGetLastError()); }
inline void sockets_init() {
    static bool done = false;
    if (!done) { WSADATA data; WSAStartup(MAKEWORD(2, 2), &data); done = true; }
}
#else
using Socket = int;
constexpr Socket invalid_socket = -1;
inline void socket_close(Socket s) { ::close(s); }
inline std::string socket_error() { return std::strerror(errno); }
inline void sockets_init() {}
#endif

inline Value socket_fail(const std::string& what) { return make_variant(false, make_text(what + ": " + socket_error()), true); }

inline Socket socket_of(const Value& v) {
    const auto fd = int_of(v);
    if (fd < 0 || fd > 1'000'000'000) throw std::runtime_error("invalid socket handle");
    return static_cast<Socket>(fd);
}

inline Value tcp_listen(Int port) {
    sockets_init();
    if (port < 0 || port > 65535) throw std::runtime_error("port must be between 0 and 65535");
    const Socket s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == invalid_socket) return socket_fail("socket");
    int yes = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::bind(s, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) { auto r = socket_fail("bind"); socket_close(s); return r; }
    if (::listen(s, 64) != 0) { auto r = socket_fail("listen"); socket_close(s); return r; }
    return make_variant(true, static_cast<Int>(s), true);
}

inline Value tcp_accept(Socket server) {
    sockaddr_in peer{};
    socklen_t size = sizeof peer;
    const Socket client = ::accept(server, reinterpret_cast<sockaddr*>(&peer), &size);
    if (client == invalid_socket) return socket_fail("accept");
    return make_variant(true, static_cast<Int>(client), true);
}

inline Value tcp_connect(const std::string& host, Int port) {
    sockets_init();
    if (port < 0 || port > 65535) throw std::runtime_error("port must be between 0 and 65535");
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    const auto status = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found);
    if (status != 0 || !found) return make_variant(false, make_text("could not resolve " + host), true);
    Socket s = invalid_socket;
    for (addrinfo* it = found; it; it = it->ai_next) {
        s = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (s == invalid_socket) continue;
        if (::connect(s, it->ai_addr, static_cast<socklen_t>(it->ai_addrlen)) == 0) break;
        socket_close(s);
        s = invalid_socket;
    }
    ::freeaddrinfo(found);
    if (s == invalid_socket) return make_variant(false, make_text("could not connect to " + host + ":" + std::to_string(port)), true);
    return make_variant(true, static_cast<Int>(s), true);
}

inline Value tcp_read(Socket s, Int limit) {
    if (limit <= 0 || static_cast<std::uint64_t>(limit) > text_limit) throw std::runtime_error("tcp_read expects a positive byte limit");
    std::string buffer(static_cast<std::size_t>(std::min<Int>(limit, 1 << 20)), '\0');
    const auto received = ::recv(s, &buffer[0], static_cast<int>(buffer.size()), 0);
    if (received < 0) return socket_fail("recv");
    buffer.resize(static_cast<std::size_t>(received));
    return make_variant(true, make_text(std::move(buffer)), true);
}

inline Value tcp_write(Socket s, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto chunk = ::send(s, data.data() + sent, static_cast<int>(std::min<std::size_t>(data.size() - sent, 1 << 20)), 0);
        if (chunk < 0) return socket_fail("send");
        sent += static_cast<std::size_t>(chunk);
    }
    return make_variant(true, static_cast<Int>(sent), true);
}

// --- value channels ---------------------------------------------------------
// A channel is a thread-safe string queue used to hand messages between workers (see
// `parallel_run`). Messages cross as string copies — nothing on the Tacqor heap is shared
// between threads — so the non-atomic reference counting stays sound; structure a message
// with `std.json` when it is more than text. A channel is named by a small integer handle
// (a scalar, so a `parallel_run` task may capture it), and the handles index a process-wide
// registry. Channels live until the program ends.
struct Channel {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::string> queue;
    bool closed = false;
};

inline std::mutex& channel_table_mutex() {
    static std::mutex m;
    return m;
}
inline std::vector<std::shared_ptr<Channel>>& channel_table() {
    static std::vector<std::shared_ptr<Channel>> table;
    return table;
}
inline std::shared_ptr<Channel> channel_at(Int handle) {
    std::lock_guard<std::mutex> guard(channel_table_mutex());
    auto& table = channel_table();
    if (handle < 0 || static_cast<std::size_t>(handle) >= table.size()) throw std::runtime_error("invalid channel handle");
    return table[static_cast<std::size_t>(handle)];
}
inline Int channel_make() {
    std::lock_guard<std::mutex> guard(channel_table_mutex());
    auto& table = channel_table();
    table.push_back(std::make_shared<Channel>());
    return static_cast<Int>(table.size() - 1);
}
inline void channel_send_message(Int handle, const std::string& message) {
    const auto channel = channel_at(handle);
    {
        std::lock_guard<std::mutex> guard(channel->mutex);
        if (channel->closed) throw std::runtime_error("channel_send on a closed channel");
        channel->queue.push_back(message);  // a copy; the receiver gets its own string
    }
    channel->ready.notify_one();
}
// Blocks until a message arrives (Some) or the channel is closed and drained (None).
inline Value channel_receive(Int handle) {
    const auto channel = channel_at(handle);
    std::unique_lock<std::mutex> lock(channel->mutex);
    channel->ready.wait(lock, [&] { return !channel->queue.empty() || channel->closed; });
    if (channel->queue.empty()) return make_variant(false, Value());  // closed and drained -> None
    std::string message = std::move(channel->queue.front());
    channel->queue.pop_front();
    return make_variant(true, make_text(std::move(message)));
}
// Never blocks: Some if a message is waiting, None otherwise.
inline Value channel_try_receive(Int handle) {
    const auto channel = channel_at(handle);
    std::lock_guard<std::mutex> guard(channel->mutex);
    if (channel->queue.empty()) return make_variant(false, Value());
    std::string message = std::move(channel->queue.front());
    channel->queue.pop_front();
    return make_variant(true, make_text(std::move(message)));
}
inline void channel_shut(Int handle) {
    const auto channel = channel_at(handle);
    {
        std::lock_guard<std::mutex> guard(channel->mutex);
        channel->closed = true;
    }
    channel->ready.notify_all();
}

inline std::string run_capture(const std::string& command) {
#if defined(_WIN32)
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = ::popen(command.c_str(), "r");
#endif
    if (!pipe) throw std::runtime_error("could not run: " + command);
    std::string out;
    char buffer[4096];
    std::size_t count;
    while ((count = std::fread(buffer, 1, sizeof buffer, pipe)) > 0) {
        if (out.size() + count > text_limit) break;
        out.append(buffer, count);
    }
#if defined(_WIN32)
    _pclose(pipe);
#else
    ::pclose(pipe);
#endif
    return out;
}

// Runs a builtin on `args` (builtin_arity[which] values) and returns its result (unit for procedures).
inline Value call_builtin(Builtin which, Value* args, Context& ctx) {
    switch (which) {
        case Builtin::print: {
            std::string line;
            format(line, args[0], ctx.structs, false);
            line += '\n';
            std::lock_guard<std::mutex> guard(ctx.output_lock);
            ctx.output << line;
   )tacqor_runtime",
R"tacqor_runtime(         if (!ctx.output) throw std::runtime_error("could not write output");
            return {};
        }
        case Builtin::argc: return checked_size(ctx.arguments.size());
        case Builtin::arg: return make_text(ctx.arguments[checked_index(int_of(args[0]), ctx.arguments.size(), "argument")]);
        case Builtin::read_text: {
            const auto& path = text_of(args[0]);
            if (path.find('\0') != std::string::npos) throw std::runtime_error("invalid file path");
            const auto file = std::filesystem::u8path(path);
            if (!std::filesystem::is_regular_file(file)) throw std::runtime_error("expected a regular file: " + path);
            std::ifstream input(file, std::ios::binary);
            if (!input) throw std::runtime_error("could not open file: " + path);
            return make_text(read_text(input, text_limit));
        }
        case Builtin::write_text: {
            const auto& path = text_of(args[0]);
            const auto& content = text_of(args[1]);
            if (path.find('\0') != std::string::npos) throw std::runtime_error("invalid file path");
            std::ofstream output(std::filesystem::u8path(path), std::ios::binary);
            if (!output) throw std::runtime_error("could not open file for writing: " + path);
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            output.close();
            if (!output) throw std::runtime_error("could not write file: " + path);
            return {};
        }
        case Builtin::input: {
            std::lock_guard<std::mutex> guard(ctx.input_lock);
            ctx.output.flush();
            std::string line;
            if (!std::getline(std::cin, line)) line.clear();
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return make_text(std::move(line));
        }
        case Builtin::to_text: {
            std::string out;
            format(out, args[0], ctx.structs, false);
            return make_text(std::move(out));
        }
        case Builtin::to_integer: {
            const Value& value = args[0];
            switch (value.tag) {
                case Tag::integer: return value.i;
                case Tag::floating: return float_to_int(value.f);
                case Tag::boolean: return Int{value.b};
                case Tag::text: return parse_int(text_of(value));
                default: throw std::runtime_error("int expects i64, f64, bool, or str");
            }
        }
        case Builtin::to_float: {
            const Value& value = args[0];
            switch (value.tag) {
                case Tag::integer: return static_cast<double>(value.i);
                case Tag::floating: return value.f;
                case Tag::text: return parse_float(text_of(value));
                default: throw std::runtime_error("float expects i64, f64, or str");
            }
        }
        case Builtin::abs: {
            const Value& value = args[0];
            if (value.tag == Tag::floating) return std::fabs(value.f);
            return int_of(value) < 0 ? arithmetic(Op::negate, value.i, 0) : value.i;
        }
        case Builtin::min: case Builtin::max: {
            const auto order = compare(args[0], args[1]);
            return (which == Builtin::min) == (order <= 0) ? args[0] : args[1];
        }
        case Builtin::sqrt: return std::sqrt(float_of(args[0]));
        case Builtin::pow: return std::pow(float_of(args[0]), float_of(args[1]));
        case Builtin::floor: return float_to_int(std::floor(float_of(args[0])));
        case Builtin::ceil: return float_to_int(std::ceil(float_of(args[0])));
        case Builtin::round: return float_to_int(std::round(float_of(args[0])));
        case Builtin::push: list_of(args[0]).push(std::move(args[1])); return {};
        case Builtin::pop: return list_of(args[0]).pop();
        case Builtin::insert: {
            auto& list = list_of(args[0]);
            list.insert(checked_index(int_of(args[1]), list.size() + 1, "list"), std::move(args[2]));
            return {};
        }
        case Builtin::remove_at: {
            auto& list = list_of(args[0]);
            return list.erase(checked_index(int_of(args[1]), list.size(), "list"));
        }
        case Builtin::index_of: {
            const auto& items = list_of(args[0]);
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (equal(items.get(i), args[1])) return static_cast<Int>(i);
            }
            return Int{-1};
        }
        case Builtin::sort: list_of(args[0]).sort(); return {};
        case Builtin::reverse: list_of(args[0]).reverse(); return {};
        case Builtin::clear: {
            if (args[0].tag == Tag::dict) {
                dict_of(args[0]).entries.clear();
                dict_of(args[0]).index.clear();
            } else {
                list_of(args[0]).clear();
            }
            return {};
        }
        case Builtin::keys: case Builtin::values: {
            const auto& entries = dict_of(args[0]).entries;
            std::vector<Value> items;
            items.reserve(entries.size());
            for (const auto& entry : entries) items.push_back(which == Builtin::keys ? entry.first : entry.second);
            return make_list(std::move(items));
        }
        case Builtin::remove_key: return dict_of(args[0]).remove(args[1]);
        case Builtin::split: {
            const auto& text = text_of(args[0]);
            const auto& separator = text_of(args[1]);
            if (separator.empty()) throw std::runtime_error("split expects a non-empty separator");
            std::vector<Value> parts;
            std::size_t start = 0;
            for (;;) {
                const auto found = text.find(separator, start);
                parts.push_back(make_text(text.substr(start, found == std::string::npos ? std::string::npos : found - start)));
                if (found == std::string::npos) break;
                start = found + separator.size();
            }
            return make_list(std::move(parts));
        }
        case Builtin::join: {
            const auto& items = list_of(args[0]);
            const auto& separator = text_of(args[1]);
            std::string out;
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i) out += separator;
                out += text_of(items.get(i));
                if (out.size() > text_limit) throw std::runtime_error("text exceeds the size limit");
            }
            return make_text(std::move(out));
        }
        case Builtin::find: {
            const auto found = text_of(args[0]).find(text_of(args[1]));
            return found == std::string::npos ? Int{-1} : static_cast<Int>(found);
        }
        case Builtin::upper: case Builtin::lower: {
            auto text = text_of(args[0]);
            for (auto& ch : text) {
                if (which == Builtin::upper && ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 32);
                if (which == Builtin::lower && ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 32);
            }
            return make_text(std::move(text));
        }
        case Builtin::strip: return make_text(std::string(trimmed(text_of(args[0]))));
        case Builtin::replace: {
            const auto& text = text_of(args[0]);
            const auto& what = text_of(args[1]);
            const auto& with = text_of(args[2]);
            std::string out;
            if (what.empty()) {
                out = text;
            } else {
                std::size_t start = 0;
                for (;;) {
                    const auto found = text.find(what, start);
                    out.append(text, start, found == std::string::npos ? std::string::npos : found - start);
                    if (found == std::string::npos) break;
                    out += with;
                    start = found + what.size();
                    if (out.size() > text_limit) throw std::runtime_error("text exceeds the size limit");
                }
            }
            return make_text(std::move(out));
        }
        case Builtin::starts_with: case Builtin::ends_with: {
            const auto& text = text_of(args[0]);
            const auto& part = text_of(args[1]);
            if (part.size() > text.size()) return false;
            return which == Builtin::starts_with ? text.compare(0, part.size(), part) == 0
                                                 : text.compare(text.size() - part.size(), part.size(), part) == 0;
        }
        case Builtin::chr: return make_text(utf8_encode(int_of(args[0])));
        case Builtin::ord: return utf8_decode(text_of(args[0]));
        case Builtin::exit: {
            const auto code = int_of(args[0]);
            if (code < 0 || code > 255) throw std::runtime_error("exit expects a code from 0 to 255");
            throw ExitRequest{static_cast<int>(code)};
        }
        case Builtin::time_ms: {
            const auto now = std::chrono::steady_clock::now().time_since_epoch();
            return static_cast<Int>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
        }
        case Builtin::fail: throw std::runtime_error(text_of(args[0]));
        // --- 1.0 ---
        case Builtin::is_some: case Builtin::is_ok: return variant_of(args[0]).which != 0;
        case Builtin::is_none: case Builtin::is_err: return variant_of(args[0]).which == 0;
        case Builtin::unwrap: {
            auto& variant = variant_of(args[0]);
            if (variant.which) return variant.payload;
            if (variant.payload.tag == Tag::unit) throw std::runtime_error("unwrap on None");
            throw std::runtime_error("unwrap on Err: " + value_text(variant.payload, ctx));
        }
        case Builtin::unwrap_or: {
            auto& variant = variant_of(args[0]);
            return variant.which ? variant.payload : std::move(args[1]);
        }
        case Builtin::expect: {
            auto& variant = variant_of(args[0]);
            if (variant.which) return variant.payload;
            throw std::runtime_error(text_of(args[1]));
        }
        case Builtin::lookup: {
            const auto* found = dict_of(args[0]).find(args[1]);
            return found ? make_variant(true, *found) : make_variant(false, Value());
        }
        case Builtin::parse_int: {
            try { return make_variant(true, parse_int(text_of(args[0]))); } catch (const std::runtime_error&) { return make_variant(false, Value()); }
        }
        case Builtin::parse_float: {
            try { return make_variant(true, parse_float(text_of(args[0]))); } catch (const std::runtime_error&) { return make_variant(false, Value()); }
        }
        case Builtin::env: {
            const char* value = std::getenv(text_of(args[0]).c_str());
            return value ? make_variant(true, make_text(value)) : make_variant(false, Value());
        }
        case Builtin::random_float: return std::uniform_real_distribution<double>(0.0, 1.0)(random_engine());
        case Builtin::random_int: {
            const auto lo = int_of(args[0]), hi = int_of(args[1]);
            if (lo > hi) throw std::runtime_error("random_int expects low <= high");
            return std::uniform_int_distribution<Int>(lo, hi)(random_engine());
        }
        case Builtin::seed: random_engine().seed(static_cast<std::uint64_t>(int_of(args[0]))); return {};
        case Builtin::file_exists: {
            std::error_code ignored;
            return std::filesystem::exists(std::filesystem::u8path(text_of(args[0])), ignored);
        }
        case Builtin::read_lines: {
            const auto& path = text_of(args[0]);
            std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
            if (!input) throw std::runtime_error("could not open file: " + path);
            const auto content = read_text(input, text_limit);
            std::vector<Value> lines;
            std::siz)tacqor_runtime",
R"tacqor_runtime(e_t start = 0;
            while (start < content.size()) {
                auto end = content.find('\n', start);
                if (end == std::string::npos) end = content.size();
                auto line = content.substr(start, end - start);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(make_text(std::move(line)));
                start = end + 1;
            }
            return make_list(std::move(lines), ListObj::Elem::value);
        }
        case Builtin::append_text: {
            const auto& path = text_of(args[0]);
            std::ofstream output(std::filesystem::u8path(path), std::ios::binary | std::ios::app);
            if (!output) throw std::runtime_error("could not open file for appending: " + path);
            const auto& content = text_of(args[1]);
            output.write(content.data(), static_cast<std::streamsize>(content.size()));
            if (!output) throw std::runtime_error("could not write file: " + path);
            return {};
        }
        case Builtin::delete_file: {
            std::error_code ignored;
            return std::filesystem::remove(std::filesystem::u8path(text_of(args[0])), ignored);
        }
        case Builtin::list_dir: {
            const auto& path = text_of(args[0]);
            std::error_code error;
            std::vector<Value> names;
            for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::u8path(path), error)) {
                names.push_back(make_text(entry.path().filename().u8string()));
            }
            if (error) throw std::runtime_error("could not list directory: " + path);
            std::sort(names.begin(), names.end(), [](const Value& a, const Value& b) { return text_of(a) < text_of(b); });
            return make_list(std::move(names), ListObj::Elem::value);
        }
        case Builtin::make_dir: {
            std::error_code ignored;
            return std::filesystem::create_directories(std::filesystem::u8path(text_of(args[0])), ignored);
        }
        case Builtin::sleep_ms: {
            const auto ms = int_of(args[0]);
            if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            return {};
        }
        case Builtin::now_text: {
            const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            char buffer[32];
            std::tm parts{};
#if defined(_WIN32)
            gmtime_s(&parts, &now);
#else
            gmtime_r(&now, &parts);
#endif
            std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &parts);
            return make_text(buffer);
        }
        case Builtin::args: {
            std::vector<Value> items;
            for (const auto& argument : ctx.arguments) items.push_back(make_text(argument));
            return make_list(std::move(items), ListObj::Elem::value);
        }
        case Builtin::run: {
            ctx.output.flush();
            const int status = std::system(text_of(args[0]).c_str());
#if defined(_WIN32)
            return static_cast<Int>(status);
#else
            return static_cast<Int>(WIFEXITED(status) ? WEXITSTATUS(status) : status);
#endif
        }
        case Builtin::write: case Builtin::eprint: {
            const auto text = value_text(args[0], ctx);
            std::lock_guard<std::mutex> guard(ctx.output_lock);
            if (which == Builtin::write) ctx.output << text;
            else {
                ctx.output.flush();
                std::cerr << text << '\n';
            }
            return {};
        }
        case Builtin::flush: ctx.output.flush(); return {};
        case Builtin::chars: {
            std::vector<Value> items;
            for (auto& piece : utf8_chars(text_of(args[0]))) items.push_back(make_text(std::move(piece)));
            return make_list(std::move(items), ListObj::Elem::value);
        }
        case Builtin::char_count: return checked_size(utf8_chars(text_of(args[0])).size());
        case Builtin::repeat: {
            const auto& text = text_of(args[0]);
            const auto count = int_of(args[1]);
            if (count < 0) throw std::runtime_error("repeat expects a non-negative count");
            if (static_cast<std::uint64_t>(count) * text.size() > text_limit) throw std::runtime_error("text exceeds the size limit");
            std::string out;
            out.reserve(text.size() * static_cast<std::size_t>(count));
            for (Int i = 0; i < count; ++i) out += text;
            return make_text(std::move(out));
        }
        case Builtin::bytes: {
            std::vector<Value> items;
            for (const unsigned char byte : text_of(args[0])) items.push_back(static_cast<Int>(byte));
            return make_list(std::move(items), ListObj::Elem::integer);
        }
        case Builtin::from_bytes: {
            const auto& list = list_of(args[0]);
            std::string out;
            for (std::size_t i = 0; i < list.size(); ++i) {
                const auto byte = int_of(list.get(i));
                if (byte < 0 || byte > 255) throw std::runtime_error("from_bytes expects values from 0 to 255");
                out += static_cast<char>(byte);
            }
            return make_text(std::move(out));
        }
        case Builtin::sin: return std::sin(float_of(args[0]));
        case Builtin::cos: return std::cos(float_of(args[0]));
        case Builtin::tan: return std::tan(float_of(args[0]));
        case Builtin::asin: return std::asin(float_of(args[0]));
        case Builtin::acos: return std::acos(float_of(args[0]));
        case Builtin::atan: return std::atan(float_of(args[0]));
        case Builtin::atan2: return std::atan2(float_of(args[0]), float_of(args[1]));
        case Builtin::exp: return std::exp(float_of(args[0]));
        case Builtin::log: return std::log(float_of(args[0]));
        case Builtin::log2: return std::log2(float_of(args[0]));
        case Builtin::log10: return std::log10(float_of(args[0]));
        case Builtin::sum: {
            const auto& list = list_of(args[0]);
            if (list.elem == ListObj::Elem::floating || (list.elem == ListObj::Elem::value && list.size() > 0 && list.get(0).tag == Tag::floating)) {
                double total = 0;
                for (std::size_t i = 0; i < list.size(); ++i) total += float_of(list.get(i));
                return total;
            }
            Int total = 0;
            for (std::size_t i = 0; i < list.size(); ++i) total = arithmetic(Op::add, total, int_of(list.get(i)));
            return total;
        }
        case Builtin::min_of: case Builtin::max_of: {
            const auto& list = list_of(args[0]);
            if (list.size() == 0) throw std::runtime_error(std::string(which == Builtin::min_of ? "min_of" : "max_of") + " of an empty list");
            Value best = list.get(0);
            for (std::size_t i = 1; i < list.size(); ++i) {
                Value candidate = list.get(i);
                const auto order = compare(candidate, best);
                if (which == Builtin::min_of ? order < 0 : order > 0) best = std::move(candidate);
            }
            return best;
        }
        case Builtin::fill: {
            const auto count = int_of(args[0]);
            if (count < 0) throw std::runtime_error("fill expects a non-negative count");
            if (static_cast<std::uint64_t>(count) > text_limit) throw std::runtime_error("list exceeds the size limit");
            auto* list = new ListObj(ListObj::elem_for(args[1].tag));
            Value holder(Tag::list, list);
            list->reserve(static_cast<std::size_t>(count));
            for (Int i = 0; i < count; ++i) list->push(args[1]);
            return holder;
        }
        case Builtin::extend: {
            auto& target = list_of(args[0]);
            const auto& source = list_of(args[1]);
            const auto count = source.size();
            for (std::size_t i = 0; i < count; ++i) target.push(source.get(i));
            return {};
        }
        case Builtin::fixed: {
            const auto digits = int_of(args[1]);
            if (digits < 0 || digits > 40) throw std::runtime_error("fixed expects 0 to 40 digits");
            char buffer[128];
            std::snprintf(buffer, sizeof buffer, "%.*f", static_cast<int>(digits), float_of(args[0]));
            return make_text(buffer);
        }
        case Builtin::to_hex: {
            const auto value = int_of(args[0]);
            char buffer[32];
            std::snprintf(buffer, sizeof buffer, value < 0 ? "-%llx" : "%llx",
                          static_cast<unsigned long long>(value < 0 ? -(value + 1) + 1ULL : value));
            return make_text(buffer);
        }
        case Builtin::from_hex: {
            auto view = trimmed(text_of(args[0]));
            bool negative = false;
            if (!view.empty() && (view.front() == '-' || view.front() == '+')) { negative = view.front() == '-'; view.remove_prefix(1); }
            if (view.size() > 2 && view[0] == '0' && (view[1] == 'x' || view[1] == 'X')) view.remove_prefix(2);
            if (view.empty() || view.size() > 16) return make_variant(false, Value());
            std::uint64_t value = 0;
            for (const char ch : view) {
                int digit;
                if (ch >= '0' && ch <= '9') digit = ch - '0';
                else if (ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
                else if (ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
                else return make_variant(false, Value());
                value = value * 16 + static_cast<std::uint64_t>(digit);
            }
            if (value > static_cast<std::uint64_t>(std::numeric_limits<Int>::max()) + (negative ? 1 : 0)) return make_variant(false, Value());
            return make_variant(true, negative ? static_cast<Int>(0 - value) : static_cast<Int>(value));
        }
        case Builtin::gc_collect: return static_cast<Int>(collect_cycles());
        case Builtin::gc_count: return static_cast<Int>(gc_state().tracked);
        case Builtin::tcp_listen: return tcp_listen(int_of(args[0]));
        case Builtin::tcp_accept: return tcp_accept(socket_of(args[0]));
        case Builtin::tcp_connect: return tcp_connect(text_of(args[0]), int_of(args[1]));
        case Builtin::tcp_read: return tcp_read(socket_of(args[0]), int_of(args[1]));
        case Builtin::tcp_write: return tcp_write(socket_of(args[0]), text_of(args[1]));
        case Builtin::tcp_close: socket_close(socket_of(args[0])); return {};
        case Builtin::channel: return channel_make();
        case Builtin::channel_send: channel_send_message(int_of(args[0]), text_of(args[1])); return {};
        case Builtin::channel_recv: return channel_receive(int_of(args[0]));
        case Builtin::channel_try_recv: return channel_try_receive(int_of(args[0]));
        case Builtin::channel_close: channel_shut(int_of(args[0])); return {};
        case Builtin::run_capture: ctx.output.flush(); return make_text(run_capture(text_of(args[0])));
        case Builtin::unix_time:
            return static_cast<Int>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        case Builtin::hash: {
            // 64-bit FNV-1a, folded into the non-negative range so it can be used as a list index.
            std::uint64_t h = 14695981039346656037ULL;
            for (const unsigned char ch : text_of(args[0])) { h ^= ch; h *= 1099511628211ULL; }
            return static_cast<Int>(h & 0x7FFFFFFFFFFFFFFFULL);
        }
        case Builtin::count: break;
    }
    throw std::runtime_error("invalid builtin");
}

// Mappers passed to parallel_sum/parallel_map run on several threads at once, so they may only
// capture values that need no reference counting.
inline void parallel_check(const Value& mapper) {
    for (const auto& captured : closure_of(mapper).captures) {
        if (captured.heap()) th)tacqor_runtime",
R"tacqor_runtime(row std::runtime_error("a parallel mapper may only capture i64, f64, or bool values");
    }
}

// Deterministic parallel reduction shared by the VM and native programs. `mapper(i)` must be pure.
template <typename Mapper>
Int parallel_reduce(Int count, Int requested, Mapper mapper) {
    if (requested < 1 || requested > 64) throw std::runtime_error("parallel_sum needs 1 to 64 workers");
    if (count <= 0) return 0;
    const auto workers = std::min(count, requested);
    const auto width = count / workers;
    const auto remainder = count % workers;
    struct Job { Int first, end; WideSum total; std::exception_ptr error; };
    std::vector<Job> jobs;
    jobs.reserve(static_cast<std::size_t>(workers));
    for (Int i = 0, start = 0; i < workers; ++i) {
        const auto stop = start + width + (i < remainder);
        jobs.push_back({start, stop, {}, {}});
        start = stop;
    }
    const auto work = [&](Job& job) {
        try {
            for (auto i = job.first; i < job.end; ++i) job.total.add(mapper(i));
        } catch (...) {
            job.error = std::current_exception();
        }
    };
    struct Workers {
        std::vector<std::thread> threads;
        ~Workers() { for (auto& thread : threads) if (thread.joinable()) thread.join(); }
    };
    {
        gc_state().parallel = true;
        Workers pool;
        pool.threads.reserve(jobs.size() - 1);
        for (std::size_t i = 1; i < jobs.size(); ++i) pool.threads.emplace_back(work, std::ref(jobs[i]));
        work(jobs[0]);
    }
    gc_state().parallel = false;
    WideSum result;
    for (const auto& job : jobs) {
        if (job.error) std::rethrow_exception(job.error);
        result.add(job.total);
    }
    return result.narrow();
}

// parallel_map: every index's result in order. Each slot is written by exactly one thread.
template <typename Mapper>
std::vector<Int> parallel_collect(Int count, Int requested, Mapper mapper) {
    if (requested < 1 || requested > 64) throw std::runtime_error("parallel_map needs 1 to 64 workers");
    if (count <= 0) return {};
    if (static_cast<std::uint64_t>(count) > text_limit) throw std::runtime_error("list exceeds the size limit");
    std::vector<Int> results(static_cast<std::size_t>(count));
    const auto workers = std::min(count, requested);
    const auto width = count / workers;
    const auto remainder = count % workers;
    struct Job { Int first, end; std::exception_ptr error; };
    std::vector<Job> jobs;
    for (Int i = 0, start = 0; i < workers; ++i) {
        const auto stop = start + width + (i < remainder);
        jobs.push_back({start, stop, {}});
        start = stop;
    }
    const auto work = [&](Job& job) {
        try {
            for (auto i = job.first; i < job.end; ++i) results[static_cast<std::size_t>(i)] = mapper(i);
        } catch (...) {
            job.error = std::current_exception();
        }
    };
    struct Workers {
        std::vector<std::thread> threads;
        ~Workers() { for (auto& thread : threads) if (thread.joinable()) thread.join(); }
    };
    {
        gc_state().parallel = true;
        Workers pool;
        for (std::size_t i = 1; i < jobs.size(); ++i) pool.threads.emplace_back(work, std::ref(jobs[i]));
        work(jobs[0]);
    }
    gc_state().parallel = false;
    for (const auto& job : jobs) if (job.error) std::rethrow_exception(job.error);
    return results;
}

}  // namespace tacqor
)tacqor_runtime",
