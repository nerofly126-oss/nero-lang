#pragma once
#include "ast.hpp"
#include "grimoire.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using NeroFn = void (*)();

// tests set these to capture print output instead of writing to stdout
inline std::vector<int64_t>* nero_sink = nullptr;          // ints only
inline std::vector<std::string>* nero_text = nullptr;      // everything, as text

// ---- runtime helpers called from generated code ----
// strings are plain NUL-terminated bytes, a null pointer counts as ""
static const char* nz(const char* s) { return s ? s : ""; }

static std::string an(Type t) { return t == Type::Int ? "an int" : "a str"; }

static void nero_print(int64_t v) {
    if (nero_sink) nero_sink->push_back(v);
    if (nero_text) nero_text->push_back(std::to_string(v));
    if (!nero_sink && !nero_text) std::printf("%lld\n", static_cast<long long>(v));
}

static void nero_print_str(const char* s) {
    s = nz(s);
    if (nero_text) nero_text->push_back(s);
    else std::printf("%s\n", s);
}

static const char* nero_concat(const char* a, const char* b) {
    a = nz(a);
    b = nz(b);
    size_t la = std::strlen(a), lb = std::strlen(b);
    char* r = static_cast<char*>(std::malloc(la + lb + 1));   // never freed, fine for scripts
    if (!r) std::abort();
    std::memcpy(r, a, la);
    std::memcpy(r + la, b, lb + 1);
    return r;
}

static int64_t nero_streq(const char* a, const char* b) { return std::strcmp(nz(a), nz(b)) == 0; }
static int64_t nero_len(const char* s) { return static_cast<int64_t>(std::strlen(nz(s))); }

static const char* nero_str(int64_t v) {
    char* r = static_cast<char*>(std::malloc(32));
    if (!r) std::abort();
    std::snprintf(r, 32, "%lld", static_cast<long long>(v));
    return r;
}

[[noreturn]] static void nero_div_zero() {
    std::fflush(stdout);
    std::fprintf(stderr, "runtime error: division by zero\n");
    std::exit(1);
}

// Machine model
//   rax        result of the expression being evaluated (and the return value)
//   rcx        right operand of a binary operation
//   rbp        frame base, variable k lives at [rbp - 8*(k+1)]
// Values are 64 bits: an int, or a pointer to string bytes. Types are checked at
// compile time, so a str can never be added to an int by accident.
// String literals are embedded after the code and reached with a rip-relative lea.
// Calling convention: System V, first six arguments in rdi, rsi, rdx, rcx, r8, r9.
// Every function has its own scope, parameters are its first variables, and all
// other slots start at zero. A 'let' stays visible to the end of its function,
// even if it sits inside a block. There are no globals.
class CodeGen {
public:
    explicit CodeGen(Grimoire& g) : g_(g) {}

    NeroFn compile(const Program& prog) {
        for (const Func& f : prog.funcs) {
            if (find_builtin(f.name)) fail(f.line, f.col, "'" + f.name + "' is a builtin and cannot be redefined");
            if (funcs_.count(f.name)) fail(f.line, f.col, "function '" + f.name + "' is already defined");
            if (f.params.size() > 6) fail(f.line, f.col, "function '" + f.name + "' has too many parameters (max 6)");
            FuncInfo& fi = funcs_[f.name];
            fi.params = f.param_types;
            fi.ret = f.ret;
        }

        emit_function(prog.stmts, {}, {}, Type::Int, false, 0, 0);       // the main program comes first
        for (const Func& f : prog.funcs) {
            g_.bind(funcs_[f.name].label);
            emit_function(f.body, f.params, f.param_types, f.ret, true, f.line, f.col);
        }

        for (const std::unique_ptr<Lit>& lit : lits_) {                  // string data goes after all the code
            g_.bind(lit->label);
            for (char c : lit->text) g_.raw({static_cast<int>(static_cast<unsigned char>(c))});
            g_.raw({0});
        }
        return g_.finalize<NeroFn>();
    }

private:
    struct FuncInfo {
        Label label;
        std::vector<Type> params;
        Type ret = Type::Int;
    };

    struct Var {
        int slot;
        Type type;
    };

    struct Lit {
        Label label;
        std::string text;
    };

    struct Builtin {
        const char* name;
        Type param;
        Type ret;
        int64_t fn;
    };

    Grimoire& g_;
    std::unordered_map<std::string, FuncInfo> funcs_;
    std::unordered_map<std::string, Var> slots_;
    std::vector<std::unique_ptr<Lit>> lits_;
    std::unordered_map<std::string, Lit*> lit_index_;
    int depth_ = 0;          // 8-byte values pushed on the stack by the expression being evaluated
    bool in_func_ = false;
    Type ret_type_ = Type::Int;

    static constexpr Reg ARG_REGS[6] = {RDI, RSI, RDX, RCX, R8, R9};

