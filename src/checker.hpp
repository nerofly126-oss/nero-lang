#pragma once
#include "ast.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// "nero check": a static pass over the whole program (every function and every schema default, called or not).
// It only reports what it can PROVE is wrong. When it cannot tell a type (a plain { } record, a value that
// depends on a branch) it says nothing, so a clean result means "nothing provably wrong", not "correct".
// Messages match the evaluator's wording, so the same mistake reads the same in 'check' and in 'eval'.

struct Diagnostic {
    std::string file;
    int line = 0, col = 0;
    std::string message;
};

// What the checker knows about a value without running anything.
struct SType {
    enum Kind { Unknown, Int, Str, Bool, List, Record, Named, Secret } kind = Unknown;
    std::string name;                // Named: the schema's name
    std::shared_ptr<SType> elem;     // List: the element type, empty when it is not known
    std::vector<std::shared_ptr<SType>> choices; // possible types when branches differ
    // List written out as [a, b, c]: each distinct element type with the index of the first element that has it
    std::vector<std::pair<size_t, std::shared_ptr<SType>>> variants;
};

inline SType st(SType::Kind k) { SType t; t.kind = k; return t; }
inline SType st_named(const std::string& n) { SType t; t.kind = SType::Named; t.name = n; return t; }
inline SType st_list(const SType& e) {
    SType t;
    t.kind = SType::List;
    if (e.kind != SType::Unknown) t.elem = std::make_shared<SType>(e);
    return t;
}
inline bool st_known(const SType& t) { return t.kind != SType::Unknown; }

inline bool st_same(const SType& a, const SType& b);
inline void st_add_choice(SType& out, const SType& t);

inline void st_add_choice(SType& out, const SType& t) {
    const std::vector<std::shared_ptr<SType>>& choices = t.choices;
    if (!choices.empty()) {
        for (const auto& choice : choices) st_add_choice(out, *choice);
        return;
    }
    if (!st_known(t)) return;
    for (const auto& choice : out.choices)
        if (st_same(*choice, t)) return;
    out.choices.push_back(std::make_shared<SType>(t));
}

inline bool st_same(const SType& a, const SType& b) {
    if (a.kind != b.kind || a.name != b.name) return false;
    if (static_cast<bool>(a.elem) != static_cast<bool>(b.elem)) return false;
    if (static_cast<bool>(a.choices.empty()) != static_cast<bool>(b.choices.empty())) return false;
    if (a.choices.size() != b.choices.size()) return false;
    for (size_t i = 0; i < a.choices.size(); i++)
        if (!st_same(*a.choices[i], *b.choices[i])) return false;
    return !a.elem || st_same(*a.elem, *b.elem);
}

// the type both a and b fit, or Unknown when they differ
inline SType st_join(const SType& a, const SType& b) {
    if (!st_same(a, b)) {
        SType out = st(SType::Unknown);
        st_add_choice(out, a);
        st_add_choice(out, b);
        return out;
    }
    SType out = a;
    out.variants.clear();            // two lists with the same element type may still differ element by element
    return out;
}

inline std::string st_article(const std::string& name) {
    return std::string(std::strchr("AEIOUaeiou", name[0]) ? "an " : "a ") + name;
}

// "int", "[int]", "Port": a type as it would be written
inline std::string st_str(const SType& t) {
    switch (t.kind) {
        case SType::Int:    return "int";
        case SType::Str:    return "str";
        case SType::Bool:   return "bool";
        case SType::Secret: return "secret";
        case SType::Record: return "record";
        case SType::Named:  return t.name;
        case SType::List:   return t.elem ? "[" + st_str(*t.elem) + "]" : "list";
        case SType::Unknown: break;
    }
    return "?";
}

// "an int", "a Service", "a list of Port": a type in running text, like the evaluator's describe_value
inline std::string st_phrase(const SType& t) {
    if (!t.choices.empty()) return st_phrase(*t.choices[0]);
    switch (t.kind) {
        case SType::Named: return st_article(t.name);
        case SType::List:  return t.elem ? "a list of " + st_str(*t.elem) : "a list";
        default:           return st_article(st_str(t));
    }
}

