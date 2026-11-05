#include "tacqor.hpp"
#include "values.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <cstring>
#include <fstream>
#include <iostream>

namespace {

namespace fs = std::filesystem;

std::string json(std::string_view text) {
    std::string value = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char byte : text) {
        if (byte == '"' || byte == '\\') {
            value += '\\';
            value += static_cast<char>(byte);
        } else if (byte < 32) {
            value += "\\u00";
            value += hex[byte >> 4];
            value += hex[byte & 15];
        } else {
            value += static_cast<char>(byte);
        }
    }
    value += '"';
    return value;
}

// Where the standard library lives: next to the executable (release layout) or one level up (source tree).
std::vector<fs::path> std_search(const fs::path& executable) {
    std::vector<fs::path> dirs;
    const auto base = executable.parent_path();
    std::vector<fs::path> candidates = {base / "std", base.parent_path() / "std", base.parent_path().parent_path() / "std",
                                        base.parent_path() / "lib" / "tacqor" / "std"};
    if (const char* home = std::getenv("TACQOR_HOME")) candidates.insert(candidates.begin(), fs::path(home) / "std");
    if (const char* home = std::getenv("HOME")) candidates.push_back(fs::path(home) / ".tacqor" / "std");
    candidates.push_back(fs::path("/usr/local/lib/tacqor/std"));
    for (const auto& candidate : candidates) {
        std::error_code ignored;
        if (fs::is_directory(candidate, ignored)) {
            dirs.push_back(candidate.parent_path());
            break;  // the first std library found wins
        }
    }
    return dirs;
}

// Prints the offending source line under a diagnostic, with a caret under the location.
void show_excerpt(const std::string& path, const tacqor::Location& at) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return;
    std::string line;
    for (std::uint32_t number = 1; std::getline(file, line); ++number) {
        if (number != at.line) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() > 400) line = line.substr(0, 400) + "...";
        std::cerr << "    " << line << '\n' << "    ";
        for (std::uint32_t i = 1; i < at.column && i <= line.size(); ++i) std::cerr << (line[i - 1] == '\t' ? '\t' : ' ');
        std::cerr << '^';
        for (std::uint32_t i = 1; i < at.length && i < 80; ++i) std::cerr << '~';
        std::cerr << '\n';
        return;
    }
}

tacqor::CompileOptions compile_options(const fs::path& source, const fs::path& executable, bool require_main) {
    tacqor::CompileOptions options;
    options.source_path = source;
    options.search = std_search(executable);
    options.require_main = require_main;
    return options;
}

int diagnostics(const std::string& source, const tacqor::CompileOptions& options) {
    std::vector<tacqor::Finding> findings;
    try {
        tacqor::compile(source, findings, options);
    } catch (const tacqor::Error& error) {
        std::string message = error.what();
        if (!error.file.empty()) message = "in " + error.file + ":" + std::to_string(error.at.line) + ": " + message;
        findings = {{error.file.empty() ? error.at : tacqor::Location{1, 1, 1}, false, "compile-error", message, {}}};
    }
    // Warnings from imported modules are reported at the import line of the root file? Keep it simple: drop them.
    findings.erase(std::remove_if(findings.begin(), findings.end(), [](const tacqor::Finding& f) { return !f.file.empty(); }), findings.end());
    std::cout << '[';
    bool error = false;
    bool first = true;
    for (const auto& issue : findings) {
        if (!first) std::cout << ',';
        first = false;
        error |= !issue.warning;
        std::cout << "{\"line\":" << issue.at.line << ",\"column\":" << issue.at.column
                  << ",\"length\":" << issue.at.length << ",\"severity\":" << (issue.warning ? 2 : 1)
                  << ",\"code\":" << json(issue.code) << ",\"message\":" << json(issue.message) << '}';
    }
    std::cout << "]\n";
    return error ? 1 : 0;
}

