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
    struct StreamMutexSlot
    {
        std::streambuf* buffer = nullptr;
        std::shared_ptr<std::mutex> mutex;
    };

    static int mutex_slot_index()
    {
        static int const index = std::ios_base::xalloc();
        return index;
    }

    static int callback_flag_index()
    {
        static int const index = std::ios_base::xalloc();
        return index;
    }

    static std::mutex& registry_mutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    static std::map<std::streambuf*, std::weak_ptr<std::mutex>>& mutex_registry()
    {
        static std::map<std::streambuf*, std::weak_ptr<std::mutex>> mutexes;
        return mutexes;
    }

    static std::shared_ptr<std::mutex> fallback_mutex()
    {
        static auto mutex = std::make_shared<std::mutex>();
        return mutex;
    }

    static void cleanup_registry_entry(std::streambuf* buffer)
    {
        if (buffer == nullptr)
            return;

        auto& mutexes = mutex_registry();
        auto const it = mutexes.find(buffer);
        if (it != mutexes.end() && it->second.expired())
            mutexes.erase(it);
    }

    static void cleanup_stream_mutex(std::ios_base::event event, std::ios_base& stream, int)
    {
        if (event != std::ios_base::erase_event)
            return;

        auto* slot = static_cast<StreamMutexSlot*>(stream.pword(mutex_slot_index()));
        if (slot == nullptr)
            return;

        std::streambuf* const buffer = slot->buffer;
        delete slot;
        stream.pword(mutex_slot_index()) = nullptr;
        stream.iword(callback_flag_index()) = 0;

        std::lock_guard<std::mutex> registry_lock(registry_mutex());
        cleanup_registry_entry(buffer);
    }

    static std::shared_ptr<std::mutex> mutex_for(std::ostream& stream)
    {
        std::streambuf* const buffer = stream.rdbuf();
        if (buffer == nullptr)
            return fallback_mutex();

        if (stream.iword(callback_flag_index()) == 0)
        {
            stream.register_callback(&cleanup_stream_mutex, 0);
            stream.iword(callback_flag_index()) = 1;
        }

        void*& slot_ref = stream.pword(mutex_slot_index());
        auto* slot = static_cast<StreamMutexSlot*>(slot_ref);
        if (slot != nullptr && slot->buffer == buffer && slot->mutex)
            return slot->mutex;

        std::lock_guard<std::mutex> registry_lock(registry_mutex());
        auto& mutexes = mutex_registry();
        std::shared_ptr<std::mutex> mutex = mutexes[buffer].lock();
        if (!mutex)
        {
            mutex = std::make_shared<std::mutex>();
            mutexes[buffer] = mutex;
        }

        std::streambuf* const previous_buffer = slot != nullptr ? slot->buffer : nullptr;
        if (slot == nullptr)
        {
            slot = new StreamMutexSlot();
            slot_ref = slot;
        }
        slot->buffer = buffer;
        slot->mutex = mutex;

        if (previous_buffer != buffer)
            cleanup_registry_entry(previous_buffer);

        return mutex;
    }

    std::ostream& stream_;
    std::shared_ptr<std::mutex> mutex_;
    std::unique_lock<std::mutex> lock_;
};
