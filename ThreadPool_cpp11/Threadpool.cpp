#include "ThreadPool.h"
#include <iostream>

ThreadPool::ThreadPool(int min, int max):m_maxThread(max), m_minThread(min),
m_stop(false), m_idleThread(max), m_curThread(max)
{
    cout << "max =" << max << endl;
    // 创建管理者线程
    m_manager = new thread(&ThreadPool::manager, this);//&是固定语法;如果是类里面的成员函数，就必须要指定实例this
    //工作的线程
    for(int i=0; i<max; ++i)
    {
        thread t(&ThreadPool::worker, this);
        //匿名对象，当我把它从容器中弹出时就会自动销毁了↓
        //thread(&ThreadPool::worker, this)//push_back会发生数据拷贝，所以用emplace_back(往容器中添加数据不会发生拷贝)
        m_workers.insert(make_pair(t.get_id(), move(t)));//move进行资源转移
    }
}
void ThreadPool::addTask(function<void(void)> task)//lock_guard和unique_guard两个管理互斥锁的类：使用方法完全相同
{
    {//添加了一个作用域，为了让locker提前析构，只锁一行
        lock_guard<mutex> locker(m_queueMutex);
        m_tasks.emplace(task);//比push效率高
    }
    m_condition.notify_one();
}
void ThreadPool::manager(void)
{
    while(!m_stop.load())//线程池未退出;load()加载
    {
        this_thread::sleep_for(chrono::seconds(1));//休眠1s再检测一次
        int idle = m_idleThread.load();
        int cur = m_curThread.load();
        if(idle > cur/2 && cur >m_minThread)
        {
            //每次销毁两个线程
            m_exitThread.store(2);//store(2)存储
            m_condition.notify_all();
            lock_guard<mutex> lck(m_idsMutex);
            for(auto id : m_ids)
            {
                auto it = m_workers.find(id);//迭代器，解引用后是个对组对象
                if(it != m_workers.end())
                {
                    cout << "=================线程"<<(*it).first <<"被销毁了..."<<endl;
                    (*it).second.join();//阻塞等子线程跑完退出后回收它，再继续向下执行
                    m_workers.erase(it);//erase(it)从map中删掉键值对
                }
            }
            m_ids.clear();//所到这里为止了
        }
        else if(idle == 0 && cur < m_maxThread)
        {
            thread t(&ThreadPool::worker, this);
            m_workers.insert(make_pair(t.get_id(), move(t)));
            m_curThread++;
            m_idleThread++;
        }
    }
}
void ThreadPool::worker(void)
{
    while(!m_stop.load())//原子变量
    {
        function<void(void)> task = nullptr;
        {//增加一个locker的作用域
            unique_lock<mutex> locker(m_queueMutex);
            while(m_tasks.empty() && !m_stop)
            {
                m_condition.wait(locker);//阻塞前会解锁，防止死锁；唤醒抢到线程后又会加锁——都是在wait里面做的
                if(m_exitThread.load()>0)
                {
                    m_curThread--;
                    m_idleThread--;
                    m_exitThread--;
                    cout << "------------------------线程退出了，ID："<<this_thread::get_id() << endl;
                    lock_guard<mutex> lck(m_idsMutex);
                    m_ids.emplace_back(this_thread::get_id());
                    return;//退出工作线程了
                }
                
            }
            if(!m_tasks.empty())
            {
                cout << "取出了一个任务..."<< endl;
                task = move(m_tasks.front());// move进行了资源转移，减少拷贝
                m_tasks.pop();
            }
        }
        if(task)//若不为空
        {
            m_idleThread--;//原子变量，是线程安全的
            task();
            m_idleThread++;
        }
    }
}

ThreadPool::~ThreadPool()
{
    m_stop = true;
    m_condition.notify_all();//唤醒所有
    for(auto& it : m_workers)//引用方式取出线程对象（因为线程对象是不允许拷贝的！）
    {
        thread& t = it.second;
        if(t.joinable())//判断当前线程是否可连接（未连接：join/detach过了）
        {
            cout << "********************线程" << t.get_id() <<"将要退出了..." <<endl;
            t.join();
        } 
    }
    if(m_manager->joinable())//因为是个指针，所以->
    {
        m_manager->join();
    }
    delete m_manager;//因为是个指针，所以delete掉对应堆内存
}

void calc(int x,int y)
{
    int z = x + y;
    cout << "z = "<< z << endl;
    this_thread::sleep_for(chrono::seconds(2));
}
int calc1(int x,int y)
{
    int z = x + y;
    this_thread::sleep_for(chrono::seconds(2));
    return z;
}
//future类：存储任务函数返回的结果
int main()
{
    ThreadPool pool;//创建实例
    vector<future<int>> results;
    for(int i = 0; i < 10; ++i)
    {
        //auto obj = bind(calc, i, i * 2);//可调用对象绑定器
        results.emplace_back(pool.addTask(calc1, i, i * 2));
    }
    for(auto& item : results)
    {
        cout << "线程执行的结果：" << item.get() << endl;//get()会阻塞等future的线程跑完
    }
    //getchar();//阻塞：等待键盘输入--为了防止主线程很快退出了
    return 0;
}