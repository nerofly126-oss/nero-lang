#pragma once
#include "ast.hpp"

// Grammar
//   program   := { (func | stmt) end } End
//   func      := "fn" IDENT "(" [ IDENT { "," IDENT } ] ")" block      (top level only)
//   stmt      := "let" IDENT "=" expr
//              | IDENT "=" expr
//              | IDENT "(" ... ")"                  (a call used as a statement)
//              | "print" expr
//              | "return" [ expr ]
//              | "while" expr block
//              | "if" expr block [ "else" ( if-stmt | block ) ]
//   block     := "{" { stmt end } "}"
//   expr      := comparison
//   comparison:= additive { ("==" | "!=" | "<" | "<=" | ">" | ">=") additive }
//   additive  := term { ("+" | "-") term }
//   term      := unary { ("*" | "/" | "%") unary }
//   unary     := "-" unary | primary
//   primary   := NUMBER | IDENT | IDENT "(" [ expr { "," expr } ] ")" | "(" expr ")"
class Parser {
public:
    explicit Parser(std::vector<Token> toks) : toks_(std::move(toks)) {}

    Program parse_program() {
        Program prog;
        skip_newlines();
        while (!at(Tok::End)) {
            if (at(Tok::Fn)) prog.funcs.push_back(parse_func());
            else prog.stmts.push_back(parse_stmt());
            end_of_stmt();
        }
        return prog;
    }

private:
    std::vector<Token> toks_;
    size_t pos_ = 0;

    const Token& cur() const { return toks_[pos_]; }
    const Token& peek_tok() const { return toks_[pos_ + 1 < toks_.size() ? pos_ + 1 : pos_]; }
    bool at(Tok k) const { return cur().kind == k; }
    void next() { if (pos_ + 1 < toks_.size()) pos_++; }
    void skip_newlines() { while (at(Tok::Newline)) next(); }

    static std::string describe(const Token& t) {
        if (t.kind == Tok::End) return "end of file";
        if (t.kind == Tok::Newline) return "end of line";
        return "'" + t.text + "'";
    }

    [[noreturn]] static void fail(const Token& t, const std::string& msg) {
        throw std::runtime_error(std::to_string(t.line) + ":" + std::to_string(t.col) + ": " + msg);
    }

    const Token& expect(Tok k, const std::string& what) {
        if (!at(k)) fail(cur(), "expected " + what + ", got " + describe(cur()));
        const Token& t = cur();
        next();
        return t;
    }

    // a statement ends at a newline, or right before } or the end of the file
    void end_of_stmt() {
        if (at(Tok::Newline)) { skip_newlines(); return; }
        if (at(Tok::End) || at(Tok::RBrace)) return;
        fail(cur(), "expected end of line, got " + describe(cur()));
    }

    Type parse_type() {
        const Token& t = cur();
        if (t.kind == Tok::Ident && t.text == "int") { next(); return Type::Int; }
        if (t.kind == Tok::Ident && t.text == "str") { next(); return Type::Str; }
        fail(t, "expected a type ('int' or 'str'), got " + describe(t));
    }

    Func parse_func() {
        Func f;
        f.line = cur().line;
        f.col = cur().col;
        next();  // 'fn'
        f.name = expect(Tok::Ident, "a function name after 'fn'").text;
        expect(Tok::LParen, "'(' after the function name");
        if (!at(Tok::RParen)) {
            while (true) {
                f.params.push_back(expect(Tok::Ident, "a parameter name").text);
                Type t = Type::Int;  // parameters are int unless annotated
                if (at(Tok::Colon)) { next(); t = parse_type(); }
                f.param_types.push_back(t);
                if (at(Tok::Comma)) next();
                else break;
            }
        }
        expect(Tok::RParen, "')' after the parameters");
        if (at(Tok::Arrow)) { next(); f.ret = parse_type(); }
        f.body = parse_block();
        return f;
    }

    StmtPtr parse_stmt() {
        const Token& t = cur();
        auto s = std::make_unique<Stmt>();
        s->line = t.line;
        s->col = t.col;
        switch (t.kind) {
            case Tok::Let:
                next();
                s->kind = Stmt::Let;
                s->name = expect(Tok::Ident, "a variable name after 'let'").text;
                expect(Tok::Assign, "'=' after the variable name");
                s->expr = parse_expr();
                break;
            case Tok::Ident:
                if (peek_tok().kind == Tok::LParen) {
                    s->kind = Stmt::ExprStmt;
                    s->expr = parse_expr();
                } else {
                    s->kind = Stmt::Assign;
                    s->name = t.text;
                    next();
                    expect(Tok::Assign, "'=' after the variable name");
                    s->expr = parse_expr();
                }
                break;
            case Tok::Print:
                next();
                s->kind = Stmt::Print;
                s->expr = parse_expr();
                break;
            case Tok::Return:
                next();
                s->kind = Stmt::Return;
                if (!at(Tok::Newline) && !at(Tok::RBrace) && !at(Tok::End)) s->expr = parse_expr();
                break;
            case Tok::While:
                next();
                s->kind = Stmt::While;
                s->expr = parse_expr();
                s->body = parse_block();
                break;
            case Tok::If:
                return parse_if();
            case Tok::Fn:
                fail(t, "functions can only be declared at the top level");
            default:
                fail(t, "unexpected " + describe(t) + " at the start of a statement");
        }
        return s;
    }

