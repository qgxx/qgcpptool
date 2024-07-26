#pragma once

#include "function_wrapper.hpp"

namespace qg {

class future_thread_pool {

private:
	void worker_thread() {
		while (!done) {
			function_wrapper task;    
			if (work_queue.try_pop(task)) task();
			std::this_thread::yield();
		}
	}

public:
	static future_thread_pool& instance() {
		static  future_thread_pool pool;
		return pool;
	}

	~future_thread_pool() {
		done = true;
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
	future_thread_pool() : done(false), joiner(threads) {
		unsigned const thread_count = std::thread::hardware_concurrency();
		try {
			for (unsigned i = 0; i < thread_count; ++i)
				threads.push_back(std::thread(&future_thread_pool::worker_thread, this));
		} catch (...) {
			done = true;
			throw;
		}
	}

	std::atomic_bool done;
	safe_queue<function_wrapper> work_queue;
	std::vector<std::thread> threads;
	join_threads joiner;
};

} // namespace qg