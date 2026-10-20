#include "parser.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    std::string mode = argc >= 2 ? argv[1] : "";
    if (argc != 3 || (mode != "--tokens" && mode != "--ast")) {
        std::cerr << "usage: nero --tokens|--ast <file.nero>\n";
        return 1;
    }
    std::ifstream f(argv[2]);
    if (!f) { std::cerr << "cannot open " << argv[2] << "\n"; return 1; }
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
        } else {
            Program prog = Parser(std::move(tokens)).parse_program();
            std::cout << dump_program(prog);
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