    StmtPtr parse_if() {
        auto s = std::make_unique<Stmt>();
        s->kind = Stmt::If;
        s->line = cur().line;
        s->col = cur().col;
        next();  // 'if'
        s->expr = parse_expr();
        s->body = parse_block();
        if (at(Tok::Else)) {  // 'else' must sit on the same line as the closing brace
            next();
            s->has_else = true;
            if (at(Tok::If)) s->else_body.push_back(parse_if());
            else s->else_body = parse_block();
        }
        return s;
    }

    std::vector<StmtPtr> parse_block() {
        expect(Tok::LBrace, "'{'");
        skip_newlines();
        std::vector<StmtPtr> body;
        while (!at(Tok::RBrace)) {
            if (at(Tok::End)) fail(cur(), "missing '}'");
            body.push_back(parse_stmt());
            end_of_stmt();
        }
        next();  // '}'
        return body;
    }

    static ExprPtr make_binary(const Token& op, ExprPtr l, ExprPtr r) {
        auto e = std::make_unique<Expr>();
        e->kind = Expr::Binary;
        e->op = op.kind;
        e->lhs = std::move(l);
        e->rhs = std::move(r);
        e->line = op.line;
        e->col = op.col;
        return e;
    }

    ExprPtr parse_expr() { return parse_comparison(); }

    ExprPtr parse_comparison() {
        ExprPtr lhs = parse_additive();
        while (at(Tok::Eq) || at(Tok::Ne) || at(Tok::Lt) || at(Tok::Le) || at(Tok::Gt) || at(Tok::Ge)) {
            const Token& op = cur();
            next();
            ExprPtr rhs = parse_additive();
            lhs = make_binary(op, std::move(lhs), std::move(rhs));
        }
        return lhs;
    }

    ExprPtr parse_additive() {
        ExprPtr lhs = parse_term();
        while (at(Tok::Plus) || at(Tok::Minus)) {
            const Token& op = cur();
            next();
            ExprPtr rhs = parse_term();
            lhs = make_binary(op, std::move(lhs), std::move(rhs));
        }
        return lhs;
    }

    ExprPtr parse_term() {
        ExprPtr lhs = parse_unary();
        while (at(Tok::Star) || at(Tok::Slash) || at(Tok::Percent)) {
            const Token& op = cur();
            next();
            ExprPtr rhs = parse_unary();
            lhs = make_binary(op, std::move(lhs), std::move(rhs));
        }
        return lhs;
    }

    ExprPtr parse_unary() {
        if (at(Tok::Minus)) {
            const Token& op = cur();
            next();
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Unary;
            e->op = Tok::Minus;
            e->lhs = parse_unary();
            e->line = op.line;
            e->col = op.col;
            return e;
        }
        return parse_primary();
    }

    ExprPtr parse_primary() {
        const Token& t = cur();
        auto e = std::make_unique<Expr>();
        e->line = t.line;
        e->col = t.col;
        if (t.kind == Tok::Number) {
            next();
            e->kind = Expr::Num;
            e->value = t.value;
            return e;
        }
        if (t.kind == Tok::String) {
            next();
            e->kind = Expr::Str;
            e->name = t.text;
            return e;
        }
        if (t.kind == Tok::Ident) {
            next();
            e->name = t.text;
            if (at(Tok::LParen)) {
                next();
                e->kind = Expr::Call;
                if (!at(Tok::RParen)) {
                    while (true) {
                        e->args.push_back(parse_expr());
                        if (at(Tok::Comma)) next();
                        else break;
                    }
                }
                expect(Tok::RParen, "')' after the arguments");
            } else {
                e->kind = Expr::Var;
            }
            return e;
        }
        if (t.kind == Tok::LParen) {
            next();
            ExprPtr inner = parse_expr();
            expect(Tok::RParen, "')'");
            return inner;
        }
        fail(t, "expected a value, got " + describe(t));
    }
};
