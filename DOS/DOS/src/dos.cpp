// DOS - Windows CLI + .dos programming environment
// Finalized DOS 1.0 runtime.
// C++20, Windows-first.

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>
#include <cstring>
#include <cmath>
#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#endif

namespace fs = std::filesystem;

namespace dos {

static std::string trim(std::string s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static std::string join(const std::vector<std::string>& parts, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

struct DosError : std::runtime_error {
    size_t line;
    size_t col;

    DosError(std::string message, size_t line_ = 0, size_t col_ = 0)
        : std::runtime_error(std::move(message)), line(line_), col(col_) {}
};

struct Value;
using ValuePtr = std::shared_ptr<Value>;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

struct PointerValue {
    ValuePtr target;
};

struct Value {
    using Storage = std::variant<std::monostate, bool, int64_t, double,
                                 std::string, Array, Object, PointerValue>;
    Storage data;

    Value() = default;
    explicit Value(bool v) : data(v) {}
    explicit Value(int64_t v) : data(v) {}
    explicit Value(int v) : data(static_cast<int64_t>(v)) {}
    explicit Value(double v) : data(v) {}
    explicit Value(std::string v) : data(std::move(v)) {}
    explicit Value(const char* v) : data(std::string(v)) {}
    explicit Value(Array v) : data(std::move(v)) {}
    explicit Value(Object v) : data(std::move(v)) {}
    explicit Value(PointerValue v) : data(std::move(v)) {}

    bool is_null() const { return std::holds_alternative<std::monostate>(data); }
    bool is_bool() const { return std::holds_alternative<bool>(data); }
    bool is_int() const { return std::holds_alternative<int64_t>(data); }
    bool is_double() const { return std::holds_alternative<double>(data); }
    bool is_number() const { return is_int() || is_double(); }
    bool is_string() const { return std::holds_alternative<std::string>(data); }
    bool is_array() const { return std::holds_alternative<Array>(data); }
    bool is_object() const { return std::holds_alternative<Object>(data); }
    bool is_pointer() const { return std::holds_alternative<PointerValue>(data); }

    int64_t as_int() const {
        if (is_int()) return std::get<int64_t>(data);
        if (is_double()) return static_cast<int64_t>(std::get<double>(data));
        if (is_bool()) return std::get<bool>(data) ? 1 : 0;
        throw DosError("value is not numeric");
    }

    double as_double() const {
        if (is_double()) return std::get<double>(data);
        if (is_int()) return static_cast<double>(std::get<int64_t>(data));
        if (is_bool()) return std::get<bool>(data) ? 1.0 : 0.0;
        throw DosError("value is not numeric");
    }

    bool as_bool() const {
        if (is_bool()) return std::get<bool>(data);
        if (is_null()) return false;
        if (is_number()) return as_double() != 0.0;
        if (is_string()) return !std::get<std::string>(data).empty();
        return true;
    }

    std::string str() const {
        if (is_null()) return "null";
        if (is_bool()) return std::get<bool>(data) ? "true" : "false";
        if (is_int()) return std::to_string(std::get<int64_t>(data));
        if (is_double()) {
            std::ostringstream os;
            os << std::setprecision(15) << std::get<double>(data);
            return os.str();
        }
        if (is_string()) return std::get<std::string>(data);
        if (is_array()) {
            std::vector<std::string> items;
            for (const Value& v : std::get<Array>(data)) items.push_back(v.str());
            return "[" + join(items, ", ") + "]";
        }
        if (is_object()) return "<object>";
        if (is_pointer()) {
            return std::get<PointerValue>(data).target ? "<ptr valid>" : "<ptr null>";
        }
        return {};
    }
};

enum class TokenKind {
    End,
    Eol,
    Identifier,
    Number,
    String,
    LParen, RParen,
    LBrace, RBrace,
    LBracket, RBracket,
    Comma, Dot, Colon, Semicolon,
    Plus, Minus, Star, Slash, Percent,
    Amp, Pipe, Caret, Bang,
    Equal, EqualEqual, BangEqual,
    Less, LessEqual, Greater, GreaterEqual,
    ShiftLeft, ShiftRight,
    AndAnd, OrOr,
    PlusPlus, MinusMinus,
    PlusEqual, MinusEqual, StarEqual, SlashEqual, PercentEqual,
    Question, Tilde
};

struct Token {
    TokenKind kind;
    std::string text;
    size_t line = 1;
    size_t col = 1;
};

class Lexer {
public:
    explicit Lexer(std::string source) : source_(std::move(source)) {}

    std::vector<Token> lex() const {
        std::vector<Token> tokens;
        size_t i = 0, line = 1, col = 1;

        auto push = [&](TokenKind kind, std::string text, size_t l, size_t c) {
            tokens.push_back(Token{kind, std::move(text), l, c});
        };

        while (i < source_.size()) {
            const char c = source_[i];

            if (c == ' ' || c == '\t' || c == '\r') {
                ++i;
                ++col;
                continue;
            }

            if (c == '\n') {
                push(TokenKind::Eol, {}, line, col);
                ++i;
                ++line;
                col = 1;
                continue;
            }

            if (c == '/' && i + 1 < source_.size() && source_[i + 1] == '/') {
                i += 2;
                col += 2;
                while (i < source_.size() && source_[i] != '\n') {
                    ++i;
                    ++col;
                }
                continue;
            }

            if (c == '/' && i + 1 < source_.size() && source_[i + 1] == '*') {
                const size_t start_line = line, start_col = col;
                i += 2;
                col += 2;
                bool closed = false;
                while (i < source_.size()) {
                    if (source_[i] == '*' && i + 1 < source_.size() && source_[i + 1] == '/') {
                        i += 2;
                        col += 2;
                        closed = true;
                        break;
                    }
                    if (source_[i] == '\n') {
                        ++i;
                        ++line;
                        col = 1;
                    } else {
                        ++i;
                        ++col;
                    }
                }
                if (!closed) throw DosError("unterminated block comment", start_line, start_col);
                continue;
            }

            const size_t start_line = line, start_col = col;

            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                const size_t begin = i++;
                ++col;
                while (i < source_.size() &&
                       (std::isalnum(static_cast<unsigned char>(source_[i])) || source_[i] == '_')) {
                    ++i;
                    ++col;
                }
                push(TokenKind::Identifier, source_.substr(begin, i - begin), start_line, start_col);
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(c))) {
                const size_t begin = i;
                bool floating = false;
                if (c == '0' && i + 1 < source_.size() &&
                    (source_[i + 1] == 'x' || source_[i + 1] == 'X')) {
                    i += 2;
                    col += 2;
                    const size_t hex_begin = i;
                    while (i < source_.size() && std::isxdigit(static_cast<unsigned char>(source_[i]))) {
                        ++i;
                        ++col;
                    }
                    if (i == hex_begin) throw DosError("invalid hexadecimal number", start_line, start_col);
                } else {
                    ++i;
                    ++col;
                    while (i < source_.size() && std::isdigit(static_cast<unsigned char>(source_[i]))) {
                        ++i;
                        ++col;
                    }
                    if (i < source_.size() && source_[i] == '.') {
                        floating = true;
                        ++i;
                        ++col;
                        while (i < source_.size() && std::isdigit(static_cast<unsigned char>(source_[i]))) {
                            ++i;
                            ++col;
                        }
                    }
                    if (i < source_.size() && (source_[i] == 'e' || source_[i] == 'E')) {
                        floating = true;
                        ++i;
                        ++col;
                        if (i < source_.size() && (source_[i] == '+' || source_[i] == '-')) {
                            ++i;
                            ++col;
                        }
                        const size_t exp_begin = i;
                        while (i < source_.size() && std::isdigit(static_cast<unsigned char>(source_[i]))) {
                            ++i;
                            ++col;
                        }
                        if (i == exp_begin) throw DosError("invalid exponent", start_line, start_col);
                    }
                }
                push(TokenKind::Number, source_.substr(begin, i - begin), start_line, start_col);
                (void)floating;
                continue;
            }

            if (c == '"') {
                std::string value;
                ++i;
                ++col;
                bool closed = false;
                while (i < source_.size()) {
                    const char q = source_[i];
                    if (q == '"') {
                        ++i;
                        ++col;
                        closed = true;
                        break;
                    }
                    if (q == '\n') throw DosError("newline inside string literal", line, col);
                    if (q == '\\') {
                        ++i;
                        ++col;
                        if (i >= source_.size()) throw DosError("unterminated escape", start_line, start_col);
                        const char e = source_[i++];
                        ++col;
                        switch (e) {
                            case 'n': value += '\n'; break;
                            case 'r': value += '\r'; break;
                            case 't': value += '\t'; break;
                            case '0': value += '\0'; break;
                            case '\\': value += '\\'; break;
                            case '"': value += '"'; break;
                            default: throw DosError(std::string("unknown escape sequence: \\") + e, line, col);
                        }
                    } else {
                        value += q;
                        ++i;
                        ++col;
                    }
                }
                if (!closed) throw DosError("unterminated string literal", start_line, start_col);
                push(TokenKind::String, std::move(value), start_line, start_col);
                continue;
            }

            auto two = [&](char a, char b, TokenKind kind) -> bool {
                if (i + 1 < source_.size() && source_[i] == a && source_[i + 1] == b) {
                    push(kind, std::string() + a + b, start_line, start_col);
                    i += 2;
                    col += 2;
                    return true;
                }
                return false;
            };

            if (two('+', '+', TokenKind::PlusPlus) ||
                two('-', '-', TokenKind::MinusMinus) ||
                two('+', '=', TokenKind::PlusEqual) ||
                two('-', '=', TokenKind::MinusEqual) ||
                two('*', '=', TokenKind::StarEqual) ||
                two('/', '=', TokenKind::SlashEqual) ||
                two('%', '=', TokenKind::PercentEqual) ||
                two('=', '=', TokenKind::EqualEqual) ||
                two('!', '=', TokenKind::BangEqual) ||
                two('<', '=', TokenKind::LessEqual) ||
                two('>', '=', TokenKind::GreaterEqual) ||
                two('<', '<', TokenKind::ShiftLeft) ||
                two('>', '>', TokenKind::ShiftRight) ||
                two('&', '&', TokenKind::AndAnd) ||
                two('|', '|', TokenKind::OrOr)) {
                continue;
            }

            TokenKind kind;
            switch (c) {
                case '(': kind = TokenKind::LParen; break;
                case ')': kind = TokenKind::RParen; break;
                case '{': kind = TokenKind::LBrace; break;
                case '}': kind = TokenKind::RBrace; break;
                case '[': kind = TokenKind::LBracket; break;
                case ']': kind = TokenKind::RBracket; break;
                case ',': kind = TokenKind::Comma; break;
                case '.': kind = TokenKind::Dot; break;
                case ':': kind = TokenKind::Colon; break;
                case ';': kind = TokenKind::Semicolon; break;
                case '+': kind = TokenKind::Plus; break;
                case '-': kind = TokenKind::Minus; break;
                case '*': kind = TokenKind::Star; break;
                case '/': kind = TokenKind::Slash; break;
                case '%': kind = TokenKind::Percent; break;
                case '&': kind = TokenKind::Amp; break;
                case '|': kind = TokenKind::Pipe; break;
                case '^': kind = TokenKind::Caret; break;
                case '!': kind = TokenKind::Bang; break;
                case '=': kind = TokenKind::Equal; break;
                case '<': kind = TokenKind::Less; break;
                case '>': kind = TokenKind::Greater; break;
                case '?': kind = TokenKind::Question; break;
                case '~': kind = TokenKind::Tilde; break;
                default: throw DosError(std::string("unexpected character '") + c + "'", line, col);
            }
            push(kind, std::string(1, c), start_line, start_col);
            ++i;
            ++col;
        }

        // A final EOL simplifies line-oriented command parsing.
        if (tokens.empty() || tokens.back().kind != TokenKind::Eol) {
            push(TokenKind::Eol, {}, line, col);
        }
        push(TokenKind::End, {}, line, col);
        return tokens;
    }

private:
    std::string source_;
};

struct ReturnSignal { Value value; };
struct BreakSignal {};
struct ContinueSignal {};
struct ThrownSignal { Value value; };

struct Function {
    std::vector<std::pair<std::string, std::string>> params;
    size_t body_begin = 0;
    size_t body_end = 0;
};

struct StructDef {
    std::vector<std::pair<std::string, std::string>> fields;
};

class Shell;

struct Env {
    std::shared_ptr<Env> parent;
    std::unordered_map<std::string, ValuePtr> vars;
    std::unordered_map<std::string, Function> functions;
    std::unordered_map<std::string, bool> constants;

