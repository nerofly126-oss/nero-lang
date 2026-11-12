#pragma once
#include "ast.hpp"
#include <cstdio>
#include <cstdlib>
#include <ostream>
#include <string>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// "nero eval": a tree walking evaluator over the same AST as the compiler, but on
// richer values. A record keeps its fields in the order they were written.
struct Value {
    enum Kind { Int, Str, Bool, List, Record, Secret } kind = Int;
    int64_t i = 0;                   // Int, or 0 and 1 for Bool
    std::string s;                   // Str, or the NAME of a Secret (its value is never read)
    std::vector<std::string> keys;   // Record: field names, parallel to items
    std::vector<Value> items;        // List elements, or Record field values
    std::string type_name;           // a record built from a schema carries the schema's name

    static Value integer(int64_t v) { Value x; x.kind = Int; x.i = v; return x; }
    static Value text(std::string v) { Value x; x.kind = Str; x.s = std::move(v); return x; }
    static Value boolean(bool v) { Value x; x.kind = Bool; x.i = v ? 1 : 0; return x; }
    static Value record() { Value x; x.kind = Record; return x; }
};

inline const char* kind_name(Value::Kind k) {
    switch (k) {
        case Value::Int:    return "int";
        case Value::Str:    return "str";
        case Value::Bool:   return "bool";
        case Value::List:   return "list";
        case Value::Record: return "record";
        case Value::Secret: return "secret";
    }
    return "?";
}

inline std::string kind_an(Value::Kind k) {
    return std::string(k == Value::Int ? "an " : "a ") + kind_name(k);
}

inline Value::Kind kind_of(Type t) {
    switch (t) {
        case Type::Int:    return Value::Int;
        case Type::Str:    return Value::Str;
        case Type::Bool:   return Value::Bool;
        case Type::List:   return Value::List;
        case Type::Record:
        case Type::Named:  return Value::Record;
        case Type::Secret: return Value::Secret;
    }
    return Value::Int;
}

inline std::string with_article(const std::string& name) {
    return std::string(std::strchr("AEIOUaeiou", name[0]) ? "an " : "a ") + name;
}

inline std::string describe_value(const Value& v) {
    if (v.kind == Value::Record && !v.type_name.empty()) return with_article(v.type_name);
    return kind_an(v.kind);
}

// A type in running text: "an int", "a Service", "a list of Port".
inline std::string type_phrase(const TypeRef& t) {
    switch (t.kind) {
        case Type::Int:    return "an int";
        case Type::Str:    return "a str";
        case Type::Bool:   return "a bool";
        case Type::Record: return "a record";
        case Type::List:   return t.elem ? "a list of " + typeref_str(*t.elem) : "a list";
        case Type::Named:  return with_article(t.name);
        case Type::Secret: return "a secret";
    }
    return "?";
}

// "" when v fits the type, otherwise a phrase for what was found instead.
inline std::string mismatch(const TypeRef& t, const Value& v) {
    switch (t.kind) {
        case Type::Int:
        case Type::Str:
        case Type::Secret:
        case Type::Bool:   return v.kind == kind_of(t.kind) ? "" : describe_value(v);
        case Type::Record: return v.kind == Value::Record ? "" : describe_value(v);
        case Type::List:
            if (v.kind != Value::List) return describe_value(v);
            if (t.elem)
                for (size_t i = 0; i < v.items.size(); i++) {
                    std::string m = mismatch(*t.elem, v.items[i]);
                    if (!m.empty()) return "a list, but element " + std::to_string(i) + " is " + m;
                }
            return "";
        case Type::Named: {
            if (v.kind == Value::Record && v.type_name == t.name) return "";
            std::string got = describe_value(v);
            if (v.kind == Value::Record && v.type_name.empty()) got += " (write " + t.name + " { ... })";
            return got;
        }
    }
    return "";
}

inline Value default_value(Type t) { Value v; v.kind = kind_of(t); return v; }

// A secret is only a reference, so a reference is all that is ever written out.
inline Value secret_as_record(const Value& v) {
    Value r = Value::record();
    r.keys.push_back("secretRef");
    r.items.push_back(Value::text(v.s));
    return r;
}