// the same, but a schema counts as just "a record" (what the evaluator says about assignments, conditions, operators)
inline std::string st_kind_an(const SType& t) {
    if (!t.choices.empty()) return st_kind_an(*t.choices[0]);
    if (t.kind == SType::Named) return "a record";
    if (t.kind == SType::List) return "a list";
    return st_article(st_str(t));
}

inline SType::Kind st_k(const SType& t) { return t.kind == SType::Named ? SType::Record : t.kind; }

inline SType st_of(const TypeRef& r) {
    switch (r.kind) {
        case Type::Int:    return st(SType::Int);
        case Type::Str:    return st(SType::Str);
        case Type::Bool:   return st(SType::Bool);
        case Type::Secret: return st(SType::Secret);
        case Type::Record: return st(SType::Record);
        case Type::Named:  return st_named(r.name);
        case Type::List:   return r.elem ? st_list(st_of(*r.elem)) : st(SType::List);
    }
    return st(SType::Unknown);
}

inline bool st_holds_secret(const SType& t) {
    if (!t.choices.empty()) {
        for (const auto& choice : t.choices)
            if (st_holds_secret(*choice)) return true;
    }
    return t.kind == SType::Secret || (t.kind == SType::List && t.elem && st_holds_secret(*t.elem));
}

inline bool literal_zero(const Expr& e) {
    if (e.kind == Expr::Num) return e.value == 0;
    return e.kind == Expr::Unary && e.op == Tok::Minus && literal_zero(*e.lhs);
}

// "" when a value of type got fits the written type, otherwise a phrase for what was found (like the evaluator's mismatch)
inline std::string st_mismatch(const TypeRef& want, const SType& got) {
    if (!got.choices.empty()) {
        bool all_mismatch = true;
        std::string first_mismatch;
        for (const auto& choice : got.choices) {
            std::string mismatch = st_mismatch(want, *choice);
            if (mismatch.empty()) all_mismatch = false;
            else if (first_mismatch.empty()) first_mismatch = mismatch;
        }
        return all_mismatch ? first_mismatch : "";
    }
    if (!st_known(got)) return "";
    switch (want.kind) {
        case Type::Int:    return got.kind == SType::Int ? "" : st_phrase(got);
        case Type::Str:    return got.kind == SType::Str ? "" : st_phrase(got);
        case Type::Bool:   return got.kind == SType::Bool ? "" : st_phrase(got);
        case Type::Secret: return got.kind == SType::Secret ? "" : st_phrase(got);
        case Type::Record: return (got.kind == SType::Record || got.kind == SType::Named) ? "" : st_phrase(got);
        case Type::List:
            if (got.kind != SType::List) return st_phrase(got);
            if (!want.elem) return "";
            if (!got.variants.empty()) {       // written out: say which element, like the evaluator does
                size_t best = static_cast<size_t>(-1);
                std::string phrase;
                for (const auto& v : got.variants) {
                    std::string m = st_mismatch(*want.elem, *v.second);
                    if (!m.empty() && v.first < best) { best = v.first; phrase = m; }
                }
                if (!phrase.empty()) return "a list, but element " + std::to_string(best) + " is " + phrase;
            } else if (got.elem && !st_mismatch(*want.elem, *got.elem).empty()) {
                return st_phrase(got);
            }
            return "";
        case Type::Named:
            if (got.kind == SType::Named && got.name == want.name) return "";
            if (got.kind == SType::Record) return "a record (write " + want.name + " { ... })";
            return st_phrase(got);
    }
    return "";
}

class Checker {
public:
    std::vector<Diagnostic> check(const Program& prog) {
        diags_.clear();
        types_.clear();
        funcs_.clear();
        for (const TypeDecl& d : prog.types) types_[d.name] = &d;
        for (const Func& f : prog.funcs) {
            file_ = f.file;
            if (is_builtin(f.name)) error(f.line, f.col, "'" + f.name + "' is a builtin and cannot be redefined");
            else if (funcs_.count(f.name)) error(f.line, f.col, "function '" + f.name + "' is already defined");
            else funcs_[f.name] = &f;
        }
        for (const TypeDecl& d : prog.types)
            for (const FieldDecl& fd : d.fields)
                if (fd.def) check_default(fd);
        for (const Func& f : prog.funcs) check_func(f);

        file_ = prog.file;
        Scope top;
        collect_lets(prog.stmts, top);
        stmts(prog.stmts, top);

        std::stable_sort(diags_.begin(), diags_.end(), [](const Diagnostic& a, const Diagnostic& b) {
            if (a.file != b.file) return a.file < b.file;
            if (a.line != b.line) return a.line < b.line;
            return a.col < b.col;
        });
        return diags_;
    }

private:
    struct Scope {
        std::map<std::string, SType> vars;      // what we know about each variable, in the order the code is written
        std::set<std::string> declared;         // every name a 'let' or a parameter declares anywhere in the function
        std::set<std::string> params;
        std::set<std::string> reassigned;       // names that an assignment (x = ...) changes somewhere
        std::set<std::string> definite;         // names certainly declared before this point (to catch a second 'let')
        std::set<std::string> loops;            // loop variables that are active right now
        const Func* func = nullptr;
    };

