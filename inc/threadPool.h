/** 
 * @file threadPool.h
 * @brief A thread pool.
 * @details
 * Code originates from:
 * https://github.com/progschj/ThreadPool
 *
 *
 * The thread id extension was added by Arne Kutzner.
 * ThreadPoolAllowingRecursiveEnqueues was added by Markus Schmidt.
 */

#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#define NUM_THREADS_ALIGNER 1

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

/**
 * @brief A Threadpool.
 * @details
 * // create thread pool with 4 worker threads\n
 * ThreadPool pool(4);\n
 *
 * // enqueue and store future\n
 * auto result = pool.enqueue( [](int answer) { return answer; }, 42 );\n
 *
 * // get result from future\n
 * std::cout << result.get() << std::endl;\n
 */
class ThreadPool {
public:
	/* External definition
	 */
    ThreadPool( size_t );

	/* External definition
	 */
	~ThreadPool();

	/* add new work item to the pool
	 */
	template<class F, class... Args>
	auto enqueue( F &&f, Args&&... args ) 
		->  std::future<typename std::result_of<F( size_t, Args... )>::type>
	{
		typedef typename std::result_of<F( size_t, Args... )>::type return_type;

		/* Don't allow enqueueing after stopping the pool 
		 */
		if ( bStop )
		{
			throw std::runtime_error( "enqueue on stopped ThreadPool" );
		} // if

		/* std::placeholders::_1 and represents some future value (this value will be only known during function definition.
		 */
		auto task = std::make_shared< std::packaged_task< return_type( size_t ) > >
					(
						std::bind( std::forward<F>( f ), std::placeholders::_1, std::forward<Args>( args )... ) 
					);

		/* The future outcome of the task. The caller will be later blocked until this result will be available. 
		 */
		std::future<return_type> xFuture = task->get_future();
		
		{
			/* Mutual access to the task queue has to be synchronized. 
			 */
			std::unique_lock<std::mutex> lock( queue_mutex );

			/* The task will get delivered its task_id later during its execution. 
			 */
			tasks.push( [task] (size_t task_id) { (*task)( task_id ); } );
		} // end of scope for the lock, so we will release the lock.

		/* Inform some waiting consumer (worker )that we have a fresh task.
		 */
		condition.notify_one();
		
		return xFuture;
	} // method enqueue
   
 private:
    /* need to keep track of threads so we can join them
	 */
    std::vector< std::thread > workers;
    
	/* the task queue
	 */
    std::queue< std::function<void(size_t)> > tasks;
    
    /* Synchronization
	 */
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool bStop;
};
 
/* Constructor just launches some amount of workers
 */
inline ThreadPool::ThreadPool( size_t threads )
    :   bStop( false ) // stop must be false in the beginning
{
	for ( size_t i = 0; i < threads; ++i )
        workers.emplace_back(
            [this, i]
            {
                /* So long we have some data the thread processes these data 
				 */
				for(;;)
                {
					/* Synchronization of mutual access to the task queue.
					 */
					std::unique_lock<std::mutex> lock( this->queue_mutex );

					while ( !this->bStop && this->tasks.empty() )
					{
						/* We release the lock, so that some producer can push some task into the queue.
						 * The produser will call notify_one(), in order to release 
						 */
						this->condition.wait( lock );
					} // while

					if ( this->bStop && this->tasks.empty() )
					{
						/* All work done and signal for termination received.
						 */
						return;
					} // if
					
					/* Initialize the function variable task, so that it refers the task at the top of the queue.
					 */
					std::function<void( size_t )> task( this->tasks.front() );
					this->tasks.pop();
					
                    lock.unlock();

					try
					{
						/* Execute the task (that we received as top of the queue)
						*/
						task( i );
					} 
					catch(std::exception e) 
					{
						std::cerr << "exception when executing task:" << std::endl;
						std::cerr << e.what() << std::endl;
					}
					catch(...) 
					{
						std::cerr << "unknown exception when executing task" << std::endl;
					}
                } // for
            } // lambda
        ); // function call
} // method

/* the destructor joins all threads
 */