const char* op_name(tacqor::Op op) {
    static const char* names[] = {
        "push_integer", "push_float", "push_boolean", "push_text", "push_unit", "load", "store", "discard",
        "make_list", "make_dict", "make_object", "get_field", "set_field",
        "index", "set_index", "slice", "length", "contains",
        "add", "subtract", "multiply", "divide", "modulo", "negate",
        "fadd", "fsubtract", "fmultiply", "fdivide", "fmodulo", "fnegate", "concat",
        "equal", "not_equal", "less", "less_equal", "greater", "greater_equal", "invert",
        "jump", "jump_false", "jump_true", "jump_unless",
        "load2", "load_integer", "add_store", "subtract_store", "multiply_store", "jump_unless_locals",
        "loop_init", "loop_next", "iter_next",
        "call", "reduce", "builtin", "return_value", "finish",
        "push_function", "make_closure", "call_value", "make_variant", "variant_which", "variant_payload",
        "call_trait", "parallel",
        "bit_and", "bit_or", "bit_xor", "shift_left", "shift_right", "bit_not", "try_call",
    };
    static_assert(sizeof(names) / sizeof(names[0]) == static_cast<std::size_t>(tacqor::Op::count), "every opcode needs a name");
    const auto index = static_cast<std::size_t>(op);
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "?";
}

void dump(const tacqor::Program& program, std::ostream& out) {
    for (const auto& trait : program.traits) {
        out << "trait " << trait.name << ":";
        for (std::size_t m = 0; m < trait.method_names.size(); ++m) {
            out << (m ? ", " : " ") << trait.method_names[m] << ": " << tacqor::type_name(program, trait.method_types[m]);
        }
        out << '\n';
    }
    for (std::size_t i = 0; i < program.structs.size(); ++i) {
        const auto& structure = program.structs[i];
        out << "struct " << structure.name << ":";
        for (std::size_t f = 0; f < structure.field_names.size(); ++f) {
            out << (f ? ", " : " ") << structure.field_names[f] << ": " << tacqor::type_name(program, structure.field_types[f]);
        }
        out << '\n';
    }
    for (const auto& function : program.functions) {
        out << "def " << function.name << "(";
        for (std::size_t p = 0; p < function.params.size(); ++p) {
            out << (p ? ", " : "") << tacqor::type_name(program, function.params[p]);
        }
        out << ") -> " << tacqor::type_name(program, function.result) << "    # " << function.locals << " locals, "
            << function.code.size() << " instructions\n";
        for (std::size_t i = 0; i < function.code.size(); ++i) {
            const auto& item = function.code[i];
            out << "  " << i << "\t" << op_name(item.op);
            switch (item.op) {
                case tacqor::Op::push_text: out << " \"" << program.texts[static_cast<std::size_t>(item.operand)] << '"'; break;
                case tacqor::Op::jump_unless: case tacqor::Op::loop_init: case tacqor::Op::loop_next:
                case tacqor::Op::iter_next: case tacqor::Op::load2: case tacqor::Op::load_integer:
                    out << ' ' << (item.operand >> 32) << ", " << (item.operand & 0xFFFFFFFF); break;
                case tacqor::Op::make_list: case tacqor::Op::make_dict:
                    out << ' ' << (item.operand & 0xFFFFFFFF) << " : "
                        << tacqor::type_name(program, static_cast<tacqor::TypeId>(item.operand >> 32));
                    break;
                case tacqor::Op::jump_unless_locals: {
                    const auto bits = static_cast<std::uint64_t>(item.operand);
                    out << ' ' << op_name(static_cast<tacqor::Op>(bits >> 56)) << " [" << ((bits >> 44) & 0xFFF) << "] ["
                        << ((bits >> 32) & 0xFFF) << "] -> " << (item.operand & 0xFFFFFFFF);
                    break;
                }
                case tacqor::Op::call: case tacqor::Op::reduce: case tacqor::Op::push_function:
                    out << ' ' << program.functions[static_cast<std::size_t>(item.operand)].name; break;
                case tacqor::Op::make_closure:
                    out << ' ' << program.functions[static_cast<std::size_t>(item.operand & 0xFFFFFFFF)].name
                        << " capturing " << (item.operand >> 32); break;
                case tacqor::Op::call_trait: {
                    const auto& trait = program.traits[static_cast<std::size_t>(item.operand & 0xFFFFFFFF)];
                    out << ' ' << trait.name << '.' << trait.method_names[static_cast<std::size_t>(item.operand >> 32)];
                    break;
                }
                case tacqor::Op::push_float: {
                    double value;
                    std::memcpy(&value, &item.operand, sizeof value);
                    out << ' ' << value;
                    break;
                }
                default: if (item.operand) out << ' ' << item.operand; break;
            }
            out << "\t# line " << item.at.line << '\n';
        }
    }
}

// --- packages: a registry-free package manager (local folders or git repositories) ---------------

