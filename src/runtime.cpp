#include "tacqor.hpp"
#include "values.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <unordered_map>
#include <utility>

namespace tacqor {

namespace {

struct Shared {
    const Program& program;
    Context context;
    std::vector<Value> texts;
};

// ---------------------------------------------------------------------------
// The virtual machine: one contiguous value stack, frames are offsets into it.
// ---------------------------------------------------------------------------

class VM {
    Shared& shared;
    const Program& program;
    Value* base;        // storage start
    Value* sp;          // one past the top value
    Value* limit;       // storage end

    struct Frame {
        const Function* function;
        const Instruction* ip;
        Value* locals;
    };
    std::vector<Frame> frames;

    template <typename T>
    void push(T&& value) {
        if (!TACQOR_LIKELY(sp != limit)) fail("value stack limit exceeded");
        new (sp++) Value(std::forward<T>(value));
    }
    Value pop() {
        Value value(std::move(*--sp));
        return value;  // the moved-from slot holds unit, so no destructor call is needed
    }
    Value& top() { return sp[-1]; }
    void drop(std::size_t count) {
        while (count--) (--sp)->~Value();
    }

    void enter(std::uint32_t id, std::size_t argument_count) {
        if (frames.size() >= frame_limit) throw std::runtime_error("function nesting exceeds the runtime limit");
        const auto& function = program.functions[id];
        if (argument_count != function.params.size()) throw std::runtime_error("invalid bytecode call");
        Value* locals = sp - argument_count;
        if (static_cast<std::size_t>(limit - locals) < function.locals + 64) {
            throw std::runtime_error("value stack limit exceeded");
        }
        for (std::size_t i = argument_count; i < function.locals; ++i) new (sp++) Value();
        frames.push_back({&function, function.code.data(), locals});
    }

    void execute(std::size_t stop_depth);
    // Each worker thread lazily creates its own VM (its own value stack) and reuses it for every
    // index of one parallel operation. The generation number keeps stale thread_local pointers from
    // an earlier call from being reused.
    template <typename Body>
    auto with_workers(Body body) {
        static std::atomic<std::uint64_t> generations{0};
        const auto generation = ++generations;
        std::mutex lock;
        std::vector<std::unique_ptr<VM>> workers;
        const auto worker_vm = [&]() -> VM& {
            thread_local VM* worker = nullptr;
            thread_local std::uint64_t owner = 0;
            if (owner != generation) {
                std::lock_guard<std::mutex> guard(lock);
                workers.push_back(std::make_unique<VM>(shared, stack_limit / 8));
                worker = workers.back().get();
                owner = generation;
            }
            return *worker;
        };
        return body(worker_vm);
    }
    Int reduce(Int count, Int requested, std::uint32_t target) {
        return with_workers([&](auto& worker_vm) {
            return parallel_reduce(count, requested, [&](Int i) { return int_of(worker_vm().invoke(target, {Value(i)})); });
        });
    }
    // parallel_sum / parallel_map with a function value: captures travel with every call.
    Value parallel_closure(Int count, Int requested, const Value& mapper, bool collect) {
        parallel_check(mapper);
        const auto& closure = closure_of(mapper);
        const auto target = closure.function;
        const auto call = [&](VM& vm, Int i) {
            std::vector<Value> args;
            args.reserve(1 + closure.captures.size());
            args.push_back(Value(i));
            for (const auto& captured : closure.captures) args.push_back(captured);
            return int_of(vm.invoke(target, std::move(args)));
        };
        if (!collect) {
            return with_workers([&](auto& worker_vm) {
                return Value(parallel_reduce(count, requested, [&](Int i) { return call(worker_vm(), i); }));
            });
        }
        auto results = with_workers([&](auto& worker_vm) {
            return parallel_collect(count, requested, [&](Int i) { return call(worker_vm(), i); });
        });
        auto* list = new ListObj(ListObj::Elem::integer);
        Value holder(Tag::list, list);
        list->ints = std::move(results);
        return holder;
    }

public:
    VM(Shared& context, std::size_t capacity)
        : shared(context), program(context.program), frames() {
        base = static_cast<Value*>(::operator new(capacity * sizeof(Value)));
        sp = base;
        limit = base + capacity;
    }
    ~VM() {
        while (sp != base) (--sp)->~Value();
        ::operator delete(base);
    }
    VM(const VM&) = delete;
    VM& operator=(const VM&) = delete;

