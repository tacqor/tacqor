#include "tacqor.hpp"

#include <cstdlib>
#include <fstream>
#include <limits>
#include <random>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace tacqor {

namespace {

constexpr std::string_view magic = "TQBC\x12\r\n\x1a";
constexpr std::string_view trailer = "TQEX\x12\r\n\x1a";
constexpr std::size_t instruction_size = 21;

void number(std::string& bytes, std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) {
        bytes.push_back(static_cast<char>(value & 255));
        value >>= 8;
    }
}
void field(std::string& bytes, std::string_view value) {
    number(bytes, value.size(), 4);
    bytes.append(value);
}

class Reader {
    std::string_view rest;
public:
    explicit Reader(std::string_view bytes) : rest(bytes) {}
    std::string_view take(std::size_t count) {
        if (count > rest.size()) throw std::runtime_error("truncated Tacqor bytecode");
        const auto value = rest.substr(0, count);
        rest.remove_prefix(count);
        return value;
    }
    std::uint64_t number(unsigned width) {
        std::uint64_t result = 0;
        unsigned shift = 0;
        for (unsigned char byte : take(width)) {
            result |= static_cast<std::uint64_t>(byte) << shift;
            shift += 8;
        }
        return result;
    }
    std::size_t count(std::size_t element_size, std::size_t limit) {
        const auto amount = number(4);
        if (amount > limit || amount > rest.size() / element_size) {
            throw std::runtime_error("invalid Tacqor bytecode length");
        }
        return static_cast<std::size_t>(amount);
    }
    std::string field() { return std::string(take(count(1, bytecode_limit))); }
    bool empty() const { return rest.empty(); }
};

std::int64_t signed_integer(std::uint64_t value) {
    if (value <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return static_cast<std::int64_t>(value);
    }
    return -1 - static_cast<std::int64_t>(~value);
}


}  // namespace

