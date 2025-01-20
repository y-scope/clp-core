#ifndef TEST_TESTOUTPUTCLEANER_HPP
#define TEST_TESTOUTPUTCLEANER_HPP

#include <filesystem>
#include <string>
#include <vector>

/**
 * A class that deletes the directories and files created by test cases, both before and after each
 * test case where the class is instantiated.
 */
class TestOutputCleaner {
public:
    explicit TestOutputCleaner(std::vector<std::string> const& paths) : m_paths(paths) {
        delete_paths();
    }

    ~TestOutputCleaner() { delete_paths(); }

    // Delete copy & move constructors and assignment operators
    TestOutputCleaner(TestOutputCleaner const&) = delete;
    TestOutputCleaner(TestOutputCleaner&&) = delete;
    auto operator=(TestOutputCleaner const&) -> TestOutputCleaner& = delete;
    auto operator=(TestOutputCleaner&&) -> TestOutputCleaner& = delete;

private:
    void delete_paths() const {
        for (auto const& path : m_paths) {
            std::filesystem::remove_all(path);
        }
    }

    std::vector<std::string> m_paths;
};

#endif  // TEST_TESTOUTPUTCLEANER_HPP