    explicit Env(std::shared_ptr<Env> parent_ = {}) : parent(std::move(parent_)) {}

    ValuePtr find_cell(const std::string& name) {
        if (auto it = vars.find(name); it != vars.end()) return it->second;
        return parent ? parent->find_cell(name) : nullptr;
    }

    const Env* find_owner(const std::string& name) const {
        if (vars.contains(name)) return this;
        return parent ? parent->find_owner(name) : nullptr;
    }

    ValuePtr define(const std::string& name, Value value = {}, bool constant = false) {
        auto cell = std::make_shared<Value>(std::move(value));
        vars[name] = cell;
        constants[name] = constant;
        return cell;
    }

    bool is_constant(const std::string& name) const {
        if (auto it = constants.find(name); it != constants.end()) return it->second;
        return parent ? parent->is_constant(name) : false;
    }

    Value& require(const std::string& name) {
        auto cell = find_cell(name);
        if (!cell) throw DosError("undefined variable: " + name);
        return *cell;
    }
};

class Interpreter {
public:
    Interpreter(Shell& shell, std::vector<Token> tokens)
        : shell_(shell), tokens_(std::move(tokens)), env_(std::make_shared<Env>()) {
        index_declarations();
    }

    int run();

private:
    Shell& shell_;
    std::vector<Token> tokens_;
    size_t pos_ = 0;
    bool evaluating_ = true;
    std::shared_ptr<Env> env_;
    std::unordered_map<std::string, StructDef> structs_;

    bool is(TokenKind k) const { return tokens_[pos_].kind == k; }
    bool is_id(const std::string& s) const {
        return is(TokenKind::Identifier) && tokens_[pos_].text == s;
    }
    Token take() { return tokens_[pos_++]; }
    bool accept(TokenKind k) {
        if (is(k)) {
            ++pos_;
            return true;
        }
        return false;
    }

    [[noreturn]] void error(const std::string& msg) const {
        throw DosError(msg, tokens_[pos_].line, tokens_[pos_].col);
    }

    void expect(TokenKind k, const std::string& msg);
    void skip_separators();
    bool at_statement_end() const {
        return is(TokenKind::Eol) || is(TokenKind::Semicolon) || is(TokenKind::RBrace) || is(TokenKind::End);
    }

    Value expression();
    Value assignment();
    Value conditional();
    Value logical_or();
    Value logical_and();
    Value bit_or();
    Value shift();
    Value bit_xor();
    Value bit_and();
    Value equality();
    Value comparison();
    Value term();
    Value factor();
    Value unary();
    Value postfix();
    Value primary();

    void statement();
    void block();
    void parse_function(std::optional<std::string> return_type, const std::string& name);
    void parse_struct();
    Value make_default_value(const std::string& type) const;
    void execute_range(size_t begin, size_t end);
    size_t consume_block_end();
    void index_declarations();

    std::string parse_type();
    bool is_type_name(const std::string& name) const;
    bool is_declaration_start() const;
    Value call_builtin(const std::string& name, std::vector<Value> args);
    Value call_function(const std::string& name, std::vector<Value> args);
    void execute_command();
    void assign_member(const std::string& object_name, const std::string& member, Value value);
    void assign_name(const std::string& name, Value value);
    ValuePtr require_cell(const std::string& name);
    std::string interpolate(std::string text);
};

class Shell {
public:
    explicit Shell(fs::path executable = {});

    bool running = true;
    int exit_code = 0;

    int run_command(const std::string& raw);
    int execute_file(const fs::path& file);
    void print_prompt() const;
    void help() const;

    std::string cwd() const {
        std::error_code ec;
        const auto p = fs::current_path(ec);
        return ec ? std::string("?") : p.string();
    }

private:
    friend class Interpreter;
    int external(const std::string& line);
    int run_python_plugin(const std::vector<std::string>& args);
    int plugin_command(const std::vector<std::string>& args);
    static std::vector<std::string> split_words(const std::string& line);
    static std::string expand_env(std::string text);
    fs::path executable_path_;
    fs::path plugin_root_;
};

void Interpreter::index_declarations() {
    const size_t saved = pos_;

    // Pass 1: collect all top-level struct names so forward references work.
    pos_ = 0;
    int depth = 0;
    while (!is(TokenKind::End)) {
        if (depth == 0 && is_id("struct")) {
            if (pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].kind == TokenKind::Identifier) {
                structs_.try_emplace(tokens_[pos_ + 1].text);
            }
        }
        if (is(TokenKind::LBrace)) ++depth;
        else if (is(TokenKind::RBrace) && depth > 0) --depth;
        ++pos_;
    }

    // Pass 2: parse top-level declarations and record their executable ranges.
    pos_ = 0;
    depth = 0;
    while (!is(TokenKind::End)) {
        if (depth == 0 && is_id("struct")) {
            parse_struct();
            continue;
        }
        if (depth == 0 && is_id("function")) {
            take();
            std::optional<std::string> return_type;
            if (is(TokenKind::Identifier) && is_type_name(tokens_[pos_].text))
                return_type = parse_type();
            if (!is(TokenKind::Identifier)) error("expected function name");
            const std::string name = take().text;
            parse_function(std::move(return_type), name);
            continue;
        }
        if (depth == 0 && is(TokenKind::Identifier) && is_type_name(tokens_[pos_].text)) {
            const size_t probe = pos_;
            const std::string return_type = parse_type();
            if (is(TokenKind::Identifier)) {
                const std::string name = take().text;
                if (is(TokenKind::LParen)) {
                    parse_function(return_type, name);
                    continue;
                }
            }
            pos_ = probe;
        }
        if (is(TokenKind::LBrace)) ++depth;
        else if (is(TokenKind::RBrace) && depth > 0) --depth;
        ++pos_;
    }
    pos_ = saved;
}

void Interpreter::expect(TokenKind k, const std::string& msg) {
    if (!accept(k)) error(msg);
}

void Interpreter::skip_separators() {
    while (is(TokenKind::Eol) || is(TokenKind::Semicolon)) ++pos_;
}

bool Interpreter::is_type_name(const std::string& name) const {
    static constexpr const char* builtins[] = {
        "void", "bool", "char", "byte", "string",
        "int", "i8", "i16", "i32", "i64",
        "u8", "u16", "u32", "u64", "isize", "usize",
        "float", "f32", "f64", "double", "var", "ptr"
    };
    for (const char* t : builtins) if (name == t) return true;
    return structs_.contains(name);
}

std::string Interpreter::parse_type() {
    if (!is(TokenKind::Identifier)) return {};
    std::string type = take().text;
    if (type == "ptr" && accept(TokenKind::Less)) {
        const std::string pointee = parse_type();
        if (pointee.empty()) error("expected pointer type");
        expect(TokenKind::Greater, "expected > after pointer type");
        type += "<" + pointee + ">";
        return type;
    }
    while (accept(TokenKind::Star)) type += '*';
    if (accept(TokenKind::LBracket)) {
        expect(TokenKind::RBracket, "expected ] after type array marker");
        type += "[]";
    }
    return type;
}

bool Interpreter::is_declaration_start() const {
    if (!is(TokenKind::Identifier)) return false;
    const std::string& name = tokens_[pos_].text;
    if (name == "const" || name == "var") return true;
    return is_type_name(name);
}

Value Interpreter::primary() {
    if (accept(TokenKind::LParen)) {
        Value v = expression();
        expect(TokenKind::RParen, "expected )");
        return v;
    }

    if (is(TokenKind::Number)) {
        const std::string text = take().text;
        try {
            if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
                return Value(static_cast<int64_t>(std::stoll(text, nullptr, 16)));
            }
            if (text.find_first_of(".eE") != std::string::npos) return Value(std::stod(text));
            return Value(static_cast<int64_t>(std::stoll(text)));
        } catch (...) {
            error("invalid numeric literal");
        }
    }

    if (is(TokenKind::String)) return Value(take().text);
    if (is_id("true")) { take(); return Value(true); }
    if (is_id("false")) { take(); return Value(false); }
    if (is_id("null")) { take(); return Value(); }

    if (accept(TokenKind::LBracket)) {
        Array values;
        skip_separators();
        while (!is(TokenKind::RBracket)) {
            values.push_back(expression());
            if (!accept(TokenKind::Comma)) break;
            skip_separators();
        }
        expect(TokenKind::RBracket, "expected ]");
        return Value(std::move(values));
    }

    if (is(TokenKind::Identifier)) {
        const std::string first = take().text;
        const size_t qualified_begin = pos_;
        std::string qualified = first;
        bool has_qualifier = false;
        while (is(TokenKind::Dot) && pos_ + 1 < tokens_.size() &&
               tokens_[pos_ + 1].kind == TokenKind::Identifier) {
            take();
            qualified += "." + take().text;
            has_qualifier = true;
        }
        if (accept(TokenKind::LParen)) {
            std::vector<Value> args;
            if (!is(TokenKind::RParen)) {
                do {
                    args.push_back(expression());
                } while (accept(TokenKind::Comma));
            }
            expect(TokenKind::RParen, "expected )");
            return call_function(has_qualifier ? qualified : first, std::move(args));
        }
        pos_ = qualified_begin;
        auto cell = env_->find_cell(first);
        if (!cell) error("undefined variable: " + first);
        return *cell;
    }

    error("expected expression");
}

Value Interpreter::postfix() {
    if (is(TokenKind::Identifier) && pos_ + 1 < tokens_.size() &&
        (tokens_[pos_ + 1].kind == TokenKind::PlusPlus || tokens_[pos_ + 1].kind == TokenKind::MinusMinus)) {
        const std::string name = take().text;
        const TokenKind op = take().kind;
        auto cell = require_cell(name);
        Value old = *cell;
        if (evaluating_) {
            *cell = Value(op == TokenKind::PlusPlus ? cell->as_int() + 1 : cell->as_int() - 1);
        }
        return old;
    }
    Value value = primary();
    while (true) {
        if (accept(TokenKind::Dot)) {
            if (!is(TokenKind::Identifier)) error("expected member name");
            const std::string member = take().text;
            if (!value.is_object()) error("member access on non-object");
            auto& object = std::get<Object>(value.data);
            auto it = object.find(member);
            if (it == object.end()) error("unknown member: " + member);
            Value member_value = it->second;
            value = std::move(member_value);
            continue;
        }

        if (accept(TokenKind::LBracket)) {
            Value index = expression();
            expect(TokenKind::RBracket, "expected ]");
            if (!value.is_array()) error("indexing non-array");
            const int64_t i = index.as_int();
            auto& array = std::get<Array>(value.data);
            if (i < 0 || static_cast<size_t>(i) >= array.size()) error("array index out of range");
            Value element = array[static_cast<size_t>(i)];
            value = std::move(element);
            continue;
        }
        break;
    }
    return value;
}

