#pragma once

#include <iostream>
#include <string>

namespace testharness {

inline int failureCount = 0;
inline int checkCount = 0;

inline void check(bool condition, const char* expression, const char* file, int line,
                  const std::string& detail = {})
{
    ++checkCount;
    if (condition) {
        return;
    }
    ++failureCount;
    std::cerr << file << ":" << line << ": CHECK failed: " << expression;
    if (!detail.empty()) {
        std::cerr << " [" << detail << "]";
    }
    std::cerr << "\n";
}

inline int summary(const std::string& name)
{
    if (failureCount == 0) {
        std::cout << name << ": " << checkCount << " checks passed\n";
        return 0;
    }
    std::cerr << name << ": " << failureCount << " of " << checkCount
              << " checks failed\n";
    return 1;
}

} // namespace testharness

#define CHECK(expression) \
    testharness::check((expression), #expression, __FILE__, __LINE__)

#define CHECK_MSG(expression, detail) \
    testharness::check((expression), #expression, __FILE__, __LINE__, (detail))

#define TEST_MAIN(name) \
    int main() \
    { \
        return testharness::summary(name); \
    }
