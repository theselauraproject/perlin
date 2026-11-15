#pragma once
#include <atomic>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <utility>

namespace perlin {
    class TaskGroup {
    public:
        TaskGroup() = default;
        TaskGroup(const TaskGroup&) = delete;
        TaskGroup& operator=(const TaskGroup&) = delete;
        ~TaskGroup() { Shutdown(); }

        template <class Fn> bool Run(Fn&& task) {
            std::lock_guard lock(mutex);
            if (closed)
                return false;
            Reap();
            auto& entry = entries.emplace_back();
            entry.done = std::make_shared<std::atomic<bool>>(false);
            entry.thread = std::jthread([task = std::forward<Fn>(task), done = entry.done](std::stop_token stop) mutable {
                if constexpr (std::is_invocable_v<Fn&, std::stop_token>)
                    task(stop);
                else
                    task();
                done->store(true, std::memory_order_release);
            });
            return true;
        }

        void Shutdown() {
            std::list<Entry> pending;
            {
                std::lock_guard lock(mutex);
                closed = true;
                pending = std::move(entries);
            }
            for (Entry& e : pending)
                e.thread.request_stop();
            pending.clear();
        }

        size_t Active() {
            std::lock_guard lock(mutex);
            Reap();
            return entries.size();
        }

    private:
        struct Entry {
            std::jthread thread;
            std::shared_ptr<std::atomic<bool>> done;
        };

        void Reap() {
            std::erase_if(entries, [](const Entry& e) { return e.done->load(std::memory_order_acquire); });
        }

        std::mutex mutex;
        std::list<Entry> entries;
        bool closed = false;
    };

    inline TaskGroup& ProcessTasks() {
        static TaskGroup group;
        return group;
    }
}
