#include "tacqor.hpp"
#include "values.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace tacqor {

std::string type_name(const Program& program, TypeId type) {
    const auto& info = program.types.at(type);
    switch (info.kind) {
        case Kind::unit: return "none";
        case Kind::integer: return "i64";
        case Kind::floating: return "f64";
        case Kind::boolean: return "bool";
        case Kind::text: return "str";
        case Kind::list: return "list[" + type_name(program, info.first) + "]";
        case Kind::dict: return "dict[" + type_name(program, info.first) + ", " + type_name(program, info.second) + "]";
        case Kind::object: return program.structs.at(info.first).name;
        case Kind::option: return "Option[" + type_name(program, info.first) + "]";
        case Kind::result: return "Result[" + type_name(program, info.first) + ", " + type_name(program, info.second) + "]";
        case Kind::trait_object: return program.traits.at(info.first).name;
        case Kind::function: {
            std::string out = "fn(";
            for (std::size_t i = 0; i < info.params.size(); ++i) out += (i ? ", " : "") + type_name(program, info.params[i]);
            out += ")";
            if (info.first != 0) out += " -> " + type_name(program, info.first);
            return out;
        }
    }
    throw std::logic_error("invalid type");
}

namespace {

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------

enum class Tag { name, integer, floating, text, fstring, symbol, newline, indent, dedent, end };

struct Token {
    Tag tag;
    std::string text;
    Location at;
};

bool letter(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}
bool digit(char ch) { return ch >= '0' && ch <= '9'; }

// `base` offsets every location (used when re-lexing the expressions inside an f-string).
std::vector<Token> lex(std::string_view source, Location base = {1, 1, 1}, bool nested = false) {
    if (source.size() > source_limit) throw Error({}, "source exceeds the size limit");
    std::vector<Token> tokens;
    std::vector<std::size_t> indentation{0};
    std::vector<char> brackets;
    std::uint32_t number = 0;
    bool continued = false;
    for (std::size_t offset = 0; offset < source.size();) {
        ++number;
        const auto end = source.find('\n', offset);
        auto line = source.substr(offset, end == source.npos ? source.npos : end - offset);
        offset = end == source.npos ? source.size() : end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto location = [&](std::size_t pos, std::size_t size = 1) {
            if (nested) return Location{base.line, static_cast<std::uint32_t>(base.column + pos), static_cast<std::uint32_t>(size)};
            return Location{base.line + number - 1, static_cast<std::uint32_t>(pos + 1), static_cast<std::uint32_t>(size)};
        };
        const auto null = line.find('\0');
        if (null != line.npos) throw Error(location(null), "source contains a null byte");
        std::size_t pos = 0;
        while (pos < line.size() && line[pos] == ' ') ++pos;
        if (pos == line.size() || line[pos] == '#') continue;
        if (line[pos] == '\t' || line[pos] == '\f') throw Error(location(pos), "indent with spaces");
        if (brackets.empty() && !continued && !nested) {
            if (pos % 4 != 0) throw Error(location(pos), "indentation must be a multiple of four spaces");
            if (pos > indentation.back()) {
                indentation.push_back(pos);
                tokens.push_back({Tag::indent, "", location(pos)});
            } else if (pos < indentation.back()) {
                while (pos < indentation.back()) {
                    indentation.pop_back();
                    tokens.push_back({Tag::dedent, "", location(pos)});
                }
                if (pos != indentation.back()) throw Error(location(pos), "inconsistent indentation");
            }
        }
        continued = false;
        while (pos < line.size()) {
            const auto start = pos;
            const char ch = line[pos++];
            if (ch == '#') break;
            if (ch == ' ' || ch == '\t') continue;
            if (ch == '\\' && pos == line.size()) {
                continued = true;
                break;
            }
            if (ch == 'f' && pos < line.size() && line[pos] == '"' && (start == 0 || !letter(line[start - 1]))) {
                // f-string: keep braces raw; the parser splits them into expressions.
                ++pos;
                std::string value;
                int braces = 0;  // inside {...} a quote starts a nested string instead of ending the f-string
                while (pos < line.size() && (line[pos] != '"' || braces > 0)) {
                    char character = line[pos++];
                    if (braces > 0 && character == '"') {
                        value += '"';
                        while (pos < line.size() && line[pos] != '"') value += line[pos++];
                        if (pos == line.size()) throw Error(location(start), "unterminated string inside f-string");
                        value += line[pos++];
                        continue;
                    }
                    if (character == '{') {
                        if (pos < line.size() && line[pos] == '{' && braces == 0) { value += "{{"; ++pos; continue; }
                        ++braces;
                    } else if (character == '}') {
                        if (braces > 0) --braces;
                        else if (pos < line.size() && line[pos] == '}') { value += "}}"; ++pos; continue; }
                    }
                    if (character == '\\') {
                        if (pos == line.size()) throw Error(location(start), "unterminated string");
                        switch (line[pos++]) {
                            case 'n': character = '\n'; break;
                            case 'r': character = '\r'; break;
                            case 't': character = '\t'; break;
                            case '0': character = '\0'; break;
                            case '"': character = '"'; break;
                            case '\\': character = '\\'; break;
                            case '{': value += '{'; character = '{'; break;   // "\{" is a literal brace
                            case '}': value += '}'; character = '}'; break;
                            default: throw Error(location(pos - 1), "unknown string escape");
                        }
                    }
                    value += character;
                }
                if (pos == line.size()) throw Error(location(start), "unterminated string");
                ++pos;
                tokens.push_back({Tag::fstring, std::move(value), location(start, pos - start)});
                continue;
            }
            if (letter(ch)) {
                while (pos < line.size() && (letter(line[pos]) || digit(line[pos]))) ++pos;
                tokens.push_back({Tag::name, std::string(line.substr(start, pos - start)), location(start, pos - start)});
                continue;
            }
            if (digit(ch)) {
                auto tag = Tag::integer;
                while (pos < line.size() && (digit(line[pos]) || line[pos] == '_')) ++pos;
                if (pos + 1 < line.size() && line[pos] == '.' && digit(line[pos + 1])) {
                    tag = Tag::floating;
                    ++pos;
                    while (pos < line.size() && digit(line[pos])) ++pos;
                }
                if (pos < line.size() && (line[pos] == 'e' || line[pos] == 'E')) {
                    auto probe = pos + 1;
                    if (probe < line.size() && (line[probe] == '+' || line[probe] == '-')) ++probe;
                    if (probe < line.size() && digit(line[probe])) {
                        tag = Tag::floating;
                        pos = probe;
                        while (pos < line.size() && digit(line[pos])) ++pos;
                    }
                }
                if (pos < line.size() && (letter(line[pos]) || line[pos] == '.')) {
                    throw Error(location(pos), "unexpected character after a number");
                }
                std::string text;
                for (const char c : line.substr(start, pos - start)) if (c != '_') text += c;
                tokens.push_back({tag, std::move(text), location(start, pos - start)});
                continue;
            }
            if (ch == '"') {
                std::string value;
                while (pos < line.size() && line[pos] != '"') {
                    char character = line[pos++];
                    if (character == '\\') {
                        if (pos == line.size()) throw Error(location(start), "unterminated string");
                        switch (line[pos++]) {
                            case 'n': character = '\n'; break;
                            case 'r': character = '\r'; break;
                            case 't': character = '\t'; break;
                            case '0': character = '\0'; break;
                            case '"': character = '"'; break;
                            case '\\': character = '\\'; break;
                            default: throw Error(location(pos - 1), "unknown string escape");
                        }
                    }
                    value += character;
                }
                if (pos == line.size()) throw Error(location(start), "unterminated string");
                ++pos;
                tokens.push_back({Tag::text, std::move(value), location(start, pos - start)});
                continue;
            }
            if (std::string_view("()[]{}:,.+-*%/=!<>?&|^~").find(ch) == std::string_view::npos) {
                throw Error(location(start), "unexpected character");
            }
            if (ch == '(' || ch == '[' || ch == '{') brackets.push_back(ch == '(' ? ')' : ch == '[' ? ']' : '}');
            if (ch == ')' || ch == ']' || ch == '}') {
                if (brackets.empty() || brackets.back() != ch) throw Error(location(start), "unmatched delimiter");
                brackets.pop_back();
            }
            const auto three = line.substr(start, 3);
            const auto pair = line.substr(start, 2);
            if (three == "//=" || three == "<<=" || three == ">>=") pos += 2;
            else if (pair == "->" || pair == "=>" || pair == "//" || pair == "==" || pair == "!=" || pair == "<=" || pair == ">=" ||
                     pair == "+=" || pair == "-=" || pair == "*=" || pair == "/=" || pair == "%=" ||
                     pair == "<<" || pair == ">>" || pair == "&=" || pair == "|=" || pair == "^=") ++pos;
            tokens.push_back({Tag::symbol, std::string(line.substr(start, pos - start)), location(start, pos - start)});
        }
        if (brackets.empty() && !continued && !nested) tokens.push_back({Tag::newline, "", location(line.size())});
    }
    const Location eof = nested ? base : Location{base.line + number, 1, 1};
    if (!brackets.empty() || continued) throw Error(eof, "incomplete expression");
    while (indentation.size() > 1) {
        indentation.pop_back();
        tokens.push_back({Tag::dedent, "", eof});
    }
    tokens.push_back({Tag::end, "", eof});
    return tokens;
}

// ---------------------------------------------------------------------------
// Syntax tree
// ---------------------------------------------------------------------------

struct TypeExpr {
    Token name;                    // i64, list, fn, Option, a struct name, or a type parameter
    std::optional<Token> qualifier;  // module alias for `alias.Name`
    std::vector<TypeExpr> args;    // generic arguments, or fn parameters
    bool has_result = false;       // fn types: result is the last entry of args
};

struct Statement;

enum class Form {
    integer, floating, boolean, text, name, list, dict, unary, binary, call, index, slice, field,
    method_call, lambda, question,
};

struct Expr {
    Form form;
    Token token;
    std::vector<Expr> parts;
    std::int64_t number = 0;
    double real = 0;
    std::size_t height = 1;
    bool has_low = false, has_high = false;             // slices
    std::vector<TypeExpr> type_args;                    // explicit generic arguments on calls
    // lambdas
    std::vector<std::pair<Token, TypeExpr>> params;
    std::optional<TypeExpr> result;
    std::shared_ptr<std::vector<Statement>> block;      // block-bodied lambda; otherwise parts[0] is the body