Value Interpreter::unary() {
    if (accept(TokenKind::PlusPlus)) {
        if (!is(TokenKind::Identifier)) error("++ requires a variable");
        const std::string name = take().text;
        auto cell = require_cell(name);
        if (evaluating_) *cell = Value(cell->as_int() + 1);
        return *cell;
    }
    if (accept(TokenKind::MinusMinus)) {
        if (!is(TokenKind::Identifier)) error("-- requires a variable");
        const std::string name = take().text;
        auto cell = require_cell(name);
        if (evaluating_) *cell = Value(cell->as_int() - 1);
        return *cell;
    }
    if (accept(TokenKind::Tilde)) return Value(~unary().as_int());
    if (accept(TokenKind::Bang)) return Value(!unary().as_bool());
    if (accept(TokenKind::Minus)) {
        Value v = unary();
        return v.is_double() ? Value(-v.as_double()) : Value(-v.as_int());
    }
    if (accept(TokenKind::Plus)) return unary();
    if (accept(TokenKind::Amp)) {
        if (!is(TokenKind::Identifier)) error("& requires a variable");
        const std::string name = take().text;
        auto cell = env_->find_cell(name);
        if (!cell) error("undefined variable: " + name);
        return Value(PointerValue{cell});
    }
    if (accept(TokenKind::Star)) {
        Value p = unary();
        if (!p.is_pointer()) error("* requires a pointer");
        auto cell = std::get<PointerValue>(p.data).target;
        if (!cell) error("null pointer dereference");
        return *cell;
    }
    return postfix();
}

Value Interpreter::factor() {
    Value left = unary();
    while (is(TokenKind::Star) || is(TokenKind::Slash) || is(TokenKind::Percent)) {
        const TokenKind op = take().kind;
        Value right = unary();
        if (op == TokenKind::Star) {
            left = (left.is_double() || right.is_double())
                ? Value(left.as_double() * right.as_double())
                : Value(left.as_int() * right.as_int());
        } else if (op == TokenKind::Slash) {
            if ((right.is_double() ? right.as_double() : static_cast<double>(right.as_int())) == 0.0)
                error("division by zero");
            left = (left.is_double() || right.is_double())
                ? Value(left.as_double() / right.as_double())
                : Value(left.as_int() / right.as_int());
        } else {
            const int64_t divisor = right.as_int();
            if (divisor == 0) error("division by zero");
            left = Value(left.as_int() % divisor);
        }
    }
    return left;
}

Value Interpreter::term() {
    Value left = factor();
    while (is(TokenKind::Plus) || is(TokenKind::Minus)) {
        const TokenKind op = take().kind;
        Value right = factor();
        if (op == TokenKind::Plus && (left.is_string() || right.is_string())) {
            left = Value(left.str() + right.str());
        } else if (op == TokenKind::Plus) {
            left = (left.is_double() || right.is_double())
                ? Value(left.as_double() + right.as_double())
                : Value(left.as_int() + right.as_int());
        } else {
            left = (left.is_double() || right.is_double())
                ? Value(left.as_double() - right.as_double())
                : Value(left.as_int() - right.as_int());
        }
    }
    return left;
}

Value Interpreter::shift() {
    Value left = term();
    while (is(TokenKind::ShiftLeft) || is(TokenKind::ShiftRight)) {
        const TokenKind op = take().kind;
        const int64_t amount = term().as_int();
        if (amount < 0 || amount >= 64) error("invalid shift count");
        const uint64_t bits = static_cast<uint64_t>(left.as_int());
        left = Value(op == TokenKind::ShiftLeft
            ? static_cast<int64_t>(bits << amount)
            : static_cast<int64_t>(bits >> amount));
    }
    return left;
}

Value Interpreter::comparison() {
    Value left = shift();
    while (is(TokenKind::Less) || is(TokenKind::LessEqual) ||
           is(TokenKind::Greater) || is(TokenKind::GreaterEqual)) {
        const TokenKind op = take().kind;
        Value right = shift();
        if (left.is_string() || right.is_string()) {
            const std::string a = left.str(), b = right.str();
            if (op == TokenKind::Less) left = Value(a < b);
            else if (op == TokenKind::LessEqual) left = Value(a <= b);
            else if (op == TokenKind::Greater) left = Value(a > b);
            else left = Value(a >= b);
        } else {
            const double a = left.as_double(), b = right.as_double();
            if (op == TokenKind::Less) left = Value(a < b);
            else if (op == TokenKind::LessEqual) left = Value(a <= b);
            else if (op == TokenKind::Greater) left = Value(a > b);
            else left = Value(a >= b);
        }
    }
    return left;
}

Value Interpreter::equality() {
    Value left = comparison();
    while (is(TokenKind::EqualEqual) || is(TokenKind::BangEqual)) {
        const TokenKind op = take().kind;
        Value right = comparison();
        bool equal = false;
        if (left.is_number() && right.is_number()) equal = left.as_double() == right.as_double();
        else if (left.is_bool() && right.is_bool()) equal = left.as_bool() == right.as_bool();
        else if (left.is_null() && right.is_null()) equal = true;
        else equal = left.str() == right.str();
        left = Value(op == TokenKind::EqualEqual ? equal : !equal);
    }
    return left;
}

Value Interpreter::bit_and() {
    Value left = equality();
    while (accept(TokenKind::Amp)) left = Value(left.as_int() & equality().as_int());
    return left;
}

Value Interpreter::bit_xor() {
    Value left = bit_and();
    while (accept(TokenKind::Caret)) left = Value(left.as_int() ^ bit_and().as_int());
    return left;
}

Value Interpreter::bit_or() {
    Value left = bit_xor();
    while (accept(TokenKind::Pipe)) left = Value(left.as_int() | bit_xor().as_int());
    return left;
}

Value Interpreter::logical_and() {
    Value left = bit_or();
    while (accept(TokenKind::AndAnd)) {
        const bool first = left.as_bool();
        if (!first) {
            const bool old = evaluating_;
            evaluating_ = false;
            (void)bit_or();
            evaluating_ = old;
            left = Value(false);
        } else {
            left = Value(bit_or().as_bool());
        }
    }
    return left;
}

Value Interpreter::logical_or() {
    Value left = logical_and();
    while (accept(TokenKind::OrOr)) {
        const bool first = left.as_bool();
        if (first) {
            const bool old = evaluating_;
            evaluating_ = false;
            (void)logical_and();
            evaluating_ = old;
            left = Value(true);
        } else {
            left = Value(logical_and().as_bool());
        }
    }
    return left;
}

Value Interpreter::conditional() {
    Value condition = logical_or();
    if (!accept(TokenKind::Question)) return condition;
    const bool branch = condition.as_bool();
    const bool old = evaluating_;
    Value when_true;
    if (branch) {
        evaluating_ = old;
        when_true = expression();
    } else {
        evaluating_ = false;
        when_true = expression();
    }
    evaluating_ = old;
    expect(TokenKind::Colon, "expected : in conditional expression");
    Value when_false;
    if (branch) {
        evaluating_ = false;
        when_false = conditional();
    } else {
        evaluating_ = old;
        when_false = conditional();
    }
    evaluating_ = old;
    return branch ? when_true : when_false;
}

Value Interpreter::assignment() {
    return conditional();
}

Value Interpreter::expression() {
    return assignment();
}

ValuePtr Interpreter::require_cell(const std::string& name) {
    auto cell = env_->find_cell(name);
    if (!cell) error("undefined variable: " + name);
    if (env_->is_constant(name)) error("cannot modify constant: " + name);
    return cell;
}

void Interpreter::assign_name(const std::string& name, Value value) {
    auto cell = env_->find_cell(name);
    if (!cell) env_->define(name, std::move(value));
    else {
        if (env_->is_constant(name)) error("cannot modify constant: " + name);
        *cell = std::move(value);
    }
}

void Interpreter::assign_member(const std::string& object_name, const std::string& member, Value value) {
    auto cell = env_->find_cell(object_name);
    if (!cell || !cell->is_object()) error("not an object: " + object_name);
    auto& object = std::get<Object>(cell->data);
    auto it = object.find(member);
    if (it == object.end()) error("unknown member: " + member);
    it->second = std::move(value);
}

std::string Interpreter::interpolate(std::string text) {
    for (size_t i = 0; i < text.size();) {
        if (text[i] != '%') {
            ++i;
            continue;
        }
        const size_t end = text.find('%', i + 1);
        if (end == std::string::npos) break;
        const std::string key = text.substr(i + 1, end - i - 1);
        if (key.empty()) {
            i = end + 1;
            continue;
        }
        std::string replacement;
        if (auto cell = env_->find_cell(key)) replacement = cell->str();
        else if (const char* value = std::getenv(key.c_str())) replacement = value;
        text.replace(i, end - i + 1, replacement);
        i += replacement.size();
    }
    return text;
}

void Interpreter::parse_struct() {
    take(); // struct
    if (!is(TokenKind::Identifier)) error("expected struct name");
    const std::string name = take().text;
    skip_separators();
    expect(TokenKind::LBrace, "expected { after struct name");

    StructDef def;
    skip_separators();
    while (!is(TokenKind::RBrace)) {
        const std::string type = parse_type();
        if (type.empty()) error("expected field type");
        if (!is(TokenKind::Identifier)) error("expected field name");
        const std::string field = take().text;
        def.fields.emplace_back(type, field);
        skip_separators();
    }
    expect(TokenKind::RBrace, "expected } after struct");
    structs_[name] = std::move(def);
}

void Interpreter::parse_function(std::optional<std::string> return_type, const std::string& name) {
    (void)return_type;
    expect(TokenKind::LParen, "expected ( after function name");
    Function function;
    skip_separators();
    if (!is(TokenKind::RParen)) {
        do {
            const std::string type = parse_type();
            if (type.empty()) error("expected parameter type");
            if (!is(TokenKind::Identifier)) error("expected parameter name");
            const std::string param = take().text;
            function.params.emplace_back(type, param);
            skip_separators();
        } while (accept(TokenKind::Comma));
    }
    expect(TokenKind::RParen, "expected )");
    skip_separators();
    expect(TokenKind::LBrace, "expected { after function declaration");

    function.body_begin = pos_;
    const size_t body_end = consume_block_end();
    function.body_end = body_end;
    env_->functions[name] = function;
    pos_ = body_end + 1;
    skip_separators();
}

size_t Interpreter::consume_block_end() {
    int depth = 1;
    while (!is(TokenKind::End)) {
        if (is(TokenKind::LBrace)) ++depth;
        else if (is(TokenKind::RBrace)) {
            --depth;
            if (depth == 0) return pos_;
        }
        ++pos_;
    }
    error("missing }");
}

Value Interpreter::make_default_value(const std::string& type) const {
    if (type.find('*') != std::string::npos || type.rfind("ptr<", 0) == 0) return Value(PointerValue{nullptr});
    if (type == "bool") return Value(false);
    if (type == "string") return Value(std::string{});
    if (type == "char") return Value(int64_t(0));
    if (type == "float" || type == "f32" || type == "f64" || type == "double") return Value(0.0);
    if (type.ends_with("[]")) return Value(Array{});
    if (auto it = structs_.find(type); it != structs_.end()) {
        Object object;
        for (const auto& [field_type, field_name] : it->second.fields)
            object.emplace(field_name, make_default_value(field_type));
        return Value(std::move(object));
    }
    return Value(int64_t(0));
}

static std::vector<std::string> value_strings(const std::vector<Value>& args, size_t begin = 0) {
    std::vector<std::string> out;
    if (begin > args.size()) return out;
    out.reserve(args.size() - begin);
    for (size_t i = begin; i < args.size(); ++i) out.push_back(args[i].str());
    return out;
}

static std::string path_arg(const std::vector<Value>& args, size_t index = 0) {
    if (index >= args.size()) throw DosError("missing path argument");
    return args[index].str();
}

#ifdef _WIN32
static std::wstring widen(const std::string& text);

static std::wstring quote_windows_arg(const std::string& value) {
    const std::wstring input = widen(value);
    if (input.empty()) return L"\"\"";
    bool needs_quotes = false;
    for (wchar_t c : input) {
        if (std::iswspace(c) || c == L'\"') { needs_quotes = true; break; }
    }
    if (!needs_quotes) return input;

    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : input) {
        if (c == L'\\') {
            ++backslashes;
        } else if (c == L'\"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'\"');
            backslashes = 0;
        } else {
            out.append(backslashes, L'\\');
            backslashes = 0;
            out.push_back(c);
        }
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'\"');
    return out;
}