    std::vector<Diagnostic> diags_;
    std::map<std::string, const TypeDecl*> types_;
    std::map<std::string, const Func*> funcs_;
    std::string file_;

    static bool is_builtin(const std::string& n) {
        return n == "len" || n == "str" || n == "range" || n == "env" || n == "secret";
    }

    void error(int line, int col, const std::string& msg) { diags_.push_back({file_, line, col, msg}); }

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

    // an assignment keeps a variable's kind but may swap a list for a list of something else, or one schema for another
    static SType generalize(const SType& t) {
        if (t.kind == SType::List) return st(SType::List);
        if (t.kind == SType::Named || t.kind == SType::Record) return st(SType::Record);
        return t;
    }

    static void collect_lets(const std::vector<StmtPtr>& body, Scope& sc) {
        for (const StmtPtr& s : body) {
            if (s->kind == Stmt::Let) sc.declared.insert(s->name);
            if (s->kind == Stmt::Assign) sc.reassigned.insert(s->name);
            collect_lets(s->body, sc);
            collect_lets(s->else_body, sc);
        }
    }

    // does this block always end in a 'return'?
    static bool always_returns(const std::vector<StmtPtr>& body) {
        for (const StmtPtr& s : body) {
            if (s->kind == Stmt::Return) return true;
            if (s->kind == Stmt::If && s->has_else && always_returns(s->body) && always_returns(s->else_body)) return true;
            if (s->kind == Stmt::While && s->expr->kind == Expr::Bool && s->expr->value) return true;   // while true { ... }
        }
        return false;
    }

    void check_default(const FieldDecl& fd) {
        file_ = fd.file;
        Scope none;
        SType t = expr(*fd.def, none);
        std::string m = st_mismatch(fd.type, t);
        if (!m.empty())
            error(fd.def->line, fd.def->col, "the default of field '" + fd.name + "' must be " +
                                                 st_phrase(st_of(fd.type)) + ", got " + m);
    }

    void check_func(const Func& f) {
        file_ = f.file;
        Scope sc;
        sc.func = &f;
        for (size_t i = 0; i < f.params.size(); i++) {
            if (sc.params.count(f.params[i])) error(f.line, f.col, "duplicate parameter '" + f.params[i] + "'");
            sc.params.insert(f.params[i]);
            sc.declared.insert(f.params[i]);
        }
        collect_lets(f.body, sc);
        for (size_t i = 0; i < f.params.size(); i++) {
            SType t = st_of(f.param_refs[i]);
            sc.vars[f.params[i]] = sc.reassigned.count(f.params[i]) ? generalize(t) : t;
        }
        stmts(f.body, sc);
        if ((f.ret_ref.kind == Type::Named || f.ret_ref.kind == Type::Secret) && !always_returns(f.body))
            error(f.line, f.col, "function '" + f.name + "' can end without returning a value of type " +
                                     (f.ret_ref.kind == Type::Secret ? std::string("secret") : f.ret_ref.name));
    }

    void stmts(const std::vector<StmtPtr>& body, Scope& sc) {
        for (const StmtPtr& s : body) stmt(*s, sc);
    }

    // a nested block: what it declares is not certain to exist after it
    void block(const std::vector<StmtPtr>& body, Scope& sc) {
        std::set<std::string> saved = sc.definite;
        stmts(body, sc);
        sc.definite = saved;
    }

    // a condition: a bool (or an int, which the evaluator accepts too)
    void cond(const Expr& e, Scope& sc, const char* what) {
        SType t = expr(e, sc);
        if (st_known(t) && t.kind != SType::Bool && t.kind != SType::Int)
            error(e.line, e.col, std::string(what) + " must be a bool, got " + st_kind_an(t));
    }

