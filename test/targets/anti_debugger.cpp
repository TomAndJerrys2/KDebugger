#include <cstdio>
#include <numeric>

void an_innocent_function() {
    std::puts("Putting pineapple on pizza");
}

void an_innocent_function_end() {}

int checksum() {
    auto start = reinterpret_cast<volatile const char*>(&an_innocent_function);
    auto end   = reinterpret_cast<volatile const char*>(&an_innocent_function_end);

    return std::accumulate(start, end, 0);
}
