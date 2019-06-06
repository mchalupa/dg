#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <ctime>

// ignore unused parameters in LLVM libraries
#if (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include <llvm/ADT/SparseBitVector.h>

#if (__clang__)
#pragma clang diagnostic pop // ignore -Wunused-parameter
#else
#pragma GCC diagnostic pop
#endif

#include "dg/ADT/Bitvector.h"

using dg::ADT::SparseBitvector;

std::default_random_engine generator;
std::uniform_int_distribution<uint64_t> distribution(0, ~static_cast<uint64_t>(0));

struct Result {
    std::clock_t dg{0};
    std::clock_t llvm{0};

    void reset() {
        dg = 0;
        llvm = 0;
    }

    Result& operator+=(const Result& rhs) {
        dg += rhs.dg;
        llvm += rhs.llvm;
        return *this;
    }
};

Result benchmark1() {
    Result r;
    auto x = distribution(generator);
    SparseBitvector dgV;
    llvm::SparseBitVector<> llvmV;

    std::clock_t start;
    start = std::clock();
    dgV.set(x);
    r.dg = std::clock() - start;

    start = std::clock();
    llvmV.set(x);
    r.llvm = std::clock() - start;

    return r;
}

Result benchmark2() {
    Result r;
    SparseBitvector dgV;
    llvm::SparseBitVector<> llvmV;

    std::clock_t start;
    for (int i = 0; i < 10000; ++i) {
        auto x = distribution(generator);
        start = std::clock();
        dgV.set(x);
        r.dg += (std::clock() - start);

        start = std::clock();
        llvmV.set(x);
        r.llvm = std::clock() - start;
    }

    return r;
}

Result benchmark_fill1(size_t vlen = 1000) {
    Result r;
    SparseBitvector dgV;
    llvm::SparseBitVector<> llvmV;

    std::clock_t start;
    for (size_t i = 0; i < vlen; ++i) {
        start = std::clock();
        dgV.set(i);
        r.dg += (std::clock() - start);

        start = std::clock();
        llvmV.set(i);
        r.llvm += (std::clock() - start);
    }

    return r;
}

Result benchmark_fill2(size_t vlen = 1000) {
    Result r;
    SparseBitvector dgV;
    llvm::SparseBitVector<> llvmV;

    std::clock_t start;
    for (size_t i = 0; i < vlen; i += 2) {
        start = std::clock();
        dgV.set(i);
        r.dg += (std::clock() - start);

        start = std::clock();
        llvmV.set(i);
        r.llvm += (std::clock() - start);
    }
    for (size_t i = 1; i < vlen; i += 2) {
        start = std::clock();
        dgV.set(i);
        r.dg += (std::clock() - start);

        start = std::clock();
        llvmV.set(i);
        r.llvm += (std::clock() - start);
    }

    return r;
}

Result benchmark3(size_t vlen = 1000) {
    Result r;
    SparseBitvector dgV;
    SparseBitvector dgV2;
    llvm::SparseBitVector<> llvmV;
    llvm::SparseBitVector<> llvmV2;

    std::clock_t start;
    for (size_t i = 0; i < vlen; ++i) {
        auto x = distribution(generator);
        auto y = distribution(generator);
        dgV.set(x);
        dgV2.set(y);

        llvmV.set(x);
        llvmV2.set(y);
    }

    start = std::clock();
    dgV.set(dgV2);
    r.dg = (std::clock() - start);

    start = std::clock();
    llvmV |= llvmV2;
    r.llvm = (std::clock() - start);

    return r;
}

Result benchmark4(size_t vlen = 1000) {
    Result r;
    SparseBitvector dgV;
    SparseBitvector dgV2;
    llvm::SparseBitVector<> llvmV;
    llvm::SparseBitVector<> llvmV2;

    std::clock_t start;
    for (size_t i = 0; i < vlen; i += 2) {
        dgV.set(i);
        dgV2.set(i + 1);
        llvmV.set(i);
        llvmV2.set(i + 1);
    }

    start = std::clock();
    dgV.set(dgV2);
    r.dg = (std::clock() - start);

    start = std::clock();
    llvmV |= llvmV2;
    r.llvm = (std::clock() - start);

    return r;
}

int main()
{
    Result r;

    std::cout << "Adding 1 random number into a new bitvector\n";
    for (int i = 0; i < 10000; ++i)
        r += benchmark1();
    std::cout << "  dg bitvector: " << double(r.dg) / CLOCKS_PER_SEC << "\n";
    std::cout << "  llvm bitvector: " << double(r.llvm) / CLOCKS_PER_SEC << "\n";

    std::cout << "Adding 10000 random numbers into a bitvector\n";
    r = benchmark2();
    std::cout << "  dg bitvector: " << double(r.dg) / CLOCKS_PER_SEC << "\n";
    std::cout << "  llvm bitvector: " << double(r.llvm) / CLOCKS_PER_SEC << "\n";

    std::cout << "Filling a bitvector with 1 up to 10000\n";
    r = benchmark_fill1(10000);
    std::cout << "  dg bitvector: " << double(r.dg) / CLOCKS_PER_SEC << "\n";
    std::cout << "  llvm bitvector: " << double(r.llvm) / CLOCKS_PER_SEC << "\n";

    std::cout << "Filling a bitvector with 1 up to 10000, first even then odd\n";
    r = benchmark_fill2(10000);
    std::cout << "  dg bitvector: " << double(r.dg) / CLOCKS_PER_SEC << "\n";
    std::cout << "  llvm bitvector: " << double(r.llvm) / CLOCKS_PER_SEC << "\n";

    std::cout << "Union of 2 random bitvectors of length 1000\n";
    r = benchmark3(1000);
    std::cout << "  dg bitvector: " << double(r.dg) / CLOCKS_PER_SEC << "\n";
    std::cout << "  llvm bitvector: " << double(r.llvm) / CLOCKS_PER_SEC << "\n";

    std::cout << "Union of 2 random bitvectors of length 10000\n";
    r = benchmark3(10000);
    std::cout << "  dg bitvector: " << double(r.dg) / CLOCKS_PER_SEC << "\n";
    std::cout << "  llvm bitvector: " << double(r.llvm) / CLOCKS_PER_SEC << "\n";

    std::cout << "Union of 2 random bitvectors of length 100000\n";
    r = benchmark3(100000);
    std::cout << "  dg bitvector: " << double(r.dg) / CLOCKS_PER_SEC << "\n";
    std::cout << "  llvm bitvector: " << double(r.llvm) / CLOCKS_PER_SEC << "\n";

    std::cout << "Union of 2 mutually exclusive bitvectors of length 1000000\n";
    r = benchmark4(1000000);
    std::cout << "  dg bitvector: " << double(r.dg) / CLOCKS_PER_SEC << "\n";
    std::cout << "  llvm bitvector: " << double(r.llvm) / CLOCKS_PER_SEC << "\n";

    std::cout << "Union of 2 mutually exclusive bitvectors of length 10000000\n";
    r = benchmark4(10000000);
    std::cout << "  dg bitvector: " << double(r.dg) / CLOCKS_PER_SEC << "\n";
    std::cout << "  llvm bitvector: " << double(r.llvm) / CLOCKS_PER_SEC << "\n";

    return 0;
}