// A deep copy with every secret replaced by its reference record.
inline Value refs_as_records(const Value& v) {
    if (v.kind == Value::Secret) return secret_as_record(v);
    if (v.kind != Value::List && v.kind != Value::Record) return v;
    Value out = v;
    for (Value& item : out.items) item = refs_as_records(item);
    return out;
}

inline bool holds_secret(const Value& v) {
    if (v.kind == Value::Secret) return true;
    for (const Value& item : v.items)
        if (holds_secret(item)) return true;
    return false;
}

inline void json_string(const std::string& s, std::string& out) {
    out += '"';
    for (char ch : s) {
        unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += ch;
        }
    }
    out += '"';
}

inline void json_write(const Value& v, std::string& out, bool pretty, int level) {
    auto nl = [&](int lv) {
        if (!pretty) return;
        out += '\n';
        out.append(static_cast<size_t>(lv) * 2, ' ');
    };
    switch (v.kind) {
        case Value::Int:  out += std::to_string(v.i); break;
        case Value::Bool: out += v.i ? "true" : "false"; break;
        case Value::Str:  json_string(v.s, out); break;
        case Value::Secret: json_write(secret_as_record(v), out, pretty, level); break;
        case Value::List:
        case Value::Record: {
            bool rec = v.kind == Value::Record;
            if (v.items.empty()) { out += rec ? "{}" : "[]"; break; }
            out += rec ? '{' : '[';
            for (size_t k = 0; k < v.items.size(); k++) {
                if (k) out += pretty ? "," : ", ";
                nl(level + 1);
                if (rec) { json_string(v.keys[k], out); out += ": "; }
                json_write(v.items[k], out, pretty, level + 1);
            }
            nl(level);
            out += rec ? '}' : ']';
            break;
        }
    }
}

inline std::string to_json(const Value& v, bool pretty) {
    std::string out;
    json_write(v, out, pretty, 0);
    return out;
}

enum class OutFormat { Json, Yaml };

// True when a string can be written in YAML without quotes and still read back as the same string.
// Deliberately strict: anything unusual gets double quotes (which are the same as JSON string quotes).
inline bool yaml_plain_ok(const std::string& s) {
    auto alpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (s.empty() || !(alpha(s[0]) || s[0] == '_' || s[0] == '/')) return false;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == ' ') {                                    // single spaces between words only
            if (i + 1 == s.size() || s[i + 1] == ' ') return false;
            continue;
        }
        if (!(alpha(c) || digit(c) || std::string("_./@+=:-").find(c) != std::string::npos)) return false;
        if (c == ':' && (i + 1 == s.size() || s[i + 1] == ' ')) return false;   // "a:" would read as a key
    }
    std::string low;
    for (char c : s) low += static_cast<char>((c >= 'A' && c <= 'Z') ? c + 32 : c);
    static const char* reserved[] = {"true", "false", "yes", "no", "on", "off", "null", "y", "n"};
    for (const char* r : reserved)
        if (low == r) return false;
    return true;
}

inline std::string yaml_text(const std::string& s) {
    if (yaml_plain_ok(s)) return s;
    std::string q;
    json_string(s, q);
    return q;
}

inline bool yaml_is_block(const Value& v) {
    return (v.kind == Value::List || v.kind == Value::Record) && !v.items.empty();
}

inline std::string yaml_scalar(const Value& v) {
    switch (v.kind) {
        case Value::Int:    return std::to_string(v.i);
        case Value::Bool:   return v.i ? "true" : "false";
        case Value::Str:    return yaml_text(v.s);
        case Value::List:   return "[]";     // only empty collections are written inline
        case Value::Record: return "{}";
        case Value::Secret: return "{}";     // never reached: to_yaml turns secrets into records first
    }
    return "";
}

// A non-empty list or record as block style lines, every line indented by at least `indent` spaces.
inline std::string yaml_block(const Value& v, int indent) {
    std::string out, pad(static_cast<size_t>(indent), ' ');
    for (size_t k = 0; k < v.items.size(); k++) {
        const Value& item = v.items[k];
        if (v.kind == Value::Record) {
            out += pad + yaml_text(v.keys[k]) + ":";
            if (yaml_is_block(item)) out += "\n" + yaml_block(item, indent + 2);
            else out += " " + yaml_scalar(item) + "\n";
        } else if (yaml_is_block(item)) {
            // the item's first line shares the line with its dash: "- name: a"
            out += pad + "- " + yaml_block(item, indent + 2).substr(static_cast<size_t>(indent) + 2);
        } else {
            out += pad + "- " + yaml_scalar(item) + "\n";
        }
    }
    return out;
}

