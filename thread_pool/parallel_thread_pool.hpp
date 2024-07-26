#pragma once 

#include "function_wrapper.hpp"

namespace qg {

class parrallen_thread_pool {

private: 
	void worker_thread(int index) {
		while (!done) {
			auto task_ptr = thread_work_ques[index].wait_pop();
			if (task_ptr == nullptr) {
				continue;
			}
			(*task_ptr)();
		}
	}

public:
	static parrallen_thread_pool& instance() {
		static parrallen_thread_pool pool;
		return pool;
	}

	~parrallen_thread_pool() {
		done = true;
		for (unsigned i = 0; i < thread_work_ques.size(); i++) {
			thread_work_ques[i].exit();
		}
		
		for (unsigned i = 0; i < threads.size(); ++i) {
			threads[i].join();
		}
	}

	template<typename FunctionType>
	std::future<typename std::result_of<FunctionType()>::type>
	submit(FunctionType f) {
		int index = (atm_index.load() + 1) % thread_work_ques.size();
		atm_index.store(index);
		typedef typename std::result_of<FunctionType()>::type result_type;
		std::packaged_task<result_type()> task(std::move(f));
		std::future<result_type> res(task.get_future());
		thread_work_ques[index].push(std::move(task));
		return res;
	}

private:
	parrallen_thread_pool() : done(false), joiner(threads), atm_index(0) {
		unsigned const thread_count = std::thread::hardware_concurrency();
		try {
			thread_work_ques = std::vector < safe_queue<function_wrapper>>(thread_count);
			for (unsigned i = 0; i < thread_count; ++i)
				threads.push_back(std::thread(&parrallen_thread_pool::worker_thread, this, i));
		} catch (...) {
			done = true;
			for (int i = 0; i < thread_work_ques.size(); i++) {
				thread_work_ques[i].exit();
			}
			throw;
		}
	}

	std::atomic_bool done;
	std::vector<safe_queue<function_wrapper>> thread_work_ques;
	std::vector<std::thread> threads;
	join_threads joiner;
	std::atomic<int> atm_index;
};

} // namespace qg