static int windows_run_process(const std::string& program, const std::vector<std::string>& args) {
    std::wstring command = quote_windows_arg(program);
    for (const auto& arg : args) command += L" " + quote_windows_arg(arg);

    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        throw DosError("failed to start process: " + std::to_string(GetLastError()));
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
}
#endif

static std::string find_python_command();

Value Interpreter::call_builtin(const std::string& name, std::vector<Value> args) {
    const std::string n = lower(name);
    if (!evaluating_) return Value();

    if (n == "print" || n == "println" || n == "echo" || n == "console.print" || n == "console.println") {
        std::cout << join(value_strings(args), " ");
        if (n == "println" || n == "echo" || n == "console.println") std::cout << '\n';
        return Value();
    }
    if (n == "read_line" || n == "console.read_line") {
        std::string value;
        std::getline(std::cin, value);
        return Value(std::move(value));
    }
    if (n == "console.clear") {
#ifdef _WIN32
        HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (output != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(output, &info)) {
            const DWORD cells = static_cast<DWORD>(info.dwSize.X) * static_cast<DWORD>(info.dwSize.Y);
            DWORD written = 0;
            const COORD home{0, 0};
            FillConsoleOutputCharacterW(output, L' ', cells, home, &written);
            FillConsoleOutputAttribute(output, info.wAttributes, cells, home, &written);
            SetConsoleCursorPosition(output, home);
        }
#else
        std::cout << "\033[2J\033[H";
#endif
        return Value();
    }
    if (n == "string") return args.empty() ? Value("") : Value(args.front().str());
    if (n == "int") return args.empty() ? Value(int64_t(0)) : Value(args.front().as_int());
    if (n == "float" || n == "double") return args.empty() ? Value(0.0) : Value(args.front().as_double());
    if (n == "bool") return args.empty() ? Value(false) : Value(args.front().as_bool());

    if (n == "exists" || n == "fs.exists") return Value(!args.empty() && fs::exists(path_arg(args)));
    if (n == "cwd" || n == "pwd" || n == "fs.cwd") return Value(shell_.cwd());
    if (n == "fs.chdir") {
        std::error_code ec;
        fs::current_path(path_arg(args), ec);
        if (ec) throw DosError("chdir: " + ec.message());
        return Value(true);
    }
    if (n == "len") {
        if (args.empty()) return Value(int64_t(0));
        if (args.front().is_string()) return Value(static_cast<int64_t>(std::get<std::string>(args.front().data).size()));
        if (args.front().is_array()) return Value(static_cast<int64_t>(std::get<Array>(args.front().data).size()));
        if (args.front().is_object()) return Value(static_cast<int64_t>(std::get<Object>(args.front().data).size()));
        return Value(int64_t(0));
    }
    if (n == "sleep" || n == "time.sleep") {
        const auto ms = args.empty() ? int64_t(0) : args.front().as_int();
        if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return Value();
    }

    if (n == "fs.read") {
        std::ifstream in(path_arg(args), std::ios::binary);
        if (!in) throw DosError("cannot open file: " + path_arg(args));
        return Value(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
    }
    if (n == "fs.write" || n == "fs.append") {
        if (args.size() < 2) throw DosError("fs.write requires path and content");
        const auto mode = std::ios::binary | (n == "fs.append" ? std::ios::app : std::ios::trunc);
        std::ofstream out(path_arg(args), mode);
        if (!out) throw DosError("cannot write file: " + path_arg(args));
        out << args[1].str();
        return Value(static_cast<bool>(out));
    }
    if (n == "fs.mkdir") {
        std::error_code ec;
        const bool created = fs::create_directories(path_arg(args), ec);
        if (ec) throw DosError("mkdir: " + ec.message());
        return Value(created);
    }
    if (n == "fs.rmdir") {
        std::error_code ec;
        const auto count = fs::remove_all(path_arg(args), ec);
        if (ec) throw DosError("rmdir: " + ec.message());
        return Value(static_cast<int64_t>(count));
    }
    if (n == "fs.copy") {
        if (args.size() < 2) throw DosError("fs.copy requires source and destination");
        std::error_code ec;
        fs::copy(path_arg(args, 0), path_arg(args, 1), fs::copy_options::overwrite_existing, ec);
        if (ec) throw DosError("copy: " + ec.message());
        return Value(true);
    }
    if (n == "fs.move") {
        if (args.size() < 2) throw DosError("fs.move requires source and destination");
        std::error_code ec;
        fs::rename(path_arg(args, 0), path_arg(args, 1), ec);
        if (ec) throw DosError("move: " + ec.message());
        return Value(true);
    }
    if (n == "fs.remove") {
        std::error_code ec;
        const bool removed = fs::remove(path_arg(args), ec);
        if (ec) throw DosError("remove: " + ec.message());
        return Value(removed);
    }
    if (n == "fs.list") {
        Array result;
        std::error_code ec;
        fs::directory_iterator it(fs::path(args.empty() ? "." : args.front().str()), ec), end;
        if (ec) throw DosError("fs.list: " + ec.message());
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            Object entry;
            entry["name"] = Value(it->path().filename().string());
            entry["path"] = Value(it->path().string());
            entry["directory"] = Value(it->is_directory(ec));
            entry["file"] = Value(!it->is_directory(ec));
            if (!it->is_directory(ec)) {
                std::error_code size_ec;
                const auto size = fs::file_size(it->path(), size_ec);
                entry["size"] = Value(static_cast<int64_t>(size_ec ? 0 : size));
            } else {
                entry["size"] = Value(int64_t(0));
            }
            result.emplace_back(std::move(entry));
        }
        return Value(std::move(result));
    }
    if (n == "fs.find") {
        const fs::path root = args.empty() ? fs::current_path() : fs::path(args.front().str());
        const std::string pattern = args.size() >= 2 ? args[1].str() : "*";
        const bool recursive = args.size() < 3 || args[2].as_bool();
        Array result;
        std::error_code ec;
        auto add = [&](const fs::directory_entry& entry) {
            const std::string leaf = entry.path().filename().string();
            if (pattern == "*" || pattern == leaf || (pattern.size() > 1 && pattern.front() == '*' &&
                leaf.size() >= pattern.size() - 1 && leaf.ends_with(pattern.substr(1)))) {
                result.emplace_back(entry.path().string());
            }
        };
        if (recursive) {
            for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) add(*it);
        } else {
            for (fs::directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec)) add(*it);
        }
        if (ec) throw DosError("fs.find: " + ec.message());
        return Value(std::move(result));
    }

    if (n == "env.get") {
        if (args.empty()) throw DosError("env.get requires a name");
        const char* value = std::getenv(args.front().str().c_str());
        return Value(value ? value : "");
    }
    if (n == "env.set") {
        if (args.size() < 2) throw DosError("env.set requires name and value");
#ifdef _WIN32
        const bool ok = _putenv_s(args[0].str().c_str(), args[1].str().c_str()) == 0;
#else
        const bool ok = ::setenv(args[0].str().c_str(), args[1].str().c_str(), 1) == 0;
#endif
        return Value(ok);
    }

    if (n == "math.abs") {
        if (args.empty()) return Value(int64_t(0));
        return args[0].is_double()
            ? Value(std::abs(args[0].as_double()))
            : Value(static_cast<int64_t>(std::llabs(static_cast<long long>(args[0].as_int()))));
    }
    if (n == "math.sqrt") {
        if (args.empty()) return Value(0.0);
        const double v = args[0].as_double();
        if (v < 0.0) throw DosError("math.sqrt domain error");
        return Value(std::sqrt(v));
    }
    if (n == "math.pow") {
        if (args.size() < 2) throw DosError("math.pow requires 2 arguments");
        return Value(std::pow(args[0].as_double(), args[1].as_double()));
    }
    if (n == "math.min" || n == "math.max") {
        if (args.size() < 2) throw DosError(n + " requires 2 arguments");
        const double a = args[0].as_double(), b = args[1].as_double();
        return Value(n == "math.min" ? std::min(a, b) : std::max(a, b));
    }
    if (n == "string.upper") {
        std::string value = args.empty() ? std::string{} : args[0].str();
        for (char& c : value) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return Value(std::move(value));
    }
    if (n == "string.lower") {
        std::string value = args.empty() ? std::string{} : args[0].str();
        for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return Value(std::move(value));
    }
    if (n == "string.contains") {
        if (args.size() < 2) return Value(false);
        return Value(args[0].str().find(args[1].str()) != std::string::npos);
    }
    if (n == "string.substr") {
        if (args.size() < 3) throw DosError("string.substr requires 3 arguments");
        const std::string value = args[0].str();
        const int64_t start_index = args[1].as_int();
        const int64_t length = args[2].as_int();
        if (start_index < 0 || length < 0 || static_cast<uint64_t>(start_index) > value.size())
            throw DosError("string.substr range error");
        return Value(value.substr(static_cast<size_t>(start_index), static_cast<size_t>(length)));
    }
    if (n == "memory.alloc") {
        return Value(PointerValue{std::make_shared<Value>()});
    }
    if (n == "memory.free") {
        if (args.empty() || !args[0].is_pointer()) return Value(false);
        auto target = std::get<PointerValue>(args[0].data).target;
        if (!target) return Value(false);
        return Value(true);
    }
    if (n == "assert") {
        if (args.empty() || !args[0].as_bool()) {
            throw DosError(args.size() >= 2 ? args[1].str() : "assertion failed");
        }
        return Value(true);
    }
    if (n == "exit_code") return Value(static_cast<int64_t>(shell_.exit_code));

    if (n == "process.id") {
#ifdef _WIN32
        return Value(static_cast<int64_t>(GetCurrentProcessId()));
#else
        return Value(static_cast<int64_t>(::getpid()));
#endif
    }
    if (n == "process.run") {
        if (args.empty()) throw DosError("process.run requires a program");
#ifdef _WIN32
        return Value(static_cast<int64_t>(windows_run_process(args.front().str(), value_strings(args, 1))));
#else
        std::string command = args.front().str();
        const auto rest = value_strings(args, 1);
        for (const auto& arg : rest) command += " \"" + arg + "\"";
        return Value(static_cast<int64_t>(std::system(command.c_str())));
#endif
    }
    if (n == "process.kill") {
        if (args.empty()) throw DosError("process.kill requires pid");
#ifdef _WIN32
        HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(args.front().as_int()));
        if (!process) return Value(false);
        const bool ok = TerminateProcess(process, 1) != FALSE;
        CloseHandle(process);
        return Value(ok);
#else
        return Value(false);
#endif
    }
    if (n == "network.ping") {
        if (args.empty()) throw DosError("network.ping requires host");
#ifdef _WIN32
        const std::string command = "ping -n 1 \"" + args.front().str() + "\" >nul";
        return Value(static_cast<int64_t>(std::system(command.c_str())) == 0);
#else
        return Value(static_cast<int64_t>(std::system(("ping -c 1 \"" + args.front().str() + "\" >/dev/null 2>&1").c_str())) == 0);
#endif
    }
    if (n == "python.run") {
        std::vector<std::string> python_args;
        python_args.reserve(args.size() + 1);
        python_args.emplace_back("--run");
        for (const auto& arg : args) python_args.push_back(arg.str());
        return Value(static_cast<int64_t>(shell_.run_python_plugin(python_args)));
    }
    if (n == "python.exec") {
        if (args.empty()) throw DosError("python.exec requires source code");
        return Value(static_cast<int64_t>(shell_.run_python_plugin({"--exec", args.front().str()})));
    }
    if (n == "python.version") {
        return Value(find_python_command());
    }
    if (n == "python.available") {
        return Value(!find_python_command().empty());
    }
    if (n == "system") {
        if (args.empty()) return Value(int64_t(0));
        return Value(static_cast<int64_t>(shell_.run_command(args.front().str())));
    }
    throw DosError("unknown function: " + name);
}

