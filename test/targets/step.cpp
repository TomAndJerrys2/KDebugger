#include <cstdio>

__attribute__((always_inline))
inline void stratch_ears() {
    std::puts("Scratching ears");
}

__attribute__((always_inline))
inline void pet_cat() {
    stratch_ears();
    std::puts("Done petting cat");
}

void find_happiness() {
    pet_cat();
    std::puts("Found Happiness");
}

int main() {
    find_happiness();
    find_happiness();
}