    void stmt(const Stmt& s, Scope& sc) {
        switch (s.kind) {
            case Stmt::Let: {
                SType t = expr(*s.expr, sc);
                if (sc.reassigned.count(s.name)) t = generalize(t);
                if (sc.definite.count(s.name) || sc.params.count(s.name) || sc.loops.count(s.name))
                    error(s.line, s.col, "'" + s.name + "' is already declared");
                sc.definite.insert(s.name);
                auto it = sc.vars.find(s.name);
                if (it == sc.vars.end()) sc.vars[s.name] = t;
                else it->second = st_join(it->second, t);
                break;
            }
            case Stmt::Assign: {
                SType t = expr(*s.expr, sc);
                auto it = sc.vars.find(s.name);
                if (it == sc.vars.end()) {
                    if (!sc.declared.count(s.name)) error(s.line, s.col, "undefined variable '" + s.name + "'");
                } else if (st_known(it->second) && st_known(t) && st_k(it->second) != st_k(t)) {
                    error(s.line, s.col, "'" + s.name + "' is " + st_kind_an(it->second) + ", cannot assign " + st_kind_an(t));
                }
                break;
            }
            case Stmt::Print:
            case Stmt::Emit:
            case Stmt::ExprStmt:
                expr(*s.expr, sc);
                break;
            case Stmt::Return: {
                if (!sc.func) {
                    error(s.line, s.col, "'return' outside a function");
                    if (s.expr) expr(*s.expr, sc);
                    break;
                }
                const Func& f = *sc.func;
                if (!s.expr) {
                    if (f.ret_ref.kind == Type::Named || f.ret_ref.kind == Type::Secret)
                        error(s.line, s.col, "a bare 'return' is not allowed in a function that returns " +
                                                 (f.ret_ref.kind == Type::Secret ? std::string("secret") : f.ret_ref.name));
                    break;
                }
                std::string m = st_mismatch(f.ret_ref, expr(*s.expr, sc));
                if (!m.empty())
                    error(s.line, s.col, "this function returns " + typeref_str(f.ret_ref) + ", but the value is " + m +
                                             (f.ret_explicit ? "" : " (no return type is written, so it is an int)"));
                break;
            }
            case Stmt::For: {
                SType xs = expr(*s.expr, sc);
                SType elem = st(SType::Unknown);
                if (st_known(xs) && xs.kind != SType::List)
                    error(s.expr->line, s.expr->col, "can only loop over a list, got " + st_phrase(xs));
                else if (xs.kind == SType::List && xs.elem) elem = *xs.elem;
                loop_body(s.name, elem, s.line, s.col, sc, [&] { block(s.body, sc); });
                break;
            }
            case Stmt::While:
                cond(*s.expr, sc, "a while condition");
                block(s.body, sc);
                break;
            case Stmt::If:
                cond(*s.expr, sc, "an if condition");
                block(s.body, sc);
                block(s.else_body, sc);
                break;
        }
    }

    // runs body with a loop variable in scope, and takes it out again afterwards
    template <class F>
    void loop_body(const std::string& name, const SType& elem, int line, int col, Scope& sc, F body) {
        if (sc.params.count(name) || sc.loops.count(name) || sc.definite.count(name))
            error(line, col, "'" + name + "' is already declared");
        auto prev = sc.vars.find(name);
        bool had = prev != sc.vars.end();
        SType saved = had ? prev->second : st(SType::Unknown);
        sc.vars[name] = sc.reassigned.count(name) ? generalize(elem) : elem;
        sc.loops.insert(name);
        body();
        sc.loops.erase(name);
        if (had) sc.vars[name] = saved;
        else sc.vars.erase(name);
    }

    SType var_type(const Expr& e, Scope& sc) {
        auto it = sc.vars.find(e.name);
        if (it != sc.vars.end()) return it->second;
        if (!sc.declared.count(e.name)) error(e.line, e.col, "undefined variable '" + e.name + "'");
        return st(SType::Unknown);
    }

