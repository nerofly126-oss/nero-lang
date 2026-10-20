#pragma once
#include "lexer.hpp"
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

struct Expr {
    enum Kind { Num, Var, Unary, Binary } kind;
    int64_t value = 0;          // Num
    std::string name;           // Var
    Tok op = Tok::End;          // Unary, Binary
    ExprPtr lhs, rhs;           // Binary uses both, Unary uses lhs
    int line = 0, col = 0;
};

struct Stmt {
    enum Kind { Let, Assign, Print, While, If } kind;
    std::string name;           // Let, Assign
    ExprPtr expr;               // value, or the condition for While and If
    std::vector<StmtPtr> body;
    std::vector<StmtPtr> else_body;
    bool has_else = false;
    int line = 0, col = 0;
};

struct Program {
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

inline std::string dump_expr(const Expr& e) {
    switch (e.kind) {
        case Expr::Num:    return std::to_string(e.value);
        case Expr::Var:    return e.name;
        case Expr::Unary:  return std::string("(") + op_str(e.op) + " " + dump_expr(*e.lhs) + ")";
        case Expr::Binary: return std::string("(") + op_str(e.op) + " " + dump_expr(*e.lhs) + " " + dump_expr(*e.rhs) + ")";
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
    dump_stmts(p.stmts, 0, os);
    return os.str();
}
