#pragma once
#include "ast.hpp"
#include <set>

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
    // known_types: schema names that come from imported files. file: stamped on every function and schema.
    explicit Parser(std::vector<Token> toks, std::set<std::string> known_types = {}, std::string file = "")
        : toks_(std::move(toks)), type_names_(std::move(known_types)), file_(std::move(file)) {
        // schema names are collected up front, so a type can be used above its declaration
        for (size_t i = 0; i + 1 < toks_.size(); i++)
            if (toks_[i].kind == Tok::Type && toks_[i + 1].kind == Tok::Ident) type_names_.insert(toks_[i + 1].text);
    }

    Program parse_program() {
        Program prog;
        prog.file = file_;
        skip_newlines();
        while (!at(Tok::End)) {
            if (at(Tok::Import)) {
                prog.imports.push_back(parse_import());
            } else {
                if (at(Tok::Fn)) prog.funcs.push_back(parse_func());
                else if (at(Tok::Type)) prog.types.push_back(parse_type_decl());
                else prog.stmts.push_back(parse_stmt());
                body_started_ = true;
            }
            end_of_stmt();
        }
        return prog;
    }

private:
    std::vector<Token> toks_;
    size_t pos_ = 0;
    std::set<std::string> type_names_;   // every schema name in the file
    std::set<std::string> declared_;     // schemas seen so far, to catch duplicates
    std::string file_;                   // the source file, stamped on functions and schemas for error messages
    bool body_started_ = false;          // an import has to come before everything else

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

    // A word token: an identifier or a keyword. Field names may be either, so { type: "x" } and s.type work.
    static bool is_word(const Token& t) {
        if (t.kind == Tok::String || t.kind == Tok::Interp || t.text.empty()) return false;
        char c = t.text[0];
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }

    // import "path.nero"   (top level, before everything else, a plain string)
    ImportDecl parse_import() {
        const Token& t = cur();
        if (body_started_) fail(t, "'import' must come before every function, type and statement in the file");
        next();  // 'import'
        if (at(Tok::Interp)) fail(cur(), "an import path must be a plain string, without {...}");
        const Token& p = expect(Tok::String, "a file path in quotes after 'import'");
        if (p.text.empty()) fail(p, "the import path is empty");
        return ImportDecl{p.text, t.line, t.col};
    }

    TypeRef parse_typeref() {
        const Token& t = cur();
        TypeRef r;
        r.line = t.line;
        r.col = t.col;
        if (t.kind == Tok::LBracket) {            // [T] is a list whose elements are all T
            next();
            r.kind = Type::List;
            r.elem = std::make_shared<TypeRef>(parse_typeref());
            expect(Tok::RBracket, "']' after the element type");
            return r;
        }
        if (t.kind == Tok::Ident) {
            next();
            if (t.text == "int")         r.kind = Type::Int;
            else if (t.text == "str")    r.kind = Type::Str;
            else if (t.text == "bool")   r.kind = Type::Bool;
            else if (t.text == "list")   r.kind = Type::List;
            else if (t.text == "record") r.kind = Type::Record;
            else if (t.text == "secret") r.kind = Type::Secret;
            else if (type_names_.count(t.text)) { r.kind = Type::Named; r.name = t.text; }
            else {
                std::vector<std::string> known = {"int", "str", "bool", "list", "record", "secret"};
                known.insert(known.end(), type_names_.begin(), type_names_.end());
                std::string hint = closest(t.text, known);
                fail(t, "unknown type '" + t.text + "'" + (hint.empty() ? "" : " (did you mean '" + hint + "'?)"));
            }
            return r;
        }
        fail(t, "expected a type, got " + describe(t));
    }

    // type Name { field: type [= default] ... }   fields end at a new line or a comma
    TypeDecl parse_type_decl() {
        next();  // 'type'
        const Token& n = expect(Tok::Ident, "a type name after 'type'");
        TypeDecl d;
        d.name = n.text;
        d.line = n.line;
        d.col = n.col;
        d.file = file_;
        if (n.text == "int" || n.text == "str" || n.text == "bool" || n.text == "list" || n.text == "record" || n.text == "secret")
            fail(n, "'" + n.text + "' is a builtin type and cannot be redeclared");
        if (!declared_.insert(n.text).second) fail(n, "type '" + n.text + "' is already declared");
        expect(Tok::LBrace, "'{' after the type name");
        skip_newlines();
        while (!at(Tok::RBrace)) {
            if (at(Tok::End)) fail(cur(), "missing '}'");
            const Token& k = cur();
            if (!is_word(k) && k.kind != Tok::String) fail(k, "expected a field name, got " + describe(k));
            FieldDecl f;
            f.name = k.text;
            f.line = k.line;
            f.col = k.col;
            f.file = file_;
            for (const FieldDecl& other : d.fields)
                if (other.name == f.name) fail(k, "duplicate field '" + f.name + "' in type " + d.name);
            next();
            expect(Tok::Colon, "':' after the field name");
            f.type = parse_typeref();
            if (at(Tok::Assign)) { next(); f.def = parse_expr(); }
            d.fields.push_back(std::move(f));
            bool sep = false;
            if (at(Tok::Comma)) { next(); sep = true; }
            if (at(Tok::Newline)) { skip_newlines(); sep = true; }
            if (!sep && !at(Tok::RBrace)) fail(cur(), "expected a new line, ',' or '}' after the field, got " + describe(cur()));
        }
        next();  // '}'
        return d;
    }

    void parse_record_fields(Expr& e) {          // cur() is the '{'
        next();
        e.kind = Expr::Record;
        skip_newlines();
        while (!at(Tok::RBrace)) {
            if (at(Tok::End)) fail(cur(), "missing '}'");
            const Token& k = cur();
            if (!is_word(k) && k.kind != Tok::String)
                fail(k, "expected a field name, got " + describe(k));
            e.keys.push_back(k.text);
            e.key_pos.push_back({k.line, k.col});
            next();
            expect(Tok::Colon, "':' after the field name");
            e.args.push_back(parse_expr());
            skip_newlines();
            if (at(Tok::Comma)) { next(); skip_newlines(); }
            else if (!at(Tok::RBrace)) fail(cur(), "expected ',' or '}', got " + describe(cur()));
        }
        next();  // '}'
    }


    Func parse_func() {
        Func f;
        f.line = cur().line;
        f.col = cur().col;
        f.file = file_;
        next();  // 'fn'
        f.name = expect(Tok::Ident, "a function name after 'fn'").text;
        expect(Tok::LParen, "'(' after the function name");
        if (!at(Tok::RParen)) {
            while (true) {
                f.params.push_back(expect(Tok::Ident, "a parameter name").text);
                TypeRef t;  // parameters are int unless annotated
                bool written = false;
                if (at(Tok::Colon)) { next(); t = parse_typeref(); written = true; }
                f.param_types.push_back(t.kind);
                f.param_refs.push_back(t);
                f.param_explicit.push_back(written);
                if (at(Tok::Comma)) next();
                else break;
            }
        }
        expect(Tok::RParen, "')' after the parameters");
        if (at(Tok::Arrow)) { next(); f.ret_ref = parse_typeref(); f.ret = f.ret_ref.kind; f.ret_explicit = true; }
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
            case Tok::For:
                next();
                s->kind = Stmt::For;
                s->name = expect(Tok::Ident, "a loop variable after 'for'").text;
                expect(Tok::In, "'in' after the loop variable");
                s->expr = parse_expr();
                s->body = parse_block();
                break;
            case Tok::Emit:
                next();
                s->kind = Stmt::Emit;
                s->expr = parse_expr();
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
            case Tok::Type:
                fail(t, "types can only be declared at the top level");
            case Tok::Import:
                fail(t, "'import' can only be at the top level, before everything else");
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

    ExprPtr parse_expr() { return parse_or(); }

    ExprPtr parse_or() {
        ExprPtr lhs = parse_and();
        while (at(Tok::Or)) {
            const Token& op = cur();
            next();
            ExprPtr rhs = parse_and();
            lhs = make_binary(op, std::move(lhs), std::move(rhs));
        }
        return lhs;
    }

    ExprPtr parse_and() {
        ExprPtr lhs = parse_not();
        while (at(Tok::And)) {
            const Token& op = cur();
            next();
            ExprPtr rhs = parse_not();
            lhs = make_binary(op, std::move(lhs), std::move(rhs));
        }
        return lhs;
    }

    ExprPtr parse_not() {   // 'not' binds looser than comparisons: not a == b means not (a == b)
        if (at(Tok::Not)) {
            const Token& op = cur();
            next();
            auto e = std::make_unique<Expr>();
            e->kind = Expr::Unary;
            e->op = Tok::Not;
            e->lhs = parse_not();
            e->line = op.line;
            e->col = op.col;
            return e;
        }
        return parse_comparison();
    }

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
        return parse_postfix();
    }

    ExprPtr parse_postfix() {   // a.b and a[i], as many as you like
        ExprPtr e = parse_primary();
        while (at(Tok::Dot) || at(Tok::LBracket)) {
            const Token& op = cur();
            auto p = std::make_unique<Expr>();
            p->line = op.line;
            p->col = op.col;
            p->lhs = std::move(e);
            next();
            if (op.kind == Tok::Dot) {
                p->kind = Expr::Field;
                if (cur().kind == Tok::Number && p->lhs->kind == Expr::Num)
                    fail(cur(), "there are no decimal numbers in n+, only whole numbers (write \"" + std::to_string(p->lhs->value) +
                                    "." + cur().text + "\" as a string if you need the text)");
                if (!is_word(cur())) fail(cur(), "expected a field name after '.', got " + describe(cur()));
                p->name = cur().text;
                next();
            } else {
                p->kind = Expr::Index;
                p->rhs = parse_expr();
                expect(Tok::RBracket, "']' after the index");
            }
            e = std::move(p);
        }
        return e;
    }

    // if cond { value } else { value }, used where a value is expected. The else is required.
    ExprPtr parse_if_expr() {
        const Token& t = cur();
        auto e = std::make_unique<Expr>();
        e->kind = Expr::IfExpr;
        e->line = t.line;
        e->col = t.col;
        next();  // 'if'
        e->lhs = parse_expr();
        e->rhs = parse_branch();
        if (!at(Tok::Else))
            fail(cur(), "an 'if' used as a value needs an 'else' on the same line as the closing '}', got " + describe(cur()));
        next();  // 'else'
        e->alt = at(Tok::If) ? parse_if_expr() : parse_branch();
        return e;
    }

    ExprPtr parse_branch() {
        expect(Tok::LBrace, "'{'");
        skip_newlines();
        ExprPtr v = parse_expr();
        skip_newlines();
        expect(Tok::RBrace, "'}' after the value");
        return v;
    }

    // [elem for name in list if filter], cur() is at 'for'
    ExprPtr parse_comprehension(ExprPtr elem, int line, int col) {
        next();  // 'for'
        auto c = std::make_unique<Expr>();
        c->kind = Expr::Comp;
        c->line = line;
        c->col = col;
        c->lhs = std::move(elem);
        c->name = expect(Tok::Ident, "a loop variable after 'for'").text;
        expect(Tok::In, "'in' after the loop variable");
        c->rhs = parse_expr();
        if (at(Tok::If)) { next(); c->alt = parse_expr(); }
        skip_newlines();
        expect(Tok::RBracket, "']' at the end of the list");
        return c;
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
            } else if (at(Tok::LBrace) && type_names_.count(e->name)) {
                parse_record_fields(*e);          // Name { field: value, ... } builds a value of that schema
            } else {
                e->kind = Expr::Var;
            }
            return e;
        }
        if (t.kind == Tok::If) return parse_if_expr();
        if (t.kind == Tok::Interp) {              // "text {expression} text": each expression is parsed on its own
            next();
            e->kind = Expr::Interp;
            for (const StrPart& p : t.parts) {
                if (!p.expr) {
                    auto lit = std::make_unique<Expr>();
                    lit->kind = Expr::Str;
                    lit->name = p.text;
                    lit->line = t.line;
                    lit->col = t.col;
                    e->args.push_back(std::move(lit));
                } else {
                    Parser sub(Lexer(p.text, p.line, p.col).tokenize());
                    sub.type_names_ = type_names_;
                    ExprPtr inner = sub.parse_expr();
                    sub.skip_newlines();
                    if (!sub.at(Tok::End)) fail(sub.cur(), "unexpected " + describe(sub.cur()) + " inside {} in a string");
                    e->args.push_back(std::move(inner));
                }
            }
            return e;
        }
        if (t.kind == Tok::True || t.kind == Tok::False) {
            next();
            e->kind = Expr::Bool;
            e->value = t.kind == Tok::True ? 1 : 0;
            return e;
        }
        if (t.kind == Tok::LBracket) {            // list literal, may span lines, trailing comma is fine
            next();
            e->kind = Expr::List;
            skip_newlines();
            while (!at(Tok::RBracket)) {
                if (at(Tok::End)) fail(cur(), "missing ']'");
                e->args.push_back(parse_expr());
                if (e->args.size() == 1 && at(Tok::For)) return parse_comprehension(std::move(e->args[0]), e->line, e->col);
                skip_newlines();
                if (at(Tok::Comma)) { next(); skip_newlines(); }
                else if (!at(Tok::RBracket)) fail(cur(), "expected ',' or ']', got " + describe(cur()));
            }
            next();  // ']'
            return e;
        }
        if (t.kind == Tok::LBrace) {              // record literal: { name: value, "odd key": value }
            parse_record_fields(*e);
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