    SType expr(const Expr& e, Scope& sc) {
        switch (e.kind) {
            case Expr::Num:  return st(SType::Int);
            case Expr::Str:  return st(SType::Str);
            case Expr::Bool: return st(SType::Bool);
            case Expr::Var:  return var_type(e, sc);
            case Expr::Unary: {
                SType t = expr(*e.lhs, sc);
                if (e.op == Tok::Not) {
                    if (st_known(t) && t.kind != SType::Bool)
                        error(e.lhs->line, e.lhs->col, "the operand of 'not' must be a bool, got " + st_kind_an(t));
                    return st(SType::Bool);
                }
                if (st_known(t) && t.kind != SType::Int)
                    error(e.lhs->line, e.lhs->col, "the operand of '-' must be an int, got " + st_kind_an(t));
                return st(SType::Int);
            }
            case Expr::IfExpr: {
                cond(*e.lhs, sc, "an if condition");
                SType a = expr(*e.rhs, sc), b = expr(*e.alt, sc);
                return st_join(a, b);
            }
            case Expr::Interp: {
                for (const ExprPtr& a : e.args) {
                    SType t = expr(*a, sc);
                    if (t.kind == SType::List || t.kind == SType::Record || t.kind == SType::Named || t.kind == SType::Secret)
                        error(a->line, a->col, "cannot put " + st_phrase(t) + " inside a string (only int, str and bool)");
                }
                return st(SType::Str);
            }
            case Expr::Comp: {
                SType xs = expr(*e.rhs, sc);
                SType elem = st(SType::Unknown);
                if (st_known(xs) && xs.kind != SType::List)
                    error(e.rhs->line, e.rhs->col, "can only loop over a list, got " + st_phrase(xs));
                else if (xs.kind == SType::List && xs.elem) elem = *xs.elem;
                SType out = st(SType::Unknown);
                loop_body(e.name, elem, e.line, e.col, sc, [&] {
                    if (e.alt) cond(*e.alt, sc, "a list filter");
                    out = expr(*e.lhs, sc);
                });
                return st_list(out);
            }
            case Expr::Binary: return binary(e, sc);
            case Expr::Call:   return call(e, sc);
            case Expr::List: {
                SType elem = st(SType::Unknown);
                std::vector<std::pair<size_t, std::shared_ptr<SType>>> variants;
                for (size_t k = 0; k < e.args.size(); k++) {
                    SType t = expr(*e.args[k], sc);
                    elem = k == 0 ? t : st_join(elem, t);
                    if (!st_known(t)) continue;
                    bool seen = false;
                    for (const auto& v : variants) seen = seen || st_same(*v.second, t);
                    if (!seen) variants.push_back({k, std::make_shared<SType>(t)});
                }
                SType out = st_list(elem);
                out.variants = variants;
                return out;
            }
            case Expr::Record: return e.name.empty() ? plain_record(e, sc) : typed_record(e, sc);
            case Expr::Field: {
                SType base = expr(*e.lhs, sc);
                if (!st_known(base) || base.kind == SType::Record) return st(SType::Unknown);
                if (base.kind != SType::Named) {
                    error(e.line, e.col, "cannot read field '" + e.name + "' of " + st_kind_an(base));
                    return st(SType::Unknown);
                }
                return field_of(base.name, e.name, e.line, e.col);
            }
            case Expr::Index: {
                SType base = expr(*e.lhs, sc);
                SType idx = expr(*e.rhs, sc);
                if (!st_known(base)) return st(SType::Unknown);
                if (base.kind == SType::List) {
                    if (st_known(idx) && idx.kind != SType::Int)
                        error(e.rhs->line, e.rhs->col, "a list index must be an int, got " + st_kind_an(idx));
                    return base.elem ? *base.elem : st(SType::Unknown);
                }
                if (base.kind == SType::Record || base.kind == SType::Named) {
                    if (st_known(idx) && idx.kind != SType::Str) {
                        error(e.rhs->line, e.rhs->col, "a record key must be a str, got " + st_kind_an(idx));
                        return st(SType::Unknown);
                    }
                    if (base.kind == SType::Named && e.rhs->kind == Expr::Str) return field_of(base.name, e.rhs->name, e.line, e.col);
                    return st(SType::Unknown);
                }
                error(e.line, e.col, "cannot index " + st_kind_an(base));
                return st(SType::Unknown);
            }
        }
        return st(SType::Unknown);
    }

