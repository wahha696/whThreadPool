#ifndef _WHTHREADPOOL_H_
#define _WHTHREADPOOL_H_
#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <queue>
#include <map>

class whThreadPool
{
public:
    /**
     * @brief 构造函数
     * @param mintp 最小线程数，必填
     * @param maxtp 最大线程数，默认为核心数-1
     * @param maxTaskSize 任务队列最大长度，默认为核心数-1
     * @param opTIme 管理者线程每次检查的间隔
     */
    whThreadPool(int mintp, int maxtp = -1, int maxTaskSize = -1, int opTime = 2000);

    /**
     * @brief 析构函数
     */
    ~whThreadPool() noexcept;

    /**
     * @brief 手动释放线程池
     * @note 析构函数中会调用该函数, 也可以在析构前手动调用该函数, 以提前释放线程池资源
     * @return 无
     */
    void freeThreadPool();

    /**
     * @brief 添加任务的函数，带参数
     * @param func 需要完成的任务
     * @param args func的参数
     * @return 成功返回true，失败返回false
     */
    template <typename Function, typename... Args>
    bool addTask(Function &&func, Args &&...args);

    /**
     * @brief 添加无参数的任务
     * @param func 需要完成的任务
     * @return 成功返回true，失败返回false
     */
    template <typename Function>
    bool addTask(Function &&func);

private:
    /**
     * @brief 工作线程的主体，静态函数，显示传递引用，生命周期清晰，拓展性好
     * @param pool 传入该对象本身的引用，std::ref(*this)
     */
    static void work_thread(whThreadPool &pool);

    /**
     * @brief 管理者线程的主体，静态函数，显示传递引用，生命周期清晰，拓展性好
     * @param pool 传入该对象本身的引用，std::ref(*this)
     */
    static void op_thread(whThreadPool &pool);

    std::mutex m_mutex; // 全局互斥锁

    std::queue<std::function<void()>> m_tasks; // 任务队列

    std::map<std::thread::id, std::thread *> m_workers; // 工作线程用map存，方便查找
    std::queue<std::thread *> m_freeWorkers; // 需要释放的线程队列

    std::condition_variable m_Empty;   // 条件变量：任务队列空了，用于消费者消费时判断
    std::condition_variable m_Full;    // 条件变量：任务队列满了，用于生产者生产时判断
    std::condition_variable m_Release; // 需要释放线程的条件变量，用于线程释放时

    //----------------------线程池信息----------------

    std::thread *m_op;
    int m_opTime; // 管理者线程每次检查的间隔

    size_t m_maxTaskSize; // 任务队列可存储的最大数量
    int m_maxtp;       // 最大的线程数量
    int m_mintp;       // 最小的线程数量
    int m_singleAdd;    // 单次增加的线程数量
    int m_singleSub;    // 单次减少的线程数量

    std::atomic_int m_activeNum;  // 在工作的线程数量
    std::atomic_int m_restNum;    // 在休息的线程数量
    std::atomic_int m_allNum;     // 总的线程数量 = m_activeNum + m_restNum
    std::atomic_int m_releaseNum; // 需要释放的线程数量

    std::atomic_bool m_stop; // 线程池是否停止
};

template <typename Function, typename... Args>
bool whThreadPool::addTask(Function &&func, Args &&...args)
{
    std::unique_lock<std::mutex> lock(m_mutex); // 获取锁
    m_Full.wait(lock, [this]
                { return m_tasks.size() < m_maxTaskSize || m_stop.load(); });

    if (this->m_stop.load())
    {
        printf("无法添加任务：线程池已停止！\n");
        return false;
    }

    if (m_tasks.size() < m_maxTaskSize)
    {
        this->m_tasks.emplace(std::bind(std::forward<Function>(func), std::forward<Args>(args)...));
        // 通知workers工作
        this->m_Empty.notify_one();
        printf("任务添加成功！\n");
        return true;
    }

    printf("无法添加任务：队列已满！\n");
    return false;
}

template <typename Function>
bool whThreadPool::addTask(Function &&func)
{
    std::unique_lock<std::mutex> lock(m_mutex); // 获取锁
    m_Full.wait(lock, [this]
                { return m_tasks.size() < m_maxTaskSize || m_stop.load(); });

    if (this->m_stop.load())
    {
        printf("无法添加任务：线程池已停止！\n");
        return false;
    }

    if (m_tasks.size() < m_maxTaskSize)
    {
        this->m_tasks.emplace(std::forward<Function>(func));
        // 通知workers工作
        this->m_Empty.notify_one();
        printf("任务添加成功！\n");
        return true;
    }

    printf("无法添加任务：队列已满！\n");
    return false;
}

#endif