void verify(const Program& program) {
    const auto bounded = [](std::int64_t operand, std::size_t bound) {
        return operand >= 0 && static_cast<std::uint64_t>(operand) < bound;
    };
    if (program.types.empty() || program.types[0].kind != Kind::unit) throw std::runtime_error("invalid bytecode type table");
    for (const auto& type : program.types) {
        switch (type.kind) {
            case Kind::list: if (type.first >= program.types.size() || type.first == 0) throw std::runtime_error("invalid bytecode type"); break;
            case Kind::dict:
                if (type.first >= program.types.size() || type.second >= program.types.size() || type.second == 0 ||
                    (program.types[type.first].kind != Kind::integer && program.types[type.first].kind != Kind::text)) {
                    throw std::runtime_error("invalid bytecode type");
                }
                break;
            case Kind::object: if (type.first >= program.structs.size()) throw std::runtime_error("invalid bytecode type"); break;
            case Kind::trait_object: if (type.first >= program.traits.size()) throw std::runtime_error("invalid bytecode type"); break;
            case Kind::option: if (type.first >= program.types.size() || type.first == 0) throw std::runtime_error("invalid bytecode type"); break;
            case Kind::result:
                if (type.first >= program.types.size() || type.first == 0 || type.second >= program.types.size() || type.second == 0) {
                    throw std::runtime_error("invalid bytecode type");
                }
                break;
            case Kind::function:
                if (type.first >= program.types.size()) throw std::runtime_error("invalid bytecode type");
                for (const auto param : type.params) {
                    if (param == 0 || param >= program.types.size()) throw std::runtime_error("invalid bytecode type");
                }
                break;
            default: if (type.first || type.second) throw std::runtime_error("invalid bytecode type"); break;
        }
        if (type.kind != Kind::function && !type.params.empty()) throw std::runtime_error("invalid bytecode type");
    }
    for (const auto& trait : program.traits) {
        if (trait.method_names.size() != trait.method_types.size()) throw std::runtime_error("invalid bytecode trait");
        for (const auto type : trait.method_types) {
            if (type >= program.types.size() || program.types[type].kind != Kind::function) throw std::runtime_error("invalid bytecode trait method");
        }
    }
    for (const auto& structure : program.structs) {
        if (structure.field_names.size() != structure.field_types.size()) throw std::runtime_error("invalid bytecode struct");
        for (const auto type : structure.field_types) {
            if (type == 0 || type >= program.types.size()) throw std::runtime_error("invalid bytecode struct field");
        }
        for (const auto& [trait, table] : structure.vtables) {
            if (trait >= program.traits.size() || table.size() != program.traits[trait].method_names.size()) throw std::runtime_error("invalid bytecode vtable");
            for (std::size_t m = 0; m < table.size(); ++m) {
                if (table[m] >= program.functions.size()) throw std::runtime_error("invalid bytecode vtable");
                const auto& method = program.functions[table[m]];
                const auto& signature = program.types[program.traits[trait].method_types[m]];
                if (method.params.size() != signature.params.size() + 1 || method.captures != 0 || method.result != signature.first) {
                    throw std::runtime_error("invalid bytecode vtable entry");
                }
            }
        }
    }
    if (program.entry >= program.functions.size()) throw std::runtime_error("invalid bytecode entry point");
    for (std::size_t id = 0; id < program.functions.size(); ++id) {
        const auto& function = program.functions[id];
        const bool entry = id == program.entry;
        if ((entry && (!function.params.empty() || function.name != "main" || function.result != 0)) ||
            function.params.size() > function.locals || function.locals > value_limit || function.captures > function.params.size() ||
            function.result >= program.types.size() ||
            function.code.empty() || function.code.back().op != Op::finish) {
            throw std::runtime_error("invalid bytecode function");
        }
        for (const auto param : function.params) {
            if (param == 0 || param >= program.types.size()) throw std::runtime_error("invalid bytecode parameter type");
        }
        const auto size = function.code.size();
        for (const auto& item : function.code) {
            bool valid = item.at.line != 0 && item.at.column != 0 && item.at.length != 0;
            const auto operand = item.operand;
            switch (item.op) {
                case Op::load: case Op::store: valid &= bounded(operand, function.locals); break;
                case Op::push_text: valid &= bounded(operand, program.texts.size()); break;
                case Op::push_boolean: valid &= operand == 0 || operand == 1; break;
                case Op::make_list: case Op::make_dict: {
                    const auto type = static_cast<std::uint64_t>(operand) >> 32;
                    valid &= operand >= 0 && bounded(operand & 0xFFFFFFFF, value_limit + 1) && type < program.types.size() &&
                             program.types[type].kind == (item.op == Op::make_list ? Kind::list : Kind::dict);
                    break;
                }
                case Op::make_object: valid &= bounded(operand, program.structs.size()); break;
                case Op::get_field: case Op::set_field: valid &= bounded(operand, value_limit); break;
                case Op::jump: case Op::jump_false: case Op::jump_true: valid &= bounded(operand, size); break;
                case Op::load2: {
                    valid &= operand >= 0 && bounded(operand & 0xFFFFFFFF, function.locals) &&
                             bounded(operand >> 32, function.locals);
                    break;
                }
                case Op::load_integer: valid &= operand >= 0 && bounded(operand & 0xFFFFFFFF, function.locals); break;
                case Op::add_store: case Op::subtract_store: case Op::multiply_store:
                    valid &= bounded(operand, function.locals);
                    break;
                case Op::jump_unless_locals: {
                    const auto bits = static_cast<std::uint64_t>(operand);
                    const auto comparison = bits >> 56;
                    const auto a = (bits >> 44) & 0xFFF, b = (bits >> 32) & 0xFFF;
                    valid &= a < function.locals && b < function.locals && bounded(operand & 0xFFFFFFFF, size) &&
                             (comparison == static_cast<unsigned>(Op::less) || comparison == static_cast<unsigned>(Op::less_equal) ||
                              comparison == static_cast<unsigned>(Op::greater) || comparison == static_cast<unsigned>(Op::greater_equal) ||
                              comparison == static_cast<unsigned>(Op::equal) || comparison == static_cast<unsigned>(Op::not_equal));
                    break;
                }
                case Op::jump_unless: {
                    const auto comparison = static_cast<std::uint64_t>(operand) >> 32;
                    valid &= operand >= 0 && bounded(operand & 0xFFFFFFFF, size) &&
                             (comparison == static_cast<unsigned>(Op::less) || comparison == static_cast<unsigned>(Op::less_equal) ||
                              comparison == static_cast<unsigned>(Op::greater) || comparison == static_cast<unsigned>(Op::greater_equal) ||
                              comparison == static_cast<unsigned>(Op::equal) || comparison == static_cast<unsigned>(Op::not_equal));
                    break;
                }
                case Op::loop_init: case Op::loop_next: case Op::iter_next: {
                    const auto slot = static_cast<std::uint64_t>(operand) >> 32;
                    const auto target = operand & 0xFFFFFFFF;
                    const auto width = item.op == Op::iter_next ? 3 : 2;
                    valid &= operand >= 0 && slot + width <= function.locals && bounded(target, size);
                    break;
                }
                case Op::reduce:
                    valid &= bounded(operand, program.functions.size()) && operand != program.entry;
                    if (valid) {
                        const auto& mapper = program.functions[static_cast<std::size_t>(operand)];
                        valid = mapper.params.size() == 1 && program.types[mapper.params[0]].kind == Kind::integer &&
                                program.types[mapper.result].kind == Kind::integer;
                    }
                    break;
                case Op::call:
                    valid &= bounded(operand, program.functions.size()) && operand != program.entry;
                    break;
                case Op::builtin: valid &= bounded(operand, static_cast<std::size_t>(Builtin::count)); break;
                case Op::push_function: valid &= bounded(operand, program.functions.size()) && operand != program.entry; break;
                case Op::make_closure: {
                    const auto target = operand & 0xFFFFFFFF;
                    const auto count = static_cast<std::uint64_t>(operand) >> 32;
                    valid &= operand >= 0 && bounded(target, program.functions.size()) && target != program.entry &&
                             count == program.functions[static_cast<std::size_t>(target)].captures;
                    break;
                }
                case Op::call_value: valid &= bounded(operand, value_limit); break;
                case Op::make_variant: {
                    const auto bits = static_cast<std::uint64_t>(operand);
                    const auto type = bits >> 40;
                    const bool is_result = (bits >> 33) & 1;
                    valid &= operand >= 0 && (operand & 0xFFFFFFFF) <= 1 && ((bits >> 32) & 0xFF) <= 3 && type < program.types.size() &&
                             program.types[type].kind == (is_result ? Kind::result : Kind::option);
                    break;
                }
                case Op::variant_payload: valid &= operand == 0 || operand == 1; break;
                case Op::call_trait: {
                    const auto trait = static_cast<std::uint64_t>(operand) & 0xFFFFFFFF;
                    const auto slot = static_cast<std::uint64_t>(operand) >> 32;
                    valid &= operand >= 0 && trait < program.traits.size() && slot < program.traits[trait].method_names.size();
                    break;
                }
                case Op::parallel: case Op::try_call: valid &= operand == 0 || operand == 1; break;
                case Op::push_integer: case Op::push_float: break;
                default: valid &= operand == 0; break;
            }
            if (!valid) throw std::runtime_error("invalid bytecode instruction");
        }
    }
}