Value Interpreter::call_function(const std::string& name, std::vector<Value> args) {
    try {
        return call_builtin(name, args);
    } catch (const DosError& e) {
        if (std::string(e.what()).rfind("unknown function:", 0) != 0) throw;
    }

    for (auto current = env_; current; current = current->parent) {
        auto it = current->functions.find(name);
        if (it == current->functions.end()) continue;
        const Function& function = it->second;
        if (args.size() != function.params.size())
            throw DosError("argument count mismatch in " + name);

        auto call_env = std::make_shared<Env>(current);
        for (size_t i = 0; i < args.size(); ++i)
            call_env->define(function.params[i].second, std::move(args[i]));

        const auto old_env = env_;
        const size_t old_pos = pos_;
        env_ = std::move(call_env);
        try {
            execute_range(function.body_begin, function.body_end);
            env_ = old_env;
            pos_ = old_pos;
            return Value();
        } catch (const ReturnSignal& signal) {
            env_ = old_env;
            pos_ = old_pos;
            return signal.value;
        } catch (...) {
            env_ = old_env;
            pos_ = old_pos;
            throw;
        }
    }

    throw DosError("unknown function: " + name);
}

void Interpreter::block() {
    expect(TokenKind::LBrace, "expected {");
    skip_separators();
    while (!is(TokenKind::RBrace) && !is(TokenKind::End)) {
        statement();
        skip_separators();
    }
    expect(TokenKind::RBrace, "expected }");
}

void Interpreter::execute_range(size_t begin, size_t end) {
    const size_t saved = pos_;
    pos_ = begin;
    try {
        skip_separators();
        while (pos_ < end) {
            statement();
            skip_separators();
        }
    } catch (...) {
        pos_ = saved;
        throw;
    }
    pos_ = saved;
}

void Interpreter::statement() {
    if (is(TokenKind::Eol) || is(TokenKind::Semicolon)) {
        ++pos_;
        return;
    }

    if (is_id("struct")) {
        parse_struct();
        return;
    }

    if (is_id("import")) {
        take();
        if (!is(TokenKind::Identifier)) error("import requires a module name");
        take();
        while (accept(TokenKind::Dot)) {
            if (!is(TokenKind::Identifier)) error("expected module name after .");
            take();
        }
        return;
    }

    if (is_id("function")) {
        take();
        std::optional<std::string> return_type;
        if (is(TokenKind::Identifier) && is_type_name(tokens_[pos_].text)) {
            return_type = parse_type();
        }
        if (!is(TokenKind::Identifier)) error("expected function name");
        const std::string name = take().text;
        parse_function(std::move(return_type), name);
        return;
    }

    if (is(TokenKind::Identifier) && is_type_name(tokens_[pos_].text)) {
        const size_t saved = pos_;
        const std::string type = parse_type();
        if (!is(TokenKind::Identifier)) {
            pos_ = saved;
        } else {
            const std::string name = take().text;
            if (is(TokenKind::LParen)) {
                parse_function(type, name);
                return;
            }
            Value value = is(TokenKind::Equal) ? (take(), expression()) : make_default_value(type);
            env_->define(name, std::move(value));
            return;
        }
    }

    if (is_id("const")) {
        take();
        const std::string type = parse_type();
        if (type.empty()) error("const requires a type");
        if (!is(TokenKind::Identifier)) error("const requires a name");
        const std::string name = take().text;
        expect(TokenKind::Equal, "const requires =");
        env_->define(name, expression(), true);
        return;
    }

    if (is_id("if")) {
        take();
        const bool condition = expression().as_bool();
        skip_separators();
        expect(TokenKind::LBrace, "expected { after if condition");
        const size_t body_begin = pos_;
        const size_t close = consume_block_end();
        const size_t after_if = close + 1;
        if (condition) execute_range(body_begin, close);
        pos_ = after_if;
        skip_separators();

        bool consumed_else = false;
        if (is_id("else")) {
            consumed_else = true;
            take();
            skip_separators();
            if (is_id("if")) {
                // Rewind to the else-if token and let the normal if parser handle it.
                if (!condition) {
                    statement();
                } else {
                    // Skip the else-if branch without executing it.
                    take();
                    (void)expression();
                    skip_separators();
                    expect(TokenKind::LBrace, "expected { after else if condition");
                    const size_t nested_begin = pos_;
                    const size_t nested_close = consume_block_end();
                    pos_ = nested_close + 1;
                    skip_separators();
                    if (is_id("else")) {
                        take();
                        skip_separators();
                        expect(TokenKind::LBrace, "expected { after else");
                        const size_t nested_else_begin = pos_;
                        const size_t nested_else_close = consume_block_end();
                        pos_ = nested_else_close + 1;
                        (void)nested_begin;
                        (void)nested_else_begin;
                    }
                }
            } else {
                expect(TokenKind::LBrace, "expected { after else");
                const size_t else_begin = pos_;
                const size_t else_close = consume_block_end();
                if (!condition) execute_range(else_begin, else_close);
                pos_ = else_close + 1;
            }
        }
        (void)consumed_else;
        return;
    }

    if (is_id("throw")) {
        take();
        throw ThrownSignal{at_statement_end() ? Value() : expression()};
    }

    if (is_id("try")) {
        take();
        skip_separators();
        expect(TokenKind::LBrace, "expected { after try");
        const size_t try_begin = pos_;
        const size_t try_close = consume_block_end();
        pos_ = try_close + 1;
        skip_separators();
        if (!is_id("catch")) error("expected catch after try block");
        take();
        std::string catch_name;
        if (accept(TokenKind::LParen)) {
            if (!is(TokenKind::Identifier)) error("expected catch variable");
            catch_name = take().text;
            expect(TokenKind::RParen, "expected ) after catch variable");
        } else {
            if (!is(TokenKind::Identifier)) error("expected catch variable");
            catch_name = take().text;
        }
        skip_separators();
        expect(TokenKind::LBrace, "expected { after catch");
        const size_t catch_begin = pos_;
        const size_t catch_close = consume_block_end();
        pos_ = catch_close + 1;
        try {
            execute_range(try_begin, try_close);
        } catch (const ThrownSignal& thrown) {
            const auto old_env = env_;
            auto catch_env = std::make_shared<Env>(env_);
            catch_env->define(catch_name, thrown.value);
            env_ = std::move(catch_env);
            try {
                execute_range(catch_begin, catch_close);
            } catch (...) {
                env_ = old_env;
                throw;
            }
            env_ = old_env;
        } catch (const DosError& error) {
            const auto old_env = env_;
            auto catch_env = std::make_shared<Env>(env_);
            Object error_value;
            error_value["message"] = Value(error.what());
            error_value["line"] = Value(static_cast<int64_t>(error.line));
            error_value["col"] = Value(static_cast<int64_t>(error.col));
            catch_env->define(catch_name, Value(std::move(error_value)));
            env_ = std::move(catch_env);
            try {
                execute_range(catch_begin, catch_close);
            } catch (...) {
                env_ = old_env;
                throw;
            }
            env_ = old_env;
        }
        return;
    }

    if (is_id("switch")) {
        take();
        const Value control = expression();
        skip_separators();
        expect(TokenKind::LBrace, "expected { after switch expression");
        const size_t body_begin = pos_;
        const size_t close = consume_block_end();
        struct CaseRange { bool is_default; bool match; size_t begin; size_t label; };
        std::vector<CaseRange> ranges;
        size_t scan = body_begin;
        int depth = 0;
        std::optional<size_t> active_begin;
        std::optional<size_t> active_label;
        bool active_match = false;
        while (scan < close) {
            if (tokens_[scan].kind == TokenKind::LBrace) { ++depth; ++scan; continue; }
            if (tokens_[scan].kind == TokenKind::RBrace) { --depth; ++scan; continue; }
            if (depth == 0 && tokens_[scan].kind == TokenKind::Identifier && tokens_[scan].text == "case") {
                if (active_begin && active_label) ranges.push_back({false, active_match, *active_begin, scan});
                pos_ = scan + 1;
                Value case_value = expression();
                expect(TokenKind::Colon, "expected : after case value");
                active_match = (control.str() == case_value.str());
                active_begin = pos_;
                active_label = scan;
                scan = pos_;
                continue;
            }
            if (depth == 0 && tokens_[scan].kind == TokenKind::Identifier && tokens_[scan].text == "default") {
                if (active_begin && active_label) ranges.push_back({false, active_match, *active_begin, scan});
                pos_ = scan + 1;
                expect(TokenKind::Colon, "expected : after default");
                active_match = true;
                active_begin = pos_;
                active_label = scan;
                scan = pos_;
                continue;
            }
            ++scan;
        }
        if (active_begin && active_label) ranges.push_back({false, active_match, *active_begin, close});
        bool executed = false;
        try {
            for (const auto& range : ranges) {
                if (!range.match || executed) continue;
                execute_range(range.begin, range.label);
                executed = true;
            }
        } catch (const BreakSignal&) {
            executed = true;
        }
        pos_ = close + 1;
        return;
    }

    if (is_id("while")) {
        take();
        const size_t condition_pos = pos_;
        (void)expression();
        skip_separators();
        expect(TokenKind::LBrace, "expected { after while condition");
        const size_t body_begin = pos_;
        const size_t body_close = consume_block_end();
        const size_t after = body_close + 1;
        for (;;) {
            pos_ = condition_pos;
            if (!expression().as_bool()) break;
            try {
                execute_range(body_begin, body_close);
            } catch (const BreakSignal&) {
                break;
            } catch (const ContinueSignal&) {
            }
        }
        pos_ = after;
        return;
    }

    if (is_id("for")) {
        take();

        // Typed C-style loop: for int i = 0; i < 10; i++ { ... }
        if (!is(TokenKind::Identifier) || !is_type_name(tokens_[pos_].text))
            error("for requires a typed initializer in DOS 1.0");
        const std::string type = parse_type();
        if (!is(TokenKind::Identifier)) error("expected loop variable");
        const std::string name = take().text;
        expect(TokenKind::Equal, "expected = in for initializer");
        Value initial = expression();
        env_->define(name, std::move(initial));
        expect(TokenKind::Semicolon, "expected ; after for initializer");

        const size_t condition_pos = pos_;
        (void)expression();
        expect(TokenKind::Semicolon, "expected ; after for condition");
        const size_t increment_pos = pos_;
        while (!is(TokenKind::LBrace) && !is(TokenKind::End)) ++pos_;
        expect(TokenKind::LBrace, "expected { after for increment");
        const size_t body_begin = pos_;
        const size_t body_close = consume_block_end();
        const size_t after = body_close + 1;

        for (;;) {
            pos_ = condition_pos;
            if (!expression().as_bool()) break;
            try {
                execute_range(body_begin, body_close);
            } catch (const BreakSignal&) {
                break;
            } catch (const ContinueSignal&) {
            }

            pos_ = increment_pos;
            if (is(TokenKind::Identifier)) {
                const std::string variable = take().text;
                auto loop_cell = require_cell(variable);
                Value& cell = *loop_cell;
                if (accept(TokenKind::PlusPlus)) cell = Value(cell.as_int() + 1);
                else if (accept(TokenKind::MinusMinus)) cell = Value(cell.as_int() - 1);
                else if (accept(TokenKind::Equal)) cell = expression();
                else error("unsupported for increment");
            } else {
                error("invalid for increment");
            }
        }
        pos_ = after;
        (void)type;
        return;
    }

    if (is_id("break")) {
        take();
        throw BreakSignal{};
    }
    if (is_id("continue")) {
        take();
        throw ContinueSignal{};
    }
    if (is_id("return")) {
        take();
        throw ReturnSignal{at_statement_end() ? Value() : expression()};
    }

    // Pointer assignment: *ptr = expression
    if (is(TokenKind::Star)) {
        take();
        if (!is(TokenKind::Identifier)) error("expected pointer variable");
        const std::string pointer_name = take().text;
        auto pointer_cell = require_cell(pointer_name);
        if (!pointer_cell->is_pointer()) error("not a pointer: " + pointer_name);
        auto target = std::get<PointerValue>(pointer_cell->data).target;
        if (!target) error("null pointer");
        expect(TokenKind::Equal, "expected = after pointer");
        *target = expression();
        return;
    }

    if (is(TokenKind::Identifier)) {
        const size_t saved = pos_;
        const std::string name = take().text;

        // Qualified builtin/API call such as fs.write(...) or process.run(...).
        if (is(TokenKind::Dot)) {
            size_t probe = pos_;
            while (probe + 1 < tokens_.size() && tokens_[probe].kind == TokenKind::Dot &&
                   tokens_[probe + 1].kind == TokenKind::Identifier) {
                probe += 2;
            }
            if (probe < tokens_.size() && tokens_[probe].kind == TokenKind::LParen) {
                pos_ = saved;
                (void)expression();
                return;
            }
        }

        if (is(TokenKind::LBracket)) {
            take();
            const Value index = expression();
            expect(TokenKind::RBracket, "expected ] after index");
            const int64_t idx = index.as_int();
            auto cell = require_cell(name);
            if (!cell->is_array()) error("not an array: " + name);
            auto& array = std::get<Array>(cell->data);
            if (idx < 0 || static_cast<size_t>(idx) >= array.size()) error("array index out of range");
            if (is(TokenKind::Equal) || is(TokenKind::PlusEqual) || is(TokenKind::MinusEqual) ||
                is(TokenKind::StarEqual) || is(TokenKind::SlashEqual) || is(TokenKind::PercentEqual)) {
                const TokenKind op = take().kind;
                Value rhs = expression();
                Value& target = array[static_cast<size_t>(idx)];
                if (op == TokenKind::Equal) target = std::move(rhs);
                else if (op == TokenKind::PlusEqual) target = (target.is_string() || rhs.is_string())
                    ? Value(target.str() + rhs.str())
                    : ((target.is_double() || rhs.is_double())
                        ? Value(target.as_double() + rhs.as_double())
                        : Value(target.as_int() + rhs.as_int()));
                else if (op == TokenKind::MinusEqual) target = (target.is_double() || rhs.is_double())
                    ? Value(target.as_double() - rhs.as_double()) : Value(target.as_int() - rhs.as_int());
                else if (op == TokenKind::StarEqual) target = (target.is_double() || rhs.is_double())
                    ? Value(target.as_double() * rhs.as_double()) : Value(target.as_int() * rhs.as_int());
                else if (op == TokenKind::SlashEqual) {
                    if (rhs.as_double() == 0.0) error("division by zero");
                    target = (target.is_double() || rhs.is_double())
                        ? Value(target.as_double() / rhs.as_double()) : Value(target.as_int() / rhs.as_int());
                } else {
                    const int64_t r = rhs.as_int();
                    if (r == 0) error("division by zero");
                    target = Value(target.as_int() % r);
                }
                return;
            }
            // Restore to use the normal expression machinery if this is not an assignment.
            pos_ = saved;
        }

        if (is(TokenKind::Dot)) {
            take();
            if (!is(TokenKind::Identifier)) error("expected member name");
            const std::string member = take().text;
            if (is(TokenKind::Equal)) {
                take();
                assign_member(name, member, expression());
                return;
            }
            error("only direct member assignment is supported as a statement");
        }

        if (is(TokenKind::Equal) || is(TokenKind::PlusEqual) || is(TokenKind::MinusEqual) ||
            is(TokenKind::StarEqual) || is(TokenKind::SlashEqual) || is(TokenKind::PercentEqual)) {
            const TokenKind op = take().kind;
            Value rhs = expression();
            auto cell = require_cell(name);
            if (op == TokenKind::Equal) *cell = std::move(rhs);
            else if (op == TokenKind::PlusEqual) {
                if (cell->is_string() || rhs.is_string()) {
                    *cell = Value(cell->str() + rhs.str());
                } else if (cell->is_int() && rhs.is_int()) {
                    *cell = Value(cell->as_int() + rhs.as_int());
                } else {
                    *cell = Value(cell->as_double() + rhs.as_double());
                }
            }
            else if (op == TokenKind::MinusEqual) *cell = Value(cell->as_double() - rhs.as_double());
            else if (op == TokenKind::StarEqual) *cell = Value(cell->as_double() * rhs.as_double());
            else if (op == TokenKind::SlashEqual) {
                if (rhs.as_double() == 0.0) error("division by zero");
                *cell = Value(cell->as_double() / rhs.as_double());
            } else {
                const int64_t r = rhs.as_int();
                if (r == 0) error("division by zero");
                *cell = Value(cell->as_int() % r);
            }
            return;
        }

        if (accept(TokenKind::PlusPlus)) {
            auto cell = require_cell(name);
            *cell = Value(cell->as_int() + 1);
            return;
        }
        if (accept(TokenKind::MinusMinus)) {
            auto cell = require_cell(name);
            *cell = Value(cell->as_int() - 1);
            return;
        }

        // Function call expression used as a statement.
        pos_ = saved;
        if (is(TokenKind::Identifier) && pos_ + 1 < tokens_.size() && tokens_[pos_ + 1].kind == TokenKind::LParen) {
            (void)expression();
            return;
        }

        pos_ = saved;
        execute_command();
        return;
    }

    error("unknown statement");
}