    Expr(Form kind, Token at, std::vector<Expr> children, std::int64_t value = 0)
        : form(kind), token(std::move(at)), parts(std::move(children)), number(value) {
        for (const auto& child : parts) height = std::max(height, child.height + 1);
        if (height > 192) throw Error(token.at, "expression exceeds the compiler limit");
    }
};

enum class PatternKind { some, none, ok, err, literal, wildcard };

struct Case {
    PatternKind kind;
    Token token;
    std::optional<Token> binding;
    std::optional<Expr> literal;
    std::vector<Statement> body;
};

enum class FormStmt {
    let, var, assign, expression, result, branch, loop_range, loop_list, loop_while, stop, next, check, match
};

struct Statement {
    FormStmt form;
    Token token;
    std::optional<Expr> value;
    std::optional<Expr> target;      // assignment target or loop start
    std::optional<TypeExpr> annotation;
    std::string op;                  // "=", "+=", ...
    std::vector<Statement> body;
    std::vector<Statement> alternate;
    std::vector<Case> cases;
};

struct TypeParam {
    Token name;
    std::optional<TypeExpr> bound;   // T: Trait
};

struct FunctionDecl {
    Token name;
    std::vector<TypeParam> type_params;
    std::vector<std::pair<Token, TypeExpr>> params;
    std::optional<TypeExpr> result;
    std::vector<Statement> body;
    bool method = false;             // first parameter is `self`
};

struct StructDecl {
    Token name;
    std::vector<TypeParam> type_params;
    std::vector<TypeExpr> traits;          // struct Name(TraitA, TraitB):
    std::vector<std::pair<Token, TypeExpr>> fields;
    std::vector<FunctionDecl> methods;
};

struct TraitDecl {
    Token name;
    std::vector<FunctionDecl> methods;     // signatures only; bodies are empty
};

struct ConstDecl {
    Token name;
    Expr value;
};

struct ImportDecl {
    std::vector<Token> path;
    Token alias;
};

struct Module {
    std::vector<ImportDecl> imports;
    std::vector<ConstDecl> consts;
    std::vector<FunctionDecl> functions;
    std::vector<StructDecl> structs;
    std::vector<TraitDecl> traits;
};

bool comparison(std::string_view text) {
    return text == "==" || text == "!=" || text == "<" || text == "<=" || text == ">" || text == ">=" || text == "in";
}

int precedence(std::string_view text) {
    if (text == "or") return 1;
    if (text == "and") return 2;
    if (comparison(text)) return 4;
    if (text == "|") return 5;
    if (text == "^") return 6;
    if (text == "&") return 7;
    if (text == "<<" || text == ">>") return 8;
    if (text == "+" || text == "-") return 9;
    if (text == "*" || text == "/" || text == "//" || text == "%") return 10;
    return 0;
}
constexpr int unary_minimum = 11;  // operands of unary - and ~ bind tighter than every binary operator

struct BuiltinSpec {
    const char* name;
    Builtin id;
    int args;
};

constexpr BuiltinSpec builtins[] = {
    {"print", Builtin::print, 1}, {"argc", Builtin::argc, 0}, {"arg", Builtin::arg, 1},
    {"read_text", Builtin::read_text, 1}, {"write_text", Builtin::write_text, 2}, {"input", Builtin::input, 0},
    {"str", Builtin::to_text, 1}, {"int", Builtin::to_integer, 1}, {"float", Builtin::to_float, 1},
    {"abs", Builtin::abs, 1}, {"min", Builtin::min, 2}, {"max", Builtin::max, 2}, {"sqrt", Builtin::sqrt, 1},
    {"pow", Builtin::pow, 2}, {"floor", Builtin::floor, 1}, {"ceil", Builtin::ceil, 1}, {"round", Builtin::round, 1},
    {"push", Builtin::push, 2}, {"pop", Builtin::pop, 1}, {"insert", Builtin::insert, 3},
    {"remove_at", Builtin::remove_at, 2}, {"index_of", Builtin::index_of, 2}, {"sort", Builtin::sort, 1},
    {"reverse", Builtin::reverse, 1}, {"clear", Builtin::clear, 1},
    {"keys", Builtin::keys, 1}, {"values", Builtin::values, 1}, {"remove_key", Builtin::remove_key, 2},
    {"split", Builtin::split, 2}, {"join", Builtin::join, 2}, {"find", Builtin::find, 2},
    {"upper", Builtin::upper, 1}, {"lower", Builtin::lower, 1}, {"strip", Builtin::strip, 1},
    {"replace", Builtin::replace, 3}, {"starts_with", Builtin::starts_with, 2}, {"ends_with", Builtin::ends_with, 2},
    {"chr", Builtin::chr, 1}, {"ord", Builtin::ord, 1}, {"exit", Builtin::exit, 1}, {"time_ms", Builtin::time_ms, 0},
    {"fail", Builtin::fail, 1},
    {"is_some", Builtin::is_some, 1}, {"is_none", Builtin::is_none, 1}, {"is_ok", Builtin::is_ok, 1}, {"is_err", Builtin::is_err, 1},
    {"unwrap", Builtin::unwrap, 1}, {"unwrap_or", Builtin::unwrap_or, 2}, {"expect", Builtin::expect, 2},
    {"lookup", Builtin::lookup, 2}, {"parse_int", Builtin::parse_int, 1}, {"parse_float", Builtin::parse_float, 1}, {"env", Builtin::env, 1},
    {"random_float", Builtin::random_float, 0}, {"random_int", Builtin::random_int, 2}, {"seed", Builtin::seed, 1},
    {"file_exists", Builtin::file_exists, 1}, {"read_lines", Builtin::read_lines, 1}, {"append_text", Builtin::append_text, 2},
    {"delete_file", Builtin::delete_file, 1}, {"list_dir", Builtin::list_dir, 1}, {"make_dir", Builtin::make_dir, 1},
    {"sleep_ms", Builtin::sleep_ms, 1}, {"now_text", Builtin::now_text, 0}, {"args", Builtin::args, 0}, {"run", Builtin::run, 1},
    {"write", Builtin::write, 1}, {"eprint", Builtin::eprint, 1}, {"flush", Builtin::flush, 0},
    {"chars", Builtin::chars, 1}, {"char_count", Builtin::char_count, 1}, {"repeat", Builtin::repeat, 2},
    {"bytes", Builtin::bytes, 1}, {"from_bytes", Builtin::from_bytes, 1},
    {"sin", Builtin::sin, 1}, {"cos", Builtin::cos, 1}, {"tan", Builtin::tan, 1}, {"asin", Builtin::asin, 1},
    {"acos", Builtin::acos, 1}, {"atan", Builtin::atan, 1}, {"atan2", Builtin::atan2, 2}, {"exp", Builtin::exp, 1},
    {"log", Builtin::log, 1}, {"log2", Builtin::log2, 1}, {"log10", Builtin::log10, 1},
    {"sum", Builtin::sum, 1}, {"min_of", Builtin::min_of, 1}, {"max_of", Builtin::max_of, 1},
    {"fill", Builtin::fill, 2}, {"extend", Builtin::extend, 2},
    {"fixed", Builtin::fixed, 2}, {"to_hex", Builtin::to_hex, 1}, {"from_hex", Builtin::from_hex, 1},
    {"gc_collect", Builtin::gc_collect, 0}, {"gc_count", Builtin::gc_count, 0},
    {"tcp_listen", Builtin::tcp_listen, 1}, {"tcp_accept", Builtin::tcp_accept, 1}, {"tcp_connect", Builtin::tcp_connect, 2},
    {"tcp_read", Builtin::tcp_read, 2}, {"tcp_write", Builtin::tcp_write, 2}, {"tcp_close", Builtin::tcp_close, 1},
    {"run_capture", Builtin::run_capture, 1}, {"unix_time", Builtin::unix_time, 0}, {"hash", Builtin::hash, 1},
};

const BuiltinSpec* builtin_named(std::string_view name) {
    for (const auto& spec : builtins) if (name == spec.name) return &spec;
    return nullptr;
}

const std::unordered_set<std::string>& reserved_words() {
    static const std::unordered_set<std::string> words = {
        "def", "struct", "let", "var", "if", "elif", "else", "while", "for", "in", "break", "continue",
        "return", "assert", "and", "or", "not", "True", "False", "None", "range", "fn", "match", "case",
        "import", "as", "const", "Some", "Ok", "Err", "Option", "Result", "trait",
        "i64", "f64", "bool", "str", "list", "dict",
    };
    return words;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

class Parser {
    std::vector<Token> tokens;
    std::size_t pos = 0;
    std::size_t depth = 0;

    struct Guard {
        std::size_t& depth;
        Guard(std::size_t& level, Location at) : depth(level) {
            if (++depth > 96) throw Error(at, "program nesting exceeds the compiler limit");
        }
        ~Guard() { --depth; }
    };

    const Token& current() const { return tokens[pos]; }
    const Token& peek(std::size_t ahead = 1) const {
        return tokens[std::min(pos + ahead, tokens.size() - 1)];
    }
    Token take() {
        if (current().tag == Tag::end) throw Error(current().at, "unexpected end of source");
        return tokens[pos++];
    }
    bool matches(std::string_view text) const {
        return (current().tag == Tag::name || current().tag == Tag::symbol) && current().text == text;
    }
    bool accept(std::string_view text) {
        if (!matches(text)) return false;
        take();
        return true;
    }
    void expect(std::string_view text) {
        if (!accept(text)) throw Error(current().at, "expected '" + std::string(text) + "'");
    }
    Token expect(Tag tag, std::string_view description) {
        if (current().tag != tag) throw Error(current().at, "expected " + std::string(description));
        return take();
    }
    Token name() {
        auto word = expect(Tag::name, "an identifier");
        if (reserved_words().count(word.text)) throw Error(word.at, "reserved identifier '" + word.text + "'");
        return word;
    }
    TypeExpr type() {
        Guard guard(depth, current().at);
        auto word = expect(Tag::name, "a type");
        TypeExpr result{word, {}, {}, false};
        if (word.text == "fn") {
            expect("(");
            while (!matches(")")) {
                result.args.push_back(type());
                if (!accept(",")) break;
            }
            expect(")");
            if (accept("->")) {
                result.args.push_back(type());
                result.has_result = true;
            }
            return result;
        }
        if (reserved_words().count(word.text) && word.text != "i64" && word.text != "f64" && word.text != "bool" &&
            word.text != "str" && word.text != "list" && word.text != "dict" && word.text != "Option" && word.text != "Result") {
            throw Error(word.at, "expected a type");
        }
        if (matches(".") && peek().tag == Tag::name) {
            take();
            result.qualifier = word;
            result.name = take();
        }
        if (accept("[")) {
            do {
                result.args.push_back(type());
            } while (accept(","));
            expect("]");
        }
        const auto& n = result.name.text;
        if (!result.qualifier) {
            if ((n == "list" || n == "Option") && result.args.size() != 1) throw Error(word.at, n + " takes one type argument");
            if ((n == "dict" || n == "Result") && result.args.size() != 2) throw Error(word.at, n + " takes two type arguments");
        }
        return result;
    }
    std::vector<Expr> expressions(std::string_view close) {
        std::vector<Expr> result;
        if (!matches(close)) {
            result.push_back(expression());
            while (accept(",") && !matches(close)) result.push_back(expression());
        }
        expect(close);
        return result;
    }
    // Explicit generic arguments: name[T, U](...). Backtracks when the brackets are an index.
    std::optional<std::vector<TypeExpr>> try_type_args() {
        if (!matches("[")) return std::nullopt;
        const auto saved = pos;
        try {
            take();
            std::vector<TypeExpr> args;
            do {
                args.push_back(type());
            } while (accept(","));
            expect("]");
            if (!matches("(")) throw Error(current().at, "not a generic call");
            return args;
        } catch (const Error&) {
            pos = saved;
            return std::nullopt;
        }
    }
    Expr fstring(const Token& token) {
        // Split "text {expr} text" into a chain of concatenations with str() applied to each expression.
        std::vector<Expr> pieces;
        std::string literal;
        const auto& text = token.text;
        const auto flush_literal = [&]() {
            if (literal.empty()) return;
            Token piece{Tag::text, literal, token.at};
            pieces.push_back({Form::text, piece, {}});
            literal.clear();
        };
        for (std::size_t i = 0; i < text.size();) {
            if (text[i] == '{' && i + 1 < text.size() && text[i + 1] == '{') { literal += '{'; i += 2; continue; }
            if (text[i] == '}' && i + 1 < text.size() && text[i + 1] == '}') { literal += '}'; i += 2; continue; }
            if (text[i] == '{') {
                std::size_t level = 0, j = i + 1;
                bool quoted = false;
                for (; j < text.size(); ++j) {
                    if (text[j] == '"') quoted = !quoted;
                    if (quoted) continue;
                    if (text[j] == '{' || text[j] == '(' || text[j] == '[') ++level;
                    else if (text[j] == ')' || text[j] == ']') --level;
                    else if (text[j] == '}') { if (level == 0) break; --level; }
                }
                if (j >= text.size()) throw Error(token.at, "unterminated '{' in f-string");
                const auto inner = text.substr(i + 1, j - i - 1);
                if (inner.find_first_not_of(" \t") == std::string::npos) throw Error(token.at, "empty '{}' in f-string");
                flush_literal();
                Location at{token.at.line, static_cast<std::uint32_t>(token.at.column + 2 + i + 1), static_cast<std::uint32_t>(inner.size())};
                Parser sub(inner, at, true);
                auto inner_expr = sub.expression();
                if (sub.current().tag != Tag::end) throw Error(sub.current().at, "unexpected text in f-string expression");
                Token call{Tag::name, "str", at};
                std::vector<Expr> args;
                args.push_back(std::move(inner_expr));
                pieces.push_back({Form::call, call, std::move(args)});
                i = j + 1;
                continue;
            }
            literal += text[i++];
        }
        flush_literal();
        if (pieces.empty()) return {Form::text, Token{Tag::text, "", token.at}, {}};
        Expr result = std::move(pieces[0]);
        if (result.form != Form::text) {
            // make sure the chain starts as a str so that + means concatenation
            std::vector<Expr> parts;
            parts.push_back({Form::text, Token{Tag::text, "", token.at}, {}});
            parts.push_back(std::move(result));
            result = {Form::binary, Token{Tag::symbol, "+", token.at}, std::move(parts)};
        }
        for (std::size_t k = 1; k < pieces.size(); ++k) {
            std::vector<Expr> parts;
            parts.push_back(std::move(result));
            parts.push_back(std::move(pieces[k]));
            result = {Form::binary, Token{Tag::symbol, "+", token.at}, std::move(parts)};
        }
        return result;
    }
    Expr lambda(const Token& at) {
        Expr result{Form::lambda, at, {}};
        expect("(");
        while (!matches(")")) {
            auto parameter = name();
            expect(":");
            result.params.emplace_back(std::move(parameter), type());
            if (!accept(",")) break;
        }
        expect(")");
        if (accept("->")) result.result = type();
        if (accept("=>")) {
            result.parts.push_back(expression());
            return result;
        }
        expect(":");
        result.block = std::make_shared<std::vector<Statement>>(block());
        return result;
    }
    Expr atom(int minimum) {
        const auto at = current();
        if (matches("-") || matches("not") || matches("~")) {
            take();
            if (at.text == "not" && minimum > 3) throw Error(at.at, "parenthesize 'not' here");
            if (at.text == "-" && current().tag == Tag::integer && current().text == "9223372036854775808") {
                take();
                return {Form::integer, at, {}, std::numeric_limits<std::int64_t>::min()};
            }
            return {Form::unary, at, {expression(at.text == "not" ? 3 : unary_minimum)}};
        }
        if (accept("(")) {
            auto inner = expression();
            expect(")");
            return inner;
        }
        if (accept("[")) return {Form::list, at, expressions("]")};
        if (accept("{")) {
            std::vector<Expr> parts;
            if (!matches("}")) {
                do {
                    if (matches("}")) break;
                    parts.push_back(expression());
                    expect(":");
                    parts.push_back(expression());
                } while (accept(","));
            }
            expect("}");
            return {Form::dict, at, std::move(parts)};
        }
        if (at.tag == Tag::integer) {
            take();
            std::int64_t value{};
            const auto end = at.text.data() + at.text.size();
            const auto parsed = std::from_chars(at.text.data(), end, value);
            if (parsed.ptr != end || (at.text.size() > 1 && at.text[0] == '0')) {
                throw Error(at.at, "expected a decimal integer without leading zeroes");
            }
            if (parsed.ec != std::errc{}) throw Error(at.at, "integer literal is outside the i64 range");
            return {Form::integer, at, {}, value};
        }
        if (at.tag == Tag::floating) {
            take();
            double value{};
            const auto end = at.text.data() + at.text.size();
            const auto parsed = std::from_chars(at.text.data(), end, value);
            if (parsed.ptr != end || parsed.ec != std::errc{}) throw Error(at.at, "invalid float literal");
            Expr result{Form::floating, at, {}};
            result.real = value;
            return result;
        }
        if (at.tag == Tag::text) {
            take();
            return {Form::text, at, {}};
        }
        if (at.tag == Tag::fstring) {
            take();
            return fstring(at);
        }
        if (accept("True") || accept("False")) return {Form::boolean, at, {}, at.text == "True"};
        if (accept("None")) return {Form::call, at, {}};
        if (matches("fn")) {
            take();
            return lambda(at);
        }
        if ((matches("str") || matches("Some") || matches("Ok") || matches("Err")) && peek().tag == Tag::symbol && peek().text == "(") {
            take();
            take();
            return {Form::call, at, expressions(")")};
        }
        auto id = name();
        if (auto type_args = try_type_args()) {
            expect("(");
            Expr call{Form::call, std::move(id), expressions(")")};
            call.type_args = std::move(*type_args);
            return call;
        }
        if (accept("(")) return {Form::call, std::move(id), expressions(")")};
        return {Form::name, std::move(id), {}};
    }
    Expr postfix(Expr left) {
        std::size_t operators = 0;
        for (;;) {
            if (matches("[")) {
                if (++operators > 96) throw Error(current().at, "expression exceeds the compiler limit");
                auto bracket = take();
                std::vector<Expr> parts;
                parts.push_back(std::move(left));
                if (accept(":")) {
                    Expr result{Form::slice, std::move(bracket), {}};
                    if (!matches("]")) {
                        parts.push_back(expression());
                        result.has_high = true;
                    }
                    expect("]");
                    result.parts = std::move(parts);
                    left = std::move(result);
                    continue;
                }
                parts.push_back(expression());
                if (accept(":")) {
                    Expr result{Form::slice, std::move(bracket), {}};
                    result.has_low = true;
                    if (!matches("]")) {
                        parts.push_back(expression());
                        result.has_high = true;
                    }
                    expect("]");
                    result.parts = std::move(parts);
                    left = std::move(result);
                    continue;
                }
                expect("]");
                left = {Form::index, std::move(bracket), std::move(parts)};
                continue;
            }
            if (matches(".")) {
                if (++operators > 96) throw Error(current().at, "expression exceeds the compiler limit");
                take();
                auto member = name();
                std::vector<Expr> parts;
                parts.push_back(std::move(left));
                if (auto type_args = try_type_args()) {
                    expect("(");
                    auto args = expressions(")");
                    for (auto& arg : args) parts.push_back(std::move(arg));
                    Expr call{Form::method_call, std::move(member), std::move(parts)};
                    call.type_args = std::move(*type_args);
                    left = std::move(call);
                    continue;
                }
                if (accept("(")) {
                    auto args = expressions(")");
                    for (auto& arg : args) parts.push_back(std::move(arg));
                    left = {Form::method_call, std::move(member), std::move(parts)};
                    continue;
                }
                left = {Form::field, std::move(member), std::move(parts)};
                continue;
            }
            if (matches("?")) {
                auto mark = take();
                std::vector<Expr> parts;
                parts.push_back(std::move(left));
                left = {Form::question, std::move(mark), std::move(parts)};
                continue;
            }
            return left;
        }
    }
    Expr expression(int minimum = 1) {
        Guard guard(depth, current().at);
        auto left = postfix(atom(minimum));
        std::size_t operators = 0;
        for (;;) {
            if ((current().tag != Tag::symbol && current().tag != Tag::name) ||
                precedence(current().text) < minimum) break;
            if (++operators > 96) throw Error(current().at, "expression exceeds the compiler limit");
            auto op = take();
            const auto level = precedence(op.text);
            auto right = expression(level + 1);
            if (level == 4 && (current().tag == Tag::symbol || current().tag == Tag::name) &&
                comparison(current().text)) {
                throw Error(current().at, "use 'and' to combine comparisons");
            }
            std::vector<Expr> parts;
            parts.push_back(std::move(left));
            parts.push_back(std::move(right));
            left = {Form::binary, std::move(op), std::move(parts)};
        }
        return left;
    }
    std::vector<Statement> block() {
        Guard guard(depth, current().at);
        expect(Tag::newline, "a newline");
        expect(Tag::indent, "an indented block");
        std::vector<Statement> body;
        while (current().tag != Tag::dedent) body.push_back(statement());
        take();
        return body;
    }
    Statement branch(const Token& at) {
        Statement result{FormStmt::branch, at, expression(), {}, {}, "", {}, {}, {}};
        expect(":");
        result.body = block();
        if (matches("elif")) {
            auto word = take();
            result.alternate.push_back(branch(word));
        } else if (accept("else")) {
            expect(":");
            result.alternate = block();
        }
        return result;
    }
    Case match_case() {
        auto at = current();
        expect("case");
        Case result{PatternKind::wildcard, at, {}, {}, {}};
        if (accept("_")) {
            result.kind = PatternKind::wildcard;
        } else if (matches("Some") || matches("Ok") || matches("Err")) {
            auto word = take();
            result.kind = word.text == "Some" ? PatternKind::some : word.text == "Ok" ? PatternKind::ok : PatternKind::err;
            expect("(");
            if (accept("_")) result.binding = std::nullopt;
            else result.binding = name();
            expect(")");
        } else if (accept("None")) {
            result.kind = PatternKind::none;
        } else {
            result.kind = PatternKind::literal;
            result.literal = expression();
        }
        expect(":");
        result.body = block();
        return result;
    }
    Statement statement() {
        const auto at = current();
        if (accept("if")) return branch(at);
        if (accept("while")) {
            Statement result{FormStmt::loop_while, at, expression(), {}, {}, "", {}, {}, {}};
            expect(":");
            result.body = block();
            return result;
        }
        if (accept("for")) {
            auto index = name();
            expect("in");
            if (accept("range")) {
                expect("(");
                auto first = expression();
                std::optional<Expr> second;
                if (accept(",")) second = expression();
                expect(")");
                expect(":");
                Statement result{FormStmt::loop_range, std::move(index), {}, {}, {}, "", {}, {}, {}};
                if (second) {
                    result.target = std::move(first);
                    result.value = std::move(*second);
                } else {
                    result.value = std::move(first);
                }
                result.body = block();
                return result;
            }
            Statement result{FormStmt::loop_list, std::move(index), expression(), {}, {}, "", {}, {}, {}};
            expect(":");
            result.body = block();
            return result;
        }
        if (accept("match")) {
            Statement result{FormStmt::match, at, expression(), {}, {}, "", {}, {}, {}};
            expect(":");
            expect(Tag::newline, "a newline");
            expect(Tag::indent, "an indented block");
            while (current().tag != Tag::dedent) result.cases.push_back(match_case());
            take();
            if (result.cases.empty()) throw Error(at.at, "match needs at least one case");
            return result;
        }
        if (accept("break") || accept("continue")) {
            expect(Tag::newline, "a newline");
            return {at.text == "break" ? FormStmt::stop : FormStmt::next, at, {}, {}, {}, "", {}, {}, {}};
        }
        if (accept("return")) {
            Statement result{FormStmt::result, at, {}, {}, {}, "", {}, {}, {}};
            if (current().tag != Tag::newline) result.value = expression();
            expect(Tag::newline, "a newline");
            return result;
        }
        if (accept("assert")) {
            Statement result{FormStmt::check, at, expression(), {}, {}, "", {}, {}, {}};
            expect(Tag::newline, "a newline");
            return result;
        }
        if (matches("let") || matches("var")) {
            take();
            auto id = name();
            Statement result{at.text == "let" ? FormStmt::let : FormStmt::var, std::move(id), {}, {}, {}, "", {}, {}, {}};
            if (accept(":")) result.annotation = type();
            expect("=");
            result.value = expression();
            if (!(result.value->form == Form::lambda && result.value->block)) expect(Tag::newline, "a newline");
            return result;
        }
        auto target = expression();
        if (current().tag == Tag::symbol) {
            const auto& op = current().text;
            if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" || op == "//=" || op == "%=" ||
                op == "&=" || op == "|=" || op == "^=" || op == "<<=" || op == ">>=") {
                take();
                if (target.form != Form::name && target.form != Form::index && target.form != Form::field) {
                    throw Error(target.token.at, "cannot assign to this expression");
                }
                Statement result{FormStmt::assign, at, expression(), std::move(target), {}, op, {}, {}, {}};
                if (!(result.value->form == Form::lambda && result.value->block)) expect(Tag::newline, "a newline");
                return result;
            }
        }
        if (target.form != Form::call && target.form != Form::method_call && target.form != Form::question) {
            throw Error(at.at, "expected a statement");
        }
        expect(Tag::newline, "a newline");
        return {FormStmt::expression, at, std::move(target), {}, {}, "", {}, {}, {}};
    }
    std::vector<TypeParam> type_params() {
        std::vector<TypeParam> out;
        if (!accept("[")) return out;
        do {
            TypeParam param{name(), std::nullopt};
            if (accept(":")) param.bound = type();
            out.push_back(std::move(param));
        } while (accept(","));
        expect("]");
        return out;
    }
    FunctionDecl function(bool method, bool signature_only = false) {
        expect("def");
        auto id = name();
        FunctionDecl decl{std::move(id), {}, {}, {}, {}, method};
        decl.type_params = type_params();
        expect("(");
        if (method) {
            auto self = expect(Tag::name, "self");
            if (self.text != "self") throw Error(self.at, "a method's first parameter must be self");
            decl.params.emplace_back(self, TypeExpr{Token{Tag::name, "self", self.at}, {}, {}, false});
            if (!matches(")")) expect(",");
        }
        while (!matches(")")) {
            auto parameter = name();
            expect(":");
            decl.params.emplace_back(std::move(parameter), type());
            if (!accept(",")) break;
        }
        expect(")");
        if (accept("->")) decl.result = type();
        if (signature_only) {
            expect(Tag::newline, "a newline");
            return decl;
        }
        expect(":");
        decl.body = block();
        return decl;
    }
    TraitDecl trait() {
        expect("trait");
        auto id = name();
        TraitDecl result{std::move(id), {}};
        expect(":");
        expect(Tag::newline, "a newline");
        expect(Tag::indent, "an indented block");
        while (current().tag != Tag::dedent) {
            if (!matches("def")) throw Error(current().at, "a trait body lists method signatures: def name(self, ...) -> type");
            result.methods.push_back(function(true, true));
        }
        take();
        if (result.methods.empty()) throw Error(result.name.at, "a trait needs at least one method");
        return result;
    }
    StructDecl structure() {
        expect("struct");
        auto id = name();
        StructDecl result{std::move(id), {}, {}, {}, {}};
        result.type_params = type_params();
        if (accept("(")) {
            while (!matches(")")) {
                result.traits.push_back(type());
                if (!accept(",")) break;
            }
            expect(")");
        }
        expect(":");
        expect(Tag::newline, "a newline");
        expect(Tag::indent, "an indented block");
        while (current().tag != Tag::dedent) {
            if (matches("def")) {
                result.methods.push_back(function(true));
                continue;
            }
            auto field = name();
            expect(":");
            result.fields.emplace_back(std::move(field), type());
            expect(Tag::newline, "a newline");
        }
        take();
        return result;
    }
public:
    explicit Parser(std::string_view text, Location base = {1, 1, 1}, bool nested = false)
        : tokens(lex(text, base, nested)) {}
    Expr expression_only() { return expression(); }
    const Token& current_token() const { return current(); }
    Module parse() {
        Module result;
        while (current().tag != Tag::end) {
            if (matches("struct")) result.structs.push_back(structure());
            else if (matches("trait")) result.traits.push_back(trait());
            else if (matches("def")) result.functions.push_back(function(false));
            else if (accept("import")) {
                ImportDecl decl;
                decl.path.push_back(name());
                while (accept(".")) decl.path.push_back(name());
                decl.alias = accept("as") ? name() : decl.path.back();
                expect(Tag::newline, "a newline");
                result.imports.push_back(std::move(decl));
            } else if (accept("const")) {
                auto id = name();
                expect("=");
                auto value = expression();
                expect(Tag::newline, "a newline");
                result.consts.push_back({std::move(id), std::move(value)});
            } else {
                throw Error(current().at, "expected 'def', 'struct', 'trait', 'const', or 'import'");
            }
        }
        return result;
    }
};

// ---------------------------------------------------------------------------
// Static facts used for the live scanner's warnings
// ---------------------------------------------------------------------------

struct Interval {
    std::int64_t low;
    std::int64_t high;
};

struct Fact {
    TypeId type;
    std::optional<Interval> interval;
    std::optional<bool> truth;
    std::optional<std::int64_t> extent;  // str length or list literal length