bool looks_like_url(const std::string& source) {
    return source.find("://") != std::string::npos || source.rfind("git@", 0) == 0 ||
           (source.size() > 4 && source.compare(source.size() - 4, 4, ".git") == 0);
}

std::string package_name(const std::string& source) {
    auto name = fs::path(source).filename().string();
    if (name.empty()) name = fs::path(source).parent_path().filename().string();
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".git") == 0) name.resize(name.size() - 4);
    return name;
}

void fetch_package(const std::string& source, const std::string& name) {
    const auto destination = fs::path("packages") / name;
    if (fs::exists(destination)) throw std::runtime_error("packages/" + name + " already exists (use `tacqor remove " + name + "` first)");
    fs::create_directories("packages");
    if (looks_like_url(source)) {
        const std::string command = "git clone --quiet --depth 1 \"" + source + "\" \"" + destination.string() + "\"";
        std::cout << "fetching " << source << "\n";
        if (std::system(command.c_str()) != 0) throw std::runtime_error("git clone failed for " + source);
    } else {
        if (!fs::is_directory(source)) throw std::runtime_error("'" + source + "' is neither a folder nor a git URL");
        fs::copy(source, destination, fs::copy_options::recursive);
    }
    std::error_code ignored;
    fs::remove_all(destination / ".git", ignored);
    if (!fs::exists(destination / "lib.tq")) {
        bool any = false;
        for (const auto& entry : fs::directory_iterator(destination)) if (entry.path().extension() == ".tq") any = true;
        if (!any) std::cout << "warning: " << destination.string() << " contains no .tq files\n";
    }
    std::cout << "installed packages/" << name << "\n";
}

std::vector<std::pair<std::string, std::string>> read_manifest() {
    std::vector<std::pair<std::string, std::string>> entries;
    std::ifstream file("tacqor.pkg");
    std::string line;
    while (std::getline(file, line)) {
        const auto space = line.find(' ');
        if (line.empty() || line[0] == '#' || space == std::string::npos) continue;
        entries.emplace_back(line.substr(0, space), line.substr(space + 1));
    }
    return entries;
}

void write_manifest(const std::vector<std::pair<std::string, std::string>>& entries) {
    std::ofstream file("tacqor.pkg");
    file << "# Packages used by this project: NAME SOURCE. Restore them with `tacqor install`.\n";
    for (const auto& [name, source] : entries) file << name << ' ' << source << '\n';
}

int packages(const std::string& command, int argc, char** argv) {
    if (command == "add") {
        if (argc < 3) throw std::runtime_error("usage: tacqor add SOURCE [NAME]");
        const std::string source = argv[2];
        const std::string name = argc > 3 ? argv[3] : package_name(source);
        fetch_package(source, name);
        auto entries = read_manifest();
        bool present = false;
        for (auto& [n, src] : entries) if (n == name) { src = source; present = true; }
        if (!present) entries.emplace_back(name, source);
        write_manifest(entries);
        return 0;
    }
    if (command == "remove") {
        if (argc != 3) throw std::runtime_error("usage: tacqor remove NAME");
        const std::string name = argv[2];
        std::error_code ignored;
        fs::remove_all(fs::path("packages") / name, ignored);
        auto entries = read_manifest();
        entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const auto& e) { return e.first == name; }), entries.end());
        write_manifest(entries);
        std::cout << "removed packages/" << name << "\n";
        return 0;
    }
    // install: fetch whatever the manifest lists and is missing
    int fetched = 0;
    for (const auto& [name, source] : read_manifest()) {
        if (fs::exists(fs::path("packages") / name)) continue;
        fetch_package(source, name);
        ++fetched;
    }
    std::cout << fetched << " package(s) installed\n";
    return 0;
}

// --- repl: a scratchpad that recompiles and re-runs the session after every entry ----------------

