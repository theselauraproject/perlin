#pragma once
#include <atomic>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

namespace perlin {
    class StopToken {
    public:
        StopToken() = default;
        explicit StopToken(std::shared_ptr<std::atomic<bool>> flag) : flag(std::move(flag)) {}
        bool stop_requested() const noexcept { return flag && flag->load(std::memory_order_acquire); }

    private:
        std::shared_ptr<std::atomic<bool>> flag;
    };

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
            entry.stop = std::make_shared<std::atomic<bool>>(false);
            entry.thread = std::thread([task = std::forward<Fn>(task), done = entry.done, token = StopToken(entry.stop)]() mutable {
                if constexpr (std::is_invocable_v<Fn&, StopToken>)
                    task(token);
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
                e.stop->store(true, std::memory_order_release);
            for (Entry& e : pending)
                if (e.thread.joinable())
                    e.thread.join();
        }

        size_t Active() {
            std::lock_guard lock(mutex);
            Reap();
            return entries.size();
        }

    private:
        struct Entry {
            std::thread thread;
            std::shared_ptr<std::atomic<bool>> done;
            std::shared_ptr<std::atomic<bool>> stop;
        };

        void Reap() {
            for (auto it = entries.begin(); it != entries.end();) {
                if (it->done->load(std::memory_order_acquire)) {
                    if (it->thread.joinable())
                        it->thread.join();
                    it = entries.erase(it);
                } else
                    ++it;
            }
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
