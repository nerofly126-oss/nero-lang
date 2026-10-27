#pragma once
#include "lexer.hpp"
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

enum class Type { Int, Str };

inline const char* type_name(Type t) { return t == Type::Int ? "int" : "str"; }

struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

struct Expr {
    enum Kind { Num, Str, Var, Unary, Binary, Call } kind;
    int64_t value = 0;          // Num
    std::string name;           // Var, Call, and the text of a Str literal
    Tok op = Tok::End;          // Unary, Binary
    ExprPtr lhs, rhs;           // Binary uses both, Unary uses lhs
    std::vector<ExprPtr> args;  // Call
    int line = 0, col = 0;
};

struct Stmt {
    enum Kind { Let, Assign, Print, While, If, Return, ExprStmt } kind;
    std::string name;           // Let, Assign
    ExprPtr expr;               // value, or the condition for While and If (Return may have none)
    std::vector<StmtPtr> body;
    std::vector<StmtPtr> else_body;
    bool has_else = false;
    int line = 0, col = 0;
};

struct Func {
    std::string name;
    std::vector<std::string> params;
    std::vector<Type> param_types;
    Type ret = Type::Int;
    std::vector<StmtPtr> body;
    int line = 0, col = 0;
};

struct Program {
    std::vector<Func> funcs;
    std::vector<StmtPtr> stmts;
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
    for (const Func& f : p.funcs) {
        os << "fn " << f.name << "(";
        for (size_t i = 0; i < f.params.size(); i++)
            os << (i ? ", " : "") << f.params[i] << ": " << type_name(f.param_types[i]);
        os << ") -> " << type_name(f.ret) << "\n";
        dump_stmts(f.body, 2, os);
    }
    dump_stmts(p.stmts, 0, os);
    return os.str();
}