namespace {

class TemporaryOutput {
public:
    std::filesystem::path directory;

    explicit TemporaryOutput(const std::filesystem::path& parent) {
        std::random_device random;
        for (int attempt = 0; attempt < 16; ++attempt) {
            auto candidate = parent / (".tacqor-" + std::to_string(random()));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                directory = std::move(candidate);
                return;
            }
            if (error && error != std::errc::file_exists) throw std::system_error(error, "create output directory");
        }
        throw std::runtime_error("could not create a temporary output directory");
    }

    ~TemporaryOutput() {
        std::error_code ignored;
        std::filesystem::remove(directory / "output", ignored);
        std::filesystem::remove(directory, ignored);
    }
};

}  // namespace

std::string encode(const Program& program) {
    verify(program);
    std::string bytes(magic);
    number(bytes, program.texts.size(), 4);
    for (const auto& value : program.texts) field(bytes, value);
    number(bytes, program.types.size(), 4);
    for (const auto& type : program.types) {
        number(bytes, static_cast<unsigned>(type.kind), 1);
        number(bytes, type.first, 4);
        number(bytes, type.second, 4);
        number(bytes, type.params.size(), 4);
        for (const auto param : type.params) number(bytes, param, 4);
    }
    number(bytes, program.structs.size(), 4);
    for (const auto& structure : program.structs) {
        field(bytes, structure.name);
        number(bytes, structure.field_names.size(), 4);
        for (std::size_t i = 0; i < structure.field_names.size(); ++i) {
            field(bytes, structure.field_names[i]);
            number(bytes, structure.field_types[i], 4);
        }
    }
    number(bytes, program.traits.size(), 4);
    for (const auto& trait : program.traits) {
        field(bytes, trait.name);
        number(bytes, trait.method_names.size(), 4);
        for (std::size_t m = 0; m < trait.method_names.size(); ++m) {
            field(bytes, trait.method_names[m]);
            number(bytes, trait.method_types[m], 4);
        }
    }
    for (const auto& structure : program.structs) {
        number(bytes, structure.vtables.size(), 4);
        for (const auto& [trait, table] : structure.vtables) {
            number(bytes, trait, 4);
            number(bytes, table.size(), 4);
            for (const auto id : table) number(bytes, id, 4);
        }
    }
    number(bytes, program.entry, 4);
    number(bytes, program.functions.size(), 4);
    for (const auto& function : program.functions) {
        field(bytes, function.name);
        number(bytes, function.params.size(), 4);
        for (const auto type : function.params) number(bytes, type, 4);
        number(bytes, function.result, 4);
        number(bytes, function.captures, 4);
        number(bytes, function.locals, 4);
        number(bytes, function.code.size(), 4);
        for (const auto& item : function.code) {
            number(bytes, static_cast<unsigned>(item.op), 1);
            number(bytes, static_cast<std::uint64_t>(item.operand), 8);
            number(bytes, item.at.line, 4);
            number(bytes, item.at.column, 4);
            number(bytes, item.at.length, 4);
        }
        if (bytes.size() > bytecode_limit) throw std::runtime_error("compiled program exceeds the size limit");
    }
    return bytes;
}

