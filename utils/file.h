#include <fstream>
#include <iostream>
#include <string>

std::string read_file(const std::string& filename) {
    std::ifstream file(filename);
    if(!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filename);
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}
