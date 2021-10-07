#include "Cassia.h"

#include <fstream>
#include <iostream>
#include <vector>
#include <unistd.h>

int main(int argc, const char**argv) {
    if (argc != 3) {
        std::cout << "Usage: cassia_test [SEGMENT_FILE] [STYLINGS_FILE]" << std::endl;
        return 1;
    }

    std::ifstream segmentFile(argv[1]);
    if (segmentFile.bad()) {
        std::cout << "Couldn't open " << argv[1] << std::endl;
        return 1;
    }
    std::vector<char> segments((std::istreambuf_iterator<char>(segmentFile)), std::istreambuf_iterator<char>());

    std::ifstream stylingFile(argv[2]);
    if (stylingFile.bad()) {
        std::cout << "Couldn't open " << argv[2] << std::endl;
        return 1;
    }
    std::vector<char> stylings((std::istreambuf_iterator<char>(stylingFile)), std::istreambuf_iterator<char>());

    cassia_init(1000, 1000);
    cassia_render(reinterpret_cast<const uint64_t*>(segments.data()), segments.size() / 8,
                  reinterpret_cast<const CassiaStyling*>(stylings.data()), stylings.size() / sizeof(CassiaStyling));
    sleep(1);
    cassia_shutdown();

    return 0;
}
