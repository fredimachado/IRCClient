#include "../support/LoopbackServer.h"
#include "../support/ProcessHelper.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <cstdint>
#else
#include <unistd.h>
#endif

namespace {

constexpr auto kWait = std::chrono::seconds{5};

#ifdef _WIN32
constexpr const char* kCliName = "ircclient.exe";
#else
constexpr const char* kCliName = "ircclient";
#endif

std::filesystem::path this_executable_dir()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD const n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return std::filesystem::current_path();
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0)
        return std::filesystem::current_path();
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(buf, ec);
    if (ec)
        return std::filesystem::path(buf).parent_path();
    return canonical.parent_path();
#else
    char buf[4096];
    ssize_t const n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return std::filesystem::current_path();
    buf[n] = '\0';
    return std::filesystem::path(buf).parent_path();
#endif
}

bool is_cli_file(std::filesystem::path const& path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::filesystem::path find_cli_binary()
{
#ifdef _WIN32
    char* env_buf = nullptr;
    std::size_t env_len = 0;
    if (_dupenv_s(&env_buf, &env_len, "IRCCLIENT_BIN") == 0 && env_buf != nullptr)
    {
        std::filesystem::path from_env(env_buf);
        free(env_buf);
        if (is_cli_file(from_env))
            return std::filesystem::absolute(from_env);
    }
#else
    if (char const* env = std::getenv("IRCCLIENT_BIN"))
    {
        std::filesystem::path from_env(env);
        if (is_cli_file(from_env))
            return std::filesystem::absolute(from_env);
    }
#endif

    std::filesystem::path const testdir = this_executable_dir();
    std::filesystem::path const cwd = std::filesystem::current_path();
    std::filesystem::path const parents[] = {
        testdir,
        testdir.parent_path(),
        testdir.parent_path() / "Debug",
        testdir.parent_path() / "Release",
        testdir.parent_path().parent_path(),
        testdir.parent_path().parent_path() / "Debug",
        testdir.parent_path().parent_path() / "Release",
        cwd,
        cwd / "Debug",
        cwd / "Release",
        cwd.parent_path(),
        cwd.parent_path() / "Debug",
        cwd.parent_path() / "Release",
    };

    for (auto const& dir : parents)
    {
        if (is_cli_file(dir / kCliName))
            return std::filesystem::absolute(dir / kCliName);
#ifndef _WIN32
        if (is_cli_file(dir / "ircclient"))
            return std::filesystem::absolute(dir / "ircclient");
#endif
    }

    return {};
}

std::filesystem::path require_cli()
{
    std::filesystem::path const cli = find_cli_binary();
    INFO("CLI binary not found. Set IRCCLIENT_BIN to the ircclient executable.");
    REQUIRE_FALSE(cli.empty());
    return cli;
}

std::string as_text(std::vector<std::uint8_t> const& bytes)
{
    return std::string(reinterpret_cast<char const*>(bytes.data()), bytes.size());
}

std::string registration_bytes(std::string_view nick, std::string_view user)
{
    std::string out = "NICK ";
    out += nick;
    out += "\r\nUSER ";
    out += user;
    out += " 8 * :Cpp IRC Client\r\n";
    return out;
}

} // namespace

TEST_CASE("CLI /quit sends QUIT and the process exits")
{
    auto const cli = require_cli();
    LoopbackServer server;
    ProcessHelper proc;

    REQUIRE(proc.start(
        cli,
        {"127.0.0.1", std::to_string(server.ipv4_port()), "alice", "aliceuser"}));
    REQUIRE(server.wait_for_connection(kWait));

    std::string const expected = registration_bytes("alice", "aliceuser");
    REQUIRE(server.wait_until_received(expected.size(), kWait));

    REQUIRE(proc.write_stdin("/quit\n"));
    auto const code = proc.wait_for_exit(kWait);
    REQUIRE(code.has_value());
    REQUIRE(*code == 0);

    std::string const got = as_text(server.received());
    REQUIRE(got.find("QUIT\r\n") != std::string::npos);
}

TEST_CASE("CLI stdin EOF exits and sends QUIT")
{
    auto const cli = require_cli();
    LoopbackServer server;
    ProcessHelper proc;

    REQUIRE(proc.start(
        cli,
        {"127.0.0.1", std::to_string(server.ipv4_port()), "alice", "aliceuser"}));
    REQUIRE(server.wait_for_connection(kWait));

    std::string const expected = registration_bytes("alice", "aliceuser");
    REQUIRE(server.wait_until_received(expected.size(), kWait));

    REQUIRE(proc.close_stdin());
    auto const code = proc.wait_for_exit(kWait);
    REQUIRE(code.has_value());
    REQUIRE(*code == 0);

    std::string const got = as_text(server.received());
    REQUIRE(got.find("QUIT\r\n") != std::string::npos);
}

#ifdef _WIN32
TEST_CASE("CLI waits efficiently for the rest of a piped stdin line")
{
    auto const cli = require_cli();
    LoopbackServer server;
    ProcessHelper proc;

    REQUIRE(proc.start(
        cli,
        {"127.0.0.1", std::to_string(server.ipv4_port()), "alice", "aliceuser"}));
    REQUIRE(server.wait_for_connection(kWait));

    std::string const expected = registration_bytes("alice", "aliceuser");
    REQUIRE(server.wait_until_received(expected.size(), kWait));

    auto const before = proc.cpu_time();
    REQUIRE(before.has_value());
    REQUIRE(proc.write_stdin("partial"));
    std::this_thread::sleep_for(std::chrono::milliseconds{400});
    auto const after = proc.cpu_time();
    REQUIRE(after.has_value());

    REQUIRE(*after - *before < std::chrono::milliseconds{150});

    REQUIRE(proc.write_stdin("\n/quit\n"));
    auto const code = proc.wait_for_exit(kWait);
    REQUIRE(code.has_value());
    REQUIRE(*code == 0);
}
#endif

TEST_CASE("CLI remote disconnect exits without hanging")
{
    auto const cli = require_cli();
    LoopbackServer server;
    ProcessHelper proc;

    REQUIRE(proc.start(
        cli,
        {"127.0.0.1", std::to_string(server.ipv4_port()), "alice", "aliceuser"}));
    REQUIRE(server.wait_for_connection(kWait));

    std::string const expected = registration_bytes("alice", "aliceuser");
    REQUIRE(server.wait_until_received(expected.size(), kWait));

    server.stop();
    auto const code = proc.wait_for_exit(kWait);
    REQUIRE(code.has_value());
    REQUIRE(*code == 0);
}
