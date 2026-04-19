#include "whThreadPool.h"

whThreadPool::whThreadPool(int mintp, int maxtp, int maxTaskSize, int opTime)
{
    this->m_stop = false;

    this->m_mintp = mintp;
    this->m_maxtp = (maxtp == -1) ? std::thread::hardware_concurrency() - 1 : maxtp;
    this->m_maxTaskSize = (maxTaskSize == -1) ? static_cast<size_t>(std::thread::hardware_concurrency() - 1) : static_cast<size_t>(maxTaskSize);
    this->m_opTime = opTime;
    this->m_singleAdd = 2;
    this->m_singleSub = 2;

    this->m_activeNum = 0;
    this->m_releaseNum = 0;
    this->m_restNum = mintp;
    this->m_allNum = mintp;

    printf("初始化成员变量成功！\n");

    // 开始创建线程
    for (int i = 0; i < m_allNum; ++i)
    {
        auto tmp = new std::thread(this->work_thread, std::ref(*this));
        m_workers.insert({tmp->get_id(), tmp});
    }
    printf("已启用%d个工作线程！\n", static_cast<int>(m_allNum.load()));

    // 启动管理者线程
    this->m_op = new std::thread(this->op_thread, std::ref(*this));
    printf("已启用管理者线程！\n");
}

void whThreadPool::freeThreadPool() 
{
    this->m_stop = true; // 停止线程池
        // 阻塞回收管理者线程，管理者线程中收到m_stop==true后，会回收工作线程
    this->m_op->join();
    delete this->m_op;
    printf("成功关闭线程池！\n");
}



whThreadPool::~whThreadPool() noexcept
{
    if (!this->m_stop.load())
    {
        freeThreadPool();
    }
}

void whThreadPool::work_thread(whThreadPool &pool)
{
    // 获取锁
    std::unique_lock<std::mutex> lock(pool.m_mutex);
    lock.unlock();
    while (!pool.m_stop)
    {
        lock.lock();
        // 循环等待任务
        while (pool.m_tasks.empty() && !pool.m_stop && pool.m_releaseNum.load() == 0)
        {
            printf("线程%zu没有任务，睡大觉！\n", std::hash<std::thread::id>{}(std::this_thread::get_id()));
            pool.m_Empty.wait(lock);
        }

        // 要自杀
        if (pool.m_stop || pool.m_releaseNum.load() > 0)
        {
            pool.m_releaseNum--;
            auto p = pool.m_workers[std::this_thread::get_id()];
            pool.m_freeWorkers.push(p);
            pool.m_workers.erase(std::this_thread::get_id());
            lock.unlock();
            pool.m_allNum--;
            pool.m_restNum--;
            printf("线程%zu已退出！\n", std::hash<std::thread::id>{}(std::this_thread::get_id()));
            pool.m_Release.notify_one();
            return;
        }
        // 取出一个任务
        auto task = pool.m_tasks.front();
        pool.m_tasks.pop();
        pool.m_Full.notify_one();
        lock.unlock();

        ++pool.m_activeNum;
        --pool.m_restNum;
        // 执行任务，捕获异常，防止线程崩溃
        try
        {
            task();
        }
        catch(...)
        {
            std::cerr << "任务执行出错！\n";
        }    
        --pool.m_activeNum;
        ++pool.m_restNum;
        printf("线程%zu完成了一个任务！\n", std::hash<std::thread::id>{}(std::this_thread::get_id()));
    }
    // 线程池停止了，线程自杀
    lock.lock();
    auto p = pool.m_workers[std::this_thread::get_id()];
    pool.m_freeWorkers.push(p);
    pool.m_workers.erase(std::this_thread::get_id());
    --pool.m_restNum;
    --pool.m_allNum;
    lock.unlock();
    printf("线程%zu已退出！\n", std::hash<std::thread::id>{}(std::this_thread::get_id()));
    pool.m_Release.notify_one();
}

void whThreadPool::op_thread(whThreadPool &pool)
{
    while (pool.m_stop == false || pool.m_allNum.load() > 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(pool.m_opTime));
        std::unique_lock<std::mutex> lock(pool.m_mutex);
        int queueLen = pool.m_tasks.size();
        lock.unlock();
        int now_active = pool.m_activeNum.load();
        int now_all = pool.m_allNum.load();
        // 增加线程，当任务的个数大于线程池中休息的线程数，并且线程池中的线程数小于最大线程数
        if (!pool.m_stop && queueLen > pool.m_restNum.load() && now_all < pool.m_maxtp)
        {
            // 防止超出最大线程数
            int addNum = std::min(pool.m_singleAdd, pool.m_maxtp - now_all);

            for (int i = 0; i < addNum; ++i)
            {
                auto p = new std::thread(pool.work_thread, std::ref(pool));
                lock.lock();
                pool.m_workers.insert({p->get_id(), p});
                lock.unlock();
                ++pool.m_restNum;
                ++pool.m_allNum;
            }
            printf("线程过少，增加了%d个线程！\n", addNum);
                //线程数量改变了，重新开始循环
            continue;
        }

        //销毁线程
        if(pool.m_stop || (now_active * 2 < now_all && now_all > pool.m_mintp))
        {
            if(pool.m_stop)
                pool.m_releaseNum = now_all;
            else
                pool.m_releaseNum = std::min(pool.m_singleSub, now_all - pool.m_mintp);

            int n = pool.m_releaseNum.load();
            for(int i = 0;i<n;++i)
            {
                lock.lock();
                pool.m_Empty.notify_one();
                pool.m_Release.wait(lock);
                auto p = pool.m_freeWorkers.front();
                pool.m_freeWorkers.pop();
                p->join();
                delete p;
                lock.unlock();
            }
        }
    }

    while(!pool.m_freeWorkers.empty())
    {
        auto p = pool.m_freeWorkers.front();
        pool.m_freeWorkers.pop();
        p->join();
        delete p;
    }
    
}