    Fact(TypeId kind) : type(kind) {}
    static Fact integer(std::int64_t low, std::int64_t high) {
        Fact fact(1);
        fact.interval = Interval{low, high};
        return fact;
    }
    static Fact boolean(std::optional<bool> value) {
        Fact fact(3);
        fact.truth = value;
        return fact;
    }
    std::optional<std::int64_t> fixed() const {
        if (interval && interval->low == interval->high) return interval->low;
        return std::nullopt;
    }
};

struct Binding {
    std::uint32_t slot;
    bool mutable_;
    Fact fact;
    bool captured = false;
};

constexpr TypeId type_unit = 0, type_int = 1, type_float = 2, type_bool = 3, type_text = 4;

Op binary_op(const std::string& text) {
    static const std::unordered_map<std::string, Op> operators = {
        {"+", Op::add}, {"-", Op::subtract}, {"*", Op::multiply}, {"//", Op::divide}, {"/", Op::divide},
        {"%", Op::modulo}, {"==", Op::equal}, {"!=", Op::not_equal}, {"<", Op::less},
        {"<=", Op::less_equal}, {">", Op::greater}, {">=", Op::greater_equal}, {"in", Op::contains},
        {"&", Op::bit_and}, {"|", Op::bit_or}, {"^", Op::bit_xor}, {"<<", Op::shift_left}, {">>", Op::shift_right},
    };
    return operators.at(text);
}

// ---------------------------------------------------------------------------
// Escape analysis: which names are used in ways that could change a list's length?
// ---------------------------------------------------------------------------

void collect_escapes(const std::vector<Statement>& statements, std::unordered_set<std::string>& escaped);

void collect_escapes(const Expr& expr, std::unordered_set<std::string>& escaped) {
    switch (expr.form) {
        case Form::name: escaped.insert(expr.token.text); return;
        case Form::index: case Form::slice:
            if (expr.parts[0].form != Form::name) collect_escapes(expr.parts[0], escaped);
            for (std::size_t i = 1; i < expr.parts.size(); ++i) collect_escapes(expr.parts[i], escaped);
            return;
        case Form::call:
            if (expr.token.text == "len" && expr.parts.size() == 1 && expr.parts[0].form == Form::name) return;
            break;
        case Form::lambda:
            if (expr.block) collect_escapes(*expr.block, escaped);
            break;
        default: break;
    }
    for (const auto& part : expr.parts) collect_escapes(part, escaped);
}

void collect_escapes(const std::vector<Statement>& statements, std::unordered_set<std::string>& escaped) {
    for (const auto& stmt : statements) {
        if (stmt.value) {
            if (!(stmt.form == FormStmt::loop_list && stmt.value->form == Form::name)) collect_escapes(*stmt.value, escaped);
        }
        if (stmt.target) collect_escapes(*stmt.target, escaped);
        collect_escapes(stmt.body, escaped);
        collect_escapes(stmt.alternate, escaped);
        for (const auto& item : stmt.cases) {
            if (item.literal) collect_escapes(*item.literal, escaped);
            collect_escapes(item.body, escaped);
        }
    }
}

// Names a lambda body refers to and names it declares (used to decide what a closure captures).
void collect_names(const std::vector<Statement>& statements, std::vector<std::string>& used, std::unordered_set<std::string>& declared);

void collect_names(const Expr& expr, std::vector<std::string>& used, std::unordered_set<std::string>& declared) {
    if (expr.form == Form::name || expr.form == Form::call) used.push_back(expr.token.text);
    if (expr.form == Form::lambda) {
        for (const auto& [parameter, type] : expr.params) declared.insert(parameter.text);
        if (expr.block) collect_names(*expr.block, used, declared);
    }
    for (const auto& part : expr.parts) collect_names(part, used, declared);
}

void collect_names(const std::vector<Statement>& statements, std::vector<std::string>& used, std::unordered_set<std::string>& declared) {
    for (const auto& stmt : statements) {
        if (stmt.form == FormStmt::let || stmt.form == FormStmt::var || stmt.form == FormStmt::loop_range || stmt.form == FormStmt::loop_list) {
            declared.insert(stmt.token.text);
        }
        if (stmt.value) collect_names(*stmt.value, used, declared);
        if (stmt.target) collect_names(*stmt.target, used, declared);
        collect_names(stmt.body, used, declared);
        collect_names(stmt.alternate, used, declared);
        for (const auto& item : stmt.cases) {
            if (item.binding) declared.insert(item.binding->text);
            if (item.literal) collect_names(*item.literal, used, declared);
            collect_names(item.body, used, declared);
        }
    }
}

// ---------------------------------------------------------------------------
// Peephole pass: fuse common instruction pairs into superinstructions.
// ---------------------------------------------------------------------------

std::optional<std::int64_t> jump_target(const Instruction& item) {
    switch (item.op) {
        case Op::jump: case Op::jump_false: case Op::jump_true: return item.operand;
        case Op::jump_unless: case Op::loop_init: case Op::loop_next: case Op::iter_next: case Op::jump_unless_locals:
            return item.operand & 0xFFFFFFFF;
        default: return std::nullopt;
    }
}

void set_jump_target(Instruction& item, std::int64_t target) {
    switch (item.op) {
        case Op::jump: case Op::jump_false: case Op::jump_true: item.operand = target; break;
        default: item.operand = (item.operand & ~std::int64_t{0xFFFFFFFF}) | target; break;
    }
}

bool fits_int32(std::int64_t value) {
    return value >= std::numeric_limits<std::int32_t>::min() && value <= std::numeric_limits<std::int32_t>::max();
}

template <typename Fuse>
void rewrite(std::vector<Instruction>& code, Fuse fuse) {
    std::vector<bool> is_target(code.size() + 1, false);
    for (const auto& item : code) {
        if (const auto target = jump_target(item)) is_target[static_cast<std::size_t>(*target)] = true;
    }
    std::vector<Instruction> out;
    std::vector<std::int64_t> map(code.size() + 1);
    out.reserve(code.size());
    for (std::size_t i = 0; i < code.size();) {
        map[i] = static_cast<std::int64_t>(out.size());
        const bool next_is_target = i + 1 < code.size() && is_target[i + 1];
        if (!next_is_target && i + 1 < code.size()) {
            if (auto fused = fuse(code[i], code[i + 1])) {
                out.push_back(*fused);
                map[i + 1] = static_cast<std::int64_t>(out.size()) - 1;
                i += 2;
                continue;
            }
        }
        out.push_back(code[i]);
        ++i;
    }
    map[code.size()] = static_cast<std::int64_t>(out.size());
    for (auto& item : out) {
        if (const auto target = jump_target(item)) set_jump_target(item, map[static_cast<std::size_t>(*target)]);
    }
    code = std::move(out);
}

void optimize(Function& function) {
    rewrite(function.code, [](const Instruction& a, const Instruction& b) -> std::optional<Instruction> {
        if (a.op == Op::load && b.op == Op::load) {
            return Instruction{Op::load2, a.operand | (b.operand << 32), a.at};
        }
        if (a.op == Op::load && b.op == Op::push_integer && fits_int32(b.operand)) {
            return Instruction{Op::load_integer, a.operand | (static_cast<std::int64_t>(static_cast<std::uint32_t>(b.operand)) << 32), a.at};
        }
        if ((a.op == Op::add || a.op == Op::subtract || a.op == Op::multiply) && b.op == Op::store) {
            const auto op = a.op == Op::add ? Op::add_store : a.op == Op::subtract ? Op::subtract_store : Op::multiply_store;
            return Instruction{op, b.operand, a.at};
        }
        return std::nullopt;
    });
    rewrite(function.code, [](const Instruction& a, const Instruction& b) -> std::optional<Instruction> {
        if (a.op == Op::load2 && b.op == Op::jump_unless) {
            const auto x = a.operand & 0xFFFFFFFF, y = a.operand >> 32;
            if (x < 4096 && y < 4096) {
                const auto cmp = b.operand >> 32;
                return Instruction{Op::jump_unless_locals, (cmp << 56) | (x << 44) | (y << 32) | (b.operand & 0xFFFFFFFF), b.at};
            }
        }
        return std::nullopt;
    });
}

// ---------------------------------------------------------------------------
// Modules
// ---------------------------------------------------------------------------

struct ConstValue {
    TypeId type;
    std::int64_t integer = 0;
    double real = 0;
    bool truth = false;
    std::string text;
};

struct Unit {
    std::string key;                       // canonical path, or "" for the root
    std::string display;                   // "" for the root, else the module's name used in function names
    std::filesystem::path directory;
    std::string file;                      // for error attribution ("" for the root)
    Module ast;
    std::unordered_map<std::string, std::uint32_t> functions;
    std::unordered_map<std::string, const FunctionDecl*> generic_functions;
    std::unordered_map<std::string, std::uint32_t> structs;
    std::unordered_map<std::string, const StructDecl*> generic_structs;
    std::unordered_map<std::string, std::uint32_t> traits;
    std::unordered_map<std::string, ConstValue> consts;
    std::unordered_map<std::string, std::size_t> imports;
    bool loading = false;
};

using Subst = std::unordered_map<std::string, TypeId>;

struct StructOrigin {
    std::size_t unit;
    const StructDecl* decl;
    std::vector<TypeId> args;
    Subst subst;
};

// ---------------------------------------------------------------------------
// Compiler: type checking and bytecode generation in one pass
// ---------------------------------------------------------------------------

class Compiler {
    Program program;
    std::vector<Finding>& findings;
    const CompileOptions& options;
    std::vector<std::unique_ptr<Unit>> units;
    std::unordered_map<std::string, std::uint32_t> text_ids;
    std::unordered_map<std::uint32_t, StructOrigin> struct_origins;
    std::unordered_map<std::uint32_t, std::unordered_map<std::string, std::uint32_t>> methods;  // struct id -> name -> function
    std::unordered_map<std::string, std::uint32_t> instantiations;                               // generic function cache
    std::unordered_map<std::string, std::uint32_t> struct_instantiations;
    std::size_t lambda_counter = 0;
    std::vector<TypeId> last_lambda_captures;

    struct Loop {
        std::vector<std::int64_t> breaks;
        std::vector<std::int64_t> continues;
    };
    struct Frame {
        std::uint32_t function = 0;
        std::size_t unit = 0;
        std::vector<std::unordered_map<std::string, Binding>> scopes;
        std::vector<Loop> loops;
        std::unordered_set<std::string> escaped;
        Subst subst;
        std::unordered_set<std::string> type_params;
    };
    Frame* frame = nullptr;

    Function& fn() { return program.functions[frame->function]; }
    const Function& fn() const { return program.functions[frame->function]; }
    Unit& unit() { return *units[frame->unit]; }

    // --- types ---
    TypeId intern(TypeInfo info) {
        for (std::size_t i = 0; i < program.types.size(); ++i) {
            if (program.types[i] == info) return static_cast<TypeId>(i);
        }
        if (program.types.size() >= value_limit) throw Error({}, "too many distinct types");
        program.types.push_back(std::move(info));
        return static_cast<TypeId>(program.types.size() - 1);
    }
    TypeId list_of(TypeId element) { return intern({Kind::list, element, 0, {}}); }
    TypeId dict_of(TypeId key, TypeId value) { return intern({Kind::dict, key, value, {}}); }
    TypeId option_of(TypeId payload) { return intern({Kind::option, payload, 0, {}}); }
    TypeId result_of(TypeId payload, TypeId error) { return intern({Kind::result, payload, error, {}}); }
    TypeId function_of(std::vector<TypeId> params, TypeId result) { return intern({Kind::function, result, 0, std::move(params)}); }
    TypeId object_of(std::uint32_t structure) { return intern({Kind::object, structure, 0, {}}); }
    TypeId trait_of(std::uint32_t trait) { return intern({Kind::trait_object, trait, 0, {}}); }
    // Does a value of `actual` fit where `expected` is required? Structs fit the traits they implement.
    bool assignable(TypeId actual, TypeId expected) const {
        if (actual == expected) return true;
        const auto& e = info(expected);
        if (e.kind != Kind::trait_object) return false;
        return implements(actual, e.first);
    }
    bool implements(TypeId type, std::uint32_t trait) const {
        const auto& t = info(type);
        if (t.kind == Kind::trait_object) return t.first == trait;
        if (t.kind != Kind::object) return false;
        for (const auto& [id, table] : program.structs[t.first].vtables) if (id == trait) return true;
        return false;
    }
    const TypeInfo& info(TypeId type) const { return program.types.at(type); }
    Kind kind(TypeId type) const { return info(type).kind; }
    std::string name_of(TypeId type) const { return type_name(program, type); }

    Unit& resolve_qualifier(const Token& alias, Unit& from) {
        const auto found = from.imports.find(alias.text);
        if (found == from.imports.end()) throw Error(alias.at, "unknown module '" + alias.text + "'; import it first");
        return *units[found->second];
    }

    TypeId resolve(const TypeExpr& expr) { return resolve_in(expr, unit(), frame ? &frame->subst : nullptr); }

