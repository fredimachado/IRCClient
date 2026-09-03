#pragma once

#include <ios>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>

class SyncOStream
{
public:
    explicit SyncOStream(std::ostream& stream)
        : stream_(stream), mutex_(mutex_for(stream)), lock_(*mutex_)
    {
    }

    template <typename T>
    SyncOStream& operator<<(T const& value)
    {
        stream_ << value;
        return *this;
    }

    SyncOStream& operator<<(std::ostream& (*manipulator)(std::ostream&))
    {
        stream_ << manipulator;
        return *this;
    }

    SyncOStream& operator<<(std::ios_base& (*manipulator)(std::ios_base&))
    {
        stream_ << manipulator;
        return *this;
    }

private:
    static std::shared_ptr<std::mutex> mutex_for(std::ostream& stream)
    {
        static std::mutex registry_mutex;
        static std::map<std::streambuf*, std::shared_ptr<std::mutex>> mutexes;
        static auto fallback_mutex = std::make_shared<std::mutex>();

        std::streambuf* const buffer = stream.rdbuf();
        if (buffer == nullptr)
            return fallback_mutex;

        std::lock_guard<std::mutex> registry_lock(registry_mutex);
        std::shared_ptr<std::mutex>& mutex = mutexes[buffer];
        if (!mutex)
            mutex = std::make_shared<std::mutex>();
        return mutex;
    }

    std::ostream& stream_;
    std::shared_ptr<std::mutex> mutex_;
    std::unique_lock<std::mutex> lock_;
};
