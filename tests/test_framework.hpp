// Tiny self-registering unit-test framework (no external dependencies).
#pragma once
#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace owr::test {

struct Case { std::string name; std::function<void()> fn; };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }
struct Registrar { Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); } };
struct RequireFailure {};

inline void fail(const char* file, int line, const std::string& msg) {
    ++failures();
    std::cerr << "    FAIL " << file << ":" << line << "  " << msg << "\n";
}
template <class A, class B>
std::string show(const A& a, const B& b) { std::ostringstream s; s << "(" << a << " vs " << b << ")"; return s.str(); }

}  // namespace owr::test

#define TEST(name) \
    static void name(); \
    static owr::test::Registrar reg_##name(#name, name); \
    static void name()
#define CHECK(c) do { ++owr::test::checks(); if (!(c)) owr::test::fail(__FILE__, __LINE__, #c); } while (0)
#define CHECK_EQ(a, b) do { ++owr::test::checks(); auto va = (a); auto vb = (b); if (!(va == vb)) owr::test::fail(__FILE__, __LINE__, std::string(#a " == " #b " ") + owr::test::show(va, vb)); } while (0)
#define CHECK_NEAR(a, b, eps) do { ++owr::test::checks(); double va = (a), vb = (b); if (std::fabs(va - vb) > (eps)) owr::test::fail(__FILE__, __LINE__, std::string(#a " ≈ " #b " ") + owr::test::show(va, vb)); } while (0)
#define REQUIRE(c) do { ++owr::test::checks(); if (!(c)) { owr::test::fail(__FILE__, __LINE__, "REQUIRE " #c); throw owr::test::RequireFailure(); } } while (0)
