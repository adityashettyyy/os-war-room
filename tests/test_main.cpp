#include <cstring>
#include "test_framework.hpp"

int main(int argc, char** argv) {
    using namespace owr::test;
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0, bad = 0;
    for (auto& c : registry()) {
        if (filter && c.name.find(filter) == std::string::npos) continue;
        int before = failures();
        try { c.fn(); }
        catch (const RequireFailure&) {}
        catch (const std::exception& e) { fail(__FILE__, __LINE__, std::string("exception: ") + e.what()); }
        ++ran;
        bool ok = failures() == before;
        if (!ok) ++bad;
        std::cout << (ok ? "  ok   " : "  FAIL ") << c.name << "\n";
    }
    std::cout << "\n" << ran << " tests, " << checks() << " checks, " << bad << " failed\n";
    return bad ? 1 : 0;
}