int repl(const fs::path& executable) {
    std::cout << "Tacqor " << tacqor::version << " scratchpad. Definitions and statements accumulate; the whole session re-runs\n"
                 "after each entry (side effects repeat), and only new output is shown. :help for commands.\n";
    std::string definitions;
    std::vector<std::string> statements;
    std::size_t shown = 0;
    std::string line;
    const auto trim = [](std::string text) {
        while (!text.empty() && (text.back() == ' ' || text.back() == '\r' || text.back() == '\t')) text.pop_back();
        return text;
    };
    const auto first_word = [](const std::string& text) {
        std::size_t i = 0;
        while (i < text.size() && text[i] == ' ') ++i;
        std::size_t j = i;
        while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_')) ++j;
        return text.substr(i, j - i);
    };
    const auto indent = [](const std::string& text) {
        std::string out;
        std::size_t start = 0;
        while (start <= text.size()) {
            auto end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            const auto piece = text.substr(start, end - start);
            if (!piece.empty()) out += "    " + piece + "\n";
            if (end == text.size()) break;
            start = end + 1;
        }
        return out;
    };
    const auto attempt = [&](const std::string& defs, const std::vector<std::string>& stmts, std::string& output) -> std::string {
        std::string source = defs + "\ndef main():\n";
        for (const auto& statement : stmts) source += indent(statement);
        if (stmts.empty()) source += "    pass_()\n";
        source = source.find("pass_()") == std::string::npos ? source : defs + "\ndef main():\n    return\n";
        std::vector<tacqor::Finding> findings;
        try {
            auto program = tacqor::compile(source, findings, compile_options({}, executable, true));
            std::ostringstream captured;
            tacqor::run(program, {}, captured);
            output = captured.str();
            return "";
        } catch (const tacqor::Error& error) {
            return (error.file.empty() ? "" : error.file + ": ") + std::string(error.what());
        } catch (const std::exception& error) {
            return error.what();
        }
    };
    while (true) {
        std::cout << ">>> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        line = trim(line);
        if (line.empty()) continue;
        if (line == ":quit" || line == ":q") break;
        if (line == ":help") {
            std::cout << "  :show   print the session source   :reset  forget everything   :quit  leave\n"
                         "  Enter a definition (def/struct/trait/import/const) or a statement; an expression alone is printed.\n"
                         "  A line ending in ':' starts a block; finish it with an empty line.\n";
            continue;
        }
        if (line == ":reset") {
            definitions.clear();
            statements.clear();
            shown = 0;
            continue;
        }
        if (line == ":show") {
            std::cout << definitions;
            for (const auto& statement : statements) std::cout << statement << '\n';
            continue;
        }
        std::string entry = line;
        const auto word = first_word(line);
        const bool definition = word == "def" || word == "struct" || word == "trait" || word == "import" || word == "const";
        if (!line.empty() && line.back() == ':') {
            std::string more;
            while (true) {
                std::cout << "... " << std::flush;
                if (!std::getline(std::cin, more)) break;
                more = trim(more);
                if (more.empty()) break;
                entry += "\n" + more;
            }
        }
        std::string output, error;
        if (definition) {
            error = attempt(definitions + entry + "\n", statements, output);
            if (error.empty()) definitions += entry + "\n";
        } else {
            const bool statement_keyword = word == "let" || word == "var" || word == "if" || word == "while" || word == "for" ||
                                           word == "match" || word == "return" || word == "assert" || word == "break" || word == "continue";
            auto trial = statements;
            bool committed = false;
            const auto looks_like_assignment = [](const std::string& text) {
                for (std::size_t i = 0; i < text.size(); ++i) {
                    if (text[i] != '=') continue;
                    const char before = i > 0 ? text[i - 1] : ' ';
                    const char after = i + 1 < text.size() ? text[i + 1] : ' ';
                    if (after == '=' || after == '>' || before == '=' || before == '!' || before == '<' || before == '>') { ++i; continue; }
                    return true;
                }
                return false;
            };
            if (!statement_keyword && entry.find('\n') == std::string::npos && !looks_like_assignment(entry)) {
                trial.push_back("print(" + entry + ")");
                error = attempt(definitions, trial, output);
                committed = error.empty();
            }
            if (!committed) {
                trial = statements;
                trial.push_back(entry);
                error = attempt(definitions, trial, output);
                committed = error.empty();
            }
            if (committed) statements = trial;
        }
        if (!error.empty()) {
            std::cout << "error: " << error << '\n';
            continue;
        }
        if (output.size() > shown) std::cout << output.substr(shown);
        shown = output.size();
    }
    std::cout << '\n';
    return 0;
}

