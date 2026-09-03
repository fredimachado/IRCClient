#include "ProcessHelper.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32
std::wstring utf8_to_wide(std::string_view text)
{
    if (text.empty())
        return {};
    int wide_count = MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (wide_count <= 0)
        return {};
    std::wstring wide(static_cast<std::size_t>(wide_count), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        wide.data(),
        wide_count);
    return wide;
}

std::wstring quote_windows_arg(const std::wstring& arg)
{
    if (arg.find_first_of(L" \t\"") == std::wstring::npos)
        return arg;

    std::wstring quoted;
    quoted.push_back(L'"');
    for (wchar_t ch : arg)
    {
        if (ch == L'"')
            quoted.push_back(L'\\');
        quoted.push_back(ch);
    }
    quoted.push_back(L'"');
    return quoted;
}

void close_handle(HANDLE& handle)
{
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(handle);
        handle = nullptr;
    }
}
#else
void close_fd(int& fd)
{
    if (fd >= 0)
    {
        ::close(fd);
        fd = -1;
    }
}
#endif

} // namespace

struct ProcessHelper::Impl
{
    mutable std::mutex mutex;
    std::string stdout_buf;
    std::string stderr_buf;
    std::thread stdout_thread;
    std::thread stderr_thread;
    std::atomic<bool> alive{false};
    std::optional<int> exit_code;

#ifdef _WIN32
    HANDLE process = nullptr;
    HANDLE stdin_write = nullptr;
    HANDLE stdout_read = nullptr;
    HANDLE stderr_read = nullptr;
#else
    pid_t pid = -1;
    int stdin_fd = -1;
    int stdout_fd = -1;
    int stderr_fd = -1;
#endif

    ~Impl()
    {
        terminate_if_running();
        reap(std::chrono::milliseconds{2000});
        join_readers();
        close_all();
    }

    void join_readers()
    {
        if (stdout_thread.joinable())
            stdout_thread.join();
        if (stderr_thread.joinable())
            stderr_thread.join();
    }

    void close_all()
    {
#ifdef _WIN32
        close_handle(stdin_write);
        close_handle(stdout_read);
        close_handle(stderr_read);
        close_handle(process);
#else
        close_fd(stdin_fd);
        close_fd(stdout_fd);
        close_fd(stderr_fd);
#endif
    }

    void terminate_if_running()
    {
        if (!alive.load(std::memory_order_acquire))
            return;
#ifdef _WIN32
        if (process != nullptr)
            TerminateProcess(process, 1);
#else
        if (pid > 0)
            ::kill(pid, SIGKILL);
#endif
    }

    bool reap(std::chrono::milliseconds timeout)
    {
#ifdef _WIN32
        if (process == nullptr)
            return true;
        DWORD ms = 0;
        if (timeout.count() > 0)
        {
            const auto clamped = std::min<std::chrono::milliseconds::rep>(
                timeout.count(),
                static_cast<std::chrono::milliseconds::rep>(INFINITE - 1));
            ms = static_cast<DWORD>(clamped);
        }
        DWORD wait = WaitForSingleObject(process, ms);
        if (wait != WAIT_OBJECT_0)
            return false;
        DWORD code = 0;
        GetExitCodeProcess(process, &code);
        exit_code = static_cast<int>(code);
        alive.store(false, std::memory_order_release);
        return true;
#else
        if (pid <= 0)
            return true;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;)
        {
            int status = 0;
            pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid)
            {
                if (WIFEXITED(status))
                    exit_code = WEXITSTATUS(status);
                else if (WIFSIGNALED(status))
                    exit_code = 128 + WTERMSIG(status);
                else
                    exit_code = -1;
                alive.store(false, std::memory_order_release);
                pid = -1;
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
#endif
    }

#ifdef _WIN32
    static void read_handle(HANDLE handle, std::string& dest, std::mutex& mutex)
    {
        char buffer[4096];
        DWORD n = 0;
        while (ReadFile(handle, buffer, sizeof(buffer), &n, nullptr) && n > 0)
        {
            std::lock_guard lock(mutex);
            dest.append(buffer, buffer + n);
        }
    }
#else
    static void read_pipe(int fd, std::string& dest, std::mutex& mutex)
    {
        char buffer[4096];
        for (;;)
        {
            ssize_t n = ::read(fd, buffer, sizeof(buffer));
            if (n > 0)
            {
                std::lock_guard lock(mutex);
                dest.append(buffer, buffer + n);
                continue;
            }
            if (n == 0)
                break;
            if (errno == EINTR)
                continue;
            break;
        }
    }
#endif
};

