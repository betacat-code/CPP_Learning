#pragma once
#include <coroutine>
#include "ChannelAwaiter.h"
#include <exception>

template<typename ValueType>
struct Channel {

    struct ChannelClosedException : std::exception {
        const char* what() const noexcept override {
            return "Channel is closed.";
        }
    };

    void check_closed() {
        if (!_is_active.load(std::memory_order_relaxed)) {
            throw ChannelClosedException();
        }
    }

    void try_push_reader(ReaderAwaiter<ValueType>* reader_awaiter) {
        std::unique_lock lock(channel_lock);
        check_closed();

        if (!buffer.empty()) {
            auto value = buffer.front();
            buffer.pop();

            if (!writer_list.empty()) {
                auto writer = writer_list.front();
                writer_list.pop_front();
                buffer.push(writer->_value);
                lock.unlock();

                writer->resume();
            }
            else {
                lock.unlock();
            }

            reader_awaiter->resume(value);
            return;
        }

        if (!writer_list.empty()) {
            auto writer = writer_list.front();
            writer_list.pop_front();
            lock.unlock();

            reader_awaiter->resume(writer->_value);
            writer->resume();
            return;
        }

        reader_list.push_back(reader_awaiter);
    }

    void try_push_writer(WriterAwaiter<ValueType>* writer_awaiter) {
        std::unique_lock lock(channel_lock);
        check_closed();
        if (!reader_list.empty()) {
            auto reader = reader_list.front();
            reader_list.pop_front();
            lock.unlock();

            reader->resume(writer_awaiter->_value);
            writer_awaiter->resume();
            return;
        }

        if (buffer.size() < buffer_capacity) {
            buffer.push(writer_awaiter->_value);
            lock.unlock();
            writer_awaiter->resume();
            return;
        }

        writer_list.push_back(writer_awaiter);
    }

    void remove_writer(WriterAwaiter<ValueType>* writer_awaiter) {
        std::lock_guard lock(channel_lock);
        auto size = writer_list.remove(writer_awaiter);
        debug("remove writer ", size);
    }

    void remove_reader(ReaderAwaiter<ValueType>* reader_awaiter) {
        std::lock_guard lock(channel_lock);
        auto size = reader_list.remove(reader_awaiter);
        debug("remove reader ", size);
    }

    auto write(ValueType value) {
        check_closed();
        return WriterAwaiter<ValueType>(this, value);
    }

    auto operator<<(ValueType value) {
        return write(value);
    }

    auto read() {
        check_closed();
        return ReaderAwaiter<ValueType>(this);
    }

    auto operator>>(ValueType& value_ref) {
        auto awaiter = read();
        awaiter.p_value = &value_ref;
        return awaiter;
    }

    void close() {
        bool expect = true;
        if (_is_active.compare_exchange_strong(expect, false, std::memory_order_relaxed)) {
            clean_up();
        }
    }

    explicit Channel(int capacity = 0) : buffer_capacity(capacity) {
        _is_active.store(true, std::memory_order_relaxed);
    }

    bool is_active() {
        return _is_active.load(std::memory_order_relaxed);
    }

    Channel(Channel&& channel) = delete;

    Channel(Channel&) = delete;

    Channel& operator=(Channel&) = delete;

    ~Channel() {
        close();
    }

private:
    // buffer 的容量
    int buffer_capacity;
    std::queue<ValueType> buffer;
    // buffer 已满时，新来的写入者需要挂起保存在这里等待恢复
    std::list<WriterAwaiter<ValueType>*> writer_list;
    // buffer 为空时，新来的读取者需要挂起保存在这里等待恢复
    std::list<ReaderAwaiter<ValueType>*> reader_list;
    // Channel 的状态标识
    std::atomic<bool> _is_active;

    std::mutex channel_lock;
    std::condition_variable channel_condition;

    void clean_up() {
        std::lock_guard lock(channel_lock);

        // 需要对已经挂起等待的协程予以恢复执行
        for (auto writer : writer_list) {
            writer->resume();
        }
        writer_list.clear();

        for (auto reader : reader_list) {
            reader->resume();
        }
        reader_list.clear();

        // 清空 buffer
        decltype(buffer) empty_buffer;
        std::swap(buffer, empty_buffer);
    }
};