void Interpreter::execute_command() {
    std::vector<std::string> words;
    while (!at_statement_end()) {
        Token token = take();
        if (token.kind == TokenKind::String) {
            std::string quoted = "\"" + token.text;
            quoted += '"';
            words.push_back(std::move(quoted));
        } else {
            words.push_back(token.text);
        }
    }
    if (!words.empty()) {
        const int code = shell_.run_command(interpolate(join(words, " ")));
        if (code != 0) shell_.exit_code = code;
    }
}

int Interpreter::run() {
    try {
        skip_separators();
        while (!is(TokenKind::End)) {
            statement();
            skip_separators();
        }
        return shell_.exit_code;
    } catch (const ReturnSignal& signal) {
        return static_cast<int>(signal.value.as_int());
    } catch (const DosError& error) {
        if (error.line) {
            std::cerr << "DOS error at " << error.line << ':' << error.col << ": " << error.what() << '\n';
        } else {
            std::cerr << "DOS error: " << error.what() << '\n';
        }
        return 1;
    }
}

#ifdef _WIN32
static std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), count);
    return result;
}
#endif

Shell::Shell(fs::path executable) {
    std::error_code ec;
    if (executable.empty()) executable = "dos.exe";
    executable_path_ = fs::absolute(executable, ec);
    if (ec) executable_path_ = executable;
    plugin_root_ = executable_path_.parent_path() / "plugins";
}

static std::string quote_process_arg(const std::string& value) {
    if (value.empty()) return "\"\"";
    bool needs_quotes = false;
    for (unsigned char c : value) {
        if (std::isspace(c) || c == '"') { needs_quotes = true; break; }
    }
    if (!needs_quotes) return value;
    std::string out = "\"";
    size_t backslashes = 0;
    for (char c : value) {
        if (c == '\\') {
            ++backslashes;
        } else if (c == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
            backslashes = 0;
        } else {
            out.append(backslashes, '\\');
            backslashes = 0;
            out.push_back(c);
        }
    }
    out.append(backslashes * 2, '\\');
    out.push_back('"');
    return out;
}

static bool executable_command_works(const std::string& command) {
#ifdef _WIN32
    std::string line = command + " --version >nul 2>&1";
#else
    std::string line = command + " --version >/dev/null 2>&1";
#endif
    return std::system(line.c_str()) == 0;
}

static std::string find_python_command() {
    if (const char* custom = std::getenv("DOS_PYTHON")) {
        if (*custom) {
            const std::string raw(custom);
            const std::string candidate = quote_process_arg(raw);
            if (executable_command_works(candidate)) return candidate;
            if (executable_command_works(raw)) return raw;
        }
    }
#ifdef _WIN32
    if (executable_command_works("python")) return "python";
    if (executable_command_works("py -3")) return "py -3";
#else
    if (executable_command_works("python3")) return "python3";
    if (executable_command_works("python")) return "python";
#endif
    return {};
}

int Shell::run_python_plugin(const std::vector<std::string>& args) {
    const fs::path plugin = plugin_root_ / "python" / "plugin.py";
    std::error_code ec;
    if (!fs::is_regular_file(plugin, ec)) {
        std::cerr << "DOS Python plugin not installed: " << plugin.string() << '\n';
        return 1;
    }

    const std::string python = find_python_command();
    if (python.empty()) {
        std::cerr << "DOS Python plugin: Python 3 was not found. Set DOS_PYTHON to the interpreter.\n";
        return 1;
    }

    std::vector<std::string> request = args;
    if (request.empty()) {
        std::cerr << "usage: python <script.py> [args...]\n";
        return 2;
    }

    std::string command = python + " " + quote_process_arg(plugin.string());
    const std::string mode = request.front();
    if (mode == "--version" || mode == "--exec" || mode == "--run") {
        command += " " + mode;
        request.erase(request.begin());
    } else {
        command += " --run";
    }
    for (const auto& arg : request) command += " " + quote_process_arg(arg);
    return external(command);
}

int Shell::plugin_command(const std::vector<std::string>& args) {
    std::error_code ec;
    if (args.empty() || lower(args.front()) == "list") {
        if (!fs::exists(plugin_root_, ec)) {
            std::cout << "No DOS plugins installed.\n";
            return 0;
        }
        bool found = false;
        for (fs::directory_iterator it(plugin_root_, ec), end; it != end && !ec; it.increment(ec)) {
            if (!it->is_directory(ec)) continue;
            const fs::path manifest = it->path() / "plugin.dosplugin";
            if (!fs::is_regular_file(manifest, ec)) continue;
            std::ifstream in(manifest, std::ios::binary);
            std::string name = it->path().filename().string();
            std::string version = "unknown";
            std::string type = "unknown";
            std::string line;
            while (std::getline(in, line)) {
                const size_t eq = line.find('=');
                if (eq == std::string::npos) continue;
                const std::string key = trim(line.substr(0, eq));
                const std::string value = trim(line.substr(eq + 1));
                if (key == "name") name = value;
                else if (key == "version") version = value;
                else if (key == "type") type = value;
            }
            std::cout << name << " " << version << " [" << type << "]\n";
            found = true;
        }
        if (ec) {
            std::cerr << "plugin list: " << ec.message() << '\n';
            return 1;
        }
        if (!found) std::cout << "No DOS plugins installed.\n";
        return 0;
    }

    const std::string name = lower(args.front());
    if (name == "python") return run_python_plugin({});
    std::cerr << "plugin: unknown plugin " << args.front() << '\n';
    return 1;
}