    Value invoke(std::uint32_t id, std::vector<Value> args) {
        const auto depth = frames.size();
        for (auto& arg : args) push(std::move(arg));
        enter(id, args.size());
        execute(depth);
        return pop();
    }

    std::string traceback() const {
        std::string out;
        std::size_t shown = 0;
        for (auto it = frames.rbegin(); it != frames.rend(); ++it, ++shown) {
            if (shown == 12 && frames.size() > 16) {
                out += "\n  ... " + std::to_string(frames.size() - 14) + " more frame(s)";
                it = frames.rbegin() + static_cast<std::ptrdiff_t>(frames.size() - 2);
            }
            const auto* ip = it->ip;
            const auto& code = it->function->code;
            const auto line = ip > code.data() ? ip[-1].at.line : code.front().at.line;
            out += "\n  in " + it->function->name + " (line " + std::to_string(line) + ")";
        }
        return out;
    }
};

void VM::execute(std::size_t stop_depth) {
    // Hot registers: the instruction pointer, the code base, and the locals base of the current frame.
    const Instruction* ip = frames.back().ip;
    const Instruction* code = frames.back().function->code.data();
    Value* locals = frames.back().locals;
    const auto reload = [&] {
        ip = frames.back().ip;
        code = frames.back().function->code.data();
        locals = frames.back().locals;
    };
    const auto compare_values = [](Op op, const Value& a, const Value& b) -> bool {
        if (op == Op::equal) return equal(a, b);
        if (op == Op::not_equal) return !equal(a, b);
        if (a.tag == Tag::integer && b.tag == Tag::integer) {
            return op == Op::less ? a.i < b.i : op == Op::less_equal ? a.i <= b.i
                 : op == Op::greater ? a.i > b.i : a.i >= b.i;
        }
        const auto order = compare(a, b);
        return op == Op::less ? order < 0 : op == Op::less_equal ? order <= 0
             : op == Op::greater ? order > 0 : order >= 0;
    };
    try {
#ifdef TACQOR_THREADED
        // Threaded dispatch: one indirect jump per instruction. The table order must match enum Op.
        static const void* const table[] = { &&op_push_integer, &&op_push_float, &&op_push_boolean, &&op_push_text, &&op_push_unit, &&op_load, &&op_store, &&op_discard, &&op_make_list, &&op_make_dict, &&op_make_object, &&op_get_field, &&op_set_field, &&op_index, &&op_set_index, &&op_slice, &&op_length, &&op_contains, &&op_add, &&op_subtract, &&op_multiply, &&op_divide, &&op_modulo, &&op_negate, &&op_fadd, &&op_fsubtract, &&op_fmultiply, &&op_fdivide, &&op_fmodulo, &&op_fnegate, &&op_concat, &&op_equal, &&op_not_equal, &&op_less, &&op_less_equal, &&op_greater, &&op_greater_equal, &&op_invert, &&op_jump, &&op_jump_false, &&op_jump_true, &&op_jump_unless, &&op_load2, &&op_load_integer, &&op_add_store, &&op_subtract_store, &&op_multiply_store, &&op_jump_unless_locals, &&op_loop_init, &&op_loop_next, &&op_iter_next, &&op_call, &&op_reduce, &&op_builtin, &&op_return_value, &&op_finish, &&op_push_function, &&op_make_closure, &&op_call_value, &&op_make_variant, &&op_variant_which, &&op_variant_payload, &&op_call_trait, &&op_parallel, &&op_bit_and, &&op_bit_or, &&op_bit_xor, &&op_shift_left, &&op_shift_right, &&op_bit_not, &&op_try_call };
        static_assert(sizeof(table) / sizeof(table[0]) == static_cast<std::size_t>(Op::count), "every opcode needs a dispatch label");
#define CASE(name) op_##name:
#define DISPATCH() do { ins_ptr = ip++; goto *table[static_cast<unsigned char>(ins_ptr->op)]; } while (0)
        const Instruction* ins_ptr = nullptr;
#define ins (*ins_ptr)
        DISPATCH();
#else
#define CASE(name) case Op::name:
#define DISPATCH() break
        for (;;) {
            const Instruction& ins = *ip++;
            switch (ins.op) {
#endif
                CASE(push_integer) push(ins.operand); DISPATCH();
                CASE(push_float) {
                    double value;
                    std::memcpy(&value, &ins.operand, sizeof value);
                    push(value);
                    DISPATCH();
                }
                CASE(push_boolean) push(ins.operand != 0); DISPATCH();
                CASE(push_text) push(shared.texts[static_cast<std::size_t>(ins.operand)]); DISPATCH();
                CASE(push_unit) push(Value()); DISPATCH();
                CASE(load) {
                    const Value& value = locals[ins.operand];
                    if (!TACQOR_LIKELY(value.tag != Tag::unit)) fail("uninitialized binding");
                    push(value);
                    DISPATCH();
                }
                CASE(store) locals[ins.operand] = pop(); DISPATCH();
                CASE(discard) drop(1); DISPATCH();
                CASE(make_list) {
                    const auto count = static_cast<std::size_t>(ins.operand & 0xFFFFFFFF);
                    std::vector<Value> items;
                    items.reserve(count);
                    for (Value* it = sp - count; it != sp; ++it) items.push_back(std::move(*it));
                    drop(count);
                    push(make_list(std::move(items), elem_for_kind(program.types[static_cast<std::size_t>(ins.operand >> 32)].kind == Kind::list
                        ? program.types[program.types[static_cast<std::size_t>(ins.operand >> 32)].first].kind : Kind::unit)));
                    DISPATCH();
                }
                CASE(make_dict) {
                    const auto count = static_cast<std::size_t>(ins.operand & 0xFFFFFFFF);
                    auto* dict = new DictObj;
                    Value holder(Tag::dict, dict);
                    for (Value* it = sp - 2 * count; it != sp; it += 2) dict->set(it[0], std::move(it[1]));
                    drop(2 * count);
                    push(std::move(holder));
                    DISPATCH();
                }
                CASE(make_object) {
                    const auto& info = program.structs[static_cast<std::size_t>(ins.operand)];
                    auto* object = new ObjectObj;
                    Value holder(Tag::object, object);
                    object->type = static_cast<std::uint32_t>(ins.operand);
                    const auto count = info.field_types.size();
                    object->fields.reserve(count);
                    for (Value* it = sp - count; it != sp; ++it) object->fields.push_back(std::move(*it));
                    drop(count);
                    push(std::move(holder));
                    DISPATCH();
                }
                CASE(get_field) {
                    auto& object = object_of(top());
                    Value value = object.fields.at(static_cast<std::size_t>(ins.operand));
                    drop(1);
                    push(std::move(value));
                    DISPATCH();
                }
                CASE(set_field) {
                    auto value = pop();
                    object_of(top()).fields.at(static_cast<std::size_t>(ins.operand)) = std::move(value);
                    drop(1);
                    DISPATCH();
                }
                CASE(index) {
                    const Value& key = sp[-1];
                    const Value& container = sp[-2];
                    Value result;
                    if (container.tag == Tag::list) {
                        result = list_at(container, int_of(key));
                    } else if (container.tag == Tag::text) {
                        const auto& text = text_of(container);
                        result = make_text(std::string(1, text[checked_index(int_of(key), text.size(), "string")]));
                    } else {
                        auto* found = dict_of(container).find(key);
                        if (!found) {
                            std::string shown;
                            format(shown, key, program.structs, true);
                            throw std::runtime_error("missing dictionary key " + shown);
                        }
                        result = *found;
                    }
                    drop(2);
                    push(std::move(result));
                    DISPATCH();
                }
                CASE(set_index) {
                    auto value = pop();
                    const Value& key = sp[-1];
                    const Value& container = sp[-2];
                    if (container.tag == Tag::list) {
                        list_set(container, int_of(key), std::move(value));
                    } else {
                        dict_of(container).set(key, std::move(value));
                    }
                    drop(2);
                    DISPATCH();
                }
                CASE(slice) {
                    const auto hi = int_of(pop());
                    const auto lo = int_of(pop());
                    const Value& container = sp[-1];
                    Value result = slice_value(container, lo, hi);
                    drop(1);
                    push(std::move(result));
                    DISPATCH();
                }
                CASE(length) {
                    const Value& value = top();
                    Int size;
                    if (value.tag == Tag::text) size = checked_size(text_of(value).size());
                    else if (value.tag == Tag::list) size = checked_size(list_of(value).size());
                    else size = checked_size(dict_of(value).entries.size());
                    drop(1);
                    push(size);
                    DISPATCH();
                }
                CASE(contains) {
                    // Stack order follows the source: `item in container`.
                    const Value& item = sp[-2];
                    const Value& container = sp[-1];
                    const bool found = contains_value(item, container);
                    drop(2);
                    push(found);
                    DISPATCH();
                }
                CASE(add) CASE(subtract) CASE(multiply) CASE(divide) CASE(modulo) {
                    const auto b = int_of(sp[-1]);
                    const auto a = int_of(sp[-2]);
                    Int result;
#ifdef TACQOR_BUILTIN_OVERFLOW
                    if (ins.op == Op::add) {
                        if (__builtin_add_overflow(a, b, &result)) throw std::overflow_error("i64 overflow in +");
                    } else if (ins.op == Op::subtract) {
                        if (__builtin_sub_overflow(a, b, &result)) throw std::overflow_error("i64 overflow in -");
                    } else if (ins.op == Op::multiply) {
                        if (__builtin_mul_overflow(a, b, &result)) throw std::overflow_error("i64 overflow in *");
                    } else {
                        result = arithmetic(ins.op, a, b);
                    }
#else
                    result = arithmetic(ins.op, a, b);
#endif
                    sp -= 1;
                    sp[-1].i = result;
                    DISPATCH();
                }
                CASE(negate) top().i = arithmetic(Op::negate, int_of(top()), 0); DISPATCH();
                CASE(fadd) CASE(fsubtract) CASE(fmultiply) CASE(fdivide) CASE(fmodulo) {
                    const auto b = float_of(sp[-1]);
                    const auto a = float_of(sp[-2]);
                    double result;
                    switch (ins.op) {
                        case Op::fadd: result = a + b; break;
                        case Op::fsubtract: result = a - b; break;
                        case Op::fmultiply: result = a * b; break;
                        case Op::fdivide: result = a / b; break;
                        default: result = std::fmod(a, b); if (result != 0 && ((result < 0) != (b < 0))) result += b; break;
                    }
                    sp -= 1;
                    sp[-1].f = result;
                    DISPATCH();
                }
                CASE(fnegate) top().f = -float_of(top()); DISPATCH();
                CASE(concat) {
                    const auto& right = text_of(sp[-1]);
                    const auto& left = text_of(sp[-2]);
                    if (left.size() + right.size() > text_limit) throw std::runtime_error("text exceeds the size limit");
                    std::string joined;
                    joined.reserve(left.size() + right.size());
                    joined += left;
                    joined += right;
                    drop(2);
                    push(make_text(std::move(joined)));
                    DISPATCH();
                }
                CASE(equal) CASE(not_equal) CASE(less) CASE(less_equal) CASE(greater) CASE(greater_equal) {
                    const bool result = compare_values(ins.op, sp[-2], sp[-1]);
                    drop(2);
                    push(result);
                    DISPATCH();
                }
                CASE(invert) top().b = !bool_of(top()); DISPATCH();
                CASE(jump) ip = code + ins.operand; DISPATCH();
                CASE(jump_false) {
                    const bool condition = bool_of(top());
                    drop(1);
                    if (!condition) ip = code + ins.operand;
                    DISPATCH();
                }
                CASE(jump_true) {
                    const bool condition = bool_of(top());
                    drop(1);
                    if (condition) ip = code + ins.operand;
                    DISPATCH();
                }
                CASE(load2) {
                    const Value& a = locals[ins.operand & 0xFFFFFFFF];
                    const Value& b = locals[ins.operand >> 32];
                    if (!TACQOR_LIKELY(a.tag != Tag::unit && b.tag != Tag::unit)) fail("uninitialized binding");
                    push(a);
                    push(b);
                    DISPATCH();
                }
                CASE(load_integer) {
                    const Value& a = locals[ins.operand & 0xFFFFFFFF];
                    if (!TACQOR_LIKELY(a.tag != Tag::unit)) fail("uninitialized binding");
                    push(a);
                    push(static_cast<Int>(static_cast<std::int32_t>(static_cast<std::uint32_t>(ins.operand >> 32))));
                    DISPATCH();
                }
                CASE(add_store) CASE(subtract_store) CASE(multiply_store) {
                    const auto b = int_of(sp[-1]);
                    const auto a = int_of(sp[-2]);
                    Int result;
#ifdef TACQOR_BUILTIN_OVERFLOW
                    if (ins.op == Op::add_store) {
                        if (__builtin_add_overflow(a, b, &result)) throw std::overflow_error("i64 overflow in +");
                    } else if (ins.op == Op::subtract_store) {
                        if (__builtin_sub_overflow(a, b, &result)) throw std::overflow_error("i64 overflow in -");
                    } else {
                        if (__builtin_mul_overflow(a, b, &result)) throw std::overflow_error("i64 overflow in *");
                    }
#else
                    result = arithmetic(ins.op == Op::add_store ? Op::add : ins.op == Op::subtract_store ? Op::subtract : Op::multiply, a, b);
#endif
                    sp -= 2;
                    locals[ins.operand] = Value(result);
                    DISPATCH();
                }
                CASE(jump_unless_locals) {
                    const auto bits = static_cast<std::uint64_t>(ins.operand);
                    const Value& a = locals[(bits >> 44) & 0xFFF];
                    const Value& b = locals[(bits >> 32) & 0xFFF];
                    if (!TACQOR_LIKELY(a.tag != Tag::unit && b.tag != Tag::unit)) fail("uninitialized binding");
                    if (!compare_values(static_cast<Op>(bits >> 56), a, b)) ip = code + (ins.operand & 0xFFFFFFFF);
                    DISPATCH();
                }
                CASE(jump_unless) {
                    const auto comparison = static_cast<Op>(ins.operand >> 32);
                    const bool holds = compare_values(comparison, sp[-2], sp[-1]);
                    drop(2);
                    if (!holds) ip = code + (ins.operand & 0xFFFFFFFF);
                    DISPATCH();
                }
                CASE(loop_init) {
                    // operand = (slot << 32) | exit target. locals[slot] is the index, locals[slot + 1] the limit.
                    const auto slot = static_cast<std::size_t>(ins.operand >> 32);
                    if (!(int_of(locals[slot]) < int_of(locals[slot + 1]))) ip = code + (ins.operand & 0xFFFFFFFF);
                    DISPATCH();
                }
                CASE(loop_next) {
                    const auto slot = static_cast<std::size_t>(ins.operand >> 32);
                    Value& index = locals[slot];
                    if (index.i < high) ++index.i;
                    if (index.i < locals[slot + 1].i) ip = code + (ins.operand & 0xFFFFFFFF);
                    DISPATCH();
                }
                CASE(iter_next) {
                    // operand = (slot << 32) | exit target. slot = item, slot + 1 = list, slot + 2 = cursor.
                    const auto slot = static_cast<std::size_t>(ins.operand >> 32);
                    const auto& items = list_of(locals[slot + 1]);
                    auto& cursor = locals[slot + 2].i;
                    if (static_cast<std::uint64_t>(cursor) < items.size()) {
                        locals[slot] = items.get(static_cast<std::size_t>(cursor));
                        ++cursor;
                    } else {
                        ip = code + (ins.operand & 0xFFFFFFFF);
                    }
                    DISPATCH();
                }
                CASE(call) {
                    const auto id = static_cast<std::uint32_t>(ins.operand);
                    frames.back().ip = ip;
                    enter(id, program.functions[id].params.size());
                    reload();
                    DISPATCH();
                }
                CASE(reduce) {
                    const auto workers = int_of(pop());
                    const auto count = int_of(pop());
                    frames.back().ip = ip;
                    push(reduce(count, workers, static_cast<std::uint32_t>(ins.operand)));
                    DISPATCH();
                }
                CASE(builtin) {
                    frames.back().ip = ip;
                    {
                        const auto which = static_cast<Builtin>(ins.operand);
                        const auto count = static_cast<std::size_t>(builtin_arity[static_cast<std::size_t>(which)]);
                        Value result = call_builtin(which, sp - count, shared.context);
                        drop(count);
                        push(std::move(result));
                    }
                    DISPATCH();
                }
                CASE(push_function) push(make_closure(static_cast<std::uint32_t>(ins.operand), {})); DISPATCH();
                CASE(make_closure) {
                    const auto count = static_cast<std::size_t>(static_cast<std::uint64_t>(ins.operand) >> 32);
                    std::vector<Value> captures;
                    captures.reserve(count);
                    for (Value* it = sp - count; it != sp; ++it) captures.push_back(std::move(*it));
                    drop(count);
                    push(make_closure(static_cast<std::uint32_t>(ins.operand & 0xFFFFFFFF), std::move(captures)));
                    DISPATCH();
                }
                CASE(call_value) {
                    const auto argc = static_cast<std::size_t>(ins.operand);
                    Value callee = pop();
                    auto& closure = closure_of(callee);
                    const auto& target = program.functions.at(closure.function);
                    if (target.params.size() != argc + closure.captures.size() || target.captures != closure.captures.size()) {
                        throw std::runtime_error("function value called with the wrong number of arguments");
                    }
                    for (const auto& captured : closure.captures) push(captured);
                    frames.back().ip = ip;
                    enter(closure.function, argc + closure.captures.size());
                    reload();
                    DISPATCH();
                }
                CASE(make_variant) {
                    const bool has_payload = (static_cast<std::uint64_t>(ins.operand) >> 32) & 1;
                    const bool is_result = (static_cast<std::uint64_t>(ins.operand) >> 33) & 1;
                    Value payload = has_payload ? pop() : Value();
                    push(make_variant((ins.operand & 0xFFFFFFFF) != 0, std::move(payload), is_result));
                    DISPATCH();
                }
                CASE(variant_which) {
                    const bool which = variant_of(top()).which != 0;
                    drop(1);
                    push(which);
                    DISPATCH();
                }
                CASE(variant_payload) {
                    Value payload = variant_of(top()).payload;
                    drop(1);
                    push(std::move(payload));
                    DISPATCH();
                }
                CASE(call_trait) {
                    const auto trait = static_cast<std::size_t>(ins.operand & 0xFFFFFFFF);
                    const auto slot = static_cast<std::size_t>(static_cast<std::uint64_t>(ins.operand) >> 32);
                    const auto& signature = program.types[program.traits[trait].method_types[slot]];
                    const auto argc = signature.params.size();
                    const auto& self = sp[-static_cast<std::ptrdiff_t>(argc) - 1];
                    const auto structure = object_of(self).type;
                    std::uint32_t target = 0;
                    bool found = false;
                    for (const auto& [id, table] : program.structs[structure].vtables) {
                        if (id == trait) { target = table[slot]; found = true; break; }
                    }
                    if (!found) throw std::runtime_error("value does not implement " + program.traits[trait].name);
                    frames.back().ip = ip;
                    enter(target, argc + 1);
                    reload();
                    DISPATCH();
                }
                CASE(parallel) {
                    Value mapper = pop();
                    const auto workers = int_of(pop());
                    const auto count = int_of(pop());
                    frames.back().ip = ip;
                    push(parallel_closure(count, workers, mapper, ins.operand == 1));
                    DISPATCH();
                }
                CASE(bit_and) CASE(bit_or) CASE(bit_xor) CASE(shift_left) CASE(shift_right) {
                    const auto b = int_of(sp[-1]);
                    const auto a = int_of(sp[-2]);
                    Int result;
                    switch (ins.op) {
                        case Op::bit_and: result = a & b; break;
                        case Op::bit_or: result = a | b; break;
                        case Op::bit_xor: result = a ^ b; break;
                        case Op::shift_left: result = shift_left(a, b); break;
                        default: result = shift_right(a, b); break;
                    }
                    sp -= 1;
                    sp[-1].i = result;
                    DISPATCH();
                }
                CASE(bit_not) top().i = ~int_of(top()); DISPATCH();
                CASE(try_call) {
                    Value callee = pop();
                    auto& closure = closure_of(callee);
                    const auto& target = program.functions.at(closure.function);
                    if (target.params.size() != closure.captures.size()) throw std::runtime_error("attempt expects a function that takes no arguments");
                    frames.back().ip = ip;
                    const auto saved_frames = frames.size();
                    Value* const saved_sp = sp;
                    try {
                        std::vector<Value> args(closure.captures.begin(), closure.captures.end());
                        Value result = invoke(closure.function, std::move(args));
                        push(make_variant(true, ins.operand == 1 ? Value(true) : std::move(result), true));
                    } catch (const Error& error) {
                        while (sp != saved_sp) (--sp)->~Value();
                        frames.resize(saved_frames);
                        std::string message = error.what();
                        const auto newline = message.find('\n');
                        if (newline != std::string::npos) message.resize(newline);
                        push(make_variant(false, make_text(message), true));
                    }
                    reload();
                    DISPATCH();
                }
                CASE(return_value) CASE(finish) {
                    Value result = ins.op == Op::finish ? Value() : pop();
                    while (sp != locals) (--sp)->~Value();
                    frames.pop_back();
                    push(std::move(result));
                    if (frames.size() == stop_depth) return;
                    reload();
                    DISPATCH();
                }
#ifndef TACQOR_THREADED
                case Op::count:
                    throw std::logic_error("invalid bytecode opcode");  // rejected by the verifier before execution
            }
        }
#endif
#undef ins
#undef CASE
#undef DISPATCH
    } catch (const Error&) {
        throw;
    } catch (const ExitRequest&) {
        throw;
    } catch (const std::exception& error) {
        frames.back().ip = ip;
        const auto at = ip > code ? ip[-1].at : Location{};
        std::string message = error.what();
        if (frames.size() > 1) message += traceback();
        throw Error(at, message);
    }
}

}  // namespace

struct Runner {
    const Program& program;
    Shared shared;
    Runner(const Program& source, const std::vector<std::string>& args, std::ostream& output)
        : program(source), shared{source, Context{args, output, source.structs, {}, {}}, {}} {
        shared.texts.reserve(program.texts.size());
        for (const auto& constant : program.texts) {
            shared.texts.push_back(make_text(constant));
            shared.texts.back().o->immortal = true;
        }
    }
    ~Runner() {
        // Every VM is gone by now, so the constants can be released normally.
        for (auto& text : shared.texts) text.o->immortal = false;
    }
};

void run(const Program& program, const std::vector<std::string>& args, std::ostream& output) {
    gc_main_thread = true;
    Runner runner(program, args, output);
    try {
        VM vm(runner.shared, stack_limit);
        vm.invoke(program.entry, {});
    } catch (const ExitRequest& request) {
        output.flush();
        std::cerr.flush();
        std::_Exit(request.code);
    }
    output.flush();
}

// Runs every `def test_*()` procedure of the root module; returns the number of failures.
int run_tests(const Program& program, std::ostream& output) {
    gc_main_thread = true;
    const std::vector<std::string> no_args;
    Runner runner(program, no_args, output);
    int failed = 0, passed = 0;
    for (std::size_t id = 0; id < program.functions.size(); ++id) {
        const auto& function = program.functions[id];
        if (function.name.rfind("test_", 0) != 0 || function.name.find('.') != std::string::npos ||
            function.name.find('@') != std::string::npos || !function.params.empty() || function.result != 0) {
            continue;
        }
        try {
            VM vm(runner.shared, stack_limit);
            vm.invoke(static_cast<std::uint32_t>(id), {});
            output << "PASS " << function.name << '\n';
            ++passed;
        } catch (const Error& error) {
            output << "FAIL " << function.name << ": line " << error.at.line << ": " << error.what() << '\n';
            ++failed;
        } catch (const ExitRequest& request) {
            output << "FAIL " << function.name << ": exit(" << request.code << ")\n";
            ++failed;
        } catch (const std::exception& error) {
            output << "FAIL " << function.name << ": " << error.what() << '\n';
            ++failed;
        }
    }
    output << passed << " passed, " << failed << " failed\n";
    output.flush();
    return failed;
}

}  // namespace tacqor
