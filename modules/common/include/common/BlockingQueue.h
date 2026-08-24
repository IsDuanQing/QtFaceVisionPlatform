#ifndef IVP_COMMON_BLOCKINGQUEUE_H
#define IVP_COMMON_BLOCKINGQUEUE_H

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace ivp
{

// A small bounded blocking queue for producer-consumer pipelines.
// capacity_ == 0 means "unbounded", but the video pipeline will use a small
// bounded capacity to limit latency.
template <typename T>
class BlockingQueue final
{
public:
    explicit BlockingQueue(std::size_t capacity = 0)
        : capacity_(capacity)
    {
    }

    BlockingQueue(const BlockingQueue&) = delete;
    BlockingQueue& operator=(const BlockingQueue&) = delete;

    bool push(T value)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        notFull_.wait(lock, [this]() {
            return closed_ || capacity_ == 0 || queue_.size() < capacity_;
        });

        if (closed_)
        {
            return false;
        }

        queue_.push_back(std::move(value));
        notEmpty_.notify_one();
        return true;
    }

    bool pushDropOldest(T value, bool* dropped = nullptr)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_)
        {
            return false;
        }

        bool didDrop = false;
        if (capacity_ > 0 && queue_.size() >= capacity_)
        {
            queue_.pop_front();
            didDrop = true;
        }

        queue_.push_back(std::move(value));
        if (dropped != nullptr)
        {
            *dropped = didDrop;
        }
        notEmpty_.notify_one();
        return true;
    }

    bool tryPop(T* value)
    {
        if (value == nullptr)
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty())
        {
            return false;
        }

        *value = std::move(queue_.front());
        queue_.pop_front();
        notFull_.notify_one();
        return true;
    }

    bool pop(T* value)
    {
        if (value == nullptr)
        {
            return false;
        }

        std::unique_lock<std::mutex> lock(mutex_);
        notEmpty_.wait(lock, [this]() {
            return closed_ || !queue_.empty();
        });

        if (queue_.empty())
        {
            return false;
        }

        *value = std::move(queue_.front());
        queue_.pop_front();
        notFull_.notify_one();
        return true;
    }

    void close()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    void reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = false;
        queue_.clear();
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        notFull_.notify_all();
    }

    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool closed() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

private:
    std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::deque<T> queue_;
    bool closed_ = false;
};

} // namespace ivp

#endif // IVP_COMMON_BLOCKINGQUEUE_H