inline std::string to_yaml(const Value& original) {
    Value v = refs_as_records(original);
    return yaml_is_block(v) ? yaml_block(v, 0) : yaml_scalar(v) + "\n";
}

inline bool deep_eq(const Value& a, const Value& b) {
    if (a.kind != b.kind || a.type_name != b.type_name) return false;
    switch (a.kind) {
        case Value::Int:
        case Value::Bool: return a.i == b.i;
        case Value::Str:
        case Value::Secret: return a.s == b.s;
        case Value::List:
            if (a.items.size() != b.items.size()) return false;
            for (size_t k = 0; k < a.items.size(); k++)
                if (!deep_eq(a.items[k], b.items[k])) return false;
            return true;
        case Value::Record:   // same fields, order does not matter
            if (a.items.size() != b.items.size()) return false;
            for (size_t k = 0; k < a.keys.size(); k++) {
                bool found = false;
                for (size_t j = 0; j < b.keys.size() && !found; j++)
                    if (a.keys[k] == b.keys[j]) found = deep_eq(a.items[k], b.items[j]);
                if (!found) return false;
            }
            return true;
    }
    return false;
}

class Evaluator {
public:
    Evaluator(const Program& prog, std::ostream& out, OutFormat fmt = OutFormat::Json) : prog_(prog), out_(out), fmt_(fmt) {}

    // diff mode: emitted values are handed to this list instead of being printed
    void collect(std::vector<Value>& into) { collect_ = &into; }

    void run() {
        struct FileReset { ~FileReset() { cur_file_ = nullptr; } } file_reset;
        for (const Func& f : prog_.funcs) {
            cur_file_ = &f.file;
            if (is_builtin(f.name)) fail(f.line, f.col, "'" + f.name + "' is a builtin and cannot be redefined");
            if (funcs_.count(f.name)) fail(f.line, f.col, "function '" + f.name + "' is already defined");
            for (size_t i = 0; i < f.params.size(); i++)
                for (size_t j = 0; j < i; j++)
                    if (f.params[i] == f.params[j]) fail(f.line, f.col, "duplicate parameter '" + f.params[i] + "'");
            funcs_[f.name] = &f;
        }
        for (const TypeDecl& d : prog_.types) types_[d.name] = &d;
        for (const TypeDecl& d : prog_.types)            // bad defaults are reported before anything runs
            for (const FieldDecl& fd : d.fields)
                if (fd.def) default_of(fd);
        cur_file_ = &prog_.file;
        Frame top;
        exec_stmts(prog_.stmts, top);
    }

private:
    struct Frame {
        std::unordered_map<std::string, Value> vars;
        std::unordered_map<std::string, const Stmt*> decl;   // which 'let' declared a name (null for parameters)
        const Func* func = nullptr;
        bool returned = false;
        Value ret;
    };

    static constexpr int MAX_DEPTH = 1000;
    const Program& prog_;
    std::ostream& out_;
    OutFormat fmt_;
    int emitted_ = 0;
    std::vector<Value>* collect_ = nullptr;
    std::unordered_map<std::string, const Func*> funcs_;
    std::unordered_map<std::string, const TypeDecl*> types_;
    std::unordered_map<const FieldDecl*, Value> defaults_;      // evaluated once, on first use
    std::unordered_set<const FieldDecl*> defaulting_;           // defaults being evaluated right now
    int depth_ = 0;

    // The file whose code is running right now, so an error can say which file it is in.
    // "" (or none) for source that did not come from a file: then errors stay "line:col: message".
    static inline const std::string* cur_file_ = nullptr;

    [[noreturn]] static void fail(int line, int col, const std::string& msg) {
        std::string where = cur_file_ && !cur_file_->empty() ? *cur_file_ + ":" : "";
        throw std::runtime_error(where + std::to_string(line) + ":" + std::to_string(col) + ": " + msg);
    }