ProcessHelper::ProcessHelper() : impl_(std::make_unique<Impl>()) {}

ProcessHelper::~ProcessHelper() = default;

bool ProcessHelper::start(
    const std::filesystem::path& executable,
    const std::vector<std::string>& args)
{
    if (impl_->alive.load(std::memory_order_acquire))
        return false;

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdout_read = nullptr;
    HANDLE stdout_write = nullptr;
    HANDLE stderr_read = nullptr;
    HANDLE stderr_write = nullptr;
    HANDLE stdin_read = nullptr;
    HANDLE stdin_write = nullptr;

    auto fail_pipes = [&]() {
        close_handle(stdout_read);
        close_handle(stdout_write);
        close_handle(stderr_read);
        close_handle(stderr_write);
        close_handle(stdin_read);
        close_handle(stdin_write);
        return false;
    };

    if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0))
        return false;
    if (!SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0))
        return fail_pipes();
    if (!CreatePipe(&stderr_read, &stderr_write, &sa, 0))
        return fail_pipes();
    if (!SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0))
        return fail_pipes();
    if (!CreatePipe(&stdin_read, &stdin_write, &sa, 0))
        return fail_pipes();
    if (!SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0))
        return fail_pipes();

    std::wstring cmd = quote_windows_arg(executable.wstring());
    for (const auto& arg : args)
    {
        cmd.push_back(L' ');
        cmd += quote_windows_arg(utf8_to_wide(arg));
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdin_read;
    si.hStdOutput = stdout_write;
    si.hStdError = stderr_write;

    PROCESS_INFORMATION pi{};
    std::wstring mutable_cmd = cmd;
    BOOL created = CreateProcessW(
        executable.c_str(),
        mutable_cmd.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);

    close_handle(stdin_read);
    close_handle(stdout_write);
    close_handle(stderr_write);

    if (!created)
    {
        close_handle(stdin_write);
        close_handle(stdout_read);
        close_handle(stderr_read);
        return false;
    }

    CloseHandle(pi.hThread);
    impl_->process = pi.hProcess;
    impl_->stdin_write = stdin_write;
    impl_->stdout_read = stdout_read;
    impl_->stderr_read = stderr_read;
    impl_->alive.store(true, std::memory_order_release);

    impl_->stdout_thread = std::thread([this] {
        Impl::read_handle(impl_->stdout_read, impl_->stdout_buf, impl_->mutex);
    });
    impl_->stderr_thread = std::thread([this] {
        Impl::read_handle(impl_->stderr_read, impl_->stderr_buf, impl_->mutex);
    });
    return true;
#else
    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0)
    {
        close_fd(stdin_pipe[0]);
        close_fd(stdin_pipe[1]);
        close_fd(stdout_pipe[0]);
        close_fd(stdout_pipe[1]);
        close_fd(stderr_pipe[0]);
        close_fd(stderr_pipe[1]);
        return false;
    }

    fcntl(stdin_pipe[1], F_SETFD, FD_CLOEXEC);
    fcntl(stdout_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(stderr_pipe[0], F_SETFD, FD_CLOEXEC);

    pid_t child = fork();
    if (child < 0)
    {
        close_fd(stdin_pipe[0]);
        close_fd(stdin_pipe[1]);
        close_fd(stdout_pipe[0]);
        close_fd(stdout_pipe[1]);
        close_fd(stderr_pipe[0]);
        close_fd(stderr_pipe[1]);
        return false;
    }

    if (child == 0)
    {
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);

        std::vector<std::string> storage;
        storage.reserve(args.size() + 1);
        storage.push_back(executable.string());
        storage.insert(storage.end(), args.begin(), args.end());
        std::vector<char*> argv;
        argv.reserve(storage.size() + 1);
        for (auto& item : storage)
            argv.push_back(item.data());
        argv.push_back(nullptr);
        execv(argv[0], argv.data());
        _exit(127);
    }

    close_fd(stdin_pipe[0]);
    close_fd(stdout_pipe[1]);
    close_fd(stderr_pipe[1]);

    impl_->pid = child;
    impl_->stdin_fd = stdin_pipe[1];
    impl_->stdout_fd = stdout_pipe[0];
    impl_->stderr_fd = stderr_pipe[0];
    impl_->alive.store(true, std::memory_order_release);

    impl_->stdout_thread = std::thread([this] {
        Impl::read_pipe(impl_->stdout_fd, impl_->stdout_buf, impl_->mutex);
    });
    impl_->stderr_thread = std::thread([this] {
        Impl::read_pipe(impl_->stderr_fd, impl_->stderr_buf, impl_->mutex);
    });
    return true;