    TypeId resolve_in(const TypeExpr& expr, Unit& in, const Subst* subst) {
        const auto& word = expr.name.text;
        if (expr.qualifier) {
            Unit& target = resolve_qualifier(*expr.qualifier, in);
            return resolve_named(expr, target, subst);
        }
        if (subst) {
            const auto bound = subst->find(word);
            if (bound != subst->end()) {
                if (!expr.args.empty()) throw Error(expr.name.at, "type parameter '" + word + "' takes no arguments");
                return bound->second;
            }
        }
        if (word == "i64") return type_int;
        if (word == "f64") return type_float;
        if (word == "bool") return type_bool;
        if (word == "str") return type_text;
        if (word == "self") throw Error(expr.name.at, "'self' is only valid as a method parameter");
        if (word == "list") return list_of(resolve_in(expr.args.at(0), in, subst));
        if (word == "dict") {
            const auto key = resolve_in(expr.args.at(0), in, subst);
            if (key != type_int && key != type_text) throw Error(expr.args[0].name.at, "dict keys must be i64 or str");
            return dict_of(key, resolve_in(expr.args.at(1), in, subst));
        }
        if (word == "Option") return option_of(resolve_in(expr.args.at(0), in, subst));
        if (word == "Result") {
            const auto payload = resolve_in(expr.args.at(0), in, subst);
            return result_of(payload, resolve_in(expr.args.at(1), in, subst));
        }
        if (word == "fn") {
            std::vector<TypeId> params;
            const auto count = expr.args.size() - (expr.has_result ? 1 : 0);
            for (std::size_t i = 0; i < count; ++i) params.push_back(resolve_in(expr.args[i], in, subst));
            const auto result = expr.has_result ? resolve_in(expr.args.back(), in, subst) : type_unit;
            return function_of(std::move(params), result);
        }
        return resolve_named(expr, in, subst);
    }

    TypeId resolve_named(const TypeExpr& expr, Unit& in, const Subst* subst) {
        const auto& word = expr.name.text;
        if (const auto found = in.structs.find(word); found != in.structs.end()) {
            if (!expr.args.empty()) throw Error(expr.name.at, "'" + word + "' is not generic");
            return object_of(found->second);
        }
        if (const auto found = in.traits.find(word); found != in.traits.end()) {
            if (!expr.args.empty()) throw Error(expr.name.at, "'" + word + "' is a trait and takes no type arguments");
            return trait_of(found->second);
        }
        if (const auto found = in.generic_structs.find(word); found != in.generic_structs.end()) {
            const auto* decl = found->second;
            if (expr.args.size() != decl->type_params.size()) {
                throw Error(expr.name.at, "'" + word + "' takes " + std::to_string(decl->type_params.size()) + " type argument(s)");
            }
            std::vector<TypeId> args;
            for (const auto& arg : expr.args) args.push_back(resolve_in(arg, in, subst));
            return object_of(instantiate_struct(in, *decl, args));
        }
        throw Error(expr.name.at, "unknown type '" + word + "'");
    }

    std::string mangle(const std::string& base, const std::vector<TypeId>& args) const {
        std::string out = base + "[";
        for (std::size_t i = 0; i < args.size(); ++i) out += (i ? ", " : "") + name_of(args[i]);
        return out + "]";
    }

    // --- bindings & emission ---
    static bool comparison_op(Op op) {
        return op == Op::less || op == Op::less_equal || op == Op::greater || op == Op::greater_equal ||
               op == Op::equal || op == Op::not_equal;
    }
    std::int64_t emit(Op op, Location at, std::int64_t value = 0) {
        auto& code = fn().code;
        if (op == Op::jump_false && !code.empty() && comparison_op(code.back().op)) {
            auto& last = code.back();
            const auto comparison = static_cast<std::int64_t>(last.op);
            last.op = Op::jump_unless;
            last.operand = comparison << 32;
            return position_now() - 1;
        }
        const auto position = position_now();
        code.push_back({op, value, at});
        return position;
    }
    std::int64_t position_now() const { return static_cast<std::int64_t>(fn().code.size()); }
    void patch(std::int64_t index) {
        auto& target = fn().code.at(static_cast<std::size_t>(index));
        if (target.op == Op::jump_unless) target.operand = (target.operand & ~std::int64_t{0xFFFFFFFF}) | position_now();
        else target.operand = position_now();
    }
    void patch_packed(std::int64_t index, std::int64_t slot) {
        fn().code.at(static_cast<std::size_t>(index)).operand = (slot << 32) | position_now();
    }
    void require(TypeId actual, TypeId expected, Location at) const {
        if (!assignable(actual, expected)) throw Error(at, "expected " + name_of(expected) + ", found " + name_of(actual));
    }
    void require_value(const Fact& fact, Location at) const {
        if (fact.type == type_unit) throw Error(at, "this call does not return a value");
    }
    void warning(bool enabled, Location at, std::string code, std::string message) {
        if (enabled) findings.push_back({at, true, std::move(code), std::move(message), unit().file});
    }
    const Binding* lookup(const std::string& name) const {
        if (!frame) return nullptr;
        for (auto it = frame->scopes.rbegin(); it != frame->scopes.rend(); ++it) {
            const auto item = it->find(name);
            if (item != it->end()) return &item->second;
        }
        return nullptr;
    }
    const Binding& binding(const Token& token) const {
        if (const auto* found = lookup(token.text)) return *found;
        throw Error(token.at, "unknown binding '" + token.text + "'");
    }
    std::uint32_t local(Location at) {
        if (fn().locals >= value_limit) throw Error(at, "too many local bindings");
        return fn().locals++;
    }
    std::uint32_t bind(const Token& token, Fact fact, bool mutable_, std::optional<std::uint32_t> slot = {}, bool captured = false) {
        if (const auto* existing = lookup(token.text)) {
            if (existing->captured) throw Error(token.at, "'" + token.text + "' is captured from the enclosing function and cannot be redeclared here");
            throw Error(token.at, "'" + token.text + "' is already bound in this scope");
        }
        // Locals may shadow module-level names (functions, constants, structs); the local wins inside its scope.
        const auto where = slot ? *slot : local(token.at);
        Fact stored = fact;
        if (mutable_) stored = Fact(fact.type);
        if (kind(fact.type) == Kind::list && frame->escaped.count(token.text)) stored.extent = std::nullopt;
        frame->scopes.back().emplace(token.text, Binding{where, mutable_, stored, captured});
        return where;
    }
    std::uint32_t text_id(const std::string& value) {
        const auto id = static_cast<std::uint32_t>(program.texts.size());
        const auto [entry, inserted] = text_ids.emplace(value, id);
        if (inserted) program.texts.push_back(value);
        return entry->second;
    }

    // --- expressions ---
    Fact number_op(Op op, const Fact& left, const Fact& right, Location at, bool report) {
        emit(op, at);
        const auto a = left.fixed(), b = right.fixed();
        if ((op == Op::divide || op == Op::modulo) && b && *b == 0) {
            warning(report, at, "zero-divisor", "the divisor is always zero if this operation is evaluated");
            return type_int;
        }
        if (a && (b || op == Op::negate)) {
            try {
                const auto result = arithmetic(op, *a, b.value_or(0));
                return Fact::integer(result, result);
            } catch (const std::overflow_error&) {
                warning(report, at, "integer-overflow", "this operation overflows i64 if evaluated");
            }
        }
        if (left.interval && right.interval && (op == Op::add || op == Op::subtract || op == Op::multiply)) {
            try {
                const auto l = *left.interval, r = *right.interval;
                const auto values = {
                    arithmetic(op, l.low, r.low), arithmetic(op, l.low, r.high),
                    arithmetic(op, l.high, r.low), arithmetic(op, l.high, r.high),
                };
                const auto extrema = std::minmax_element(values.begin(), values.end());
                return Fact::integer(*extrema.first, *extrema.second);
            } catch (const std::overflow_error&) {
            }
        }
        return type_int;
    }

    Fact binary(const Expr& node, bool report) {
        const auto& left_expr = node.parts[0];
        const auto& right_expr = node.parts[1];
        const auto at = node.token.at;
        const auto& word = node.token.text;
        if (word == "and" || word == "or") {
            const auto left = expression(left_expr, report);
            require(left.type, type_bool, left_expr.token.at);
            const bool trigger = word == "and";
            const auto branch = emit(trigger ? Op::jump_false : Op::jump_true, at);
            const auto right = expression(right_expr, report && left.truth != !trigger);
            require(right.type, type_bool, right_expr.token.at);
            const auto end = emit(Op::jump, at);
            patch(branch);
            emit(Op::push_boolean, at, !trigger);
            patch(end);
            if (left.truth == !trigger) return Fact::boolean(!trigger);
            if (left.truth == trigger || right.truth == !trigger) return Fact::boolean(right.truth);
            return type_bool;
        }
        if (word == "in") {
            const auto left = expression(left_expr, report);
            require_value(left, left_expr.token.at);
            const auto right = expression(right_expr, report);
            const auto& container = info(right.type);
            if (container.kind == Kind::list) require(left.type, container.first, left_expr.token.at);
            else if (container.kind == Kind::dict) require(left.type, container.first, left_expr.token.at);
            else if (container.kind == Kind::text) require(left.type, type_text, left_expr.token.at);
            else throw Error(right_expr.token.at, "'in' expects a list, dict, or str on the right");
            emit(Op::contains, at);
            return type_bool;
        }

        const auto left = expression(left_expr, report);
        require_value(left, left_expr.token.at);
        const auto right = expression(right_expr, report, left.type);
        require_value(right, right_expr.token.at);
        const auto op = binary_op(word);
        if (op == Op::equal || op == Op::not_equal) {
            require(right.type, left.type, right_expr.token.at);
            emit(op, at);
            std::optional<bool> value;
            if (left.fixed() && right.fixed()) value = left.fixed() == right.fixed();
            if (left.truth && right.truth) value = left.truth == right.truth;
            if (value && op == Op::not_equal) value = !*value;
            return Fact::boolean(value);
        }
        if (comparison(word)) {
            require(right.type, left.type, right_expr.token.at);
            if (left.type != type_int && left.type != type_float && left.type != type_text) {
                throw Error(left_expr.token.at, "order comparisons apply to i64, f64, and str");
            }
            emit(op, at);
            const auto a = left.fixed(), b = right.fixed();
            if (!a || !b) return type_bool;
            if (op == Op::less) return Fact::boolean(*a < *b);
            if (op == Op::less_equal) return Fact::boolean(*a <= *b);
            if (op == Op::greater) return Fact::boolean(*a > *b);
            return Fact::boolean(*a >= *b);
        }
        if (word == "&" || word == "|" || word == "^" || word == "<<" || word == ">>") {
            require(left.type, type_int, left_expr.token.at);
            require(right.type, type_int, right_expr.token.at);
            if ((word == "<<" || word == ">>") && right.interval && (right.interval->high < 0 || right.interval->low > 63)) {
                warning(report, right_expr.token.at, "invalid-shift", "shift counts must be between 0 and 63");
            }
            emit(op, at);
            return type_int;
        }
        if (left.type == type_text) {
            if (word != "+") throw Error(at, "str supports only '+'");
            require(right.type, type_text, right_expr.token.at);
            emit(Op::concat, at);
            Fact result(type_text);
            if (left.extent && right.extent && *left.extent + *right.extent <= static_cast<std::int64_t>(text_limit)) {
                result.extent = *left.extent + *right.extent;
            }
            return result;
        }
        if (left.type == type_float) {
            require(right.type, type_float, right_expr.token.at);
            if (word == "//") throw Error(at, "f64 uses '/' for division; use floor(a / b) for an i64 result");
            emit(word == "+" ? Op::fadd : word == "-" ? Op::fsubtract : word == "*" ? Op::fmultiply
                 : word == "/" ? Op::fdivide : Op::fmodulo, at);
            return type_float;
        }
        if (left.type != type_int) throw Error(left_expr.token.at, "arithmetic expects i64 or f64, found " + name_of(left.type));
        if (right.type == type_float) throw Error(right_expr.token.at, "cannot mix i64 and f64; use float(x) or int(x)");
        require(right.type, type_int, right_expr.token.at);
        if (word == "/") throw Error(at, "i64 uses '//' for division; use float(a) / float(b) for an f64 result");
        return number_op(op, left, right, at, report);
    }

    // Compiles `args` as builtin arguments (receiver first for method sugar) and emits the builtin.
    Fact builtin_call(const Token& token, const BuiltinSpec& spec, const std::vector<const Expr*>& args, bool report) {
        const auto& name = token.text;
        if (static_cast<int>(args.size()) != spec.args) {
            throw Error(token.at, "'" + std::string(spec.name) + "' expects " + std::to_string(spec.args) + " argument(s)");
        }
        std::vector<Fact> facts;
        facts.reserve(args.size() + 1);  // references into `facts` stay valid while later arguments compile
        const auto arg = [&](std::size_t i, std::optional<TypeId> expected = {}) -> const Fact& {
            facts.push_back(expression(*args[i], report, expected));
            require_value(facts.back(), args[i]->token.at);
            if (expected) require(facts.back().type, *expected, args[i]->token.at);
            return facts.back();
        };
        const auto number = [&](std::size_t i) -> const Fact& {
            const auto& fact = arg(i);
            if (fact.type != type_int && fact.type != type_float) {
                throw Error(args[i]->token.at, "expected i64 or f64, found " + name_of(fact.type));
            }
            return fact;
        };
        const auto list = [&](std::size_t i) -> const Fact& {
            const auto& fact = arg(i);
            if (kind(fact.type) != Kind::list) throw Error(args[i]->token.at, "expected a list, found " + name_of(fact.type));
            return fact;
        };
        const auto dict = [&](std::size_t i) -> const Fact& {
            const auto& fact = arg(i);
            if (kind(fact.type) != Kind::dict) throw Error(args[i]->token.at, "expected a dict, found " + name_of(fact.type));
            return fact;
        };
        const auto variant = [&](std::size_t i, bool option) -> const Fact& {
            const auto& fact = arg(i);
            if (kind(fact.type) != (option ? Kind::option : Kind::result)) {
                throw Error(args[i]->token.at, std::string("expected ") + (option ? "an Option" : "a Result") + ", found " + name_of(fact.type));
            }
            return fact;
        };
        const auto done = [&](TypeId result) {
            emit(Op::builtin, token.at, static_cast<std::int64_t>(spec.id));
            return Fact(result);
        };
        switch (spec.id) {
            case Builtin::print: case Builtin::write: case Builtin::eprint: arg(0); return done(type_unit);
            case Builtin::to_text: arg(0); return done(type_text);
            case Builtin::argc: case Builtin::time_ms: return done(type_int);
            case Builtin::flush: return done(type_unit);
            case Builtin::arg: arg(0, type_int); return done(type_text);
            case Builtin::read_text: case Builtin::upper: case Builtin::lower: case Builtin::strip: break;
            case Builtin::write_text: case Builtin::append_text: arg(0, type_text); arg(1, type_text); return done(type_unit);
            case Builtin::input: case Builtin::now_text: return done(type_text);
            case Builtin::to_integer: {
                const auto& fact = arg(0);
                if (fact.type == type_int) return fact;
                if (fact.type != type_float && fact.type != type_text && fact.type != type_bool) {
                    throw Error(args[0]->token.at, "int expects f64, str, or bool");
                }
                return done(type_int);
            }
            case Builtin::to_float: {
                const auto& fact = arg(0);
                if (fact.type != type_int && fact.type != type_float && fact.type != type_text) {
                    throw Error(args[0]->token.at, "float expects i64, f64, or str");
                }
                return done(type_float);
            }
            case Builtin::abs: return done(number(0).type);
            case Builtin::min: case Builtin::max: {
                const auto first = arg(0).type;
                if (first != type_int && first != type_float && first != type_text) {
                    throw Error(args[0]->token.at, "min and max expect i64, f64, or str");
                }
                arg(1, first);
                return done(first);
            }
            case Builtin::sqrt: case Builtin::sin: case Builtin::cos: case Builtin::tan: case Builtin::asin: case Builtin::acos:
            case Builtin::atan: case Builtin::exp: case Builtin::log: case Builtin::log2: case Builtin::log10:
                arg(0, type_float);
                return done(type_float);
            case Builtin::pow: case Builtin::atan2: arg(0, type_float); arg(1, type_float); return done(type_float);
            case Builtin::floor: case Builtin::ceil: case Builtin::round: arg(0, type_float); return done(type_int);
            case Builtin::push: { const auto element = info(list(0).type).first; arg(1, element); return done(type_unit); }
            case Builtin::pop: return done(info(list(0).type).first);
            case Builtin::insert: { const auto element = info(list(0).type).first; arg(1, type_int); arg(2, element); return done(type_unit); }
            case Builtin::remove_at: { const auto element = info(list(0).type).first; arg(1, type_int); return done(element); }
            case Builtin::index_of: { const auto element = info(list(0).type).first; arg(1, element); return done(type_int); }
            case Builtin::sort: {
                const auto element = info(list(0).type).first;
                if (element != type_int && element != type_float && element != type_text && element != type_bool) {
                    throw Error(args[0]->token.at, "sort expects a list of i64, f64, str, or bool");
                }
                return done(type_unit);
            }
            case Builtin::reverse: list(0); return done(type_unit);
            case Builtin::clear: {
                const auto& fact = arg(0);
                if (kind(fact.type) != Kind::list && kind(fact.type) != Kind::dict) throw Error(args[0]->token.at, "clear expects a list or dict");
                return done(type_unit);
            }
            case Builtin::keys: return done(list_of(info(dict(0).type).first));
            case Builtin::values: return done(list_of(info(dict(0).type).second));
            case Builtin::remove_key: { const auto key = info(dict(0).type).first; arg(1, key); return done(type_bool); }
            case Builtin::lookup: {
                const auto& d = info(dict(0).type);
                arg(1, d.first);
                return done(option_of(d.second));
            }
            case Builtin::split: arg(0, type_text); arg(1, type_text); return done(list_of(type_text));
            case Builtin::join: arg(0, list_of(type_text)); arg(1, type_text); return done(type_text);
            case Builtin::find: arg(0, type_text); arg(1, type_text); return done(type_int);
            case Builtin::replace: arg(0, type_text); arg(1, type_text); arg(2, type_text); return done(type_text);
            case Builtin::starts_with: case Builtin::ends_with: arg(0, type_text); arg(1, type_text); return done(type_bool);
            case Builtin::chr: arg(0, type_int); return done(type_text);
            case Builtin::ord: case Builtin::char_count: arg(0, type_text); return done(type_int);
            case Builtin::chars: case Builtin::read_lines: case Builtin::list_dir: arg(0, type_text); return done(list_of(type_text));
            case Builtin::repeat: arg(0, type_text); arg(1, type_int); return done(type_text);
            case Builtin::bytes: arg(0, type_text); return done(list_of(type_int));
            case Builtin::from_bytes: arg(0, list_of(type_int)); return done(type_text);
            case Builtin::exit: case Builtin::sleep_ms: case Builtin::seed: arg(0, type_int); return done(type_unit);
            case Builtin::fail: arg(0, type_text); return done(type_unit);
            case Builtin::is_some: case Builtin::is_none: variant(0, true); return done(type_bool);
            case Builtin::is_ok: case Builtin::is_err: variant(0, false); return done(type_bool);
            case Builtin::unwrap: {
                const auto& fact = arg(0);
                const auto k = kind(fact.type);
                if (k != Kind::option && k != Kind::result) throw Error(args[0]->token.at, "unwrap expects an Option or Result");
                return done(info(fact.type).first);
            }
            case Builtin::unwrap_or: {
                const auto& fact = arg(0);
                const auto k = kind(fact.type);
                if (k != Kind::option && k != Kind::result) throw Error(args[0]->token.at, "unwrap_or expects an Option or Result");
                arg(1, info(fact.type).first);
                return done(info(fact.type).first);
            }
            case Builtin::expect: {
                const auto& fact = arg(0);
                const auto k = kind(fact.type);
                if (k != Kind::option && k != Kind::result) throw Error(args[0]->token.at, "expect expects an Option or Result");
                arg(1, type_text);
                return done(info(fact.type).first);
            }
            case Builtin::parse_int: arg(0, type_text); return done(option_of(type_int));
            case Builtin::parse_float: arg(0, type_text); return done(option_of(type_float));
            case Builtin::env: arg(0, type_text); return done(option_of(type_text));
            case Builtin::random_float: return done(type_float);
            case Builtin::random_int: arg(0, type_int); arg(1, type_int); return done(type_int);
            case Builtin::file_exists: case Builtin::delete_file: case Builtin::make_dir: arg(0, type_text); return done(type_bool);
            case Builtin::args: return done(list_of(type_text));
            case Builtin::run: arg(0, type_text); return done(type_int);
            case Builtin::sum: {
                const auto element = info(list(0).type).first;
                if (element != type_int && element != type_float) throw Error(args[0]->token.at, "sum expects a list of i64 or f64");
                return done(element);
            }
            case Builtin::min_of: case Builtin::max_of: {
                const auto element = info(list(0).type).first;
                if (element != type_int && element != type_float && element != type_text) {
                    throw Error(args[0]->token.at, name + " expects a list of i64, f64, or str");
                }
                return done(element);
            }
            case Builtin::fill: {
                arg(0, type_int);
                const auto& value = arg(1);
                return done(list_of(value.type));
            }
            case Builtin::extend: { const auto first = list(0).type; arg(1, first); return done(type_unit); }
            case Builtin::fixed: arg(0, type_float); arg(1, type_int); return done(type_text);
            case Builtin::to_hex: arg(0, type_int); return done(type_text);
            case Builtin::from_hex: arg(0, type_text); return done(option_of(type_int));
            case Builtin::gc_collect: case Builtin::gc_count: return done(type_int);
            case Builtin::tcp_listen: arg(0, type_int); return done(result_of(type_int, type_text));
            case Builtin::tcp_accept: arg(0, type_int); return done(result_of(type_int, type_text));
            case Builtin::tcp_connect: arg(0, type_text); arg(1, type_int); return done(result_of(type_int, type_text));
            case Builtin::tcp_read: arg(0, type_int); arg(1, type_int); return done(result_of(type_text, type_text));
            case Builtin::tcp_write: arg(0, type_int); arg(1, type_text); return done(result_of(type_int, type_text));
            case Builtin::tcp_close: arg(0, type_int); return done(type_unit);
            case Builtin::run_capture: arg(0, type_text); return done(type_text);
            case Builtin::unix_time: return done(type_int);
            case Builtin::hash: arg(0, type_text); return done(type_int);
            case Builtin::count: break;
        }
        // Single-argument text builtins that return text.
        if (spec.id == Builtin::read_text || spec.id == Builtin::upper || spec.id == Builtin::lower || spec.id == Builtin::strip) {
            arg(0, type_text);
            return done(type_text);
        }
        throw std::logic_error("invalid builtin");
    }

