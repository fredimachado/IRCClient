#ifndef IRCCLIENT_PROCESS_HELPER_H
#define IRCCLIENT_PROCESS_HELPER_H

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Starts the built CLI, captures stdout/stderr, writes stdin, and kills
// the child if a wait times out. Portable Windows and POSIX.
class ProcessHelper
{
public:
    ProcessHelper();
    ~ProcessHelper();

    ProcessHelper(const ProcessHelper&) = delete;
    ProcessHelper& operator=(const ProcessHelper&) = delete;
    ProcessHelper(ProcessHelper&&) = delete;
    ProcessHelper& operator=(ProcessHelper&&) = delete;

    bool start(const std::filesystem::path& executable, const std::vector<std::string>& args);
    bool write_stdin(std::string_view data);
    bool close_stdin();

    // Waits up to timeout for the child to exit. On timeout the child is
    // killed and this returns nullopt. Otherwise returns the exit code.
    std::optional<int> wait_for_exit(std::chrono::milliseconds timeout);
    void terminate();

    [[nodiscard]] bool running() const;
    [[nodiscard]] std::string stdout_text() const;
    [[nodiscard]] std::string stderr_text() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