#endif
}

bool ProcessHelper::write_stdin(std::string_view data)
{
#ifdef _WIN32
    if (impl_->stdin_write == nullptr)
        return false;
    const char* cursor = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0)
    {
        DWORD chunk = remaining > static_cast<std::size_t>(std::numeric_limits<DWORD>::max())
            ? std::numeric_limits<DWORD>::max()
            : static_cast<DWORD>(remaining);
        DWORD written = 0;
        if (!WriteFile(impl_->stdin_write, cursor, chunk, &written, nullptr) || written == 0)
            return false;
        cursor += written;
        remaining -= written;
    }
    return true;
#else
    if (impl_->stdin_fd < 0)
        return false;
    const char* cursor = data.data();
    std::size_t remaining = data.size();
    while (remaining > 0)
    {
        ssize_t n = ::write(impl_->stdin_fd, cursor, remaining);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return false;
        }
        if (n == 0)
            return false;
        cursor += n;
        remaining -= static_cast<std::size_t>(n);
    }
    return true;
#endif
}

bool ProcessHelper::close_stdin()
{
#ifdef _WIN32
    if (impl_->stdin_write == nullptr)
        return false;
    close_handle(impl_->stdin_write);
    return true;
#else
    if (impl_->stdin_fd < 0)
        return false;
    close_fd(impl_->stdin_fd);
    return true;
#endif
}

std::optional<int> ProcessHelper::wait_for_exit(std::chrono::milliseconds timeout)
{
    if (impl_->reap(timeout))
    {
        impl_->join_readers();
        return impl_->exit_code;
    }
    terminate();
    impl_->reap(std::chrono::milliseconds{2000});
    impl_->join_readers();
    return std::nullopt;
}

void ProcessHelper::terminate()
{
    impl_->terminate_if_running();
}

bool ProcessHelper::running() const
{
    return impl_->alive.load(std::memory_order_acquire);
}

std::optional<std::chrono::milliseconds> ProcessHelper::cpu_time() const
{
#ifdef _WIN32
    if (impl_->process == nullptr)
        return std::nullopt;

    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (!GetProcessTimes(impl_->process, &creation, &exit, &kernel, &user))
        return std::nullopt;

    auto const ticks = [](FILETIME const& time) {
        return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32)
            | static_cast<std::uint64_t>(time.dwLowDateTime);
    };
    constexpr std::uint64_t ticks_per_millisecond = 10'000;
    return std::chrono::milliseconds{
        static_cast<std::chrono::milliseconds::rep>(
            (ticks(kernel) + ticks(user)) / ticks_per_millisecond)};
#else
    return std::nullopt;
#endif
}

std::string ProcessHelper::stdout_text() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->stdout_buf;
}

std::string ProcessHelper::stderr_text() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->stderr_buf;
}