    std::vector<const Expr*> pointers(const std::vector<Expr>& items, std::size_t from = 0) {
        std::vector<const Expr*> out;
        for (std::size_t i = from; i < items.size(); ++i) out.push_back(&items[i]);
        return out;
    }

    // Emits a call to program function `target` with `args`.
    Fact call_function(std::uint32_t target, const Token& token, const std::vector<const Expr*>& args, bool report) {
        // Copy the header: compiling the arguments may add functions and reallocate the table.
        const auto params = program.functions[target].params;
        const auto captures = program.functions[target].captures;
        const auto result = program.functions[target].result;
        const auto callee_name = program.functions[target].name;
        if (args.size() != params.size() - captures) {
            throw Error(token.at, "'" + callee_name + "' expects " + std::to_string(params.size() - captures) + " argument(s)");
        }
        for (std::size_t i = 0; i < args.size(); ++i) {
            const auto fact = expression(*args[i], report, params[i]);
            require_value(fact, args[i]->token.at);
            require(fact.type, params[i], args[i]->token.at);
        }
        if (target == program.entry && units[0]->functions.count("main") && target == units[0]->functions.at("main")) {
            throw Error(token.at, "cannot call main");
        }
        emit(Op::call, token.at, target);
        return Fact(result);
    }

    Fact call_closure(const Binding& value, const Token& token, const std::vector<const Expr*>& args, bool report) {
        const auto type = info(value.fact.type);  // copy: the type table may grow while arguments compile
        const auto slot = value.slot;
        if (args.size() != type.params.size()) {
            throw Error(token.at, "'" + token.text + "' expects " + std::to_string(type.params.size()) + " argument(s)");
        }
        for (std::size_t i = 0; i < args.size(); ++i) {
            const auto fact = expression(*args[i], report, type.params[i]);
            require_value(fact, args[i]->token.at);
            require(fact.type, type.params[i], args[i]->token.at);
        }
        emit(Op::load, token.at, slot);
        emit(Op::call_value, token.at, static_cast<std::int64_t>(args.size()));
        return Fact(type.first);
    }

    Fact construct(std::uint32_t structure, const Token& token, const std::vector<const Expr*>& args, bool report) {
        const auto info_ = program.structs[structure];  // copy: see call_function
        if (args.size() != info_.field_types.size()) {
            throw Error(token.at, "'" + info_.name + "' expects " + std::to_string(info_.field_types.size()) + " field value(s)");
        }
        for (std::size_t i = 0; i < args.size(); ++i) {
            const auto fact = expression(*args[i], report, info_.field_types[i]);
            require_value(fact, args[i]->token.at);
            require(fact.type, info_.field_types[i], args[i]->token.at);
        }
        emit(Op::make_object, token.at, structure);
        return Fact(object_of(structure));
    }

    // Resolves a call by name inside `in` (the current unit, or an imported one).
    Fact call_named(Unit& in, const Token& token, const std::vector<TypeExpr>& type_args, const std::vector<const Expr*>& args,
                    bool report, bool qualified) {
        const auto& name = token.text;
        if (!qualified) {
            if (const auto* value = lookup(name)) {
                if (kind(value->fact.type) != Kind::function) throw Error(token.at, "'" + name + "' is not a function");
                return call_closure(*value, token, args, report);
            }
        }
        if (const auto found = in.functions.find(name); found != in.functions.end()) {
            if (!type_args.empty()) throw Error(token.at, "'" + name + "' is not generic");
            return call_function(found->second, token, args, report);
        }
        if (const auto found = in.generic_functions.find(name); found != in.generic_functions.end()) {
            return call_generic(in, *found->second, token, type_args, args, report);
        }
        if (const auto found = in.structs.find(name); found != in.structs.end()) return construct(found->second, token, args, report);
        if (const auto found = in.generic_structs.find(name); found != in.generic_structs.end()) {
            const auto* decl = found->second;
            std::vector<TypeId> resolved;
            if (!type_args.empty()) {
                if (type_args.size() != decl->type_params.size()) throw Error(token.at, "'" + name + "' takes " + std::to_string(decl->type_params.size()) + " type argument(s)");
                for (const auto& arg : type_args) resolved.push_back(resolve(arg));
            } else {
                // Infer the struct's type arguments from the field values.
                if (args.size() != decl->fields.size()) throw Error(token.at, "'" + name + "' expects " + std::to_string(decl->fields.size()) + " field value(s)");
                Subst subst;
                std::unordered_set<std::string> params;
                for (const auto& p : decl->type_params) params.insert(p.name.text);
                const auto saved = position_now();
                for (std::size_t i = 0; i < args.size(); ++i) {
                    const auto fact = probe(*args[i]);
                    unify(decl->fields[i].second, fact.type, subst, params, in, args[i]->token.at);
                }
                for (const auto& p : decl->type_params) {
                    const auto bound = subst.find(p.name.text);
                    if (bound == subst.end()) throw Error(token.at, "cannot infer type parameter '" + p.name.text + "'; write " + name + "[...](...)");
                    resolved.push_back(bound->second);
                }
                (void)saved;
            }
            return construct(instantiate_struct(in, *decl, resolved), token, args, report);
        }
        if (!qualified) {
            if (const auto* spec = builtin_named(name)) return builtin_call(token, *spec, args, report);
        }
        throw Error(token.at, "unknown function '" + name + "'");
    }

    // Type-checks an expression without keeping its code (used to infer generic arguments).
    Fact probe(const Expr& node) {
        const auto saved_size = fn().code.size();
        const auto saved_locals = fn().locals;
        const auto saved_findings = findings.size();
        const auto saved_lambda = lambda_counter;
        const auto saved_functions = program.functions.size();
        const auto saved_instances = instantiations.size() + struct_instantiations.size();
        const auto fact = expression(node, false);
        fn().code.resize(saved_size);
        fn().locals = saved_locals;
        findings.resize(saved_findings);
        // Lambdas compiled during the probe are discarded and regenerated by the real pass, unless a generic
        // instantiation happened meanwhile (its cached function ids must stay valid); then they are kept unused.
        if (program.functions.size() > saved_functions && instantiations.size() + struct_instantiations.size() == saved_instances) {
            program.functions.resize(saved_functions);
            lambda_counter = saved_lambda;
        }
        return fact;
    }

    void unify(const TypeExpr& pattern, TypeId actual, Subst& subst, const std::unordered_set<std::string>& params, Unit& in, Location at) {
        const auto& word = pattern.name.text;
        if (!pattern.qualifier && params.count(word)) {
            const auto bound = subst.find(word);
            if (bound == subst.end()) subst.emplace(word, actual);
            else if (bound->second != actual) {
                throw Error(at, "type parameter '" + word + "' is both " + name_of(bound->second) + " and " + name_of(actual));
            }
            return;
        }
        const auto& a = info(actual);
        if (!pattern.qualifier && word == "list" && a.kind == Kind::list) { unify(pattern.args[0], a.first, subst, params, in, at); return; }
        if (!pattern.qualifier && word == "dict" && a.kind == Kind::dict) {
            unify(pattern.args[0], a.first, subst, params, in, at);
            unify(pattern.args[1], a.second, subst, params, in, at);
            return;
        }
        if (!pattern.qualifier && word == "Option" && a.kind == Kind::option) { unify(pattern.args[0], a.first, subst, params, in, at); return; }
        if (!pattern.qualifier && word == "Result" && a.kind == Kind::result) {
            unify(pattern.args[0], a.first, subst, params, in, at);
            unify(pattern.args[1], a.second, subst, params, in, at);
            return;
        }
        if (!pattern.qualifier && word == "fn" && a.kind == Kind::function) {
            const auto count = pattern.args.size() - (pattern.has_result ? 1 : 0);
            if (count != a.params.size()) throw Error(at, "function arity mismatch: expected " + std::to_string(count) + " parameter(s)");
            for (std::size_t i = 0; i < count; ++i) unify(pattern.args[i], a.params[i], subst, params, in, at);
            if (pattern.has_result) unify(pattern.args.back(), a.first, subst, params, in, at);
            else if (a.first != type_unit) throw Error(at, "expected a function without a result");
            return;
        }
        if (a.kind == Kind::object && !pattern.args.empty()) {
            const auto origin = struct_origins.find(a.first);
            Unit& target = pattern.qualifier ? resolve_qualifier(*pattern.qualifier, in) : in;
            const auto generic = target.generic_structs.find(word);
            if (origin != struct_origins.end() && generic != target.generic_structs.end() && origin->second.decl == generic->second) {
                for (std::size_t i = 0; i < pattern.args.size() && i < origin->second.args.size(); ++i) {
                    unify(pattern.args[i], origin->second.args[i], subst, params, in, at);
                }
                return;
            }
        }
        // Anything else must resolve to exactly the actual type (type parameters inside are already bound).
        Subst merged = frame ? frame->subst : Subst{};
        for (const auto& [k, v] : subst) merged[k] = v;
        TypeId resolved;
        try {
            resolved = resolve_in(pattern, in, &merged);
        } catch (const Error&) {
            throw Error(at, "cannot match " + name_of(actual) + " against the declared parameter type");
        }
        if (resolved != actual) throw Error(at, "expected " + name_of(resolved) + ", found " + name_of(actual));
    }

    Fact call_generic(Unit& in, const FunctionDecl& decl, const Token& token, const std::vector<TypeExpr>& type_args,
                      const std::vector<const Expr*>& args, bool report) {
        std::vector<TypeId> resolved;
        if (!type_args.empty()) {
            if (type_args.size() != decl.type_params.size()) {
                throw Error(token.at, "'" + decl.name.text + "' takes " + std::to_string(decl.type_params.size()) + " type argument(s)");
            }
            for (const auto& arg : type_args) resolved.push_back(resolve(arg));
        } else {
            if (args.size() != decl.params.size()) {
                throw Error(token.at, "'" + decl.name.text + "' expects " + std::to_string(decl.params.size()) + " argument(s)");
            }
            Subst subst;
            std::unordered_set<std::string> params;
            for (const auto& p : decl.type_params) params.insert(p.name.text);
            for (std::size_t i = 0; i < args.size(); ++i) {
                const auto fact = probe(*args[i]);
                require_value(fact, args[i]->token.at);
                unify(decl.params[i].second, fact.type, subst, params, in, args[i]->token.at);
            }
            for (const auto& p : decl.type_params) {
                const auto bound = subst.find(p.name.text);
                if (bound == subst.end()) throw Error(token.at, "cannot infer type parameter '" + p.name.text + "'; write " + decl.name.text + "[...](...)");
                resolved.push_back(bound->second);
            }
        }
        check_bounds(in, decl.type_params, resolved, token.at);
        const auto id = instantiate_function(in, decl, resolved, std::nullopt);
        return call_function(id, token, args, report);
    }

    Fact call(const Expr& node, bool report, std::optional<TypeId> expected) {
        const auto& token = node.token;
        const auto& name = token.text;
        const auto& args = node.parts;
        if (name == "None" || name == "Some" || name == "Ok" || name == "Err") return variant_constructor(node, report, expected);
        // User definitions shadow built-in functions.
        const bool user_defined = lookup(name) || unit().functions.count(name) || unit().generic_functions.count(name) ||
                                  unit().structs.count(name) || unit().generic_structs.count(name);
        if (name == "len" && !user_defined) {
            if (args.size() != 1) throw Error(token.at, "'len' expects 1 argument(s)");
            const auto value = expression(args[0], report);
            const auto k = kind(value.type);
            if (k != Kind::text && k != Kind::list && k != Kind::dict) throw Error(args[0].token.at, "len expects str, list, or dict");
            emit(Op::length, token.at);
            return value.extent ? Fact::integer(*value.extent, *value.extent) : Fact(type_int);
        }
        if (name == "attempt" && !user_defined) {
            if (args.size() != 1) throw Error(token.at, "'attempt' expects 1 argument: a fn() -> T");
            const auto f = expression(args[0], report);
            if (kind(f.type) != Kind::function || !info(f.type).params.empty()) {
                throw Error(args[0].token.at, "attempt expects a function value that takes no arguments, found " + name_of(f.type));
            }
            const auto payload = info(f.type).first == type_unit ? type_bool : info(f.type).first;
            emit(Op::try_call, token.at, info(f.type).first == type_unit ? 1 : 0);
            return Fact(result_of(payload, type_text));
        }
        if ((name == "parallel_sum" || name == "parallel_map") && !user_defined) {
            if (args.size() != 3) throw Error(token.at, "'" + name + "' expects 3 argument(s)");
            require(expression(args[0], report).type, type_int, args[0].token.at);
            const auto workers = expression(args[1], report);
            require(workers.type, type_int, args[1].token.at);
            if (workers.interval && (workers.interval->high < 1 || workers.interval->low > 64)) {
                warning(report, args[1].token.at, "invalid-workers", "the worker count cannot be between 1 and 64");
            }
            const auto mapper_type = function_of({type_int}, type_int);
            // A named module-level function (the common case) uses the direct reduce instruction.
            if (name == "parallel_sum" && args[2].form == Form::name && !lookup(args[2].token.text)) {
                const auto found = unit().functions.find(args[2].token.text);
                if (found != unit().functions.end() && found->second != program.entry) {
                    const auto& mapper = program.functions[found->second];
                    if (mapper.params != std::vector<TypeId>{type_int} || mapper.result != type_int) {
                        throw Error(args[2].token.at, "parallel mapper must have signature fn(i64) -> i64");
                    }
                    emit(Op::reduce, token.at, found->second);
                    return type_int;
                }
            }
            const auto mapper = expression(args[2], report, mapper_type);
            if (mapper.type != mapper_type) {
                throw Error(args[2].token.at, "parallel mapper must be a fn(i64) -> i64, found " + name_of(mapper.type));
            }
            if (args[2].form == Form::lambda) {
                for (const auto captured : last_lambda_captures) {
                    if (captured != type_int && captured != type_float && captured != type_bool) {
                        throw Error(args[2].token.at, "a parallel mapper may only capture i64, f64, or bool values (captured " + name_of(captured) + ")");
                    }
                }
            }
            emit(Op::parallel, token.at, name == "parallel_sum" ? 0 : 1);
            return name == "parallel_sum" ? Fact(type_int) : Fact(list_of(type_int));
        }
        if (name == "parallel_run" && !user_defined) {
            if (args.size() != 2) throw Error(token.at, "'parallel_run' expects 2 argument(s): (workers: i64, task: fn(i64))");
            const auto workers = expression(args[0], report);
            require(workers.type, type_int, args[0].token.at);
            if (workers.interval && (workers.interval->high < 1 || workers.interval->low > 64)) {
                warning(report, args[0].token.at, "invalid-workers", "the worker count must be between 1 and 64");
            }
            const auto task_type = function_of({type_int}, type_unit);
            const auto task = expression(args[1], report, task_type);
            if (task.type != task_type) {
                throw Error(args[1].token.at, "parallel_run task must be a fn(i64) that returns nothing, found " + name_of(task.type));
            }
            // The task closure is shared, read-only, by every worker thread, so its captures
            // must need no cross-thread reference counting: scalars, or function values (which
            // parallel_run makes immortal). Any other shared state goes through files or a database.
            if (args[1].form == Form::lambda) {
                for (const auto captured : last_lambda_captures) {
                    if (captured != type_int && captured != type_float && captured != type_bool && kind(captured) != Kind::function) {
                        throw Error(args[1].token.at, "a parallel_run task may only capture i64, f64, bool, or function values (captured " + name_of(captured) + "); share other state through files or a database");
                    }
                }
            }
            emit(Op::parallel, token.at, 2);
            return Fact(type_int);
        }
        return call_named(unit(), token, node.type_args, pointers(args), report, false);
    }