    static const Builtin* find_builtin(const std::string& name) {
        static const Builtin builtins[] = {
            {"len", Type::Str, Type::Int, reinterpret_cast<int64_t>(&nero_len)},
            {"str", Type::Int, Type::Str, reinterpret_cast<int64_t>(&nero_str)},
        };
        for (const Builtin& b : builtins)
            if (name == b.name) return &b;
        return nullptr;
    }

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

    void store_reg(Reg r, int slot) {              // mov [rbp+disp32], r
        g_.raw({0x48 | ((r >> 3) << 2), 0x89, 0x85 | ((r & 7) << 3)});
        disp(-8 * (slot + 1));
    }

    void zero_slot(int slot) {                     // mov qword [rbp+disp32], 0
        g_.raw({0x48, 0xC7, 0x85});
        disp(-8 * (slot + 1));
        g_.raw({0, 0, 0, 0});
    }

    Lit& intern(const std::string& text) {
        auto it = lit_index_.find(text);
        if (it != lit_index_.end()) return *it->second;
        lits_.push_back(std::make_unique<Lit>());
        lits_.back()->text = text;
        lit_index_[text] = lits_.back().get();
        return *lits_.back();
    }

    // call a C helper, keeping the stack 16-byte aligned even when temporaries are pending
    void call_c(int64_t fn) {
        bool pad = (depth_ % 2) != 0;
        g_.mov_imm(RAX, fn);
        if (pad) g_.raw({0x48, 0x83, 0xEC, 0x08});   // sub rsp, 8
        g_.call(RAX);
        if (pad) g_.raw({0x48, 0x83, 0xC4, 0x08});   // add rsp, 8
    }

    Var& lookup(const std::string& name, int line, int col) {
        auto it = slots_.find(name);
        if (it == slots_.end()) fail(line, col, "undefined variable '" + name + "'");
        return it->second;
    }

    void emit_function(const std::vector<StmtPtr>& body, const std::vector<std::string>& params,
                       const std::vector<Type>& types, Type ret, bool in_func, int line, int col) {
        slots_.clear();
        depth_ = 0;
        in_func_ = in_func;
        ret_type_ = ret;

        int nparams = static_cast<int>(params.size());
        for (int i = 0; i < nparams; i++) {
            if (slots_.count(params[i])) fail(line, col, "duplicate parameter '" + params[i] + "'");
            slots_[params[i]] = Var{i, types[i]};
        }
        int nslots = nparams + count_lets(body);
        int frame = (nslots * 8 + 15) & ~15;

        g_.push(RBP);
        g_.mov(RBP, RSP);
        g_.sub_imm(RSP, frame);
        for (int i = nparams; i < nslots; i++) zero_slot(i);
        for (int i = 0; i < nparams; i++) store_reg(ARG_REGS[i], i);

        gen_stmts(body);

        if (ret == Type::Str) g_.lea(RAX, intern("").label);   // falling off the end returns "" or 0
        else g_.mov_imm(RAX, 0);
        g_.raw({0xC9});                            // leave
        g_.ret();
    }

    void gen_stmts(const std::vector<StmtPtr>& v) {
        for (const StmtPtr& s : v) gen_stmt(*s);
    }

    void need_int(const Expr& e, Type t, const char* what) {
        if (t != Type::Int) fail(e.line, e.col, std::string(what) + " must be an int, got " + type_name(t));
    }