    SType field_of(const std::string& type, const std::string& field, int line, int col) {
        auto it = types_.find(type);
        if (it == types_.end()) return st(SType::Unknown);
        std::vector<std::string> names;
        for (const FieldDecl& fd : it->second->fields) {
            if (fd.name == field) return st_of(fd.type);
            names.push_back(fd.name);
        }
        error(line, col, type + " " + no_field(field, names));
        return st(SType::Unknown);
    }

    SType plain_record(const Expr& e, Scope& sc) {
        std::set<std::string> seen;
        for (size_t k = 0; k < e.keys.size(); k++) {
            if (!seen.insert(e.keys[k]).second)
                error(e.key_pos[k].first, e.key_pos[k].second, "duplicate field '" + e.keys[k] + "'");
            expr(*e.args[k], sc);
        }
        return st(SType::Record);
    }

    // Name { field: value, ... }: unknown, duplicate and missing fields, and the type of every value
    SType typed_record(const Expr& e, Scope& sc) {
        auto it = types_.find(e.name);
        if (it == types_.end()) {
            for (const ExprPtr& a : e.args) expr(*a, sc);
            return st_named(e.name);
        }
        const TypeDecl& d = *it->second;
        std::vector<std::string> names;
        for (const FieldDecl& fd : d.fields) names.push_back(fd.name);
        std::set<std::string> have;
        for (size_t k = 0; k < e.keys.size(); k++) {
            SType t = expr(*e.args[k], sc);
            int kl = e.key_pos[k].first, kc = e.key_pos[k].second;
            const FieldDecl* fd = nullptr;
            for (const FieldDecl& cand : d.fields)
                if (cand.name == e.keys[k]) fd = &cand;
            if (!fd) { error(kl, kc, d.name + " " + no_field(e.keys[k], names)); continue; }
            if (!have.insert(e.keys[k]).second) { error(kl, kc, "duplicate field '" + e.keys[k] + "'"); continue; }
            std::string m = st_mismatch(fd->type, t);
            if (!m.empty())
                error(e.args[k]->line, e.args[k]->col, "field '" + e.keys[k] + "' of " + d.name + " must be " +
                                                           st_phrase(st_of(fd->type)) + ", got " + m);
        }
        std::string missing;
        int count = 0;
        for (const FieldDecl& fd : d.fields) {
            if (fd.def || have.count(fd.name)) continue;
            if (count++) missing += ", ";
            missing += "'" + fd.name + "' (" + st_phrase(st_of(fd.type)) + ")";
        }
        if (count) error(e.line, e.col, d.name + " is missing field" + (count > 1 ? "s " : " ") + missing);
        return st_named(d.name);
    }

    SType binary(const Expr& e, Scope& sc) {
        SType l = expr(*e.lhs, sc);
        SType r = expr(*e.rhs, sc);
        if (e.op == Tok::And || e.op == Tok::Or) {
            std::string name = e.op == Tok::And ? "'and'" : "'or'";
            if (st_known(l) && l.kind != SType::Bool)
                error(e.lhs->line, e.lhs->col, "both sides of " + name + " must be bools, got " + st_kind_an(l));
            if (st_known(r) && r.kind != SType::Bool)
                error(e.rhs->line, e.rhs->col, "both sides of " + name + " must be bools, got " + st_kind_an(r));
            return st(SType::Bool);
        }
        if (e.op == Tok::Eq || e.op == Tok::Ne) {
            if (st_holds_secret(l) || st_holds_secret(r)) error(e.line, e.col, "secrets cannot be compared");
            else if (st_known(l) && st_known(r) && st_k(l) != st_k(r))
                error(e.line, e.col, "cannot compare " + st_kind_an(l) + " with " + st_kind_an(r));
            return st(SType::Bool);
        }
        bool cmp = e.op == Tok::Lt || e.op == Tok::Le || e.op == Tok::Gt || e.op == Tok::Ge;
        if ((e.op == Tok::Slash || e.op == Tok::Percent) && literal_zero(*e.rhs))
            error(e.line, e.col, "division by zero");
        if (e.op == Tok::Plus && st_known(l) && st_known(r) && st_k(l) == st_k(r)) {
            if (l.kind == SType::Str) return st(SType::Str);
            if (l.kind == SType::List) return st_list(l.elem && r.elem ? st_join(*l.elem, *r.elem) : st(SType::Unknown));
        }
        if (st_known(l) && st_known(r) && (l.kind != SType::Int || r.kind != SType::Int)) {
            std::string msg = std::string("cannot use '") + op_str(e.op) + "' on " + st_kind_an(l) + " and " + st_kind_an(r);
            bool text_and_num = (l.kind == SType::Str && r.kind == SType::Int) || (l.kind == SType::Int && r.kind == SType::Str);
            if (e.op == Tok::Plus && text_and_num) msg += " (use str(...) to turn a number into text)";
            error(e.line, e.col, msg);
            return cmp ? st(SType::Bool) : st(SType::Unknown);
        }
        if (cmp) return st(SType::Bool);
        // '+' also joins strings and lists, so it is only an int when both sides are known ints; the rest are ints or an error
        if (e.op == Tok::Plus && !(l.kind == SType::Int && r.kind == SType::Int)) return st(SType::Unknown);
        return st(SType::Int);
    }