    Fact variant_constructor(const Expr& node, bool report, std::optional<TypeId> expected) {
        const auto& name = node.token.text;
        const auto at = node.token.at;
        const auto& args = node.parts;
        const auto hint = [&](Kind wanted) -> const TypeInfo* {
            if (expected && kind(*expected) == wanted) return &info(*expected);
            return nullptr;
        };
        if (name == "None") {
            if (!args.empty()) throw Error(at, "None takes no arguments");
            const auto* h = hint(Kind::option);
            if (!h) throw Error(at, "the type of None is unknown here; add an annotation such as let x: Option[i64] = None");
            const auto type = option_of(h->first);
            emit(Op::make_variant, at, 0 | (static_cast<std::int64_t>(type) << 40));
            return Fact(type);
        }
        if (name == "Some") {
            if (args.size() != 1) throw Error(at, "Some takes one argument");
            const auto* h = hint(Kind::option);
            const auto value = expression(args[0], report, h ? std::optional<TypeId>(h->first) : std::nullopt);
            require_value(value, args[0].token.at);
            const auto type = option_of(value.type);
            emit(Op::make_variant, at, 1 | (std::int64_t{1} << 32) | (static_cast<std::int64_t>(type) << 40));
            return Fact(type);
        }
        const auto* h = hint(Kind::result);
        if (name == "Ok") {
            if (args.size() != 1) throw Error(at, "Ok takes one argument");
            if (!h) throw Error(at, "the error type of Ok is unknown here; add an annotation such as let r: Result[i64, str] = Ok(1)");
            const auto value = expression(args[0], report, h->first);
            require_value(value, args[0].token.at);
            require(value.type, h->first, args[0].token.at);
            const auto type = result_of(h->first, h->second);
            emit(Op::make_variant, at, 1 | (std::int64_t{1} << 32) | (std::int64_t{1} << 33) | (static_cast<std::int64_t>(type) << 40));
            return Fact(type);
        }
        if (args.size() != 1) throw Error(at, "Err takes one argument");
        if (!h) throw Error(at, "the value type of Err is unknown here; add an annotation such as let r: Result[i64, str] = Err(\"no\")");
        const auto value = expression(args[0], report, h->second);
        require_value(value, args[0].token.at);
        require(value.type, h->second, args[0].token.at);
        const auto type = result_of(h->first, h->second);
        emit(Op::make_variant, at, 0 | (std::int64_t{1} << 32) | (std::int64_t{1} << 33) | (static_cast<std::int64_t>(type) << 40));
        return Fact(type);
    }

    Fact method_call(const Expr& node, bool report) {
        const auto& receiver = node.parts[0];
        const auto& token = node.token;
        // module.function(...)
        if (receiver.form == Form::name && !lookup(receiver.token.text) && unit().imports.count(receiver.token.text)) {
            Unit& target = resolve_qualifier(receiver.token, unit());
            return call_named(target, token, node.type_args, pointers(node.parts, 1), report, true);
        }
        const auto receiver_fact = probe(receiver);
        if (kind(receiver_fact.type) == Kind::trait_object) {
            const auto trait = info(receiver_fact.type).first;
            const auto& info_ = program.traits[trait];
            for (std::size_t slot = 0; slot < info_.method_names.size(); ++slot) {
                if (info_.method_names[slot] != token.text) continue;
                const auto signature = info(info_.method_types[slot]);  // copy: compiling arguments may grow the table
                if (node.parts.size() - 1 != signature.params.size()) {
                    throw Error(token.at, "'" + info_.name + "." + token.text + "' expects " + std::to_string(signature.params.size()) + " argument(s)");
                }
                expression(receiver, report);
                for (std::size_t i = 0; i < signature.params.size(); ++i) {
                    const auto fact = expression(node.parts[i + 1], report, signature.params[i]);
                    require_value(fact, node.parts[i + 1].token.at);
                    require(fact.type, signature.params[i], node.parts[i + 1].token.at);
                }
                emit(Op::call_trait, token.at, static_cast<std::int64_t>(trait) | (static_cast<std::int64_t>(slot) << 32));
                return Fact(signature.first);
            }
            throw Error(token.at, "trait '" + info_.name + "' has no method '" + token.text + "'");
        }
        if (kind(receiver_fact.type) == Kind::object) {
            const auto structure = info(receiver_fact.type).first;
            if (const auto id = find_method(structure, token.text)) {
                const auto arity = program.functions[*id].params.size();
                if (node.parts.size() != arity) {
                    throw Error(token.at, "'" + program.functions[*id].name + "' expects " + std::to_string(arity - 1) + " argument(s)");
                }
                return call_function(*id, token, pointers(node.parts), report);
            }
        }
        // Builtin or module-level function with the receiver as the first argument: items.push(x), s.upper(), f(x)...
        if (!node.type_args.empty()) {
            if (const auto found = unit().generic_functions.find(token.text); found != unit().generic_functions.end()) {
                return call_generic(unit(), *found->second, token, node.type_args, pointers(node.parts), report);
            }
            throw Error(token.at, "unknown generic function '" + token.text + "'");
        }
        if (const auto found = unit().functions.find(token.text); found != unit().functions.end()) {
            return call_function(found->second, token, pointers(node.parts), report);
        }
        if (const auto found = unit().generic_functions.find(token.text); found != unit().generic_functions.end()) {
            return call_generic(unit(), *found->second, token, {}, pointers(node.parts), report);
        }
        if (const auto* spec = builtin_named(token.text)) return builtin_call(token, *spec, pointers(node.parts), report);
        if (token.text == "len") {
            Expr call{Form::call, token, {}};
            call.parts.push_back(receiver);
            return this->call(call, report, std::nullopt);
        }
        if (kind(receiver_fact.type) == Kind::object) {
            throw Error(token.at, "'" + program.structs[info(receiver_fact.type).first].name + "' has no method '" + token.text + "'");
        }
        throw Error(token.at, "unknown method or function '" + token.text + "'");
    }

    std::optional<std::uint32_t> find_method(std::uint32_t structure, const std::string& name) {
        const auto table = methods.find(structure);
        if (table == methods.end()) return std::nullopt;
        const auto found = table->second.find(name);
        if (found == table->second.end()) return std::nullopt;
        return found->second;
    }

    Fact lambda(const Expr& node, bool report) {
        const auto at = node.token.at;
        // Which enclosing bindings does the body use? Those are captured by value.
        std::vector<std::string> used;
        std::unordered_set<std::string> declared;
        for (const auto& [parameter, type] : node.params) declared.insert(parameter.text);
        if (node.block) collect_names(*node.block, used, declared);
        else collect_names(node.parts[0], used, declared);
        std::vector<std::pair<std::string, const Binding*>> captures;
        std::unordered_set<std::string> seen;
        for (const auto& name : used) {
            if (declared.count(name) || seen.count(name)) continue;
            if (const auto* found = lookup(name)) {
                captures.emplace_back(name, found);
                seen.insert(name);
            }
        }
        Function header;
        header.name = fn().name + "@lambda" + std::to_string(++lambda_counter);
        std::vector<TypeId> param_types;
        for (const auto& [parameter, type] : node.params) param_types.push_back(resolve(type));
        header.params = param_types;
        for (const auto& [name, found] : captures) header.params.push_back(found->fact.type);
        header.captures = static_cast<std::uint32_t>(captures.size());
        header.result = node.result ? resolve(*node.result) : type_unit;
        const auto id = static_cast<std::uint32_t>(program.functions.size());
        program.functions.push_back(std::move(header));

        Frame inner;
        inner.function = id;
        inner.unit = frame->unit;
        inner.subst = frame->subst;
        inner.type_params = frame->type_params;
        inner.scopes.emplace_back();
        if (node.block) collect_escapes(*node.block, inner.escaped);
        {
            // Captured facts are copied before the frame switch (the pointers refer to the outer frame).
            std::vector<std::pair<std::string, Fact>> capture_facts;
            for (const auto& [name, found] : captures) capture_facts.emplace_back(name, found->fact);
            Frame* outer = frame;
            frame = &inner;
            for (std::size_t i = 0; i < node.params.size(); ++i) bind(node.params[i].first, Fact(param_types[i]), false);
            for (const auto& [name, fact] : capture_facts) {
                Token token{Tag::name, name, at};
                bind(token, fact, false, std::nullopt, true);
            }
            if (node.block) {
                const bool exits = block(*node.block, report);
                if (!exits && fn().result != type_unit) throw Error(at, "a path reaches the end of the lambda without returning a value");
                emit(Op::finish, at);
            } else {
                const auto value = expression(node.parts[0], report, node.result ? std::optional<TypeId>(fn().result) : std::nullopt);
                if (node.result) {
                    require_value(value, node.parts[0].token.at);
                    require(value.type, fn().result, node.parts[0].token.at);
                    emit(Op::return_value, at);
                } else if (value.type == type_unit) {
                    emit(Op::discard, at);
                } else {
                    fn().result = value.type;
                    emit(Op::return_value, at);
                }
                emit(Op::finish, at);  // every function ends with finish (unreachable after return_value)
            }
            optimize(fn());
            frame = outer;
        }
        last_lambda_captures.clear();
        for (const auto& [name, found] : captures) {
            last_lambda_captures.push_back(found->fact.type);
            emit(Op::load, at, found->slot);
        }
        emit(Op::make_closure, at, static_cast<std::int64_t>(id) | (static_cast<std::int64_t>(captures.size()) << 32));
        return Fact(function_of(param_types, program.functions[id].result));
    }

    Fact expression(const Expr& node, bool report, std::optional<TypeId> expected = {}) {
        const auto at = node.token.at;
        switch (node.form) {
            case Form::integer:
                emit(Op::push_integer, at, node.number);
                return Fact::integer(node.number, node.number);
            case Form::floating: {
                std::int64_t bits;
                std::memcpy(&bits, &node.real, sizeof bits);
                emit(Op::push_float, at, bits);
                return type_float;
            }
            case Form::boolean:
                emit(Op::push_boolean, at, node.number);
                return Fact::boolean(node.number != 0);
            case Form::text: {
                emit(Op::push_text, at, text_id(node.token.text));
                Fact fact(type_text);
                fact.extent = static_cast<std::int64_t>(node.token.text.size());
                return fact;
            }
            case Form::name: return name_value(node.token, unit(), false);
            case Form::unary: {
                const auto value = expression(node.parts[0], report);
                require_value(value, at);
                if (node.token.text == "not") {
                    require(value.type, type_bool, at);
                    emit(Op::invert, at);
                    return Fact::boolean(value.truth ? std::optional<bool>(!*value.truth) : std::nullopt);
                }
                if (node.token.text == "~") {
                    require(value.type, type_int, at);
                    emit(Op::bit_not, at);
                    if (const auto fixed = value.fixed()) return Fact::integer(~*fixed, ~*fixed);
                    return type_int;
                }
                if (value.type == type_float) {
                    emit(Op::fnegate, at);
                    return type_float;
                }
                require(value.type, type_int, at);
                return number_op(Op::negate, value, Fact(type_int), at, report);
            }
            case Form::binary: return binary(node, report);
            case Form::call: return call(node, report, expected);
            case Form::method_call: return method_call(node, report);
            case Form::lambda: return lambda(node, report);
            case Form::question: return question(node, report);
            case Form::list: {
                if (node.parts.size() > value_limit) throw Error(at, "too many list elements");
                std::optional<TypeId> element;
                if (expected && kind(*expected) == Kind::list) element = info(*expected).first;
                if (node.parts.empty() && !element) {
                    throw Error(at, "an empty list needs a type, for example: let items: list[i64] = []");
                }
                for (const auto& member : node.parts) {
                    const auto fact = expression(member, report, element);
                    require_value(fact, member.token.at);
                    if (!element) element = fact.type;
                    else require(fact.type, *element, member.token.at);
                }
                const auto size = static_cast<std::int64_t>(node.parts.size());
                const auto type = list_of(*element);
                emit(Op::make_list, at, size | (static_cast<std::int64_t>(type) << 32));
                Fact fact(type);
                fact.extent = size;
                return fact;
            }
            case Form::dict: {
                if (node.parts.size() > 2 * value_limit) throw Error(at, "too many dict entries");
                std::optional<TypeId> key, value;
                if (expected && kind(*expected) == Kind::dict) {
                    key = info(*expected).first;
                    value = info(*expected).second;
                }
                if (node.parts.empty() && !key) {
                    throw Error(at, "an empty dict needs a type, for example: let ages: dict[str, i64] = {}");
                }
                for (std::size_t i = 0; i < node.parts.size(); i += 2) {
                    const auto k = expression(node.parts[i], report, key);
                    if (k.type != type_int && k.type != type_text) throw Error(node.parts[i].token.at, "dict keys must be i64 or str");
                    if (!key) key = k.type;
                    else require(k.type, *key, node.parts[i].token.at);
                    const auto v = expression(node.parts[i + 1], report, value);
                    require_value(v, node.parts[i + 1].token.at);
                    if (!value) value = v.type;
                    else require(v.type, *value, node.parts[i + 1].token.at);
                }
                const auto type = dict_of(*key, *value);
                emit(Op::make_dict, at, static_cast<std::int64_t>(node.parts.size() / 2) | (static_cast<std::int64_t>(type) << 32));
                return Fact(type);
            }
            case Form::index: {
                const auto container = expression(node.parts[0], report);
                const auto container_info = info(container.type);
                const auto key = expression(node.parts[1], report);
                require_value(key, node.parts[1].token.at);
                if (container_info.kind == Kind::dict) {
                    require(key.type, container_info.first, node.parts[1].token.at);
                    emit(Op::index, at);
                    return Fact(container_info.second);
                }
                if (container_info.kind != Kind::list && container_info.kind != Kind::text) {
                    throw Error(node.parts[0].token.at, "indexing expects a list, dict, or str");
                }
                require(key.type, type_int, node.parts[1].token.at);
                if ((container.extent && *container.extent == 0) ||
                    (key.interval && (key.interval->high < 0 || (container.extent && key.interval->low >= *container.extent)))) {
                    warning(report, at, "index-out-of-bounds", "this index is outside the value if evaluated");
                }
                emit(Op::index, at);
                if (container_info.kind == Kind::text) {
                    Fact fact(type_text);
                    fact.extent = 1;
                    return fact;
                }
                return Fact(container_info.first);
            }
            case Form::slice: {
                const auto container = expression(node.parts[0], report);
                const auto k = kind(container.type);
                if (k != Kind::list && k != Kind::text) throw Error(node.parts[0].token.at, "slicing expects a list or str");
                std::size_t next = 1;
                if (node.has_low) require(expression(node.parts[next++], report).type, type_int, node.parts[1].token.at);
                else emit(Op::push_integer, at, 0);
                if (node.has_high) require(expression(node.parts[next], report).type, type_int, node.parts[next].token.at);
                else emit(Op::push_integer, at, std::numeric_limits<std::int64_t>::max());
                emit(Op::slice, at);
                return Fact(container.type);
            }
            case Form::field: {
                const auto& receiver = node.parts[0];
                if (receiver.form == Form::name && !lookup(receiver.token.text) && unit().imports.count(receiver.token.text)) {
                    Unit& target = resolve_qualifier(receiver.token, unit());
                    return name_value(node.token, target, true);
                }
                const auto object = expression(receiver, report);
                if (kind(object.type) != Kind::object) {
                    throw Error(receiver.token.at, "field access expects a struct value, found " + name_of(object.type));
                }
                const auto& structure = program.structs[info(object.type).first];
                const auto position = field_index(structure, node.token);
                emit(Op::get_field, at, static_cast<std::int64_t>(position));
                return Fact(structure.field_types[position]);
            }
        }
        throw std::logic_error("invalid expression");
    }