void usage(std::ostream& stream) {
    stream << "Tacqor " << tacqor::version << "\n"
           << "  tacqor check FILE.tq\n"
           << "  tacqor diagnose FILE.tq | --stdin [FILE.tq]\n"
           << "  tacqor dump FILE.tq | FILE.tqb\n"
           << "  tacqor FILE.tq [ARG ...]                      (same as run)\n"
           << "  tacqor run FILE.tq | FILE.tqb [-- ARG ...]\n"
           << "  tacqor compile FILE.tq -o FILE.tqb\n"
           << "  tacqor build FILE.tq -o EXECUTABLE            (bytecode + embedded VM)\n"
           << "  tacqor build --native FILE.tq -o EXECUTABLE   (machine code via the system C++ compiler)\n"
           << "  tacqor emit FILE.tq -o FILE.cpp               (the C++ the native build compiles)\n"
           << "  tacqor test FILE.tq                           (run every def test_*() in the file)\n"
           << "  tacqor new NAME                               (create a project folder)\n"
           << "  tacqor repl                                   (interactive scratchpad)\n"
           << "  tacqor lsp                                    (language server over stdio, for editors)\n"
           << "  tacqor add SOURCE [NAME]                      (install a package from a folder or git URL into packages/)\n"
           << "  tacqor install | remove NAME                  (restore packages from tacqor.pkg / uninstall)\n";
}

std::string load(const fs::path& path, std::size_t limit) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("could not open input");
    return tacqor::read_text(file, limit);
}