Program decode(std::string_view bytes) {
    if (bytes.size() > bytecode_limit) throw std::runtime_error("bytecode exceeds the size limit");
    Reader reader(bytes);
    if (reader.take(magic.size()) != magic) throw std::runtime_error("expected Tacqor 1.2 bytecode");
    Program program;
    const auto texts = reader.count(4, bytecode_limit / 4);
    for (std::size_t i = 0; i < texts; ++i) program.texts.push_back(reader.field());
    const auto types = reader.count(13, value_limit);
    for (std::size_t i = 0; i < types; ++i) {
        const auto kind = reader.number(1);
        if (kind > static_cast<unsigned>(Kind::trait_object)) throw std::runtime_error("invalid bytecode type kind");
        TypeInfo info{static_cast<Kind>(kind), 0, 0, {}};
        info.first = static_cast<TypeId>(reader.number(4));
        info.second = static_cast<TypeId>(reader.number(4));
        const auto params = reader.count(4, value_limit);
        for (std::size_t j = 0; j < params; ++j) info.params.push_back(static_cast<TypeId>(reader.number(4)));
        program.types.push_back(info);
    }
    const auto structs = reader.count(8, value_limit);
    for (std::size_t i = 0; i < structs; ++i) {
        StructInfo info;
        info.name = reader.field();
        const auto fields = reader.count(8, value_limit);
        for (std::size_t j = 0; j < fields; ++j) {
            info.field_names.push_back(reader.field());
            info.field_types.push_back(static_cast<TypeId>(reader.number(4)));
        }
        program.structs.push_back(std::move(info));
    }
    const auto traits = reader.count(8, value_limit);
    for (std::size_t i = 0; i < traits; ++i) {
        TraitInfo info;
        info.name = reader.field();
        const auto methods = reader.count(8, value_limit);
        for (std::size_t m = 0; m < methods; ++m) {
            info.method_names.push_back(reader.field());
            info.method_types.push_back(static_cast<TypeId>(reader.number(4)));
        }
        program.traits.push_back(std::move(info));
    }
    for (auto& structure : program.structs) {
        const auto tables = reader.count(8, value_limit);
        for (std::size_t t = 0; t < tables; ++t) {
            const auto trait = static_cast<std::uint32_t>(reader.number(4));
            const auto size = reader.count(4, value_limit);
            std::vector<std::uint32_t> table;
            for (std::size_t m = 0; m < size; ++m) table.push_back(static_cast<std::uint32_t>(reader.number(4)));
            structure.vtables.emplace_back(trait, std::move(table));
        }
    }
    program.entry = static_cast<std::uint32_t>(reader.number(4));
    const auto functions = reader.count(24, 65'536);
    for (std::size_t i = 0; i < functions; ++i) {
        Function function;
        function.name = reader.field();
        const auto params = reader.count(4, value_limit);
        for (std::size_t j = 0; j < params; ++j) function.params.push_back(static_cast<TypeId>(reader.number(4)));
        function.result = static_cast<TypeId>(reader.number(4));
        function.captures = static_cast<std::uint32_t>(reader.number(4));
        function.locals = static_cast<std::uint32_t>(reader.number(4));
        const auto size = reader.count(instruction_size, bytecode_limit / instruction_size);
        function.code.reserve(size);
        for (std::size_t j = 0; j < size; ++j) {
            const auto op = reader.number(1);
            if (op > static_cast<unsigned>(Op::try_call)) throw std::runtime_error("invalid bytecode opcode");
            const auto operand = signed_integer(reader.number(8));
            const auto line = static_cast<std::uint32_t>(reader.number(4));
            const auto column = static_cast<std::uint32_t>(reader.number(4));
            const auto length = static_cast<std::uint32_t>(reader.number(4));
            function.code.push_back({static_cast<Op>(op), operand, {line, column, length}});
        }
        program.functions.push_back(std::move(function));
    }
    if (!reader.empty()) throw std::runtime_error("unexpected data after Tacqor bytecode");
    verify(program);
    return program;
}

bool embedded_program(const std::filesystem::path& executable, Program& program) {
    std::ifstream file(executable, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("could not read the Tacqor executable");
    const auto length = file.tellg();
    constexpr auto marker_size = static_cast<std::streamoff>(8 + trailer.size());
    if (length < marker_size) return false;
    file.seekg(-marker_size, std::ios::end);
    char footer[8 + trailer.size()];
    if (!file.read(footer, sizeof(footer))) throw std::runtime_error("could not read executable footer");
    Reader reader(std::string_view(footer, sizeof(footer)));
    const auto payload = reader.number(8);
    if (reader.take(trailer.size()) != trailer) return false;
    if (payload > bytecode_limit || payload > static_cast<std::uint64_t>(length - marker_size)) {
        throw std::runtime_error("invalid embedded Tacqor program");
    }
    file.seekg(length - marker_size - static_cast<std::streamoff>(payload));
    std::string bytes(static_cast<std::size_t>(payload), '\0');
    if (!file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("could not read embedded Tacqor bytecode");
    }
    program = decode(bytes);
    return true;
}

void write_artifact(const std::filesystem::path& output, std::string_view bytes,
                    const std::filesystem::path& executable) {
    const auto absolute = std::filesystem::absolute(output);
    const TemporaryOutput temp(absolute.parent_path());
    const auto staged = temp.directory / "output";
    {
        std::ofstream file(staged, std::ios::binary);
        if (!file) throw std::runtime_error("could not open output file");
        if (!executable.empty()) {
            std::ifstream vm(executable, std::ios::binary);
            if (!vm) throw std::runtime_error("could not read runtime for the executable");
            file << vm.rdbuf();
        }
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!executable.empty()) {
            std::string footer;
            number(footer, bytes.size(), 8);
            footer.append(trailer);
            file.write(footer.data(), static_cast<std::streamsize>(footer.size()));
        }
        file.close();
        if (!file) throw std::runtime_error("could not write output file");
    }
#if !defined(_WIN32)
    if (!executable.empty()) {
        using std::filesystem::perms;
        std::filesystem::permissions(staged, perms::owner_exec | perms::group_exec | perms::others_exec,
                                     std::filesystem::perm_options::add);
    }
#endif
    std::filesystem::rename(staged, absolute);
}

std::filesystem::path current_executable(const char* argv0) {
#if defined(_WIN32)
    std::wstring name(32'768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, name.data(), static_cast<DWORD>(name.size()));
    if (size != 0 && size < name.size()) {
        name.resize(size);
        return std::filesystem::path(name);
    }
#elif defined(__linux__)
    std::error_code ignored;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", ignored);
    if (!ignored) return path;
#endif
    const auto arg = std::filesystem::u8path(argv0);
    if (arg.has_parent_path() || std::filesystem::is_regular_file(arg)) return std::filesystem::absolute(arg);
    if (const char* value = std::getenv("PATH")) {
#if defined(_WIN32)
        constexpr char separator = ';';
#else
        constexpr char separator = ':';
#endif
        const std::string_view entries(value);
        for (std::size_t start = 0; start <= entries.size();) {
            const auto end = entries.find(separator, start);
            const auto part = entries.substr(start, end == entries.npos ? entries.npos : end - start);
            const auto candidate = std::filesystem::u8path(part.begin(), part.end()) / arg;
            if (std::filesystem::is_regular_file(candidate)) return std::filesystem::absolute(candidate);
            if (end == entries.npos) break;
            start = end + 1;
        }
    }
    throw std::runtime_error("could not locate the Tacqor executable");
}

}  // namespace tacqor
