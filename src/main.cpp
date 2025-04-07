#include <iostream>

#include "baseline/baseline.hpp"
#include "structures.hpp"

int main() {
    // freopen("data/sample_practice.in", "r", stdin);
    std::cin.tie(nullptr);
    std::cout.tie(nullptr);
    std::ios::sync_with_stdio(false);

    // debug SIMULATE_MULT
    // for (int i = 0; i < 120; i++) {
    //     std::cerr << SIMLUATE_MULT[i] << " ";
    // }

    baseline::run();
    return 0;
}