    // A bare name: a local, a constant, or a function used as a value.
    Fact name_value(const Token& token, Unit& in, bool qualified) {
        const auto at = token.at;
        if (!qualified) {
            if (const auto* value = lookup(token.text)) {
                emit(Op::load, at, value->slot);
                return value->fact;
            }
        }
        if (const auto found = in.consts.find(token.text); found != in.consts.end()) {
            const auto& value = found->second;
            if (value.type == type_int) {
                emit(Op::push_integer, at, value.integer);
                return Fact::integer(value.integer, value.integer);
            }
            if (value.type == type_float) {
                std::int64_t bits;
                std::memcpy(&bits, &value.real, sizeof bits);
                emit(Op::push_float, at, bits);
                return type_float;
            }
            if (value.type == type_bool) {
                emit(Op::push_boolean, at, value.truth);
                return Fact::boolean(value.truth);
            }
            emit(Op::push_text, at, text_id(value.text));
            Fact fact(type_text);
            fact.extent = static_cast<std::int64_t>(value.text.size());
            return fact;
        }
        if (const auto found = in.functions.find(token.text); found != in.functions.end()) {
            const auto& target = program.functions[found->second];
            if (found->second == program.entry && in.display.empty()) throw Error(at, "main cannot be used as a value");
            emit(Op::push_function, at, found->second);
            return Fact(function_of(target.params, target.result));
        }
        if (in.generic_functions.count(token.text)) {
            throw Error(at, "'" + token.text + "' is generic; instantiate it explicitly, for example " + token.text + "[i64]");
        }
        if (qualified) throw Error(at, "module '" + in.display + "' has no '" + token.text + "'");
        throw Error(at, "unknown binding '" + token.text + "'");
    }

    Fact question(const Expr& node, bool report) {
        const auto at = node.token.at;
        const auto value = expression(node.parts[0], report);
        const auto k = kind(value.type);
        if (k != Kind::option && k != Kind::result) throw Error(at, "'?' expects an Option or Result, found " + name_of(value.type));
        const auto returning = fn().result;
        if (kind(returning) != k) {
            throw Error(at, std::string("'?' needs the enclosing function to return ") + (k == Kind::option ? "an Option" : "a Result") +
                            ", but it returns " + name_of(returning));
        }
        if (k == Kind::result && info(returning).second != info(value.type).second) {
            throw Error(at, "'?' error type " + name_of(info(value.type).second) + " does not match the function's " + name_of(info(returning).second));
        }
        const auto slot = local(at);
        emit(Op::store, at, slot);
        emit(Op::load, at, slot);
        emit(Op::variant_which, at);
        const auto ok = emit(Op::jump_true, at);
        emit(Op::load, at, slot);
        emit(Op::return_value, at);
        patch(ok);
        emit(Op::load, at, slot);
        emit(Op::variant_payload, at);
        return Fact(info(value.type).first);
    }

    static std::size_t field_index(const StructInfo& structure, const Token& field) {
        for (std::size_t i = 0; i < structure.field_names.size(); ++i) {
            if (structure.field_names[i] == field.text) return i;
        }
        throw Error(field.at, "'" + structure.name + "' has no field '" + field.text + "'");
    }

    // --- statements ---
    bool block(const std::vector<Statement>& statements, bool report) {
        bool exits = false;
        for (const auto& statement : statements) {
            if (exits) throw Error(statement.token.at, "unreachable statement");
            exits = compile_statement(statement, report);
        }
        return exits;
    }

    Fact arithmetic_assign(const std::string& op, const Fact& left, const Expr& right_expr, bool report) {
        const auto right = expression(right_expr, report, left.type);
        require_value(right, right_expr.token.at);
        const auto word = op.substr(0, op.size() - 1);
        const auto at = right_expr.token.at;
        if (left.type == type_text) {
            if (word != "+") throw Error(at, "str supports only '+='");
            require(right.type, type_text, at);
            emit(Op::concat, at);
            return Fact(type_text);
        }
        if (left.type == type_float) {
            require(right.type, type_float, at);
            if (word == "//") throw Error(at, "f64 uses '/='");
            emit(word == "+" ? Op::fadd : word == "-" ? Op::fsubtract : word == "*" ? Op::fmultiply
                 : word == "/" ? Op::fdivide : Op::fmodulo, at);
            return Fact(type_float);
        }
        require(left.type, type_int, at);
        require(right.type, type_int, at);
        if (word == "/") throw Error(at, "i64 uses '//='");
        if (word == "&" || word == "|" || word == "^" || word == "<<" || word == ">>") {
            emit(binary_op(word), at);
            return Fact(type_int);
        }
        return number_op(binary_op(word), left, right, at, report);
    }

    void assign(const Statement& stmt, bool report) {
        const auto& target = *stmt.target;
        const auto& value_expr = *stmt.value;
        const bool compound = stmt.op != "=";
        if (target.form == Form::name) {
            const auto& found = binding(target.token);
            if (found.captured) throw Error(target.token.at, "'" + target.token.text + "' is captured by value and cannot be assigned inside the lambda");
            if (!found.mutable_) throw Error(target.token.at, "'" + target.token.text + "' is immutable; declare it with var");
            if (compound) {
                emit(Op::load, target.token.at, found.slot);
                arithmetic_assign(stmt.op, found.fact, value_expr, report);
            } else {
                const auto value = expression(value_expr, report, found.fact.type);
                require_value(value, value_expr.token.at);
                require(value.type, found.fact.type, value_expr.token.at);
            }
            emit(Op::store, stmt.token.at, found.slot);
            return;
        }
        if (target.form == Form::index) {
            const auto container = expression(target.parts[0], report);
            const auto container_info = info(container.type);
            TypeId element;
            if (container_info.kind == Kind::list) {
                require(expression(target.parts[1], report).type, type_int, target.parts[1].token.at);
                element = container_info.first;
            } else if (container_info.kind == Kind::dict) {
                require(expression(target.parts[1], report).type, container_info.first, target.parts[1].token.at);
                element = container_info.second;
            } else {
                throw Error(target.parts[0].token.at, "only list and dict elements can be assigned");
            }
            if (compound) {
                const auto c = local(target.token.at), k = local(target.token.at);
                emit(Op::store, target.token.at, k);
                emit(Op::store, target.token.at, c);
                emit(Op::load, target.token.at, c);
                emit(Op::load, target.token.at, k);
                emit(Op::load, target.token.at, c);
                emit(Op::load, target.token.at, k);
                emit(Op::index, target.token.at);
                arithmetic_assign(stmt.op, Fact(element), value_expr, report);
            } else {
                const auto value = expression(value_expr, report, element);
                require_value(value, value_expr.token.at);
                require(value.type, element, value_expr.token.at);
            }
            emit(Op::set_index, stmt.token.at);
            return;
        }
        const auto object = expression(target.parts[0], report);
        if (kind(object.type) != Kind::object) throw Error(target.parts[0].token.at, "field assignment expects a struct value");
        const auto& structure = program.structs[info(object.type).first];
        const auto position = static_cast<std::int64_t>(field_index(structure, target.token));
        const auto element = structure.field_types[static_cast<std::size_t>(position)];
        if (compound) {
            const auto o = local(target.token.at);
            emit(Op::store, target.token.at, o);
            emit(Op::load, target.token.at, o);
            emit(Op::load, target.token.at, o);
            emit(Op::get_field, target.token.at, position);
            arithmetic_assign(stmt.op, Fact(element), value_expr, report);
        } else {
            const auto value = expression(value_expr, report, element);
            require_value(value, value_expr.token.at);
            require(value.type, element, value_expr.token.at);
        }
        emit(Op::set_field, stmt.token.at, position);
    }

    bool compile_match(const Statement& stmt, bool report) {
        const auto at = stmt.token.at;
        const auto value = expression(*stmt.value, report);
        require_value(value, stmt.value->token.at);
        const auto k = kind(value.type);
        const bool is_option = k == Kind::option, is_result = k == Kind::result;
        if (!is_option && !is_result && value.type != type_int && value.type != type_text && value.type != type_bool && value.type != type_float) {
            throw Error(stmt.value->token.at, "match works on Option, Result, i64, f64, bool, and str values");
        }
        const auto slot = local(at);
        emit(Op::store, at, slot);
        std::vector<std::int64_t> ends;
        bool all_exit = true;
        bool saw_positive = false, saw_negative = false, saw_wild = false;
        for (const auto& item : stmt.cases) {
            if (saw_wild) throw Error(item.token.at, "this case is unreachable after 'case _'");
            std::int64_t skip = -1;
            frame->scopes.emplace_back();
            switch (item.kind) {
                case PatternKind::some: case PatternKind::ok: case PatternKind::none: case PatternKind::err: {
                    const bool wants_option = item.kind == PatternKind::some || item.kind == PatternKind::none;
                    if (!(is_option || is_result) || wants_option != is_option) {
                        throw Error(item.token.at, std::string("this pattern needs ") + (wants_option ? "an Option" : "a Result") + ", but the value is " + name_of(value.type));
                    }
                    const bool positive = item.kind == PatternKind::some || item.kind == PatternKind::ok;
                    if (positive ? saw_positive : saw_negative) throw Error(item.token.at, "duplicate case");
                    (positive ? saw_positive : saw_negative) = true;
                    emit(Op::load, at, slot);
                    emit(Op::variant_which, at);
                    skip = emit(positive ? Op::jump_false : Op::jump_true, at);
                    if (item.binding) {
                        const TypeId payload = positive ? info(value.type).first : info(value.type).second;
                        emit(Op::load, at, slot);
                        emit(Op::variant_payload, at, positive ? 0 : 1);
                        emit(Op::store, at, bind(*item.binding, Fact(payload), false));
                    }
                    break;
                }
                case PatternKind::literal: {
                    const auto literal = expression(*item.literal, report, value.type);
                    require(literal.type, value.type, item.literal->token.at);
                    emit(Op::load, at, slot);
                    emit(Op::equal, at);
                    skip = emit(Op::jump_false, at);
                    break;
                }
                case PatternKind::wildcard: saw_wild = true; break;
            }
            const bool exits = block(item.body, report);
            all_exit = all_exit && exits;
            frame->scopes.pop_back();
            if (!exits) ends.push_back(emit(Op::jump, at));
            if (skip >= 0) patch(skip);
        }
        const bool exhaustive = saw_wild || ((is_option || is_result) && saw_positive && saw_negative);
        if (!exhaustive) {
            throw Error(at, (is_option || is_result) ? "match must handle both cases or end with 'case _'" : "match on a plain value must end with 'case _'");
        }
        for (const auto end : ends) patch(end);
        return all_exit && exhaustive;
    }

    bool compile_statement(const Statement& stmt, bool report) {
        const auto at = stmt.token.at;
        switch (stmt.form) {
            case FormStmt::let:
            case FormStmt::var: {
                std::optional<TypeId> annotated;
                if (stmt.annotation) annotated = resolve(*stmt.annotation);
                auto value = expression(*stmt.value, report, annotated);
                require_value(value, stmt.value->token.at);
                if (annotated) {
                    require(value.type, *annotated, stmt.value->token.at);
                    if (value.type != *annotated) value = Fact(*annotated);  // e.g. a struct bound as a trait
                }
                emit(Op::store, at, bind(stmt.token, value, stmt.form == FormStmt::var));
                return false;
            }
            case FormStmt::assign: assign(stmt, report); return false;
            case FormStmt::expression: {
                expression(*stmt.value, report);
                emit(Op::discard, at);
                const auto& name = stmt.value->token.text;
                return stmt.value->form == Form::call && (name == "exit" || name == "fail");
            }
            case FormStmt::result: {
                if (fn().result == type_unit) {
                    if (stmt.value) throw Error(at, "'" + fn().name + "' does not return a value");
                    emit(Op::push_unit, at);
                } else {
                    if (!stmt.value) throw Error(at, "'" + fn().name + "' must return " + name_of(fn().result));
                    const auto value = expression(*stmt.value, report, fn().result);
                    require_value(value, stmt.value->token.at);
                    require(value.type, fn().result, stmt.value->token.at);
                }
                emit(Op::return_value, at);
                return true;
            }
            case FormStmt::check: {
                const auto value = expression(*stmt.value, report);
                require(value.type, type_bool, stmt.value->token.at);
                if (value.truth == false) warning(report, at, "failing-assert", "this assertion always fails if evaluated");
                const auto ok = emit(Op::jump_true, at);
                emit(Op::push_text, at, text_id("assertion failed"));
                emit(Op::builtin, at, static_cast<std::int64_t>(Builtin::fail));
                emit(Op::discard, at);
                patch(ok);
                return false;
            }
            case FormStmt::match: return compile_match(stmt, report);
            case FormStmt::branch: {
                const auto value = expression(*stmt.value, report);
                require(value.type, type_bool, stmt.value->token.at);
                const auto otherwise = emit(Op::jump_false, at);
                frame->scopes.emplace_back();
                const bool yes = block(stmt.body, report && value.truth != false);
                frame->scopes.pop_back();
                const auto done = emit(Op::jump, at);
                patch(otherwise);
                frame->scopes.emplace_back();
                const bool no = block(stmt.alternate, report && value.truth != true);
                frame->scopes.pop_back();
                patch(done);
                return yes && no;
            }
            case FormStmt::loop_range: {
                Fact start = Fact::integer(0, 0);
                if (stmt.target) {
                    start = expression(*stmt.target, report);
                    require(start.type, type_int, stmt.target->token.at);
                } else {
                    emit(Op::push_integer, at, 0);
                }
                const auto slot = local(at);
                const auto limit = local(at);
                if (limit != slot + 1) throw std::logic_error("loop slots must be adjacent");
                emit(Op::store, at, slot);
                const auto end = expression(*stmt.value, report);
                require(end.type, type_int, stmt.value->token.at);
                emit(Op::store, at, limit);
                const bool empty = start.interval && end.interval && end.interval->high <= start.interval->low;
                if (empty) warning(report, stmt.value->token.at, "empty-range", "this loop has no iterations");
                Fact bound(type_int);
                if (start.interval && end.interval && !empty) bound = Fact::integer(start.interval->low, end.interval->high - 1);
                frame->scopes.emplace_back();
                bind(stmt.token, bound, false, slot);
                const auto init = emit(Op::loop_init, at);
                const auto body = position_now();
                frame->loops.emplace_back();
                block(stmt.body, report && !empty);
                const auto loop = frame->loops.back();
                frame->loops.pop_back();
                for (const auto fix : loop.continues) patch(fix);
                emit(Op::loop_next, at, (static_cast<std::int64_t>(slot) << 32) | body);
                patch_packed(init, slot);
                for (const auto fix : loop.breaks) patch(fix);
                frame->scopes.pop_back();
                return false;
            }
            case FormStmt::loop_list: {
                const auto items = expression(*stmt.value, report);
                if (kind(items.type) != Kind::list) throw Error(stmt.value->token.at, "for-in expects a list, found " + name_of(items.type));
                const auto slot = local(at);
                const auto holder = local(at);
                const auto cursor = local(at);
                if (holder != slot + 1 || cursor != slot + 2) throw std::logic_error("loop slots must be adjacent");
                emit(Op::store, at, holder);
                emit(Op::push_integer, at, 0);
                emit(Op::store, at, cursor);
                frame->scopes.emplace_back();
                bind(stmt.token, Fact(info(items.type).first), false, slot);
                const auto head = position_now();
                const auto next = emit(Op::iter_next, at);
                frame->loops.emplace_back();
                block(stmt.body, report);
                const auto loop = frame->loops.back();
                frame->loops.pop_back();
                for (const auto fix : loop.continues) patch(fix);
                emit(Op::jump, at, head);
                patch_packed(next, slot);
                for (const auto fix : loop.breaks) patch(fix);
                frame->scopes.pop_back();
                return false;
            }
            case FormStmt::loop_while: {
                const auto head = position_now();
                const auto value = expression(*stmt.value, report);
                require(value.type, type_bool, stmt.value->token.at);
                const auto exit = emit(Op::jump_false, at);
                frame->scopes.emplace_back();
                frame->loops.emplace_back();
                block(stmt.body, report && value.truth != false);
                const auto loop = frame->loops.back();
                frame->loops.pop_back();
                frame->scopes.pop_back();
                for (const auto fix : loop.continues) patch(fix);
                emit(Op::jump, at, head);
                patch(exit);
                for (const auto fix : loop.breaks) patch(fix);
                // `while True:` without a break never falls through.
                return value.truth == true && loop.breaks.empty();
            }
            case FormStmt::stop:
            case FormStmt::next: {
                if (frame->loops.empty()) throw Error(at, "'" + stmt.token.text + "' is only valid inside a loop");
                const auto jump = emit(Op::jump, at);
                (stmt.form == FormStmt::stop ? frame->loops.back().breaks : frame->loops.back().continues).push_back(jump);
                return true;
            }
        }
        return false;
    }

    // --- declarations ---
    ConstValue constant(const Expr& expr, const Token& name, Unit& in) {
        ConstValue value{type_unit, 0, 0.0, false, {}};
        const Expr* node = &expr;
        bool negative = false;
        if (node->form == Form::unary && node->token.text == "-") {
            negative = true;
            node = &node->parts[0];
        }
        switch (node->form) {
            case Form::integer: value.type = type_int; value.integer = negative ? arithmetic(Op::negate, node->number, 0) : node->number; break;
            case Form::floating: value.type = type_float; value.real = negative ? -node->real : node->real; break;
            case Form::boolean: if (negative) break; value.type = type_bool; value.truth = node->number != 0; break;
            case Form::text: if (negative) break; value.type = type_text; value.text = node->token.text; break;
            case Form::name: {
                if (negative) break;
                const auto found = in.consts.find(node->token.text);
                if (found == in.consts.end()) break;
                return found->second;
            }
            default: break;
        }
        if (value.type == type_unit) throw Error(name.at, "a const must be an integer, float, bool, or string literal");
        return value;
    }

