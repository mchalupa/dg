#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

void runOnInput(const std::string& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input.is_open())
        abort();

    std::vector<unsigned char> data(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());

    input.close();

    LLVMFuzzerTestOneInput(data.data(), data.size());
}

int main(void) {
    runOnInput("regressions-bin/disjunctive-intervals-map-regression1.bin");
}

