#pragma once
#include "ast.hpp"
#include "grimoire.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

using NeroFn = void (*)();

// tests set this to capture print output instead of writing to stdout
inline std::vector<int64_t>* nero_sink = nullptr;

static void nero_print(int64_t v) {
    if (nero_sink) nero_sink->push_back(v);
    else std::printf("%lld\n", static_cast<long long>(v));
}

[[noreturn]] static void nero_div_zero() {
    std::fflush(stdout);
    std::fprintf(stderr, "runtime error: division by zero\n");
    std::exit(1);
}

// Machine model
//   rax        result of the expression being evaluated
//   rcx        right operand of a binary operation
//   rbp        frame base, variable k lives at [rbp - 8*(k+1)]
// One flat scope: a variable is visible from its 'let' to the end of the program,
// even if the 'let' sits inside a block. All slots start at zero.
class CodeGen {
public:
    explicit CodeGen(Grimoire& g) : g_(g) {}

    NeroFn compile(const Program& prog) {
        int nslots = count_lets(prog.stmts);
        int frame = (nslots * 8 + 15) & ~15;

        g_.push(RBP);
        g_.mov(RBP, RSP);
        g_.sub_imm(RSP, frame);
        for (int i = 0; i < nslots; i++) {
            g_.raw({0x48, 0xC7, 0x85});            // mov qword [rbp+disp32], imm32
            disp(-8 * (i + 1));
            g_.raw({0, 0, 0, 0});
        }

        gen_stmts(prog.stmts);

        g_.raw({0xC9});                            // leave
        g_.ret();
        return g_.finalize<NeroFn>();
    }

private:
    Grimoire& g_;
    std::unordered_map<std::string, int> slots_;

    [[noreturn]] static void fail(int line, int col, const std::string& msg) {
        throw std::runtime_error(std::to_string(line) + ":" + std::to_string(col) + ": " + msg);
    }

    static int count_lets(const std::vector<StmtPtr>& stmts) {
        int n = 0;
        for (const StmtPtr& s : stmts) {
            if (s->kind == Stmt::Let) n++;
            n += count_lets(s->body);
            n += count_lets(s->else_body);
        }
        return n;
    }

    void disp(int32_t d) {
        for (int i = 0; i < 4; i++) g_.raw({(d >> (8 * i)) & 0xFF});
    }

    void load_slot(int slot) {                     // mov rax, [rbp+disp32]
        g_.raw({0x48, 0x8B, 0x85});
        disp(-8 * (slot + 1));
    }

    void store_slot(int slot) {                    // mov [rbp+disp32], rax
        g_.raw({0x48, 0x89, 0x85});
        disp(-8 * (slot + 1));
    }

    int lookup(const std::string& name, int line, int col) {
        auto it = slots_.find(name);
        if (it == slots_.end()) fail(line, col, "undefined variable '" + name + "'");
        return it->second;
    }

    void gen_stmts(const std::vector<StmtPtr>& v) {
        for (const StmtPtr& s : v) gen_stmt(*s);
    }

    void gen_stmt(const Stmt& s) {
        switch (s.kind) {
            case Stmt::Let: {
                gen_expr(*s.expr);                 // evaluate first, so "let x = x" is an error
                if (slots_.count(s.name)) fail(s.line, s.col, "'" + s.name + "' is already declared");
                int slot = static_cast<int>(slots_.size());
                slots_[s.name] = slot;
                store_slot(slot);
                break;
            }
            case Stmt::Assign: {
                gen_expr(*s.expr);
                store_slot(lookup(s.name, s.line, s.col));
                break;
            }
            case Stmt::Print:
                gen_expr(*s.expr);
                g_.mov(RDI, RAX);
                g_.mov_imm(RAX, reinterpret_cast<int64_t>(&nero_print));
                g_.call(RAX);
                break;
            case Stmt::While: {
                Label top, end;
                g_.bind(top);
                gen_expr(*s.expr);
                g_.cmp_imm(RAX, 0);
                g_.jcc(JE, end);
                gen_stmts(s.body);
                g_.jmp(top);
                g_.bind(end);
                break;
            }
            case Stmt::If: {
                Label else_l, end_l;
                gen_expr(*s.expr);
                g_.cmp_imm(RAX, 0);
                g_.jcc(JE, else_l);
                gen_stmts(s.body);
                if (s.has_else) {
                    g_.jmp(end_l);
                    g_.bind(else_l);
                    gen_stmts(s.else_body);
                    g_.bind(end_l);
                } else {
                    g_.bind(else_l);
                }
                break;
            }
        }
    }

    void gen_expr(const Expr& e) {
        switch (e.kind) {
            case Expr::Num:
                g_.mov_imm(RAX, e.value);
                break;
            case Expr::Var:
                load_slot(lookup(e.name, e.line, e.col));
                break;
            case Expr::Unary:
                gen_expr(*e.lhs);
                g_.raw({0x48, 0xF7, 0xD8});        // neg rax
                break;
            case Expr::Binary:
                gen_expr(*e.lhs);
                g_.push(RAX);
                gen_expr(*e.rhs);
                g_.mov(RCX, RAX);
                g_.pop(RAX);                       // rax = left, rcx = right
                gen_binop(e);
                break;
        }
    }

    void compare(int setcc) {
        g_.cmp(RAX, RCX);
        g_.raw({0x0F, setcc, 0xC0});               // setcc al
        g_.raw({0x0F, 0xB6, 0xC0});                // movzx eax, al
    }

    void gen_binop(const Expr& e) {
        switch (e.op) {
            case Tok::Plus:  g_.add(RAX, RCX); break;
            case Tok::Minus: g_.sub(RAX, RCX); break;
            case Tok::Star:  g_.imul(RAX, RCX); break;
            case Tok::Slash:
            case Tok::Percent: {
                Label ok;
                g_.raw({0x48, 0x85, 0xC9});        // test rcx, rcx
                g_.jcc(JNE, ok);
                g_.raw({0x48, 0x83, 0xE4, 0xF0});  // and rsp, -16 (we never come back)
                g_.mov_imm(RAX, reinterpret_cast<int64_t>(&nero_div_zero));
                g_.call(RAX);
                g_.bind(ok);
                g_.raw({0x48, 0x99});              // cqo
                g_.raw({0x48, 0xF7, 0xF9});        // idiv rcx
                if (e.op == Tok::Percent) g_.mov(RAX, RDX);
                break;
            }
            case Tok::Eq: compare(0x94); break;
            case Tok::Ne: compare(0x95); break;
            case Tok::Lt: compare(0x9C); break;
            case Tok::Le: compare(0x9E); break;
            case Tok::Gt: compare(0x9F); break;
            case Tok::Ge: compare(0x9D); break;
            default: fail(e.line, e.col, "unknown operator");
        }
    }
};