std::vector<std::string> Shell::split_words(const std::string& line) {
    std::vector<std::string> words;
    std::string current;
    char quote = 0;
    bool escape = false;
    for (char c : line) {
        if (escape) {
            current += c;
            escape = false;
            continue;
        }
        if (c == '\\' && quote) {
            escape = true;
            continue;
        }
        if (quote) {
            if (c == quote) quote = 0;
            else current += c;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                words.push_back(std::move(current));
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (quote) throw DosError("unterminated command quote");
    if (!current.empty()) words.push_back(std::move(current));
    return words;
}

std::string Shell::expand_env(std::string text) {
    for (size_t i = 0; i < text.size();) {
        if (text[i] != '%') {
            ++i;
            continue;
        }
        const size_t end = text.find('%', i + 1);
        if (end == std::string::npos) break;
        const std::string key = text.substr(i + 1, end - i - 1);
        const char* value = std::getenv(key.c_str());
        const std::string replacement = value ? value : std::string{};
        text.replace(i, end - i + 1, replacement);
        i += replacement.size();
    }
    return text;
}

int Shell::run_command(const std::string& raw) {
    std::string line = trim(raw);
    if (line.empty()) return 0;
    line = expand_env(std::move(line));

    std::vector<std::string> words;
    try {
        words = split_words(line);
    } catch (const DosError& e) {
        std::cerr << "DOS: " << e.what() << '\n';
        return 1;
    }
    if (words.empty()) return 0;

    const std::string cmd = lower(words.front());
    std::vector<std::string> args(words.begin() + 1, words.end());
    const std::string rest = join(args, " ");

    if (cmd == "exit" || cmd == "quit") {
        int code = args.empty() ? 0 : std::atoi(args.front().c_str());
        exit_code = code;
        running = false;
        return code;
    }
    if (cmd == "help") {
        help();
        return 0;
    }
    if (cmd == "ver" || cmd == "version") {
        std::cout << "DOS 1.0\n";
        return 0;
    }
    if (cmd == "cls" || cmd == "clear") {
#ifdef _WIN32
        HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (output != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(output, &info)) {
            const DWORD cells = static_cast<DWORD>(info.dwSize.X) * static_cast<DWORD>(info.dwSize.Y);
            DWORD written = 0;
            const COORD home{0, 0};
            FillConsoleOutputCharacterW(output, L' ', cells, home, &written);
            FillConsoleOutputAttribute(output, info.wAttributes, cells, home, &written);
            SetConsoleCursorPosition(output, home);
        }
#else
        std::cout << "\033[2J\033[H";
#endif
        return 0;
    }
    if (cmd == "pwd") {
        std::cout << cwd() << '\n';
        return 0;
    }
    if (cmd == "cd") {
        fs::path target = args.empty()
            ? fs::path(std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : ".")
            : fs::path(args.front());
        std::error_code ec;
        fs::current_path(target, ec);
        if (ec) {
            std::cerr << "cd: " << ec.message() << '\n';
            return 1;
        }
        return 0;
    }
    if (cmd == "dir" || cmd == "ls") {
        const fs::path target = args.empty() ? fs::current_path() : fs::path(args.front());
        std::error_code ec;
        fs::directory_iterator it(target, ec), end;
        if (ec) {
            std::cerr << "dir: " << ec.message() << '\n';
            return 1;
        }
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            const auto name = it->path().filename().string();
            if (it->is_directory(ec)) {
                std::cout << "<DIR> " << name << '\n';
            } else {
                std::error_code size_ec;
                const auto size = fs::file_size(it->path(), size_ec);
                std::cout << std::setw(12) << (size_ec ? 0ULL : static_cast<unsigned long long>(size))
                          << ' ' << name << '\n';
            }
        }
        if (ec) {
            std::cerr << "dir: " << ec.message() << '\n';
            return 1;
        }
        return 0;
    }
    if (cmd == "tree") {
        const fs::path root = args.empty() ? fs::current_path() : fs::path(args.front());
        std::function<void(const fs::path&, unsigned)> walk = [&](const fs::path& path, unsigned depth) {
            std::error_code ec;
            fs::directory_iterator it(path, ec), end;
            for (; it != end; it.increment(ec)) {
                if (ec) return;
                std::cout << std::string(depth * 2, ' ') << "- " << it->path().filename().string() << '\n';
                if (it->is_directory(ec)) walk(it->path(), depth + 1);
            }
        };
        walk(root, 0);
        return 0;
    }
    if (cmd == "mkdir" || cmd == "md") {
        if (args.empty()) return 1;
        std::error_code ec;
        if (!fs::create_directories(fs::path(args.front()), ec) && ec) {
            std::cerr << "mkdir: " << ec.message() << '\n';
            return 1;
        }
        return 0;
    }
    if (cmd == "rmdir" || cmd == "rd") {
        if (args.empty()) return 1;
        std::error_code ec;
        fs::remove_all(fs::path(args.front()), ec);
        if (ec) {
            std::cerr << "rmdir: " << ec.message() << '\n';
            return 1;
        }
        return 0;
    }
    if (cmd == "copy") {
        if (args.size() < 2) {
            std::cerr << "usage: copy <source> <destination>\n";
            return 1;
        }
        std::error_code ec;
        fs::copy(fs::path(args[0]), fs::path(args[1]), fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "copy: " << ec.message() << '\n';
            return 1;
        }
        return 0;
    }
    if (cmd == "move") {
        if (args.size() < 2) {
            std::cerr << "usage: move <source> <destination>\n";
            return 1;
        }
        std::error_code ec;
        fs::rename(fs::path(args[0]), fs::path(args[1]), ec);
        if (ec) {
            std::cerr << "move: " << ec.message() << '\n';
            return 1;
        }
        return 0;
    }
    if (cmd == "delete" || cmd == "del" || cmd == "rm") {
        if (args.empty()) return 1;
        std::error_code ec;
        fs::remove(fs::path(args.front()), ec);
        if (ec) {
            std::cerr << "delete: " << ec.message() << '\n';
            return 1;
        }
        return 0;
    }
    if (cmd == "rename" || cmd == "ren") {
        if (args.size() < 2) return 1;
        std::error_code ec;
        fs::rename(fs::path(args[0]), fs::path(args[1]), ec);
        if (ec) {
            std::cerr << "rename: " << ec.message() << '\n';
            return 1;
        }
        return 0;
    }
    if (cmd == "type" || cmd == "cat") {
        if (args.empty()) return 1;
        std::ifstream file(args.front(), std::ios::binary);
        if (!file) {
            std::cerr << "type: cannot open " << args.front() << '\n';
            return 1;
        }
        std::cout << file.rdbuf();
        return 0;
    }
    if (cmd == "echo" || cmd == "print") {
        std::cout << rest << '\n';
        return 0;
    }
    if (cmd == "set") {
        if (args.empty()) {
#ifdef _WIN32
            char** environment = *_environ;
            for (char** p = environment; p && *p; ++p) std::cout << *p << '\n';
#endif
            return 0;
        }
        const std::string& assignment = args.front();
        const size_t eq = assignment.find('=');
        if (eq == std::string::npos) {
            if (const char* value = std::getenv(assignment.c_str())) {
                std::cout << value << '\n';
                return 0;
            }
            return 1;
        }
        const std::string key = assignment.substr(0, eq);
        const std::string value = assignment.substr(eq + 1);
#ifdef _WIN32
        return _putenv_s(key.c_str(), value.c_str()) == 0 ? 0 : 1;
#else
        return setenv(key.c_str(), value.c_str(), 1) == 0 ? 0 : 1;
#endif
    }
    if (cmd == "run" || cmd == "start") {
        if (args.empty()) return 1;
        if (lower(fs::path(args.front()).extension().string()) == ".dos")
            return execute_file(fs::path(args.front()));
        return external(rest);
    }
    if (cmd == "process") {
        if (args.empty()) return 1;
        const std::string sub = lower(args.front());
#ifdef _WIN32
        if (sub == "list") {
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snapshot == INVALID_HANDLE_VALUE) return 1;
            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            if (Process32FirstW(snapshot, &entry)) {
                do {
                    std::wcout << std::setw(8) << entry.th32ProcessID << L"  " << entry.szExeFile << L'\n';
                } while (Process32NextW(snapshot, &entry));
            }
            CloseHandle(snapshot);
            return 0;
        }
        if (sub == "kill" && args.size() >= 2) {
            const DWORD pid = static_cast<DWORD>(std::strtoul(args[1].c_str(), nullptr, 10));
            HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (!process) return 1;
            const BOOL ok = TerminateProcess(process, 1);
            CloseHandle(process);
            return ok ? 0 : 1;
        }
#endif
        return 1;
    }
    if (cmd == "network" && !args.empty() && lower(args.front()) == "ping") {
        return external("ping " + join(std::vector<std::string>(args.begin() + 1, args.end()), " "));
    }
    if (cmd == "python") {
        std::vector<std::string> python_args = args;
        if (!python_args.empty() && lower(python_args.front()) == "run") {
            python_args.erase(python_args.begin());
        } else if (!python_args.empty() && lower(python_args.front()) == "exec") {
            python_args.front() = "--exec";
        } else if (!python_args.empty() && lower(python_args.front()) == "--version") {
            python_args.front() = "--version";
        }
        if (python_args.empty()) {
            std::cerr << "usage: python <script.py> [args...]\n";
            return 1;
        }
        return run_python_plugin(python_args);
    }
    if (cmd == "plugin") {
        std::vector<std::string> plugin_args(args.begin(), args.end());
        return plugin_command(plugin_args);
    }
    if (cmd == "system" && !args.empty() && lower(args.front()) == "info") {
#ifdef _WIN32
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        GlobalMemoryStatusEx(&memory);
        std::cout << "Processors: " << info.dwNumberOfProcessors << '\n';
        std::cout << "Memory: " << memory.ullTotalPhys / (1024ULL * 1024ULL) << " MB\n";
        return 0;
#else
        std::cout << "Windows: unavailable in this development build\n";
        return 0;
#endif
    }

    // DOS deliberately delegates unknown commands to Windows cmd.exe.
    return external(line);
}

int Shell::external(const std::string& line) {
#ifdef _WIN32
    std::string command = "cmd.exe /D /S /C \"" + line + "\"";
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::vector<char> buffer(command.begin(), command.end());
    buffer.push_back('\0');
    const BOOL ok = CreateProcessA(nullptr, buffer.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    if (!ok) {
        std::cerr << "command failed: " << GetLastError() << '\n';
        return 1;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
#else
    return std::system(line.c_str());
#endif
}

void Shell::print_prompt() const {
    std::cout << cwd() << "> " << std::flush;
}

void Shell::help() const {
    std::cout
        << "DOS 1.0\n"
        << "Windows CLI + .dos programming environment\n\n"
        << "Files:   dir cd pwd mkdir rmdir copy move delete rename type tree\n"
        << "Shell:   echo set run process network system python plugin cls exit\n"
        << "Python:  python <script.py> | import python; python.run(...)\n"
        << "Lang:    variables functions structs arrays pointers if/else while/for\n"
        << "Blocks:  { }\n"
        << "Source:  .dos\n";
}

int Shell::execute_file(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) {
        std::cerr << "DOS: cannot open " << file.string() << '\n';
        return 1;
    }
    std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

    const auto old_path = fs::current_path();
    std::error_code path_ec;
    const fs::path parent = fs::absolute(file, path_ec).parent_path();
    if (!path_ec && !parent.empty()) fs::current_path(parent, path_ec);

    try {
        Lexer lexer(std::move(source));
        Interpreter interpreter(*this, lexer.lex());
        const int result = interpreter.run();
        std::error_code restore_ec;
        fs::current_path(old_path, restore_ec);
        return result;
    } catch (const std::exception& e) {
        std::error_code restore_ec;
        fs::current_path(old_path, restore_ec);
        std::cerr << "DOS: " << e.what() << '\n';
        return 1;
    }
}

static constexpr const char* PACKAGE_MAGIC = "DOSPKG4";
static constexpr const char* LEGACY_PACKAGE_MAGIC = "DOSPKG3";
static constexpr uint64_t MAX_PACKAGE_SOURCE = 64ULL * 1024ULL * 1024ULL;

static constexpr std::array<uint32_t, 256> make_crc32_table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j)
            c = (c & 1U) ? (0xEDB88320U ^ (c >> 1U)) : (c >> 1U);
        table[i] = c;
    }
    return table;
}

static constexpr auto CRC32_TABLE = make_crc32_table();

static uint32_t crc32_bytes(const void* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFU;
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) crc = CRC32_TABLE[(crc ^ bytes[i]) & 0xFFU] ^ (crc >> 8U);
    return crc ^ 0xFFFFFFFFU;
}

static uint16_t read_u16(const std::vector<unsigned char>& bytes, size_t offset) {
    if (offset + sizeof(uint16_t) > bytes.size()) throw DosError("truncated PE header");
    return static_cast<uint16_t>(bytes[offset]) |
           (static_cast<uint16_t>(bytes[offset + 1]) << 8U);
}

static uint32_t read_u32(const std::vector<unsigned char>& bytes, size_t offset) {
    if (offset + sizeof(uint32_t) > bytes.size()) throw DosError("truncated PE header");
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24U);
}

