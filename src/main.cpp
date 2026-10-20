#include "lexer.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    if (argc != 3 || std::string(argv[1]) != "--tokens") {
        std::cerr << "usage: nero --tokens <file.nero>\n";
        return 1;
    }
    std::ifstream f(argv[2]);
    if (!f) { std::cerr << "cannot open " << argv[2] << "\n"; return 1; }
    std::stringstream ss;
    ss << f.rdbuf();
    try {
        for (const Token& t : Lexer(ss.str()).tokenize()) {
            std::string pos = std::to_string(t.line) + ":" + std::to_string(t.col);
            std::cout << std::left << std::setw(7) << pos << std::setw(9) << tok_name(t.kind);
            if (t.kind != Tok::Newline && t.kind != Tok::End) {
                std::cout << "'" << t.text << "'";
                if (t.kind == Tok::Number) std::cout << " = " << t.value;
            }
            std::cout << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