int cli(int argc, char** argv, const fs::path& executable, std::string& label, bool& running) {
    if (argc == 1 || std::string_view(argv[1]) == "--help") {
        usage(std::cout);
        return 0;
    }
    std::string_view command = argv[1];
    // `tacqor program.tq a b` means `tacqor run program.tq -- a b`; shebang scripts rely on this.
    std::vector<char*> rewritten;
    if ((command.size() > 3 && command.substr(command.size() - 3) == ".tq") ||
        (command.size() > 4 && command.substr(command.size() - 4) == ".tqb")) {
        static char run_word[] = "run";
        static char separator[] = "--";
        rewritten = {argv[0], run_word, argv[1], separator};
        for (int i = 2; i < argc; ++i) rewritten.push_back(argv[i]);
        argv = rewritten.data();
        argc = static_cast<int>(rewritten.size());
        command = "run";
    } else if (command == "run" && argc > 3 && std::string_view(argv[3]) != "--") {
        static char separator[] = "--";
        rewritten = {argv[0], argv[1], argv[2], separator};
        for (int i = 3; i < argc; ++i) rewritten.push_back(argv[i]);
        argv = rewritten.data();
        argc = static_cast<int>(rewritten.size());
    }
    bool native = false;
    if (command == "build" && argc > 2 && std::string_view(argv[2]) == "--native") {
        native = true;
        for (int i = 2; i + 1 < argc; ++i) argv[i] = argv[i + 1];
        --argc;
    }
    if (command == "emit") {
        native = true;
        command = "build";
    }
    if (command == "--version" && argc == 2) {
        std::cout << "Tacqor " << tacqor::version << "\n";
        return 0;
    }
    if (command == "repl" && argc == 2) return repl(executable);
    if (command == "lsp" && argc == 2) return tacqor::run_lsp(std_search(executable));
    if (command == "add" || command == "install" || command == "remove") return packages(std::string(command), argc, argv);
    const bool emits = command == "compile" || command == "build";
    const bool emit_source = native && std::string_view(argv[1]) == "emit";
    if (argc < 3 || (emits && (argc != 5 || std::string_view(argv[3]) != "-o")) ||
        (command == "run" && argc > 3 && std::string_view(argv[3]) != "--") ||
        ((command == "check" || command == "dump" || command == "test" || command == "new") && argc != 3) ||
        (command == "diagnose" && !(argc == 3 || (argc == 4 && std::string_view(argv[2]) == "--stdin"))) ||
        (!emits && command != "run" && command != "check" && command != "diagnose" && command != "dump" && command != "test" && command != "new")) {
        usage(std::cerr);
        return 2;
    }
    label = argv[2];
    if (command == "new") {
        const auto root = fs::u8path(label);
        if (fs::exists(root)) throw std::runtime_error("'" + label + "' already exists");
        fs::create_directories(root);
        const auto name = root.filename().string();
        std::ofstream(root / "main.tq") <<
            "import std.lists\n\n"
            "struct Greeting:\n    who: str\n\n    def text(self) -> str:\n        return f\"Hello, {self.who}!\"\n\n"
            "def main():\n    let names = [\"world\", \"" + name + "\"]\n"
            "    for line in lists.map(names, fn(n: str) => Greeting(n).text()):\n        print(line)\n";
        std::ofstream(root / "tests.tq") <<
            "import main\n\n"
            "def test_greeting():\n    assert main.Greeting(\"x\").text() == \"Hello, x!\"\n";
        std::ofstream(root / "README.md") <<
            "# " + name + "\n\nRun: `tacqor run main.tq`\nTest: `tacqor test tests.tq`\nBuild: `tacqor build --native main.tq -o " + name + "`\n";
        std::cout << "created " << root.string() << "/ with main.tq, tests.tq, README.md\n";
        return 0;
    }
    if (command == "diagnose" && label == "--stdin") {
        // Optional third argument: the document's path, used only to resolve relative imports.
        const fs::path document = argc == 4 ? fs::u8path(argv[3]) : fs::path{};
        try {
            return diagnostics(tacqor::read_text(std::cin, tacqor::source_limit), compile_options(document, executable, false));
        } catch (const std::runtime_error&) {
            std::cout << "[{\"line\":1,\"column\":1,\"length\":1,\"severity\":1,"
                         "\"code\":\"invalid-input\",\"message\":\"Source could not be read or exceeds one million bytes\"}]\n";
            return 1;
        }
    }
    const auto input = fs::u8path(label);
    tacqor::Program program;
    if ((command == "run" || command == "dump") && input.extension() == ".tqb") {
        program = tacqor::decode(load(input, tacqor::bytecode_limit));
    } else {
        const auto source = load(input, tacqor::source_limit);
        const bool needs_main = command != "check" && command != "diagnose" && command != "test" && command != "dump";
        if (command == "diagnose") return diagnostics(source, compile_options(input, executable, false));
        std::vector<tacqor::Finding> findings;
        program = tacqor::compile(source, findings, compile_options(input, executable, needs_main));
        if (command == "check") {
            for (const auto& issue : findings) {
                std::cerr << (issue.file.empty() ? label : issue.file) << ':' << issue.at.line << ':' << issue.at.column
                          << ": warning [" << issue.code << "]: " << issue.message << '\n';
            }
            std::cout << label << ": OK\n";
            return 0;
        }
        if (command == "test") {
            running = true;
            return tacqor::run_tests(program, std::cout) == 0 ? 0 : 1;
        }
    }
    if (command == "dump") {
        dump(program, std::cout);
        return 0;
    }
    if (emits) {
        auto output = fs::u8path(argv[4]);
        if (command == "compile" && output.extension() != ".tqb") {
            throw std::runtime_error("portable bytecode output must use the .tqb extension");
        }
#if defined(_WIN32)
        if (command == "build" && output.extension().empty()) output += ".exe";
#endif
        if (fs::exists(output) && (fs::equivalent(input, output) || fs::equivalent(executable, output))) {
            throw std::runtime_error("output points to the source or Tacqor executable");
        }
        if (emit_source) {
            tacqor::write_artifact(output, tacqor::generate_native(program));
        } else if (native) {
            tacqor::build_native(program, output);
        } else {
            tacqor::write_artifact(output, tacqor::encode(program),
                                     command == "build" ? executable : fs::path{});
        }
    } else {
        running = true;
        std::vector<std::string> args;
        for (int i = 4; i < argc; ++i) args.emplace_back(argv[i]);
        tacqor::run(program, args, std::cout);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string label = "tacqor";
    bool running = false;
    try {
        const auto executable = tacqor::current_executable(argv[0]);
        tacqor::Program program;
        if (tacqor::embedded_program(executable, program)) {
            running = true;
            label = executable.filename().u8string();
            tacqor::run(program, std::vector<std::string>(argv + 1, argv + argc), std::cout);
            return 0;
        }
        return cli(argc, argv, executable, label, running);
    } catch (const tacqor::Error& error) {
        const auto where = error.file.empty() ? label : error.file;
        std::cerr << where << ':' << error.at.line << ':' << error.at.column
                  << (running ? ": runtime error: " : ": error: ") << error.what() << '\n';
        if (!running && where.size() > 3 && where.compare(where.size() - 3, 3, ".tq") == 0) show_excerpt(where, error.at);
    } catch (const std::exception& error) {
        std::cerr << label << ": error: " << error.what() << '\n';
    }
    return 1;
}
