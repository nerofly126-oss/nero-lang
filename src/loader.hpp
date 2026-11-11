#pragma once
#include "ast.hpp"
#include "parser.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

// Loads a .nero file and everything it imports, and merges all of it into one Program.
//   import "lib.nero"   at the top of a file, path relative to the file that says it
// Rules: an imported file may only hold 'import', 'type' and 'fn' (nothing runs on import).
// There is one flat namespace: a name declared in two files is an error that names both.
// A file is loaded once even if several files import it, and a cycle is an error showing the chain.
// Every error starts with the file it happened in: "lib.nero:3:5: message".
class Loader {
public:
    Program load(const std::string& path) {
        out_ = Program();
        load_file(path, "", 0, 0, true);
        return std::move(out_);
    }

private:
    struct Origin { std::string file; int line = 0, col = 0; };
    Program out_;
    std::vector<std::string> stack_;                      // files being loaded right now, for the cycle message
    std::vector<std::string> stack_keys_;                 // the same files by canonical path
    std::map<std::string, std::set<std::string>> done_;   // canonical path -> schema names visible through that file
    std::map<std::string, Origin> type_origin_, func_origin_;

    static std::string at(const std::string& file, int line, int col) {
        if (file.empty()) return "";
        return file + ":" + std::to_string(line) + ":" + std::to_string(col) + ": ";
    }

    [[noreturn]] static void fail(const std::string& msg) { throw std::runtime_error(msg); }

    // from: the file (and position) of the import that asked for this one, "" for the file that was run
    std::set<std::string> load_file(const std::string& display, const std::string& from, int from_line, int from_col, bool is_root) {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path canon = fs::weakly_canonical(fs::path(display), ec);
        std::string key = ec ? display : canon.string();

        for (size_t i = 0; i < stack_keys_.size(); i++) {
            if (stack_keys_[i] != key) continue;
            std::string chain;
            for (size_t j = i; j < stack_.size(); j++) chain += stack_[j] + " -> ";
            fail(at(from, from_line, from_col) + "import cycle: " + chain + stack_[i]);
        }
        auto seen = done_.find(key);
        if (seen != done_.end()) return seen->second;       // a diamond: loaded already

        std::ifstream in(display);
        if (!in) fail(at(from, from_line, from_col) + "cannot open '" + display + "'");
        std::stringstream ss;
        ss << in.rdbuf();

        std::vector<Token> toks;
        try { toks = Lexer(ss.str()).tokenize(); }
        catch (const std::exception& e) { fail(display + ":" + e.what()); }

        // the imports are the first lines of the file: load them before parsing, because the parser has to know their schema names
        std::vector<ImportDecl> imps;
        size_t i = 0;
        while (i + 1 < toks.size() && toks[i].kind == Tok::Import && toks[i + 1].kind == Tok::String && !toks[i + 1].text.empty()) {
            imps.push_back(ImportDecl{toks[i + 1].text, toks[i].line, toks[i].col});
            i += 2;
            while (i < toks.size() && toks[i].kind == Tok::Newline) i++;
        }

        stack_.push_back(display);
        stack_keys_.push_back(key);
        std::set<std::string> visible;
        fs::path dir = fs::path(display).parent_path();
        for (const ImportDecl& im : imps) {
            std::string dep = (dir / im.path).lexically_normal().string();
            std::set<std::string> names = load_file(dep, display, im.line, im.col, false);
            visible.insert(names.begin(), names.end());
        }

        Program prog;
        try { prog = Parser(std::move(toks), visible, display).parse_program(); }
        catch (const std::exception& e) { fail(display + ":" + e.what()); }

        if (!is_root && !prog.stmts.empty()) {
            const Stmt& s = *prog.stmts[0];
            fail(at(display, s.line, s.col) + "an imported file can only hold 'import', 'type' and 'fn', "
                 "because nothing may run when a file is imported");
        }

        std::set<std::string> mine = visible;
        for (TypeDecl& d : prog.types) {
            auto it = type_origin_.find(d.name);
            if (it != type_origin_.end())
                fail(at(display, d.line, d.col) + "type '" + d.name + "' is already declared in " + it->second.file +
                     " (line " + std::to_string(it->second.line) + ")");
            type_origin_[d.name] = Origin{display, d.line, d.col};
            mine.insert(d.name);
            out_.types.push_back(std::move(d));
        }
        for (Func& f : prog.funcs) {
            auto it = func_origin_.find(f.name);
            if (it != func_origin_.end() && it->second.file != display)
                fail(at(display, f.line, f.col) + "function '" + f.name + "' is already defined in " + it->second.file +
                     " (line " + std::to_string(it->second.line) + ")");
            if (it == func_origin_.end()) func_origin_[f.name] = Origin{display, f.line, f.col};
            out_.funcs.push_back(std::move(f));
        }
        if (is_root) {
            out_.stmts = std::move(prog.stmts);
            out_.imports = std::move(prog.imports);
            out_.file = display;
        }

        stack_.pop_back();
        stack_keys_.pop_back();
        done_[key] = mine;
        return mine;
    }
};
