// A minimal Language Server for Tacqor. It speaks LSP over stdio and turns the
// diagnostics the compiler already produces into editor squiggles, so any editor with
// an LSP client (VS Code, Neovim, Helix, Emacs, ...) gets live errors and warnings.
//
// Scope: textDocument/didOpen, didChange (full sync) and didClose publish diagnostics.
// Hover, completion and go-to-definition are not implemented yet; the server advertises
// only what it supports, so clients degrade gracefully.
#include "tacqor.hpp"

#include <cctype>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tacqor {
namespace {

namespace fs = std::filesystem;

// --- a small JSON reader, enough for LSP requests ---------------------------
struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    const Json* find(std::string_view key) const {
        if (type != Type::Object) return nullptr;
        for (const auto& [name, value] : members) if (name == key) return &value;
        return nullptr;
    }
    std::string as_string() const { return type == Type::String ? text : std::string(); }
};

struct JsonParser {
    std::string_view s;
    std::size_t i = 0;

    void skip() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i; }
    bool parse(Json& out) { skip(); return value(out); }

    bool value(Json& out) {
        skip();
        if (i >= s.size()) return false;
        const char c = s[i];
        if (c == '{') return object(out);
        if (c == '[') return array(out);
        if (c == '"') { out.type = Json::Type::String; return string(out.text); }
        if (c == 't' || c == 'f') return boolean(out);
        if (c == 'n') return null(out);
        return number(out);
    }
    bool object(Json& out) {
        out.type = Json::Type::Object;
        ++i;  // {
        skip();
        if (i < s.size() && s[i] == '}') { ++i; return true; }
        while (i < s.size()) {
            skip();
            std::string key;
            if (i >= s.size() || s[i] != '"' || !string(key)) return false;
            skip();
            if (i >= s.size() || s[i] != ':') return false;
            ++i;
            Json child;
            if (!value(child)) return false;
            out.members.emplace_back(std::move(key), std::move(child));
            skip();
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            return false;
        }
        return false;
    }
    bool array(Json& out) {
        out.type = Json::Type::Array;
        ++i;  // [
        skip();
        if (i < s.size() && s[i] == ']') { ++i; return true; }
        while (i < s.size()) {
            Json child;
            if (!value(child)) return false;
            out.items.push_back(std::move(child));
            skip();
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            return false;
        }
        return false;
    }
    bool string(std::string& out) {
        ++i;  // opening quote
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) return false;
            const char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (i + 4 > s.size()) return false;
                    unsigned code = 0;
                    for (int k = 0; k < 4; ++k) {
                        const char h = s[i++];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                        else return false;
                    }
                    // Encode the code point as UTF-8 (surrogate pairs are left as-is; rare in LSP text).
                    if (code < 0x80) out += static_cast<char>(code);
                    else if (code < 0x800) {
                        out += static_cast<char>(0xC0 | (code >> 6));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (code >> 12));
                        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    }
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool boolean(Json& out) {
        if (s.compare(i, 4, "true") == 0) { out = {}; out.type = Json::Type::Bool; out.boolean = true; i += 4; return true; }
        if (s.compare(i, 5, "false") == 0) { out = {}; out.type = Json::Type::Bool; out.boolean = false; i += 5; return true; }
        return false;
    }
    bool null(Json& out) {
        if (s.compare(i, 4, "null") == 0) { out = {}; out.type = Json::Type::Null; i += 4; return true; }
        return false;
    }
    bool number(Json& out) {
        const std::size_t start = i;
        while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+' ||
                                s[i] == '.' || s[i] == 'e' || s[i] == 'E')) ++i;
        if (i == start) return false;
        out.type = Json::Type::Number;
        try { out.number = std::stod(std::string(s.substr(start, i - start))); } catch (...) { return false; }
        return true;
    }
};

// --- JSON string escaping for responses -------------------------------------
std::string quote(std::string_view text) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char byte : text) {
        if (byte == '"' || byte == '\\') { out += '\\'; out += static_cast<char>(byte); }
        else if (byte < 32) { out += "\\u00"; out += hex[byte >> 4]; out += hex[byte & 15]; }
        else out += static_cast<char>(byte);
    }
    out += '"';
    return out;
}

// --- LSP stdio framing ------------------------------------------------------
std::optional<std::string> read_message() {
    std::size_t length = 0;
    std::string line;
    bool have_length = false;
    // Read headers until a blank line.
    while (true) {
        line.clear();
        std::getline(std::cin, line);
        if (!std::cin) return std::nullopt;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;  // end of headers
        const auto colon = line.find(':');
        if (colon != std::string::npos) {
            std::string name = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            while (!value.empty() && value.front() == ' ') value.erase(value.begin());
            if (name == "Content-Length") { try { length = std::stoul(value); have_length = true; } catch (...) {} }
        }
    }
    if (!have_length) return std::nullopt;
    std::string body(length, '\0');
    std::cin.read(&body[0], static_cast<std::streamsize>(length));
    if (static_cast<std::size_t>(std::cin.gcount()) != length) return std::nullopt;
    return body;
}

void write_message(const std::string& body) {
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    std::cout.flush();
}