    static bool is_builtin(const std::string& n) {
        return n == "len" || n == "str" || n == "range" || n == "env" || n == "secret";
    }

    static std::string show(const Value& v) { return v.kind == Value::Str ? v.s : to_json(v, false); }

    void exec_stmts(const std::vector<StmtPtr>& body, Frame& f) {
        for (const StmtPtr& s : body) {
            exec_stmt(*s, f);
            if (f.returned) return;
        }
    }

    // loop variables live only inside their loop, and may not reuse a name that is already taken
    static void check_free(const std::string& name, const Frame& f, int line, int col) {
        if (f.vars.count(name) || f.decl.count(name)) fail(line, col, "'" + name + "' is already declared");
    }

    bool truthy(const Expr& e, Frame& f, const char* what) {
        Value v = eval(e, f);
        if (v.kind != Value::Bool && v.kind != Value::Int)
            fail(e.line, e.col, std::string(what) + " must be a bool, got " + kind_an(v.kind));
        return v.i != 0;
    }

    void exec_stmt(const Stmt& s, Frame& f) {
        switch (s.kind) {
            case Stmt::Let: {
                Value v = eval(*s.expr, f);
                auto d = f.decl.find(s.name);
                if (d != f.decl.end() && d->second != &s) fail(s.line, s.col, "'" + s.name + "' is already declared");
                f.decl[s.name] = &s;
                f.vars[s.name] = std::move(v);
                break;
            }
            case Stmt::Assign: {
                Value v = eval(*s.expr, f);
                auto it = f.vars.find(s.name);
                if (it == f.vars.end()) fail(s.line, s.col, "undefined variable '" + s.name + "'");
                if (it->second.kind != v.kind)
                    fail(s.line, s.col, "'" + s.name + "' is " + kind_an(it->second.kind) + ", cannot assign " + kind_an(v.kind));
                it->second = std::move(v);
                break;
            }
            case Stmt::Print:
                out_ << show(eval(*s.expr, f)) << "\n";
                break;
            case Stmt::Emit: {
                Value v = eval(*s.expr, f);
                if (collect_) { collect_->push_back(std::move(v)); break; }
                if (fmt_ == OutFormat::Yaml) {
                    if (emitted_++) out_ << "---\n";      // YAML documents are separated by ---
                    out_ << to_yaml(v);
                } else {
                    out_ << to_json(v, true) << "\n";
                }
                break;
            }
            case Stmt::ExprStmt:
                eval(*s.expr, f);
                break;
            case Stmt::Return: {
                if (!f.func) fail(s.line, s.col, "'return' outside a function");
                Value v = s.expr ? eval(*s.expr, f) : implicit_return(*f.func, s.line, s.col, true);
                {
                    std::string m = mismatch(f.func->ret_ref, v);
                    if (!m.empty())
                        fail(s.line, s.col, "this function returns " + typeref_str(f.func->ret_ref) + ", but the value is " + m +
                                                (f.func->ret_explicit ? "" : " (no return type is written, so it is an int)"));
                }
                f.ret = std::move(v);
                f.returned = true;
                break;
            }
            case Stmt::For: {
                Value xs = eval(*s.expr, f);
                if (xs.kind != Value::List)
                    fail(s.expr->line, s.expr->col, "can only loop over a list, got " + describe_value(xs));
                check_free(s.name, f, s.line, s.col);
                f.decl[s.name] = nullptr;
                for (const Value& item : xs.items) {
                    f.vars[s.name] = item;
                    exec_stmts(s.body, f);
                    if (f.returned) break;
                }
                f.vars.erase(s.name);
                f.decl.erase(s.name);
                break;
            }
            case Stmt::While:
                while (truthy(*s.expr, f, "a while condition")) {
                    exec_stmts(s.body, f);
                    if (f.returned) break;
                }
                break;
            case Stmt::If:
                if (truthy(*s.expr, f, "an if condition")) exec_stmts(s.body, f);
                else if (s.has_else) exec_stmts(s.else_body, f);
                break;
        }
    }

