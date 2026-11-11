#pragma once
#include "lexer.hpp"
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

enum class Type { Int, Str, Bool, List, Record, Named, Secret };

inline const char* type_name(Type t) {
    switch (t) {
        case Type::Int:    return "int";
        case Type::Str:    return "str";
        case Type::Bool:   return "bool";
        case Type::List:   return "list";
        case Type::Record: return "record";
        case Type::Named:  return "named";
        case Type::Secret: return "secret";
    }
    return "?";
}

// A type as written in the source: int, str, bool, list, record, [T] or the name of a schema.
struct TypeRef {
    Type kind = Type::Int;
    std::string name;                  // Named: the schema's name
    std::shared_ptr<TypeRef> elem;     // List: the element type for [T], empty for a plain list
    int line = 0, col = 0;
};

inline std::string typeref_str(const TypeRef& t) {
    if (t.kind == Type::Named) return t.name;
    if (t.kind == Type::List && t.elem) return "[" + typeref_str(*t.elem) + "]";
    return type_name(t.kind);
}

inline size_t edit_distance(const std::string& a, const std::string& b) {
    std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); j++) prev[j] = j;
    for (size_t i = 1; i <= a.size(); i++) {
        cur[0] = i;
        for (size_t j = 1; j <= b.size(); j++) {
            size_t sub = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            cur[j] = std::min(std::min(prev[j] + 1, cur[j - 1] + 1), sub);
        }
        prev = cur;
    }
    return prev[b.size()];
}

// The candidate that looks like a typo of name, or "" when nothing is close enough.
inline std::string closest(const std::string& name, const std::vector<std::string>& candidates) {
    size_t limit = name.size() <= 4 ? 1 : 2;
    std::string best;
    size_t best_d = 0;
    for (const std::string& c : candidates) {
        size_t d = edit_distance(name, c);
        if (d == 0 || d > limit || d >= name.size()) continue;
        if (best.empty() || d < best_d) { best = c; best_d = d; }
    }
    return best;
}

struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

struct Expr {
    enum Kind { Num, Str, Var, Unary, Binary, Call, Bool, List, Record, Field, Index, IfExpr, Interp, Comp } kind;
    int64_t value = 0;          // Num
    std::string name;           // Var, Call, and the text of a Str literal
    Tok op = Tok::End;          // Unary, Binary
    ExprPtr lhs, rhs;           // Binary uses both, Unary uses lhs
    ExprPtr alt;                // IfExpr: the else value.  Comp: the optional filter
    std::vector<ExprPtr> args;  // Call arguments, List elements, Record values
    std::vector<std::string> keys;              // Record field names
    std::vector<std::pair<int, int>> key_pos;   // Record: line and column of each field name
    int line = 0, col = 0;
};

struct Stmt {
    enum Kind { Let, Assign, Print, While, If, Return, ExprStmt, Emit, For } kind;
    std::string name;           // Let, Assign
    ExprPtr expr;               // value, or the condition for While and If (Return may have none)
    std::vector<StmtPtr> body;
    std::vector<StmtPtr> else_body;
    bool has_else = false;
    int line = 0, col = 0;
};

struct FieldDecl {
    std::string name;
    TypeRef type;
    ExprPtr def;                  // default value, empty when the field is required
    std::string file;             // the source file it was written in ("" when there is none)
    int line = 0, col = 0;
};

struct TypeDecl {
    std::string name;
    std::vector<FieldDecl> fields;
    std::string file;
    int line = 0, col = 0;
};

struct Func {
    std::string name;
    std::vector<std::string> params;
    std::vector<Type> param_types;
    std::vector<bool> param_explicit;   // was the parameter's type written down
    std::vector<TypeRef> param_refs;    // the same types with their details (schema names, [T])
    Type ret = Type::Int;
    TypeRef ret_ref;
    bool ret_explicit = false;
    std::vector<StmtPtr> body;
    std::string file;
    int line = 0, col = 0;
};

struct ImportDecl {
    std::string path;           // as written in the source
    int line = 0, col = 0;
};

struct Program {
    std::vector<ImportDecl> imports;   // only the file that was run keeps these, after loading
    std::vector<TypeDecl> types;       // after loading: the types of every imported file too
    std::vector<Func> funcs;
    std::vector<StmtPtr> stmts;
    std::string file;                  // the file the statements came from
};

inline const char* op_str(Tok t) {
    switch (t) {
        case Tok::Plus:    return "+";
        case Tok::Minus:   return "-";
        case Tok::Star:    return "*";
        case Tok::Slash:   return "/";
        case Tok::Percent: return "%";
        case Tok::Eq:      return "==";
        case Tok::Ne:      return "!=";
        case Tok::Lt:      return "<";
        case Tok::Le:      return "<=";
        case Tok::Gt:      return ">";
        case Tok::Ge:      return ">=";
        case Tok::And:     return "and";
        case Tok::Or:      return "or";
        case Tok::Not:     return "not";
        default:           return "?";
    }
}