// file:// URI -> filesystem path (minimal: strip scheme, percent-decode, fix Windows drive).
std::string uri_to_path(const std::string& uri) {
    std::string rest = uri;
    const std::string scheme = "file://";
    if (rest.rfind(scheme, 0) == 0) rest = rest.substr(scheme.size());
    std::string decoded;
    for (std::size_t k = 0; k < rest.size(); ++k) {
        if (rest[k] == '%' && k + 2 < rest.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = hex(rest[k + 1]), lo = hex(rest[k + 2]);
            if (hi >= 0 && lo >= 0) { decoded += static_cast<char>(hi * 16 + lo); k += 2; continue; }
        }
        decoded += rest[k];
    }
    // "/C:/path" on Windows -> "C:/path"
    if (decoded.size() >= 3 && decoded[0] == '/' && std::isalpha(static_cast<unsigned char>(decoded[1])) && decoded[2] == ':') {
        decoded.erase(decoded.begin());
    }
    return decoded;
}

struct Server {
    std::vector<fs::path> search;
    bool initialized = false;
    bool shutting_down = false;

    CompileOptions options_for(const std::string& path) const {
        CompileOptions options;
        options.source_path = path;
        options.search = search;
        options.require_main = false;   // a file being edited may be a module, not a program
        return options;
    }

    // Compile the buffer and publish its diagnostics for `uri`.
    void publish(const std::string& uri, const std::string& text) {
        std::vector<Finding> findings;
        try {
            compile(text, findings, options_for(uri_to_path(uri)));
        } catch (const Error& error) {
            findings.clear();
            findings.push_back({error.at.line ? error.at : Location{1, 1, 1}, false, "compile-error", error.what(), {}});
        }
        std::string out = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":" + quote(uri) + ",\"diagnostics\":[";
        bool first = true;
        for (const auto& f : findings) {
            if (!f.file.empty()) continue;  // only the open file, not its imports
            const long line = f.at.line > 0 ? static_cast<long>(f.at.line) - 1 : 0;
            const long col = f.at.column > 0 ? static_cast<long>(f.at.column) - 1 : 0;
            const long end = col + (f.at.length > 0 ? static_cast<long>(f.at.length) : 1);
            if (!first) out += ',';
            first = false;
            out += "{\"range\":{\"start\":{\"line\":" + std::to_string(line) + ",\"character\":" + std::to_string(col) +
                   "},\"end\":{\"line\":" + std::to_string(line) + ",\"character\":" + std::to_string(end) + "}},\"severity\":" +
                   std::to_string(f.warning ? 2 : 1) + ",\"source\":\"tacqor\",\"code\":" + quote(f.code) +
                   ",\"message\":" + quote(f.message) + "}";
        }
        out += "]}}";
        write_message(out);
    }

    void reply(const Json& id, const std::string& result) {
        std::string id_text = "null";
        if (id.type == Json::Type::Number) {
            // Keep integer ids integer.
            const long long n = static_cast<long long>(id.number);
            id_text = static_cast<double>(n) == id.number ? std::to_string(n) : std::to_string(id.number);
        } else if (id.type == Json::Type::String) {
            id_text = quote(id.text);
        }
        write_message("{\"jsonrpc\":\"2.0\",\"id\":" + id_text + ",\"result\":" + result + "}");
    }

    // Returns false when the server should exit.
    bool handle(const Json& message) {
        const Json* method = message.find("method");
        if (!method || method->type != Json::Type::String) return true;
        const std::string name = method->text;
        const Json* id = message.find("id");
        const Json* params = message.find("params");

        if (name == "initialize") {
            // Advertise exactly what we support: full-document sync.
            const std::string caps =
                "{\"capabilities\":{\"textDocumentSync\":1},\"serverInfo\":{\"name\":\"tacqor-lsp\",\"version\":\"" +
                std::string(version) + "\"}}";
            if (id) reply(*id, caps);
            initialized = true;
            return true;
        }
        if (name == "shutdown") {
            shutting_down = true;
            if (id) reply(*id, "null");
            return true;
        }
        if (name == "exit") return false;

        if (name == "textDocument/didOpen" && params) {
            if (const Json* doc = params->find("textDocument")) {
                const std::string uri = doc->find("uri") ? doc->find("uri")->as_string() : "";
                const std::string text = doc->find("text") ? doc->find("text")->as_string() : "";
                if (!uri.empty()) publish(uri, text);
            }
            return true;
        }
        if (name == "textDocument/didChange" && params) {
            const Json* doc = params->find("textDocument");
            const Json* changes = params->find("contentChanges");
            if (doc && changes && changes->type == Json::Type::Array && !changes->items.empty()) {
                const std::string uri = doc->find("uri") ? doc->find("uri")->as_string() : "";
                // Full sync: the last change carries the whole document.
                const Json& last = changes->items.back();
                const std::string text = last.find("text") ? last.find("text")->as_string() : "";
                if (!uri.empty()) publish(uri, text);
            }
            return true;
        }
        if (name == "textDocument/didClose" && params) {
            if (const Json* doc = params->find("textDocument")) {
                const std::string uri = doc->find("uri") ? doc->find("uri")->as_string() : "";
                if (!uri.empty()) write_message(
                    "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":" +
                    quote(uri) + ",\"diagnostics\":[]}}");
            }
            return true;
        }
        // Unknown request: answer with null so the client is not left waiting.
        if (id) reply(*id, "null");
        return true;
    }
};

}  // namespace

int run_lsp(const std::vector<std::filesystem::path>& search) {
    std::ios::sync_with_stdio(false);
    Server server;
    server.search = search;
    while (true) {
        auto body = read_message();
        if (!body) break;
        Json message;
        JsonParser parser{*body, 0};
        if (!parser.parse(message)) continue;  // ignore malformed frames
        if (!server.handle(message)) break;
    }
    return 0;
}

}  // namespace tacqor
