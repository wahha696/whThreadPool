#include <iostream>
#include <queue>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <future>
#include "whThreadPool.h"

void testFunction(int id) {
    std::cout << "Task " << id << " started by thread " << std::this_thread::get_id() << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::cout << "Task " << id << " completed by thread " << std::this_thread::get_id() << std::endl;
}

int main()
{
    std::cout << "Creating thread pool..." << std::endl;
    whThreadPool pool(2, 4, 10, 1000); // min 2, max 4, queue 10, check every 1s

    std::cout << "Adding tasks..." << std::endl;
    for (int i = 0; i < 5; ++i) {
        pool.addTask(testFunction, i);
    }

    std::cout << "Waiting for tasks to complete..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "Program ending, thread pool will wait for remaining tasks..." << std::endl;
    return 0;
}