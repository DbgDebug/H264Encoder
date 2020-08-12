#pragma once
// Must use it higher than C++11

#include <condition_variable>
#include <mutex>
#include <queue>
#include <vector>
#include <assert.h>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

template<typename T>
class BoundedBlockingQueue {
public:
	// make class non-copyable
	BoundedBlockingQueue(const BoundedBlockingQueue<T>&) = delete;
	BoundedBlockingQueue& operator=(const BoundedBlockingQueue<T>&) = delete;

	explicit BoundedBlockingQueue<T>(size_t maxSize)
		: mtx_(),
		maxSize_(maxSize)
	{

	}

	void put(const T& x) {
	//	std::cout << std::this_thread::get_id() << " puting" << x << std::endl;
		std::unique_lock<std::mutex> locker(mtx_);
		notFullCV_.wait(locker, [this]() {return queue_.size() < maxSize_; });			

		queue_.push(x);
		notEmptyCV_.notify_one();
	}

	T take() {
	//	std::cout << std::this_thread::get_id() << " taking" << std::endl;
		std::unique_lock<std::mutex> locker(mtx_);
		notEmptyCV_.wait(locker, [this]() {return !queue_.empty(); });

		T front(queue_.front());
		queue_.pop();
		notFullCV_.notify_one();

		return front;
	}

	// with time out
	// @param timeout: max wait time, ms
	// @param outRes: reference result if take successfully
	// @return take successfully or not
	bool take(int timeout, T& outRes) {
		std::unique_lock<std::mutex> locker(mtx_);
		notEmptyCV_.wait_for(locker, timeout*1ms, [this]() {return !queue_.empty(); });
		if(queue_.empty()) return false;
		
		outRes = queue_.front(); queue_.pop();
		notFullCV_.notify_one();

		return true;
	}

	// Checking BlockingQueue status from outside
	// DO NOT use it as internal call, which will cause DEADLOCK
	bool empty() const {
		std::unique_lock<std::mutex> locker(mtx_);
		return queue_.empty();
	}

	size_t size() const {
		std::unique_lock<std::mutex> locker(mtx_);
		return queue_.size();
	}

	size_t maxSize() const {
		return maxSize_;
	}

private:
	mutable std::mutex mtx_;
	std::condition_variable notEmptyCV_;
	std::condition_variable notFullCV_;
	size_t maxSize_;
	std::queue<T> queue_;
};