    Value eval(const Expr& e, Frame& f) {
        switch (e.kind) {
            case Expr::Num:  return Value::integer(e.value);
            case Expr::Str:  return Value::text(e.name);
            case Expr::Bool: return Value::boolean(e.value != 0);
            case Expr::Var: {
                auto it = f.vars.find(e.name);
                if (it == f.vars.end()) fail(e.line, e.col, "undefined variable '" + e.name + "'");
                return it->second;
            }
            case Expr::Unary: {
                Value v = eval(*e.lhs, f);
                if (e.op == Tok::Not) {
                    if (v.kind != Value::Bool)
                        fail(e.lhs->line, e.lhs->col, "the operand of 'not' must be a bool, got " + kind_an(v.kind));
                    return Value::boolean(v.i == 0);
                }
                if (v.kind != Value::Int)
                    fail(e.lhs->line, e.lhs->col, "the operand of '-' must be an int, got " + kind_an(v.kind));
                if (v.i == INT64_MIN) fail(e.line, e.col, "integer overflow");
                return Value::integer(-v.i);
            }
            case Expr::IfExpr:
                return truthy(*e.lhs, f, "an if condition") ? eval(*e.rhs, f) : eval(*e.alt, f);
            case Expr::Interp: {
                std::string out;
                for (const ExprPtr& a : e.args) {
                    Value v = eval(*a, f);
                    switch (v.kind) {
                        case Value::Str:  out += v.s; break;
                        case Value::Int:  out += std::to_string(v.i); break;
                        case Value::Bool: out += v.i ? "true" : "false"; break;
                        default: fail(a->line, a->col, "cannot put " + describe_value(v) + " inside a string (only int, str and bool)");
                    }
                }
                return Value::text(std::move(out));
            }
            case Expr::Comp: {
                Value xs = eval(*e.rhs, f);
                if (xs.kind != Value::List)
                    fail(e.rhs->line, e.rhs->col, "can only loop over a list, got " + describe_value(xs));
                check_free(e.name, f, e.line, e.col);
                f.decl[e.name] = nullptr;
                Value out;
                out.kind = Value::List;
                for (const Value& item : xs.items) {
                    f.vars[e.name] = item;
                    if (e.alt && !truthy(*e.alt, f, "a list filter")) continue;
                    out.items.push_back(eval(*e.lhs, f));
                }
                f.vars.erase(e.name);
                f.decl.erase(e.name);
                return out;
            }
            case Expr::Binary: return eval_binary(e, f);
            case Expr::Call:   return eval_call(e, f);
            case Expr::List: {
                Value v;
                v.kind = Value::List;
                for (const ExprPtr& a : e.args) v.items.push_back(eval(*a, f));
                return v;
            }
            case Expr::Record: {
                if (!e.name.empty()) return eval_typed(e, f);
                Value v = Value::record();
                for (size_t k = 0; k < e.keys.size(); k++) {
                    for (const std::string& seen : v.keys)
                        if (seen == e.keys[k])
                            fail(e.key_pos[k].first, e.key_pos[k].second, "duplicate field '" + e.keys[k] + "'");
                    Value item = eval(*e.args[k], f);
                    v.keys.push_back(e.keys[k]);
                    v.items.push_back(std::move(item));
                }
                return v;
            }
            case Expr::Field: {
                Value base = eval(*e.lhs, f);
                if (base.kind != Value::Record)
                    fail(e.line, e.col, "cannot read field '" + e.name + "' of " + kind_an(base.kind));
                return get_field(base, e.name, e.line, e.col);
            }
            case Expr::Index: {
                Value base = eval(*e.lhs, f);
                Value idx = eval(*e.rhs, f);
                if (base.kind == Value::List) {
                    if (idx.kind != Value::Int)
                        fail(e.rhs->line, e.rhs->col, "a list index must be an int, got " + kind_an(idx.kind));
                    if (idx.i < 0 || idx.i >= static_cast<int64_t>(base.items.size()))
                        fail(e.line, e.col, "index " + std::to_string(idx.i) + " is out of range for a list of length " +
                                                std::to_string(base.items.size()));
                    return base.items[static_cast<size_t>(idx.i)];
                }
                if (base.kind == Value::Record) {
                    if (idx.kind != Value::Str)
                        fail(e.rhs->line, e.rhs->col, "a record key must be a str, got " + kind_an(idx.kind));
                    return get_field(base, idx.s, e.line, e.col);
                }
                fail(e.line, e.col, "cannot index " + kind_an(base.kind));
            }
        }
        return Value();
    }