inline std::string quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out + "\"";
}

inline std::string dump_expr(const Expr& e) {
    switch (e.kind) {
        case Expr::Num:    return std::to_string(e.value);
        case Expr::Str:    return quote(e.name);
        case Expr::Var:    return e.name;
        case Expr::Unary:  return std::string("(") + op_str(e.op) + " " + dump_expr(*e.lhs) + ")";
        case Expr::Binary: return std::string("(") + op_str(e.op) + " " + dump_expr(*e.lhs) + " " + dump_expr(*e.rhs) + ")";
        case Expr::Bool:   return e.value ? "true" : "false";
        case Expr::List: {
            std::string s = "(list";
            for (const ExprPtr& a : e.args) s += " " + dump_expr(*a);
            return s + ")";
        }
        case Expr::Record: {
            std::string s = "(record";
            if (!e.name.empty()) s += " " + e.name;   // built from a schema
            for (size_t i = 0; i < e.keys.size(); i++) s += " " + quote(e.keys[i]) + " " + dump_expr(*e.args[i]);
            return s + ")";
        }
        case Expr::Field:  return "(. " + dump_expr(*e.lhs) + " " + e.name + ")";
        case Expr::Index:  return "(index " + dump_expr(*e.lhs) + " " + dump_expr(*e.rhs) + ")";
        case Expr::IfExpr: return "(if " + dump_expr(*e.lhs) + " " + dump_expr(*e.rhs) + " " + dump_expr(*e.alt) + ")";
        case Expr::Interp: {
            std::string s = "(interp";
            for (const ExprPtr& a : e.args) s += " " + dump_expr(*a);
            return s + ")";
        }
        case Expr::Comp: {   // [lhs for name in rhs if alt]
            std::string s = "(comp " + dump_expr(*e.lhs) + " " + e.name + " " + dump_expr(*e.rhs);
            if (e.alt) s += " " + dump_expr(*e.alt);
            return s + ")";
        }
        case Expr::Call: {
            std::string s = "(call " + e.name;
            for (const ExprPtr& a : e.args) s += " " + dump_expr(*a);
            return s + ")";
        }
    }
    return "";
}

inline void dump_stmts(const std::vector<StmtPtr>& stmts, int indent, std::ostream& os) {
    std::string pad(static_cast<size_t>(indent), ' ');
    for (const StmtPtr& sp : stmts) {
        const Stmt& s = *sp;
        switch (s.kind) {
            case Stmt::Let:
                os << pad << "let " << s.name << " := " << dump_expr(*s.expr) << "\n";
                break;
            case Stmt::Assign:
                os << pad << "assign " << s.name << " := " << dump_expr(*s.expr) << "\n";
                break;
            case Stmt::Print:
                os << pad << "print " << dump_expr(*s.expr) << "\n";
                break;
            case Stmt::Return:
                os << pad << "return";
                if (s.expr) os << " " << dump_expr(*s.expr);
                os << "\n";
                break;
            case Stmt::Emit:
                os << pad << "emit " << dump_expr(*s.expr) << "\n";
                break;
            case Stmt::For:
                os << pad << "for " << s.name << " in " << dump_expr(*s.expr) << "\n";
                dump_stmts(s.body, indent + 2, os);
                break;
            case Stmt::ExprStmt:
                os << pad << "do " << dump_expr(*s.expr) << "\n";
                break;
            case Stmt::While:
                os << pad << "while " << dump_expr(*s.expr) << "\n";
                dump_stmts(s.body, indent + 2, os);
                break;
            case Stmt::If:
                os << pad << "if " << dump_expr(*s.expr) << "\n";
                dump_stmts(s.body, indent + 2, os);
                if (s.has_else) {
                    os << pad << "else\n";
                    dump_stmts(s.else_body, indent + 2, os);
                }
                break;
        }
    }
}

inline std::string dump_program(const Program& p) {
    std::ostringstream os;
    for (const ImportDecl& im : p.imports) os << "import " << quote(im.path) << "\n";
    for (const TypeDecl& d : p.types) {
        os << "type " << d.name << "\n";
        for (const FieldDecl& fd : d.fields) {
            os << "  " << fd.name << ": " << typeref_str(fd.type);
            if (fd.def) os << " = " << dump_expr(*fd.def);
            os << "\n";
        }
    }
    for (const Func& f : p.funcs) {
        os << "fn " << f.name << "(";
        for (size_t i = 0; i < f.params.size(); i++)
            os << (i ? ", " : "") << f.params[i] << ": " << typeref_str(f.param_refs[i]);
        os << ") -> " << typeref_str(f.ret_ref) << "\n";
        dump_stmts(f.body, 2, os);
    }
    dump_stmts(p.stmts, 0, os);
    return os.str();
}