    void gen_stmt(const Stmt& s) {
        switch (s.kind) {
            case Stmt::Let: {
                Type t = gen_expr(*s.expr);        // evaluate first, so "let x = x" is an error
                if (slots_.count(s.name)) fail(s.line, s.col, "'" + s.name + "' is already declared");
                int slot = static_cast<int>(slots_.size());
                slots_[s.name] = Var{slot, t};
                store_slot(slot);
                break;
            }
            case Stmt::Assign: {
                Type t = gen_expr(*s.expr);
                Var& v = lookup(s.name, s.line, s.col);
                if (t != v.type)
                    fail(s.line, s.col, "'" + s.name + "' is " + an(v.type) + ", cannot assign " + an(t));
                store_slot(v.slot);
                break;
            }
            case Stmt::Print: {
                Type t = gen_expr(*s.expr);
                g_.mov(RDI, RAX);
                if (t == Type::Str) call_c(reinterpret_cast<int64_t>(&nero_print_str));
                else call_c(reinterpret_cast<int64_t>(&nero_print));
                break;
            }
            case Stmt::ExprStmt:
                gen_expr(*s.expr);
                break;
            case Stmt::Return: {
                if (!in_func_) fail(s.line, s.col, "'return' outside a function");
                if (s.expr) {
                    Type t = gen_expr(*s.expr);
                    if (t != ret_type_)
                        fail(s.line, s.col, std::string("this function returns ") + type_name(ret_type_) +
                                                ", but the value is " + an(t));
                } else {
                    if (ret_type_ != Type::Int) fail(s.line, s.col, "this function must return a str");
                    g_.mov_imm(RAX, 0);
                }
                g_.raw({0xC9});                    // leave
                g_.ret();
                break;
            }
            case Stmt::While: {
                Label top, end;
                g_.bind(top);
                need_int(*s.expr, gen_expr(*s.expr), "a while condition");
                g_.cmp_imm(RAX, 0);
                g_.jcc(JE, end);
                gen_stmts(s.body);
                g_.jmp(top);
                g_.bind(end);
                break;
            }
            case Stmt::If: {
                Label else_l, end_l;
                need_int(*s.expr, gen_expr(*s.expr), "an if condition");
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

    Type gen_expr(const Expr& e) {
        switch (e.kind) {
            case Expr::Num:
                g_.mov_imm(RAX, e.value);
                return Type::Int;
            case Expr::Str:
                g_.lea(RAX, intern(e.name).label);
                return Type::Str;
            case Expr::Var: {
                Var& v = lookup(e.name, e.line, e.col);
                load_slot(v.slot);
                return v.type;
            }
            case Expr::Unary:
                need_int(*e.lhs, gen_expr(*e.lhs), "the operand of '-'");
                g_.raw({0x48, 0xF7, 0xD8});        // neg rax
                return Type::Int;
            case Expr::Binary:
                return gen_binary(e);
            case Expr::Call:
                return gen_call(e);
        }
        return Type::Int;
    }

    Type gen_binary(const Expr& e) {
        Type tl = gen_expr(*e.lhs);
        g_.push(RAX);
        depth_++;
        Type tr = gen_expr(*e.rhs);
        g_.mov(RCX, RAX);
        g_.pop(RAX);                               // rax = left, rcx = right
        depth_--;

        if (tl != tr) {
            std::string msg = std::string("cannot use '") + op_str(e.op) + "' on " + an(tl) +
                              " and " + an(tr);
            if (e.op == Tok::Plus) msg += " (use str(...) to turn a number into text)";
            fail(e.line, e.col, msg);
        }
        if (tl == Type::Int) {
            gen_int_op(e);
            return Type::Int;
        }

        switch (e.op) {                            // both sides are strings
            case Tok::Plus:
                g_.mov(RDI, RAX);
                g_.mov(RSI, RCX);
                call_c(reinterpret_cast<int64_t>(&nero_concat));
                return Type::Str;
            case Tok::Eq:
            case Tok::Ne:
                g_.mov(RDI, RAX);
                g_.mov(RSI, RCX);
                call_c(reinterpret_cast<int64_t>(&nero_streq));
                if (e.op == Tok::Ne) g_.raw({0x48, 0x83, 0xF0, 0x01});   // xor rax, 1
                return Type::Int;
            default:
                fail(e.line, e.col, std::string("'") + op_str(e.op) + "' does not work on strings");
        }
    }

    Type gen_call(const Expr& e) {
        size_t n = e.args.size();

        if (const Builtin* b = find_builtin(e.name)) {
            if (n != 1) fail(e.line, e.col, "'" + e.name + "' takes 1 argument, got " + std::to_string(n));
            Type t = gen_expr(*e.args[0]);
            if (t != b->param)
                fail(e.args[0]->line, e.args[0]->col, "'" + e.name + "' needs " + an(b->param) +
                                                          ", got " + an(t));
            g_.mov(RDI, RAX);
            call_c(b->fn);
            return b->ret;
        }

        auto it = funcs_.find(e.name);
        if (it == funcs_.end()) fail(e.line, e.col, "undefined function '" + e.name + "'");
        const FuncInfo& fi = it->second;
        if (n != fi.params.size())
            fail(e.line, e.col, "function '" + e.name + "' takes " + std::to_string(fi.params.size()) +
                                    " argument(s), got " + std::to_string(n));

        for (size_t i = 0; i < n; i++) {           // evaluate left to right, park on the stack
            Type t = gen_expr(*e.args[i]);
            if (t != fi.params[i])
                fail(e.args[i]->line, e.args[i]->col, "argument " + std::to_string(i + 1) + " of '" + e.name +
                                                          "' must be " + an(fi.params[i]) + ", got " + an(t));
            g_.push(RAX);
            depth_++;
        }
        for (size_t i = n; i-- > 0;) {             // then load the argument registers
            g_.pop(ARG_REGS[i]);
            depth_--;
        }
        bool pad = (depth_ % 2) != 0;              // keep the stack 16-byte aligned at the call
        if (pad) g_.raw({0x48, 0x83, 0xEC, 0x08}); // sub rsp, 8
        g_.call(it->second.label);
        if (pad) g_.raw({0x48, 0x83, 0xC4, 0x08}); // add rsp, 8
        return fi.ret;
    }

    void compare(int setcc) {
        g_.cmp(RAX, RCX);
        g_.raw({0x0F, setcc, 0xC0});               // setcc al
        g_.raw({0x0F, 0xB6, 0xC0});                // movzx eax, al
    }

    void gen_int_op(const Expr& e) {
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