    static Value get_field(const Value& rec, const std::string& name, int line, int col) {
        for (size_t k = 0; k < rec.keys.size(); k++)
            if (rec.keys[k] == name) return rec.items[k];
        fail(line, col, (rec.type_name.empty() ? std::string("record") : rec.type_name) + " " + no_field(name, rec.keys));
    }

    static std::string no_field(const std::string& name, const std::vector<std::string>& names) {
        std::string msg = "has no field '" + name + "'";
        std::string hint = closest(name, names);
        if (!hint.empty()) return msg + " (did you mean '" + hint + "'?)";
        if (!names.empty()) {
            msg += " (fields: ";
            for (size_t k = 0; k < names.size(); k++) msg += (k ? ", " : "") + names[k];
            msg += ")";
        }
        return msg;
    }

    // The default of a schema field, evaluated once and checked against the field's type.
    const Value& default_of(const FieldDecl& fd) {
        auto it = defaults_.find(&fd);
        if (it != defaults_.end()) return it->second;
        if (defaulting_.count(&fd)) fail(fd.def->line, fd.def->col, "the default of field '" + fd.name + "' refers to itself");
        defaulting_.insert(&fd);
        Frame none;
        const std::string* saved_file = cur_file_;
        cur_file_ = &fd.file;                      // errors in a default point into the file that declared it
        Value v = eval(*fd.def, none);
        std::string m = mismatch(fd.type, v);
        if (!m.empty())
            fail(fd.def->line, fd.def->col, "the default of field '" + fd.name + "' must be " + type_phrase(fd.type) + ", got " + m);
        defaulting_.erase(&fd);
        cur_file_ = saved_file;
        return defaults_[&fd] = std::move(v);
    }

    // Name { field: value, ... }: every field is checked, defaults fill the gaps, output follows the schema's order.
    Value eval_typed(const Expr& e, Frame& f) {
        const TypeDecl& d = *types_.at(e.name);
        size_t n = d.fields.size();
        std::vector<std::string> names;
        for (const FieldDecl& fd : d.fields) names.push_back(fd.name);
        std::vector<Value> given(n);
        std::vector<bool> have(n, false);
        for (size_t k = 0; k < e.keys.size(); k++) {
            size_t idx = n;
            for (size_t j = 0; j < n; j++)
                if (d.fields[j].name == e.keys[k]) idx = j;
            int kl = e.key_pos[k].first, kc = e.key_pos[k].second;
            if (idx == n) fail(kl, kc, d.name + " " + no_field(e.keys[k], names));
            if (have[idx]) fail(kl, kc, "duplicate field '" + e.keys[k] + "'");
            Value v = eval(*e.args[k], f);
            std::string m = mismatch(d.fields[idx].type, v);
            if (!m.empty())
                fail(e.args[k]->line, e.args[k]->col, "field '" + e.keys[k] + "' of " + d.name + " must be " +
                                                          type_phrase(d.fields[idx].type) + ", got " + m);
            given[idx] = std::move(v);
            have[idx] = true;
        }
        Value out = Value::record();
        out.type_name = d.name;
        std::string missing;
        int count = 0;
        for (size_t j = 0; j < n; j++) {
            const FieldDecl& fd = d.fields[j];
            if (!have[j]) {
                if (fd.def) given[j] = default_of(fd);
                else {
                    if (count++) missing += ", ";
                    missing += "'" + fd.name + "' (" + type_phrase(fd.type) + ")";
                    continue;
                }
            }
            out.keys.push_back(fd.name);
            out.items.push_back(std::move(given[j]));
        }
        if (count) fail(e.line, e.col, d.name + " is missing field" + (count > 1 ? "s " : " ") + missing);
        return out;
    }

    // What a function gives back when it falls off its end, or says a bare 'return'.
    static Value implicit_return(const Func& fn, int line, int col, bool bare) {
        if (fn.ret_ref.kind == Type::Named)
            fail(line, col, bare ? "a bare 'return' is not allowed in a function that returns " + fn.ret_ref.name
                                 : "function '" + fn.name + "' ended without returning a value of type " + fn.ret_ref.name);
        return default_value(fn.ret);
    }

