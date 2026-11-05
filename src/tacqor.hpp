#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace tacqor {

constexpr std::string_view version = "1.4.0";
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
    // 1.4 — opt-in unchecked arithmetic and SIMD-friendly bulk numeric ops
    wrapping_add, wrapping_sub, wrapping_mul,
    dot, vadd, vmul, vscale,
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
