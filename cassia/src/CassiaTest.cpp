#include "Cassia.h"

#include <fstream>
#include <iostream>
#include <vector>
#include <unistd.h>

int main(int argc, const char**argv) {
    if (argc != 2) {
        std::cout << "Usage: cassia_test [SEGMENT_FILE]" << std::endl;
        return 1;
    }

    std::ifstream segmentFile(argv[1]);
    if (segmentFile.bad()) {
        std::cout << "Couldn't open " << argv[1] << std::endl;
        return 1;
    }

    std::vector<char> content((std::istreambuf_iterator<char>(segmentFile)), std::istreambuf_iterator<char>());

    cassia_init(1000, 1000);
    cassia_render(reinterpret_cast<const uint64_t*>(content.data()), content.size() / 8, nullptr, 0);
    sleep(1);
    cassia_shutdown();

    return 0;
}