inline ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock( queue_mutex );
        bStop = true;
    } // end of scope for queue_mutex, so we unlock the mutex
    condition.notify_all();

	/* We wait until all workers finished their job.
	 * (Destruction thread is blocked until all workers finished their job.)
	 */
	for ( size_t i = 0; i < workers.size(); ++i )
	{
		workers[i].join();
	} // for
}


/**
 * @brief A Threadpool.
 * @details
 * // create thread pool with 4 worker threads\n
 * ThreadPool pool(4);\n
 *
 * // enqueue and store future\n
 * auto result = pool.enqueue( [](int answer) { return answer; }, 42 );\n
 *
 * // get result from future\n
 * std::cout << result.get() << std::endl;\n
 *
 * @note This pool allows enqueues from within a worker thread.
 */
class ThreadPoolAllowingRecursiveEnqueues {
public:
	/* External definition
	*/
	ThreadPoolAllowingRecursiveEnqueues(size_t);

	/* External definition
	*/
	~ThreadPoolAllowingRecursiveEnqueues();

	/* add new work item to the pool
	*/
	template<class F, class... Args>
	auto enqueue(F &&f, Args&&... args)
		->  std::future<typename std::result_of<F(size_t, Args...)>::type>
	{
		typedef typename std::result_of<F(size_t, Args...)>::type return_type;

		/* 
		 * std::placeholders::_1 and represents some future value 
		 * (this value will be only known during function definition.
		 */
		auto task = std::make_shared< std::packaged_task< return_type(size_t) > >
			(
			std::bind(std::forward<F>(f), std::placeholders::_1, std::forward<Args>(args)...)
			);

		/* The future outcome of the task. The caller will be later blocked until this result will be available.
		*/
		std::future<return_type> xFuture = task->get_future();

		{
			/* Mutual access to the task queue has to be synchronized.
			*/
			std::unique_lock<std::mutex> lock(queue_mutex);

			/* The task will get delivered its task_id later during its execution.
			*/
			tasks.push([task](size_t task_id) { (*task)(task_id); });
		} // end of scope for the lock, so we will release the lock.

		/* Inform some waiting consumer (worker )that we have a fresh task.
		*/
		condition.notify_one();

		return xFuture;
	} // method enqueue

private:
	/* need to keep track of threads so we can join them
	*/
	std::vector< std::thread > workers;

	/* the task queue
	*/
	std::queue< std::function<void(size_t)> > tasks;

	/* Synchronization
	*/
	std::mutex queue_mutex;
	std::condition_variable condition;
	bool bStop;
};

/* Constructor just launches some amount of workers
*/
inline ThreadPoolAllowingRecursiveEnqueues::ThreadPoolAllowingRecursiveEnqueues(size_t threads)
	: bStop(false) // stop must be false in the beginning
{
	for (size_t i = 0; i < threads; ++i)
		workers.emplace_back(
		[this, i]
	{
		/* So long we have some data the thread processes these data
		*/
		for (;;)
		{
			/* Synchronization of mutual access to the task queue.
			*/
			std::unique_lock<std::mutex> lock(this->queue_mutex);

			while (!this->bStop && this->tasks.empty())
			{
				/* We release the lock, so that some producer can push some task into the queue.
				* The produser will call notify_one(), in order to release
				*/
				this->condition.wait(lock);
			} // while

			if (this->bStop && this->tasks.empty())
			{
				/* All work done and signal for termination received.
				*/
				return;
			} // if

			/* Initialize the function variable task, so that it refers the task at the top of the queue.
			*/
			std::function<void(size_t)> task(this->tasks.front());
			this->tasks.pop();

			lock.unlock();

			/* Execute the task (that we received as top of the queue)
			*/
			task(i);
		} // for
	} // lambda
	); // function call
} // method

/* the destructor joins all threads
*/
inline ThreadPoolAllowingRecursiveEnqueues::~ThreadPoolAllowingRecursiveEnqueues()
{
	{
		std::unique_lock<std::mutex> lock(queue_mutex);
		bStop = true;
	} // end of scope for queue_mutex, so we unlock the mutex
	condition.notify_all();

	/* We wait until all workers finished their job.
	* (Destruction thread is blocked until all workers finished their job.)
	*/
	for (size_t i = 0; i < workers.size(); ++i)
	{
		workers[i].join();
	} // for
}


#endif
