#include "codegen.hpp"
#include "evaluator.hpp"
#include "parser.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

int main(int argc, char** argv) {
    OutFormat fmt = OutFormat::Json;
    bool format_given = false;
    std::vector<std::string> pos;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--yaml") { fmt = OutFormat::Yaml; format_given = true; }
        else if (a == "--json") { fmt = OutFormat::Json; format_given = true; }
        else pos.push_back(a);
    }
    std::string mode;
    const char* path = nullptr;
    if (pos.size() == 1) { mode = "run"; path = pos[0].c_str(); }
    else if (pos.size() == 2) { mode = pos[0]; path = pos[1].c_str(); }
    if (!path || (mode != "run" && mode != "eval" && mode != "--tokens" && mode != "--ast" && mode != "--bin")) {
        std::cerr << "usage: nero [run|eval|--tokens|--ast|--bin] <file.nero> [--yaml|--json]\n";
        return 1;
    }
    if (format_given && mode != "eval") {
        std::cerr << "--yaml and --json only work with 'nero eval'\n";
        return 1;
    }
    std::ifstream f(path);
    if (!f) { std::cerr << "cannot open " << path << "\n"; return 1; }
    std::stringstream ss;
    ss << f.rdbuf();
    try {
        std::vector<Token> tokens = Lexer(ss.str()).tokenize();
        if (mode == "--tokens") {
            for (const Token& t : tokens) {
                std::string p = std::to_string(t.line) + ":" + std::to_string(t.col);
                std::cout << std::left << std::setw(7) << p << std::setw(9) << tok_name(t.kind);
                if (t.kind != Tok::Newline && t.kind != Tok::End) {
                    std::cout << "'" << t.text << "'";
                    if (t.kind == Tok::Number) std::cout << " = " << t.value;
                }
                std::cout << "\n";
            }
            return 0;
        }

        Program prog = Parser(std::move(tokens)).parse_program();
        if (mode == "--ast") {
            std::cout << dump_program(prog);
            return 0;
        }
        if (mode == "eval") {
            Evaluator(prog, std::cout, fmt).run();
            std::cout.flush();
            return 0;
        }

        Grimoire g;
        NeroFn fn = CodeGen(g).compile(prog);
        if (mode == "--bin") {
            std::ofstream("out.bin", std::ios::binary)
                .write(reinterpret_cast<const char*>(g.bytes().data()), g.bytes().size());
            std::cerr << "wrote " << g.bytes().size() << " bytes to out.bin\n";
            return 0;
        }
        fn();
        std::fflush(stdout);
    } catch (const std::exception& e) {
        std::cout.flush();
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
