#pragma once

#include "function_wrapper.hpp"

namespace qg {

class notify_thread_pool {

private:
	void worker_thread() {
		while (!done) {
			auto task_ptr = work_queue.wait_pop();
			if (task_ptr == nullptr) continue;
			(*task_ptr)();
		}
	}

public:
	static notify_thread_pool& instance() {
		static notify_thread_pool pool;
		return pool;
	}

	~notify_thread_pool() {		
        done = true;
		work_queue.exit();
		for (unsigned i = 0; i < threads.size(); ++i) {
			threads[i].join();
		}
	}

	template<typename FunctionType>
	std::future<typename std::result_of<FunctionType()>::type>   
	submit(FunctionType f) {
		typedef typename std::result_of<FunctionType()>::type result_type;   
        std::packaged_task<result_type()> task(std::move(f));   
        std::future<result_type> res(task.get_future());    
        work_queue.push(std::move(task));    
        return res;   
	}

private:
	notify_thread_pool() : done(false), joiner(threads) {
		unsigned const thread_count = std::thread::hardware_concurrency();
		try {
			for (unsigned i = 0; i < thread_count; ++i)
			    threads.push_back(std::thread(&notify_thread_pool::worker_thread, this));
		} catch (...) {
			done = true;
			work_queue.exit();
			throw;
		}
	}

	std::atomic_bool done;
	safe_queue<function_wrapper> work_queue;
	std::vector<std::thread> threads;
	join_threads joiner;
};

} // namesapce qg