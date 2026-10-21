#include "codegen.hpp"
#include "parser.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    std::string mode;
    const char* path = nullptr;
    if (argc == 2) { mode = "run"; path = argv[1]; }
    else if (argc == 3) { mode = argv[1]; path = argv[2]; }
    if (!path || (mode != "run" && mode != "--tokens" && mode != "--ast" && mode != "--bin")) {
        std::cerr << "usage: nero [--tokens|--ast|--bin] <file.nero>\n";
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
                std::string pos = std::to_string(t.line) + ":" + std::to_string(t.col);
                std::cout << std::left << std::setw(7) << pos << std::setw(9) << tok_name(t.kind);
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
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
