#ifndef AWAITABLE_FLAG_H
#define AWAITABLE_FLAG_H

#include <asio.hpp>
#include <asio/awaitable.hpp>
#include <asio/steady_timer.hpp>
#include <mutex>

class AwaitableFlag {
public:
    AwaitableFlag() = delete;
    explicit AwaitableFlag(asio::any_io_executor executor) : m_executor(executor), m_timer(executor), m_flag(false) {}

    void Reset() {
        {
            std::lock_guard lock(m_mutex);
            m_flag = false;
        }
    }

    void Signal() {
        {
            std::lock_guard lock(m_mutex);
            m_flag = true;
        }

        m_timer.cancel();
    }

    asio::awaitable<void> Wait() {
        asio::error_code errorCode;
        while (true) {
            {
                std::lock_guard lock(m_mutex);
                if (m_flag) co_return;
            }

            co_await m_timer.async_wait(asio::redirect_error(asio::use_awaitable, errorCode));
        }
    }
private:
    asio::any_io_executor m_executor;
    mutable std::mutex    m_mutex;
    asio::steady_timer    m_timer;
    bool                  m_flag;
};

#endif //AWAITABLE_FLAG_H