static bool is_pe_image(const fs::path& file, std::string* reason = nullptr) {
    std::ifstream input(file, std::ios::binary);
    if (!input) { if (reason) *reason = "cannot open image"; return false; }
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0 || size > static_cast<std::streamoff>(256ULL * 1024ULL * 1024ULL)) {
        if (reason) *reason = "invalid image size";
        return false;
    }
    input.seekg(0, std::ios::beg);
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) { if (reason) *reason = "cannot read image"; return false; }
    if (bytes.size() < 0x40 || bytes[0] != 'M' || bytes[1] != 'Z') {
        if (reason) *reason = "missing MZ signature";
        return false;
    }
    const uint32_t pe_offset = read_u32(bytes, 0x3c);
    if (pe_offset > bytes.size() || pe_offset + 24 > bytes.size()) {
        if (reason) *reason = "invalid PE header offset";
        return false;
    }
    if (read_u32(bytes, pe_offset) != 0x00004550U) {
        if (reason) *reason = "missing PE signature";
        return false;
    }
    const uint16_t machine = read_u16(bytes, pe_offset + 4);
    if (machine != 0x8664 && machine != 0xAA64 && machine != 0x014CU) {
        if (reason) *reason = "unsupported PE machine";
        return false;
    }
    const uint16_t sections = read_u16(bytes, pe_offset + 6);
    const uint16_t optional_size = read_u16(bytes, pe_offset + 20);
    const size_t optional = static_cast<size_t>(pe_offset) + 24;
    if (sections == 0 || optional + optional_size > bytes.size()) {
        if (reason) *reason = "invalid PE section/header table";
        return false;
    }
    const uint16_t magic = read_u16(bytes, optional);
    if (magic != 0x20BU && magic != 0x10BU) {
        if (reason) *reason = "unsupported PE optional header";
        return false;
    }
    const size_t section_table = optional + optional_size;
    if (section_table + static_cast<size_t>(sections) * 40 > bytes.size()) {
        if (reason) *reason = "truncated PE sections";
        return false;
    }
    return true;
}

static bool read_embedded_dos(const fs::path& exe, std::string& source) {
    std::ifstream input(exe, std::ios::binary);
    if (!input) return false;
    input.seekg(0, std::ios::end);
    const std::streamoff file_size = input.tellg();
    const size_t magic_len = std::strlen(PACKAGE_MAGIC);
    const size_t legacy_magic_len = std::strlen(LEGACY_PACKAGE_MAGIC);
    const std::streamoff footer3 = static_cast<std::streamoff>(legacy_magic_len + sizeof(uint64_t));
    if (file_size < footer3) return false;

    auto read_source = [&](const char* magic, size_t magic_size, bool with_crc) -> bool {
        const std::streamoff footer = static_cast<std::streamoff>(magic_size + sizeof(uint64_t) + (with_crc ? sizeof(uint32_t) : 0));
        if (file_size < footer) return false;
        input.clear();
        input.seekg(file_size - footer, std::ios::beg);
        std::string found(magic_size, '\0');
        input.read(found.data(), static_cast<std::streamsize>(magic_size));
        if (!input || found != magic) return false;
        uint64_t source_len = 0;
        input.read(reinterpret_cast<char*>(&source_len), sizeof(source_len));
        if (!input || source_len > MAX_PACKAGE_SOURCE) return false;
        uint32_t expected_crc = 0;
        if (with_crc) {
            input.read(reinterpret_cast<char*>(&expected_crc), sizeof(expected_crc));
            if (!input) return false;
        }
        const uint64_t payload = static_cast<uint64_t>(file_size) - static_cast<uint64_t>(footer);
        if (source_len > payload) return false;
        const std::streamoff source_pos = file_size - footer - static_cast<std::streamoff>(source_len);
        input.clear();
        input.seekg(source_pos, std::ios::beg);
        source.resize(static_cast<size_t>(source_len));
        if (source_len != 0) input.read(source.data(), static_cast<std::streamsize>(source_len));
        if (!input) return false;
        if (with_crc && crc32_bytes(source.data(), source.size()) != expected_crc) return false;
        return true;
    };

    if (read_source(PACKAGE_MAGIC, magic_len, true)) return true;
    return read_source(LEGACY_PACKAGE_MAGIC, legacy_magic_len, false);
}

static int build_embedded_exe(const fs::path& self, const fs::path& input_path,
                              const fs::path& output_path, const fs::path& base_path = {}) {
    std::ifstream source_file(input_path, std::ios::binary);
    if (!source_file) {
        std::cerr << "DOS build: cannot open " << input_path.string() << '\n';
        return 1;
    }
    std::string source((std::istreambuf_iterator<char>(source_file)), std::istreambuf_iterator<char>());
    constexpr size_t max_source = 64ULL * 1024ULL * 1024ULL;
    if (source.size() > max_source) {
        std::cerr << "DOS build: source is larger than 64 MiB\n";
        return 1;
    }

    try {
        (void)Lexer(source).lex();
    } catch (const DosError& error) {
        std::cerr << "DOS build: lexical scan failed at " << error.line << ':' << error.col
                  << ": " << error.what() << '\n';
        return 1;
    }

    std::error_code ec1, ec2;
    const fs::path base = base_path.empty() ? self : base_path;
    const fs::path self_abs = fs::weakly_canonical(fs::absolute(self, ec1), ec1);
    const fs::path base_abs = fs::weakly_canonical(fs::absolute(base, ec2), ec2);
    std::error_code ec3;
    const fs::path output_abs = fs::weakly_canonical(fs::absolute(output_path, ec3), ec3);
    if (!ec1 && !ec3 && self_abs == output_abs) {
        std::cerr << "DOS build: output cannot replace dos.exe\n";
        return 1;
    }
    if (!ec2 && !ec3 && base_abs == output_abs) {
        std::cerr << "DOS build: output cannot replace PE base image\n";
        return 1;
    }

    if (!base_path.empty() ||
#ifdef _WIN32
        true
#else
        false
#endif
    ) {
        std::string reason;
        if (!is_pe_image(base, &reason)) {
            std::cerr << "DOS build: base image is not a supported PE executable: " << reason << '\n';
            return 1;
        }
    }

    std::ifstream base_file(base, std::ios::binary);
    if (!base_file) {
        std::cerr << "DOS build: cannot open base executable " << base.string() << '\n';
        return 1;
    }

    fs::path tmp = output_path;
    tmp += ".dos-tmp";
    std::ofstream output(tmp, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "DOS build: cannot create temporary output " << tmp.string() << '\n';
        return 1;
    }
    output << base_file.rdbuf();
    output.write(source.data(), static_cast<std::streamsize>(source.size()));
    output.write(PACKAGE_MAGIC, static_cast<std::streamsize>(std::strlen(PACKAGE_MAGIC)));
    const uint64_t length = static_cast<uint64_t>(source.size());
    const uint32_t crc = crc32_bytes(source.data(), source.size());
    output.write(reinterpret_cast<const char*>(&length), sizeof(length));
    output.write(reinterpret_cast<const char*>(&crc), sizeof(crc));
    output.flush();
    if (!output) {
        output.close();
        std::error_code remove_ec;
        fs::remove(tmp, remove_ec);
        std::cerr << "DOS build: write failed\n";
        return 1;
    }
    output.close();

    std::error_code rename_ec;
    fs::remove(output_path, rename_ec);
    rename_ec.clear();
    fs::rename(tmp, output_path, rename_ec);
    if (rename_ec) {
        std::error_code cleanup_ec;
        fs::remove(tmp, cleanup_ec);
        std::cerr << "DOS build: cannot install output: " << rename_ec.message() << '\n';
        return 1;
    }

    std::string verify_reason;
    if (!is_pe_image(output_path, &verify_reason) && !base_path.empty()) {
        std::cerr << "DOS build: output PE verification failed: " << verify_reason << '\n';
        return 1;
    }
    std::cout << "Built: " << output_path.string();
    if (!base_path.empty()) std::cout << " (PE)";
    std::cout << '\n';
    return 0;
}

static int verify_executable(const fs::path& file, bool require_package) {
    std::string pe_reason;
    const bool pe = is_pe_image(file, &pe_reason);
    std::cout << "PE: " << (pe ? "valid" : "invalid") << '\n';
    if (!pe) {
        std::cout << "PE reason: " << pe_reason << '\n';
        return 1;
    }

    std::string source;
    const bool package = read_embedded_dos(file, source);
    std::cout << "DOS package: " << (package ? "valid" : "missing-or-invalid") << '\n';
    if (!package && require_package) return 2;
    if (package) std::cout << "Source bytes: " << source.size() << '\n';
    return 0;
}

static bool has_dos_extension(const std::string& text) {
    const std::string extension = lower(fs::path(text).extension().string());
    return extension == ".dos";
}

} // namespace dos

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    dos::Shell shell(argv[0]);

    if (argc > 1) {
        const std::string first = argv[1];
        if (first == "--version" || first == "-v") {
            std::cout << "DOS 1.0\n";
            return 0;
        }
        if (first == "--help" || first == "-h" || first == "help") {
            shell.help();
            return 0;
        }
        if (first == "run") {
            if (argc < 3) {
                std::cerr << "usage: dos run file.dos\n";
                return 1;
            }
            return shell.execute_file(argv[2]);
        }
        if (first == "verify") {
            if (argc < 3) {
                std::cerr << "usage: dos verify [pe|package] file.exe\n";
                return 1;
            }
            const std::string mode = argc >= 4 ? dos::lower(argv[2]) : "pe";
            const fs::path file = argc >= 4 ? fs::path(argv[3]) : fs::path(argv[2]);
            if (mode != "pe" && mode != "package") {
                std::cerr << "verify: expected pe or package\n";
                return 1;
            }
            return dos::verify_executable(file, mode == "package");
        }
        if (first == "build" || first == "pe-build") {
            if (argc < 3) {
                std::cerr << "usage: dos " << first << " file.dos [-o output.exe] [-b base.exe]\n";
                return 1;
            }
            fs::path input = argv[2];
            fs::path output = input;
            output.replace_extension(".exe");
            fs::path base;
            for (int i = 3; i + 1 < argc; ++i) {
                const std::string flag = argv[i];
                if (flag == "-o" || flag == "--output") output = argv[++i];
                else if (flag == "-b" || flag == "--base") base = argv[++i];
            }
            if (first == "pe-build" && base.empty()) {
                std::cerr << "usage: dos pe-build file.dos -b base.exe [-o output.exe]\n";
                return 1;
            }
            return dos::build_embedded_exe(argv[0], input, output, base);
        }
        if (dos::has_dos_extension(first)) {
            const int code = shell.execute_file(first);
#ifdef _WIN32
            if (argc == 2 && GetConsoleWindow()) {
                std::cout << "\nPress Enter to close..." << std::flush;
                std::string ignored;
                std::getline(std::cin, ignored);
            }
#endif
            return code;
        }

        std::string command;
        for (int i = 1; i < argc; ++i) {
            if (i > 1) command += ' ';
            command += argv[i];
        }
        return shell.run_command(command);
    }

    std::string embedded;
    if (dos::read_embedded_dos(argv[0], embedded)) {
        try {
            dos::Lexer lexer(std::move(embedded));
            dos::Interpreter interpreter(shell, lexer.lex());
            return interpreter.run();
        } catch (const std::exception& e) {
            std::cerr << "DOS packaged program error: " << e.what() << '\n';
            return 1;
        }
    }

    const std::string executable_name = dos::lower(fs::path(argv[0]).stem().string());
    if (executable_name != "dos") {
        std::cerr << "DOS: invalid or missing embedded program package\n";
        return 1;
    }

    std::cout << "DOS 1.0\nType HELP for help.\n\n";
    std::string line;
    while (shell.running) {
        shell.print_prompt();
        if (!std::getline(std::cin, line)) break;
        if (dos::trim(line).empty()) continue;
        shell.run_command(line);
    }
    return shell.exit_code;
}