    static void need_bool(const Value& v, const Expr& at, const char* op) {
        if (v.kind != Value::Bool)
            fail(at.line, at.col, std::string("both sides of ") + op + " must be bools, got " + kind_an(v.kind));
    }

    Value eval_binary(const Expr& e, Frame& f) {
        if (e.op == Tok::And || e.op == Tok::Or) {          // short circuit
            bool is_and = e.op == Tok::And;
            const char* name = is_and ? "'and'" : "'or'";
            Value l = eval(*e.lhs, f);
            need_bool(l, *e.lhs, name);
            if (is_and ? l.i == 0 : l.i != 0) return Value::boolean(!is_and);
            Value r = eval(*e.rhs, f);
            need_bool(r, *e.rhs, name);
            return Value::boolean(r.i != 0);
        }

        Value l = eval(*e.lhs, f);
        Value r = eval(*e.rhs, f);

        if (e.op == Tok::Eq || e.op == Tok::Ne) {
            if (holds_secret(l) || holds_secret(r)) fail(e.line, e.col, "secrets cannot be compared");
            if (l.kind != r.kind)
                fail(e.line, e.col, "cannot compare " + kind_an(l.kind) + " with " + kind_an(r.kind));
            bool eq = deep_eq(l, r);
            return Value::boolean(e.op == Tok::Eq ? eq : !eq);
        }
        if (e.op == Tok::Plus && l.kind == r.kind) {
            if (l.kind == Value::Str) return Value::text(l.s + r.s);
            if (l.kind == Value::List) {
                Value out = l;
                out.items.insert(out.items.end(), r.items.begin(), r.items.end());
                return out;
            }
        }
        if (l.kind != Value::Int || r.kind != Value::Int) {
            std::string msg = std::string("cannot use '") + op_str(e.op) + "' on " + kind_an(l.kind) + " and " + kind_an(r.kind);
            bool text_and_num = (l.kind == Value::Str && r.kind == Value::Int) || (l.kind == Value::Int && r.kind == Value::Str);
            if (e.op == Tok::Plus && text_and_num) msg += " (use str(...) to turn a number into text)";
            fail(e.line, e.col, msg);
        }
        return int_op(e, l.i, r.i);
    }

    static Value int_op(const Expr& e, int64_t a, int64_t b) {
        int64_t out = 0;
        switch (e.op) {
            case Tok::Plus:
                if (__builtin_add_overflow(a, b, &out)) fail(e.line, e.col, "integer overflow");
                return Value::integer(out);
            case Tok::Minus:
                if (__builtin_sub_overflow(a, b, &out)) fail(e.line, e.col, "integer overflow");
                return Value::integer(out);
            case Tok::Star:
                if (__builtin_mul_overflow(a, b, &out)) fail(e.line, e.col, "integer overflow");
                return Value::integer(out);
            case Tok::Slash:
            case Tok::Percent:
                if (b == 0) fail(e.line, e.col, "division by zero");
                if (b == -1) {
                    if (e.op == Tok::Percent) return Value::integer(0);
                    if (a == INT64_MIN) fail(e.line, e.col, "integer overflow");
                    return Value::integer(-a);
                }
                return Value::integer(e.op == Tok::Slash ? a / b : a % b);
            case Tok::Lt: return Value::boolean(a < b);
            case Tok::Le: return Value::boolean(a <= b);
            case Tok::Gt: return Value::boolean(a > b);
            case Tok::Ge: return Value::boolean(a >= b);
            default: fail(e.line, e.col, "unknown operator");
        }
    }

