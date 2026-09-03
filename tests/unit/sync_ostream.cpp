#include "SyncOStream.h"

#include <catch2/catch_test_macros.hpp>

#include <barrier>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

TEST_CASE("SyncOStream serializes concurrent writes to the same stream")
{
    constexpr int thread_count = 8;
    constexpr int messages_per_thread = 64;

    std::ostringstream output;
    std::barrier start_line(thread_count);
    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    for (int thread_index = 0; thread_index < thread_count; ++thread_index)
    {
        threads.emplace_back([&, thread_index] {
            start_line.arrive_and_wait();
            for (int message_index = 0; message_index < messages_per_thread; ++message_index)
            {
                SyncOStream(output) << "thread=" << thread_index << " message=" << message_index << '\n';
            }
        });
    }

    for (std::thread& thread : threads)
        thread.join();

    std::map<std::string, int> counts;
    std::istringstream lines(output.str());
    std::string line;
    while (std::getline(lines, line))
        ++counts[line];

    REQUIRE(counts.size() == static_cast<std::size_t>(thread_count * messages_per_thread));
    for (int thread_index = 0; thread_index < thread_count; ++thread_index)
    {
        for (int message_index = 0; message_index < messages_per_thread; ++message_index)
        {
            REQUIRE(counts["thread=" + std::to_string(thread_index) + " message=" + std::to_string(message_index)] == 1);
        }
    }

    TEST_CASE("SyncOStream keeps different destination streams independent")
    {
        constexpr int thread_count = 8;
        constexpr int messages_per_thread = 32;

        std::ostringstream left_output;
        std::ostringstream right_output;
        std::barrier start_line(thread_count);
        std::vector<std::thread> threads;
        threads.reserve(thread_count);

        for (int thread_index = 0; thread_index < thread_count; ++thread_index)
        {
            threads.emplace_back([&, thread_index] {
                start_line.arrive_and_wait();
                std::ostringstream& target = (thread_index % 2 == 0) ? left_output : right_output;
                for (int message_index = 0; message_index < messages_per_thread; ++message_index)
                    SyncOStream(target) << "thread=" << thread_index << " message=" << message_index << '\n';
            });
        }

        for (std::thread& thread : threads)
            thread.join();

        auto count_lines = [](std::string const& text) {
            std::map<std::string, int> counts;
            std::istringstream lines(text);
            std::string line;
            while (std::getline(lines, line))
                ++counts[line];
            return counts;
        };

        std::map<std::string, int> const left_counts = count_lines(left_output.str());
        std::map<std::string, int> const right_counts = count_lines(right_output.str());

        REQUIRE(left_counts.size() == static_cast<std::size_t>((thread_count / 2) * messages_per_thread));
        REQUIRE(right_counts.size() == static_cast<std::size_t>((thread_count / 2) * messages_per_thread));

        for (int thread_index = 0; thread_index < thread_count; ++thread_index)
        {
            std::map<std::string, int> const& counts = (thread_index % 2 == 0) ? left_counts : right_counts;
            for (int message_index = 0; message_index < messages_per_thread; ++message_index)
            {
                REQUIRE(counts.at("thread=" + std::to_string(thread_index) + " message=" + std::to_string(message_index)) == 1);
            }
        }
    }
}