    std::uint32_t declare_struct(Unit& in, const StructDecl& decl, const std::string& display, std::vector<TypeId> args, Subst subst) {
        const auto id = static_cast<std::uint32_t>(program.structs.size());
        program.structs.push_back({display, {}, {}, {}});
        struct_origins[id] = {units_index(in), &decl, std::move(args), subst};
        return id;
    }

    std::size_t units_index(const Unit& in) const {
        for (std::size_t i = 0; i < units.size(); ++i) if (units[i].get() == &in) return i;
        throw std::logic_error("unknown unit");
    }

    void fill_struct(std::uint32_t id) {
        const auto origin = struct_origins.at(id);  // copy: instantiating field types may add structs
        Unit& in = *units[origin.unit];
        for (const auto& [field, type] : origin.decl->fields) {
            const auto& names = program.structs[id].field_names;
            if (std::find(names.begin(), names.end(), field.text) != names.end()) {
                throw Error(field.at, "duplicate field '" + field.text + "'", in.file);
            }
            program.structs[id].field_names.push_back(field.text);
            const auto resolved = resolve_in(type, in, &origin.subst);
            program.structs[id].field_types.push_back(resolved);
        }
    }

    std::uint32_t instantiate_struct(Unit& in, const StructDecl& decl, const std::vector<TypeId>& args) {
        const auto key = std::to_string(units_index(in)) + ":" + mangle(decl.name.text, args);
        if (const auto found = struct_instantiations.find(key); found != struct_instantiations.end()) return found->second;
        check_bounds(in, decl.type_params, args, decl.name.at);
        Subst subst;
        for (std::size_t i = 0; i < decl.type_params.size(); ++i) subst[decl.type_params[i].name.text] = args[i];
        const auto id = declare_struct(in, decl, mangle(decl.name.text, args), args, subst);
        struct_instantiations[key] = id;
        fill_struct(id);
        declare_methods(in, decl, id, subst);
        conform(in, decl, id, subst);
        compile_methods(in, decl, id);
        return id;
    }

    void check_bounds(Unit& in, const std::vector<TypeParam>& params, const std::vector<TypeId>& args, Location at) {
        for (std::size_t i = 0; i < params.size() && i < args.size(); ++i) {
            if (!params[i].bound) continue;
            const auto bound = resolve_in(*params[i].bound, in, nullptr);
            if (kind(bound) != Kind::trait_object) throw Error(params[i].bound->name.at, "a type parameter bound must be a trait", in.file);
            if (!implements(args[i], info(bound).first)) {
                throw Error(at, "type argument " + name_of(args[i]) + " for '" + params[i].name.text + "' does not implement " + name_of(bound));
            }
        }
    }

    // Checks that struct `id` provides every method of each trait it declares, and records the dispatch tables.
    void conform(Unit& in, const StructDecl& decl, std::uint32_t id, const Subst& subst) {
        for (const auto& trait_expr : decl.traits) {
            const auto type = resolve_in(trait_expr, in, &subst);
            if (kind(type) != Kind::trait_object) throw Error(trait_expr.name.at, "'" + name_of(type) + "' is not a trait", in.file);
            const auto trait = info(type).first;
            const auto& info_ = program.traits[trait];
            std::vector<std::uint32_t> table;
            for (std::size_t m = 0; m < info_.method_names.size(); ++m) {
                const auto found = find_method(id, info_.method_names[m]);
                if (!found) {
                    throw Error(decl.name.at, "'" + program.structs[id].name + "' does not implement " + info_.name + "." + info_.method_names[m], in.file);
                }
                const auto& actual = program.functions[*found];
                const auto& wanted = info(info_.method_types[m]);
                std::vector<TypeId> actual_params(actual.params.begin() + 1, actual.params.end());
                if (actual_params != wanted.params || actual.result != wanted.first) {
                    throw Error(decl.name.at, "'" + program.structs[id].name + "." + info_.method_names[m] + "' has signature " +
                                name_of(function_of(actual_params, actual.result)) + " but " + info_.name + " requires " + name_of(info_.method_types[m]), in.file);
                }
                table.push_back(*found);
            }
            program.structs[id].vtables.emplace_back(trait, std::move(table));
        }
    }

    void declare_methods(Unit& in, const StructDecl& decl, std::uint32_t id, const Subst& subst) {
        for (const auto& method : decl.methods) {
            if (methods[id].count(method.name.text)) throw Error(method.name.at, "duplicate method '" + method.name.text + "'", in.file);
            if (!method.type_params.empty()) throw Error(method.name.at, "methods cannot have their own type parameters", in.file);
            Function header;
            header.name = program.structs[id].name + "." + method.name.text;
            header.params.push_back(object_of(id));
            for (std::size_t p = 1; p < method.params.size(); ++p) header.params.push_back(resolve_in(method.params[p].second, in, &subst));
            header.result = method.result ? resolve_in(*method.result, in, &subst) : type_unit;
            const auto function_id = static_cast<std::uint32_t>(program.functions.size());
            program.functions.push_back(std::move(header));
            methods[id][method.name.text] = function_id;
        }
    }

    void compile_methods(Unit& in, const StructDecl& decl, std::uint32_t id) {
        const auto origin = struct_origins.at(id);
        for (const auto& method : decl.methods) {
            Frame inner;
            inner.function = methods[id][method.name.text];
            inner.unit = units_index(in);
            inner.subst = origin.subst;
            compile_body(inner, method, in);
        }
    }

    std::uint32_t instantiate_function(Unit& in, const FunctionDecl& decl, const std::vector<TypeId>& args, std::optional<Subst> extra) {
        const auto key = std::to_string(units_index(in)) + ":" + mangle(decl.name.text, args);
        if (const auto found = instantiations.find(key); found != instantiations.end()) return found->second;
        Subst subst = extra ? *extra : Subst{};
        for (std::size_t i = 0; i < decl.type_params.size(); ++i) subst[decl.type_params[i].name.text] = args[i];
        Function header;
        header.name = (in.display.empty() ? "" : in.display + ".") + mangle(decl.name.text, args);
        for (const auto& [parameter, type] : decl.params) header.params.push_back(resolve_in(type, in, &subst));
        header.result = decl.result ? resolve_in(*decl.result, in, &subst) : type_unit;
        const auto id = static_cast<std::uint32_t>(program.functions.size());
        program.functions.push_back(std::move(header));
        instantiations[key] = id;
        Frame inner;
        inner.function = id;
        inner.unit = units_index(in);
        inner.subst = subst;
        compile_body(inner, decl, in);
        return id;
    }

    void compile_body(Frame& inner, const FunctionDecl& decl, Unit& in) {
        Frame* outer = frame;
        frame = &inner;
        try {
            inner.scopes.emplace_back();
            collect_escapes(decl.body, inner.escaped);
            for (std::size_t p = 0; p < decl.params.size(); ++p) bind(decl.params[p].first, Fact(fn().params[p]), false);
            const bool exits = block(decl.body, true);
            if (!exits && fn().result != type_unit) throw Error(decl.name.at, "a path reaches the end without returning a value");
            emit(Op::finish, decl.name.at);
            optimize(fn());
        } catch (Error& error) {
            frame = outer;
            if (error.file.empty()) error.file = in.file;
            throw;
        }
        frame = outer;
    }

    // --- modules ---
    std::size_t load_unit(const std::string& source, const std::filesystem::path& path, const std::string& display, bool root) {
        auto unit_ptr = std::make_unique<Unit>();
        Unit& u = *unit_ptr;
        u.key = root ? "" : path.string();
        u.display = display;
        u.file = root ? "" : path.string();
        u.directory = path.empty() ? std::filesystem::current_path() : std::filesystem::absolute(path).parent_path();
        u.loading = true;
        try {
            u.ast = Parser(source).parse();
        } catch (Error& error) {
            if (error.file.empty()) error.file = u.file;
            throw;
        }
        const auto index = units.size();
        units.push_back(std::move(unit_ptr));
        for (const auto& import : units[index]->ast.imports) {
            const auto target = load_import(*units[index], import);
            auto& current = *units[index];
            if (current.imports.count(import.alias.text)) throw Error(import.alias.at, "duplicate import alias '" + import.alias.text + "'", current.file);
            current.imports[import.alias.text] = target;
        }
        units[index]->loading = false;
        return index;
    }

    std::size_t load_import(Unit& from, const ImportDecl& import) {
        std::filesystem::path relative;
        for (const auto& part : import.path) relative /= part.text;
        relative += ".tq";
        std::vector<std::filesystem::path> candidates;
        const auto add_root = [&](const std::filesystem::path& root) {
            candidates.push_back(root / relative);                                   // a/b/c.tq
            candidates.push_back(root / relative.parent_path() / relative.stem() / "lib.tq");  // a/b/c/lib.tq
        };
        add_root(from.directory);
        // Packages installed with `tacqor add` live in packages/ next to the root program or in the working directory.
        add_root(units[0]->directory / "packages");
        add_root(std::filesystem::current_path() / "packages");
        for (const auto& dir : options.search) add_root(dir);
        if (const char* extra = std::getenv("TACQOR_PATH")) {
            const std::string_view entries(extra);
            for (std::size_t start = 0; start <= entries.size();) {
                const auto end = entries.find(':', start);
                const auto part = entries.substr(start, end == entries.npos ? entries.npos : end - start);
                if (!part.empty()) candidates.push_back(std::filesystem::path(std::string(part)) / relative);
                if (end == entries.npos) break;
                start = end + 1;
            }
        }
        for (const auto& candidate : candidates) {
            std::error_code ignored;
            if (!std::filesystem::is_regular_file(candidate, ignored)) continue;
            const auto key = std::filesystem::weakly_canonical(candidate, ignored).string();
            for (std::size_t i = 0; i < units.size(); ++i) {
                if (units[i]->key == key) {
                    if (units[i]->loading) throw Error(import.path.front().at, "circular import of '" + import.path.back().text + "'", from.file);
                    return i;
                }
            }
            std::ifstream file(candidate, std::ios::binary);
            if (!file) throw Error(import.path.front().at, "could not read module file " + candidate.string(), from.file);
            std::string source;
            try {
                source = read_text(file, source_limit);
            } catch (const std::exception& error) {
                throw Error(import.path.front().at, std::string("could not read module: ") + error.what(), from.file);
            }
            const auto index = load_unit(source, std::filesystem::path(key), import.path.back().text, false);
            return index;
        }
        std::string looked;
        for (const auto& candidate : candidates) looked += "\n  " + candidate.string();
        throw Error(import.path.front().at, "module '" + relative.string() + "' not found; looked in:" + looked, from.file);
    }

public:
    Compiler(std::vector<Finding>& output, const CompileOptions& opts) : findings(output), options(opts) {
        program.types = {{Kind::unit, 0, 0, {}}, {Kind::integer, 0, 0, {}}, {Kind::floating, 0, 0, {}},
                         {Kind::boolean, 0, 0, {}}, {Kind::text, 0, 0, {}}};
    }

    Program compile(std::string_view source) {
        load_unit(std::string(source), options.source_path, "", true);
        // 1. Register struct and function names in every unit.
        for (std::size_t ui = 0; ui < units.size(); ++ui) {
            Unit& u = *units[ui];
            for (const auto& decl : u.ast.structs) {
                if (u.structs.count(decl.name.text) || u.generic_structs.count(decl.name.text) || u.imports.count(decl.name.text)) {
                    throw Error(decl.name.at, "duplicate struct '" + decl.name.text + "'", u.file);
                }
                if (decl.type_params.empty()) {
                    const auto id = declare_struct(u, decl, u.display.empty() ? decl.name.text : u.display + "." + decl.name.text, {}, {});
                    u.structs[decl.name.text] = id;
                } else {
                    u.generic_structs[decl.name.text] = &decl;
                }
            }
            for (const auto& decl : u.ast.consts) {
                if (u.consts.count(decl.name.text) || u.structs.count(decl.name.text) || u.imports.count(decl.name.text)) {
                    throw Error(decl.name.at, "duplicate name '" + decl.name.text + "'", u.file);
                }
                try {
                    u.consts[decl.name.text] = constant(decl.value, decl.name, u);
                } catch (Error& error) {
                    if (error.file.empty()) error.file = u.file;
                    throw;
                }
            }
        }
        // 1b. Trait names, then their method signatures (which may mention structs and other traits).
        for (std::size_t ui = 0; ui < units.size(); ++ui) {
            Unit& u = *units[ui];
            for (const auto& decl : u.ast.traits) {
                if (u.structs.count(decl.name.text) || u.generic_structs.count(decl.name.text) || u.traits.count(decl.name.text) ||
                    u.imports.count(decl.name.text) || u.consts.count(decl.name.text)) {
                    throw Error(decl.name.at, "duplicate name '" + decl.name.text + "'", u.file);
                }
                const auto id = static_cast<std::uint32_t>(program.traits.size());
                program.traits.push_back({u.display.empty() ? decl.name.text : u.display + "." + decl.name.text, {}, {}});
                u.traits[decl.name.text] = id;
            }
        }
        for (std::size_t ui = 0; ui < units.size(); ++ui) {
            Unit& u = *units[ui];
            for (const auto& decl : u.ast.traits) {
                const auto id = u.traits.at(decl.name.text);
                for (const auto& method : decl.methods) {
                    if (!method.type_params.empty()) throw Error(method.name.at, "trait methods cannot be generic", u.file);
                    auto& info_ = program.traits[id];
                    if (std::find(info_.method_names.begin(), info_.method_names.end(), method.name.text) != info_.method_names.end()) {
                        throw Error(method.name.at, "duplicate trait method '" + method.name.text + "'", u.file);
                    }
                    std::vector<TypeId> params;
                    TypeId result;
                    try {
                        for (std::size_t p = 1; p < method.params.size(); ++p) params.push_back(resolve_in(method.params[p].second, u, nullptr));
                        result = method.result ? resolve_in(*method.result, u, nullptr) : type_unit;
                    } catch (Error& error) {
                        if (error.file.empty()) error.file = u.file;
                        throw;
                    }
                    const auto type = function_of(params, result);
                    program.traits[id].method_names.push_back(method.name.text);
                    program.traits[id].method_types.push_back(type);
                }
            }
        }
        // 2. Resolve the fields of non-generic structs.
        for (std::size_t ui = 0; ui < units.size(); ++ui) {
            for (const auto& [name, id] : units[ui]->structs) fill_struct(id);
        }
        // 3. Function headers.
        for (std::size_t ui = 0; ui < units.size(); ++ui) {
            Unit& u = *units[ui];
            for (const auto& decl : u.ast.functions) {
                const auto& name = decl.name;
                if (u.structs.count(name.text) || u.generic_structs.count(name.text) || u.functions.count(name.text) ||
                    u.generic_functions.count(name.text) || u.consts.count(name.text) || u.imports.count(name.text)) {
                    throw Error(name.at, "duplicate function '" + name.text + "'", u.file);
                }
                if (!decl.type_params.empty()) {
                    u.generic_functions[name.text] = &decl;
                    continue;
                }
                Function header;
                header.name = u.display.empty() ? name.text : u.display + "." + name.text;
                try {
                    for (const auto& [parameter, kind] : decl.params) header.params.push_back(resolve_in(kind, u, nullptr));
                    header.result = decl.result ? resolve_in(*decl.result, u, nullptr) : type_unit;
                } catch (Error& error) {
                    if (error.file.empty()) error.file = u.file;
                    throw;
                }
                if (name.text == "main" && (!header.params.empty() || header.result != type_unit)) {
                    throw Error(name.at, "entry point must be def main():", u.file);
                }
                const auto id = static_cast<std::uint32_t>(program.functions.size());
                program.functions.push_back(std::move(header));
                u.functions[name.text] = id;
            }
            for (const auto& decl : u.ast.structs) {
                if (!decl.type_params.empty()) continue;
                try {
                    declare_methods(u, decl, u.structs.at(decl.name.text), {});
                } catch (Error& error) {
                    if (error.file.empty()) error.file = u.file;
                    throw;
                }
            }
        }
        // 3b. Trait conformance of non-generic structs (needs every method header declared first).
        for (std::size_t ui = 0; ui < units.size(); ++ui) {
            Unit& u = *units[ui];
            for (const auto& decl : u.ast.structs) {
                if (!decl.type_params.empty()) continue;
                try {
                    conform(u, decl, u.structs.at(decl.name.text), {});
                } catch (Error& error) {
                    if (error.file.empty()) error.file = u.file;
                    throw;
                }
            }
        }
        const auto entry = units[0]->functions.find("main");
        if (entry == units[0]->functions.end()) {
            if (options.require_main) throw Error({}, "missing def main():");
            // A module compiled on its own (e.g. for `tacqor test` or `check`): synthesize an empty main.
            Function header;
            header.name = "main";
            program.functions.push_back(std::move(header));
            units[0]->functions["main"] = static_cast<std::uint32_t>(program.functions.size() - 1);
            program.entry = units[0]->functions["main"];
            program.functions[program.entry].code.push_back({Op::finish, 0, {}});
        } else {
            program.entry = entry->second;
        }
        // 4. Bodies of all non-generic functions and methods (generic ones are compiled on demand).
        for (std::size_t ui = 0; ui < units.size(); ++ui) {
            Unit& u = *units[ui];
            for (const auto& decl : u.ast.functions) {
                if (!decl.type_params.empty()) continue;
                Frame inner;
                inner.function = u.functions.at(decl.name.text);
                inner.unit = ui;
                compile_body(inner, decl, u);
            }
            for (const auto& decl : u.ast.structs) {
                if (!decl.type_params.empty()) continue;
                compile_methods(u, decl, u.structs.at(decl.name.text));
            }
        }
        return std::move(program);
    }
};

}  // namespace

Program compile(std::string_view source, std::vector<Finding>& findings, const CompileOptions& options) {
    return Compiler(findings, options).compile(source);
}

}  // namespace tacqor
