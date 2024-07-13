#ifndef __THREAD_POOL_H__
#define __THREAD_POOL_H__

#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <functional>
#include <future>

class function_wrapper {
public:
    function_wrapper() = default;
    template<typename F> function_wrapper(F&& f) : impl { new impl_type<F>(std::move(f)) } {} 
    function_wrapper(function_wrapper&& other) : impl { std::move(other.impl) } {}
    function_wrapper(const function_wrapper&) = delete;
    function_wrapper(function_wrapper&) = delete;
    function_wrapper& operator=(function_wrapper&& other) {
        impl = std::move(other.impl);
        return *this;
    }
    function_wrapper& operator=(const function_wrapper&) = delete;
    function_wrapper& operator=(function_wrapper&) = delete;

    void call() { impl->call(); }

private:
    struct impl_base {
        virtual void call() = 0;
        virtual ~impl_base() {}
    };
    std::unique_ptr<impl_base> impl;

    template<typename F> struct impl_type : impl_base {
        F func;

        impl_type(F&& f) : func { std::move(f) } {} 
        void call() { func(); }
    };

};

template<typename T> class safe_queue {
public:
    safe_queue() {}
    safe_queue(safe_queue&& other) {}
    ~safe_queue() {}

    bool empty() {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    int size() {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

    void push(T& t) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_queue.emplace(t);
    }

    bool try_pop(T& t) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_queue.empty()) return false;
        t = std::move(m_queue.front());
        m_queue.pop();
        return true;
    }

private:
    std::queue<T> m_queue;
    std::mutex m_mutex;

};

class stealing_queue {
using data_type = function_wrapper;

public:
    stealing_queue() {}
    stealing_queue(const stealing_queue&) = delete;
    stealing_queue& operator=(const stealing_queue&) = delete;

    bool empty() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    int size() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

    void push(data_type data) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_front(std::move(data));
    }

    bool try_pop(data_type& data) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) return false;
        data = std::move(m_queue.front());
        m_queue.pop_front();
        return true;
    }

    bool try_steal(data_type& data) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) return false;
        data = std::move(m_queue.back());
        m_queue.pop_back();
        return true;
    }

private:
    std::deque<data_type> m_queue;
    mutable std::mutex m_mutex;

};

class thread_pool {
using task_type = function_wrapper;

public:
    thread_pool(unsigned int n_thread = std::thread::hardware_concurrency()) : done { false } {
        try {
            for (int i = 0; i < n_thread; i++) {
                m_private_tasks.push_back(std::unique_ptr<stealing_queue>(new stealing_queue));
                m_threads.push_back(std::thread(&worker_thread, this, i));
            }
        } catch(...) {
            done = true;
            throw;
        }
    }
    ~thread_pool() { done = true; }

    bool try_pop_from_public(task_type& task) {
        return m_public_tasks.try_pop(task);
    }

    bool try_pop_from_private(task_type& task) {
        return local_tasks && local_tasks->try_pop(task);
    }

    bool try_steal(task_type& task) {
        for (unsigned int i = 0; i < m_private_tasks.size(); i++) {
            unsigned int idx = (local_index + 1 + i) % m_private_tasks.size();
            if (m_private_tasks[idx]->try_steal(task)) return true;
        }
        return false;
    }

    template<typename F, typename... Args>
    std::future<typename std::result_of<F()>::type> submit(F func, Args... args) {
        using result_type = typename std::result_of<decltype(func(args...))()>::type;

        std::packaged_task<result_type()> task(std::move(func));
        std::future<result_type> ret(task.get_future());
        if (local_tasks) local_tasks->push(std::move(task));
        else m_public_tasks.push(std::move(task));
        return ret;
    }

    void run_pending_task() {
        task_type task;
        if (try_pop_from_private(task) || try_pop_from_public(task) || try_steal(task)) task.call();
        else std::this_thread::yield();
    }

    void worker_thread(unsigned int index) {
        local_index = index;
        local_tasks = m_private_tasks[index].get();
        while (!done) run_pending_task();
    }

    void shutdown() {

    }

private:
    std::atomic_bool done;
    safe_queue<task_type> m_public_tasks;
    std::vector<std::unique_ptr<stealing_queue>> m_private_tasks;
    std::vector<std::thread> m_threads;
    std::condition_variable m_conditional_lock;

    static thread_local stealing_queue* local_tasks;
    static thread_local unsigned int local_index;
};

#endif // !__THREAD_POOL_H__