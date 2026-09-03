#include "SyncOStream.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
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
}