    // env("NAME") reads an environment variable, env("NAME", "fallback") gives it a default.
    // secret("NAME") is only a reference to a secret. Its value is never read, so it cannot end up in the output.
    Value eval_env(const Expr& e, Frame& f) {
        size_t n = e.args.size();
        bool is_secret = e.name == "secret";
        if (n < 1 || n > (is_secret ? 1u : 2u))
            fail(e.line, e.col, "'" + e.name + "' takes " + (is_secret ? "1 argument" : "1 or 2 arguments") +
                                    ", got " + std::to_string(n));
        const Expr& a = *e.args[0];
        Value name = eval(a, f);
        if (name.kind != Value::Str) fail(a.line, a.col, "'" + e.name + "' needs the name as a str, got " + describe_value(name));
        if (name.s.empty()) fail(a.line, a.col, "'" + e.name + "' needs a name that is not empty");
        if (is_secret) {
            Value v;
            v.kind = Value::Secret;
            v.s = name.s;
            return v;
        }
        Value fallback;
        if (n == 2) {
            fallback = eval(*e.args[1], f);        // checked even when the variable is set, so a typo shows up everywhere
            if (fallback.kind != Value::Str)
                fail(e.args[1]->line, e.args[1]->col, "the fallback of 'env' must be a str, got " + describe_value(fallback));
        }
        if (const char* got = std::getenv(name.s.c_str())) return Value::text(got);
        if (n == 2) return fallback;
        fail(e.line, e.col, "environment variable '" + name.s + "' is not set (env(\"" + name.s +
                                "\", \"...\") gives it a fallback)");
    }

    Value eval_call(const Expr& e, Frame& f) {
        size_t n = e.args.size();

        if (e.name == "env" || e.name == "secret") return eval_env(e, f);

        if (is_builtin(e.name)) {
            if (n != 1) fail(e.line, e.col, "'" + e.name + "' takes 1 argument, got " + std::to_string(n));
            const Expr& a = *e.args[0];
            Value v = eval(a, f);
            if (e.name == "range") {
                if (v.kind != Value::Int) fail(a.line, a.col, "'range' needs an int, got " + describe_value(v));
                if (v.i < 0) fail(a.line, a.col, "'range' needs a number that is not negative");
                if (v.i > 1000000) fail(a.line, a.col, "'range' is limited to 1000000 elements");
                Value out;
                out.kind = Value::List;
                for (int64_t k = 0; k < v.i; k++) out.items.push_back(Value::integer(k));
                return out;
            }
            if (e.name == "len") {
                if (v.kind == Value::Str) return Value::integer(static_cast<int64_t>(v.s.size()));
                if (v.kind == Value::List) return Value::integer(static_cast<int64_t>(v.items.size()));
                fail(a.line, a.col, "'len' needs a str or a list, got " + kind_an(v.kind));
            }
            if (v.kind == Value::Int) return Value::text(std::to_string(v.i));
            if (v.kind == Value::Bool) return Value::text(v.i ? "true" : "false");
            fail(a.line, a.col, "'str' needs an int or a bool, got " + kind_an(v.kind));
        }

        auto it = funcs_.find(e.name);
        if (it == funcs_.end()) fail(e.line, e.col, "undefined function '" + e.name + "'");
        const Func& fn = *it->second;
        if (n != fn.params.size())
            fail(e.line, e.col, "function '" + e.name + "' takes " + std::to_string(fn.params.size()) +
                                    " argument(s), got " + std::to_string(n));

        Frame callee;
        callee.func = &fn;
        for (size_t i = 0; i < n; i++) {
            const Expr& a = *e.args[i];
            Value v = eval(a, f);
            {
                std::string m = mismatch(fn.param_refs[i], v);
                if (!m.empty()) {
                    bool written = i < fn.param_explicit.size() && fn.param_explicit[i];
                    fail(a.line, a.col, "argument " + std::to_string(i + 1) + " of '" + e.name + "' must be " +
                                            type_phrase(fn.param_refs[i]) + ", got " + m +
                                            (written ? "" : " (parameter '" + fn.params[i] + "' has no type written, so it is an int)"));
                }
            }
            callee.vars[fn.params[i]] = std::move(v);
            callee.decl[fn.params[i]] = nullptr;
        }
        if (++depth_ > MAX_DEPTH) fail(e.line, e.col, "recursion too deep in '" + e.name + "'");
        const std::string* caller_file = cur_file_;
        cur_file_ = &fn.file;                      // errors inside the body point into the file that defined it
        exec_stmts(fn.body, callee);
        cur_file_ = caller_file;
        depth_--;
        return callee.returned ? std::move(callee.ret) : implicit_return(fn, e.line, e.col, false);
    }
};