    SType call(const Expr& e, Scope& sc) {
        size_t n = e.args.size();
        std::vector<SType> args;
        for (const ExprPtr& a : e.args) args.push_back(expr(*a, sc));

        if (e.name == "env" || e.name == "secret") {
            bool is_secret = e.name == "secret";
            if (n < 1 || n > (is_secret ? 1u : 2u)) {
                error(e.line, e.col, "'" + e.name + "' takes " + (is_secret ? "1 argument" : "1 or 2 arguments") + ", got " + std::to_string(n));
            } else {
                if (st_known(args[0]) && args[0].kind != SType::Str)
                    error(e.args[0]->line, e.args[0]->col, "'" + e.name + "' needs the name as a str, got " + st_phrase(args[0]));
                else if (e.args[0]->kind == Expr::Str && e.args[0]->name.empty())
                    error(e.args[0]->line, e.args[0]->col, "'" + e.name + "' needs a name that is not empty");
                if (n == 2 && st_known(args[1]) && args[1].kind != SType::Str)
                    error(e.args[1]->line, e.args[1]->col, "the fallback of 'env' must be a str, got " + st_phrase(args[1]));
            }
            return is_secret ? st(SType::Secret) : st(SType::Str);
        }

        if (is_builtin(e.name)) {
            if (n != 1) {
                error(e.line, e.col, "'" + e.name + "' takes 1 argument, got " + std::to_string(n));
                return e.name == "range" ? st_list(st(SType::Int)) : st(e.name == "len" ? SType::Int : SType::Str);
            }
            const SType& a = args[0];
            const Expr& ae = *e.args[0];
            if (e.name == "range") {
                if (st_known(a) && a.kind != SType::Int) error(ae.line, ae.col, "'range' needs an int, got " + st_phrase(a));
                return st_list(st(SType::Int));
            }
            if (e.name == "len") {
                if (st_known(a) && a.kind != SType::Str && a.kind != SType::List)
                    error(ae.line, ae.col, "'len' needs a str or a list, got " + st_kind_an(a));
                return st(SType::Int);
            }
            if (st_known(a) && a.kind != SType::Int && a.kind != SType::Bool)
                error(ae.line, ae.col, "'str' needs an int or a bool, got " + st_kind_an(a));
            return st(SType::Str);
        }

        auto it = funcs_.find(e.name);
        if (it == funcs_.end()) {
            error(e.line, e.col, "undefined function '" + e.name + "'");
            return st(SType::Unknown);
        }
        const Func& fn = *it->second;
        if (n != fn.params.size()) {
            error(e.line, e.col, "function '" + e.name + "' takes " + std::to_string(fn.params.size()) +
                                     " argument(s), got " + std::to_string(n));
        } else {
            for (size_t i = 0; i < n; i++) {
                std::string m = st_mismatch(fn.param_refs[i], args[i]);
                if (m.empty()) continue;
                bool written = i < fn.param_explicit.size() && fn.param_explicit[i];
                error(e.args[i]->line, e.args[i]->col,
                      "argument " + std::to_string(i + 1) + " of '" + e.name + "' must be " + st_phrase(st_of(fn.param_refs[i])) +
                          ", got " + m + (written ? "" : " (parameter '" + fn.params[i] + "' has no type written, so it is an int)"));
            }
        }
        return st_of(fn.ret_ref);
    }
};
