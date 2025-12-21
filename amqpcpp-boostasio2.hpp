/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 * * amqpcpp-boostasio2.hpp
 *
 * Implementation for the AMQP::TcpHandler for boost::asio. You can use this class
 * instead of a AMQP::TcpHandler class, just pass the boost asio service to the
 * constructor and you're all set.  See tests/libboostasio.cpp for example.
 *
 * @author Gavin Smith <gavin.smith@coralbay.tv>
 */


/**
 * Include guard
 */
#pragma once

/**
 * Dependencies
 */
#include "amqpcpp/linux_tcp.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/bind_executor.hpp>

#include <chrono>
#include <functional>
#include <map>
#include <memory>


/**
 * Set up namespace
 */
namespace AMQP {

/**
 * Class definition
 * @note Because of a limitation on Windows, this will only work on POSIX based systems - see https://github.com/chriskohlhoff/asio/issues/70
 */
class LibBoostAsioHandler2 : public TcpHandler
{
protected:

    /**
     * Helper class that wraps a boost io_context socket monitor.
     */
    class Watcher : public std::enable_shared_from_this<Watcher>
    {
    private:

        /**
         * The boost asio io_context which is responsible for detecting events.
         * @var class boost::asio::io_context&
         */
        boost::asio::io_context & _iocontext;

        /**
         * The boost asio io_context::strand managed pointer.
         * @var class std::shared_ptr<boost::asio::io_context>
         */
        std::shared_ptr<boost::asio::io_context::strand> _strand;

        /**
         * The boost tcp socket.
         * @var class boost::asio::ip::tcp::socket
         * @note https://stackoverflow.com/questions/38906711/destroying-boost-asio-socket-without-closing-native-handler
         */
        boost::asio::posix::stream_descriptor _socket;

        /**
         * The boost asynchronous steady timer.
         * @var class boost::asio::steady_timer
         */
        boost::asio::steady_timer _timer;

        /**
         * The connection being watched.
         * Stored here to avoid passing it through every handler signature.
         */
        TcpConnection* _connection;

        /**
         * A boolean that indicates if the watcher is monitoring for read events.
         * @var _read True if reads are being monitored else false.
         */
        bool _read{false};

        /**
         * A boolean that indicates if the watcher has a pending read event.
         * @var _read True if read is pending else false.
         */
        bool _read_pending{false};

        /**
         * A boolean that indicates if the watcher is monitoring for write events.
         * @var _read True if writes are being monitored else false.
         */
        bool _write{false};

        /**
         * A boolean that indicates if the watcher has a pending write event.
         * @var _read True if read is pending else false.
         */
        bool _write_pending{false};

        /**
         * The socket descriptor that is being monitored.
         * @var _fd The OS's underlying socket descriptor.
         */
        int _fd;

        using handler_cb = std::function<void(boost::system::error_code, std::size_t)>;
        using io_handler = std::function<void(const boost::system::error_code&, const std::size_t)>;
        using timer_handler = std::function<void(boost::system::error_code)>;

        /**
         * Helper to bind a handler callback to the connection's strand.
         * * This wraps the provided function using boost::asio::bind_executor, ensuring
         * that when the handler is eventually invoked (e.g., by an async completion),
         * it runs strictly within the context of the _strand. This guarantees thread
         * safety by preventing concurrent execution of handlers for this watcher.
         * * @tparam Function The type of the handler function (deduced).
         * @param  func The handler function (lambda or functor) to bind.
         * @return A wrapper around the function that enforces execution on the strand.
         */
        template <typename Function>
        auto bind_to_strand(Function &&func)
        {
            // boost::asio::bind_executor returns a wrapper that strictly
            // forces the function to run on the specified strand.
            return boost::asio::bind_executor(*_strand, std::forward<Function>(func));
        }

        /**
         * Binds and returns a read handler for the io operation.
         * @return handler callback
         */
        handler_cb get_read_handler()
        {
            auto self = weak_from_this();
            auto fn = [this, self{std::move(self)}](const boost::system::error_code &ec, const std::size_t bytes) {
                this->read_handler(ec, bytes, self);
            };
            return bind_to_strand(std::move(fn));
        }

        /**
         * Binds and returns a write handler for the io operation.
         * @return handler callback
         */
        handler_cb get_write_handler()
        {
            auto self = weak_from_this();
            auto fn = [this, self{std::move(self)}](const boost::system::error_code &ec, const std::size_t bytes) {
                this->write_handler(ec, bytes, self);
            };
            return bind_to_strand(std::move(fn));
        }

        /**
         * Binds and returns a lambda function handler for the timer operation.
         * @param  timeout      The timeout interval in seconds.
         * @return handler callback bound to the strand
         */
        std::function<void(const boost::system::error_code&)> get_timer_handler(const uint16_t timeout)
        {
            auto self = weak_from_this();

            // The actual logic that runs when timer fires
            auto fn = [this, self{std::move(self)}, timeout](const boost::system::error_code &ec) {
                // Pass to the member function
                this->timeout_handler(ec, self, timeout);
            };

            // Wrap it so it runs strictly on the strand
            return bind_to_strand(std::move(fn));
        }

        /**
         * Handler method that is called by boost's io_context when the socket pumps a read event.
         * @param  ec          The status of the callback.
         * @param  bytes_transferred The number of bytes transferred.
         * @param  awpWatcher  A weak pointer to this object.
         * @note   The handler will get called if a read is cancelled.
         */
        void read_handler(const boost::system::error_code &ec,
                          const std::size_t /*bytes_transferred*/, // Stops: -Wunused-parameter
                          const std::weak_ptr<Watcher> awpWatcher)
        {
            // Resolve any potential problems with dangling pointers
            // (remember we are using async).
            const std::shared_ptr<Watcher> watcher = awpWatcher.lock();
            if (!watcher) { return; }

            _read_pending = false;

            if ((!ec || ec == boost::asio::error::would_block) && _read)
            {
                // Use member _connection and _fd
                _connection->process(_fd, AMQP::readable);

                _read_pending = true;

                _socket.async_read_some(
                    boost::asio::null_buffers(),
                    get_read_handler());
            }
        }

        /**
         * Handler method that is called by boost's io_context when the socket pumps a write event.
         * @param  ec          The status of the callback.
         * @param  bytes_transferred The number of bytes transferred.
         * @param  awpWatcher  A weak pointer to this object.
         * @note   The handler will get called if a write is cancelled.
         */
        void write_handler(const boost::system::error_code ec,
                           const std::size_t /*bytes_transferred*/, // Stops: -Wunused-parameter
                           const std::weak_ptr<Watcher> awpWatcher)
        {
            // Resolve any potential problems with dangling pointers
            // (remember we are using async).
            const std::shared_ptr<Watcher> watcher = awpWatcher.lock();
            if (!watcher) { return; }

            _write_pending = false;

            if ((!ec || ec == boost::asio::error::would_block) && _write)
            {
                // Use member _connection and _fd
                _connection->process(_fd, AMQP::writable);

                _write_pending = true;

                _socket.async_write_some(
                    boost::asio::null_buffers(),
                    get_write_handler());
            }
        }

        /**
         * Callback method that is called by libev when the timer expires
         * @param  ec          error code returned from loop
         * @param  loop        The loop in which the event was triggered
         * @param  timeout
         */
        void timeout_handler(const boost::system::error_code &ec,
                     std::weak_ptr<Watcher> awpThis,
                     const uint16_t timeout)
        {
            // Resolve any potential problems with dangling pointers
            // (remember we are using async).
            const std::shared_ptr<Watcher> apTimer = awpThis.lock();
            if (!apTimer) { return; }

            if (!ec)
            {
                if (_connection)
                {
                    // send the heartbeat
                    _connection->heartbeat();
                }

                // Reschedule the timer for the future:
                _timer.expires_after(std::chrono::seconds(timeout));

                // Posts the timer event
                _timer.async_wait(get_timer_handler(timeout));
            }
        }

    public:
        /**
         * Constructor- initialises the watcher and assigns the filedescriptor to
         * a boost socket for monitoring.
         * @param  io_context      The boost io_context
         * @param  strand          A pointer to a io_context::strand instance.
         * @param  connection      The connection being watched
         * @param  fd              The filedescriptor being watched
         */
        Watcher(boost::asio::io_context &io_context,
                const std::shared_ptr<boost::asio::io_context::strand> strand,
                TcpConnection* connection,
                const int fd) :
            _iocontext(io_context),
            _strand(strand),
            _socket(io_context),
            _timer(io_context),
            _connection(connection),
            _fd(fd)
        {
            _socket.assign(fd);
            _socket.non_blocking(true);
        }

        /**
         * Watchers cannot be copied or moved
         *
         * @param  that    The object to not move or copy
         */
        Watcher(Watcher &&that) = delete;
        Watcher(const Watcher &that) = delete;

        /**
         * Destructor
         */
        ~Watcher()
        {
            _read = false;
            _write = false;
            _socket.release();
            _timer.cancel();
        }

        /**
         * Change the events for which the filedescriptor is monitored
         * @param  events
         */
        void events(int events)
        {
            // 1. Handle reads?
            _read = ((events & AMQP::readable) != 0);

            // Read requsted but no read pending?
            if (_read && !_read_pending)
            {
                _read_pending = true;

                _socket.async_read_some(
                    boost::asio::null_buffers(),
                    get_read_handler());
            }

            // 2. Handle writes?
            _write = ((events & AMQP::writable) != 0);

            // Write requested but no write pending?
            if (_write && !_write_pending)
            {
                _write_pending = true;

                _socket.async_write_some(
                    boost::asio::null_buffers(),
                    get_write_handler());
            }
        }

        /**
         * Change the expire time
         * @param  timeout
         */
        void set_timer(uint16_t timeout)
        {
            // stop timer in case it was already set
            stop_timer();

            // Reschedule the timer for the future:
            _timer.expires_after(std::chrono::seconds(timeout));

            // Posts the timer event
            _timer.async_wait(get_timer_handler(timeout));
        }

        /**
         * Stop the timer
         */
        void stop_timer()
        {
            // do nothing if it was never set
            _timer.cancel();
        }
    };

    /**
     * The boost asio io_context.
     * @var class boost::asio::io_context&
     */
    boost::asio::io_context & _iocontext;

    using strand_shared_ptr = std::shared_ptr<boost::asio::io_context::strand>;

    /**
     * The boost asio io_context::strand managed pointer.
     * @var class std::shared_ptr<boost::asio::io_context>
     */
    strand_shared_ptr _strand;

    /**
     * All I/O watchers that are active, indexed by their filedescriptor
     * @var std::map<int,Watcher>
     */
    std::map<int, std::shared_ptr<Watcher> > _watchers;

    /**
     * Method that is called by AMQP-CPP to register a filedescriptor for readability or writability
     * @param  connection  The TCP connection object that is reporting
     * @param  fd          The filedescriptor to be monitored
     * @param  flags       Should the object be monitored for readability or writability?
     */
    void monitor(TcpConnection *const connection,
                 const int fd,
                 const int flags) override
    {
		// Case 1: Stop monitoring (flags == 0)
		if (flags == 0)
		{
			_watchers.erase(fd);
			return;
		}

        // Case 2: Start or Update monitoring
        auto [iter, inserted] = _watchers.try_emplace(fd, nullptr);

        if (inserted)
        {
            // Pass connection pointer here once during construction
            iter->second = std::make_shared<Watcher>(_iocontext, _strand, connection, fd);
        }

        // Call events without passing connection or fd again
        iter->second->events(flags);
    }

protected:
    /**
     * Method that is called when the heartbeat frequency is negotiated between the server and the client.
     * @param  connection      The connection that suggested a heartbeat interval
     * @param  interval        The suggested interval from the server
     * @return uint16_t        The interval to use
     */
    uint16_t onNegotiate(TcpConnection *connection, uint16_t interval) override
    {
        // skip if no heartbeats are needed
        if (interval == 0) return 0;

        const auto fd = connection->fileno();

        auto iter = _watchers.find(fd);
        if (iter == _watchers.end()) return 0;

        // set the timer (no connection needed)
        iter->second->set_timer(interval);

        // we agree with the interval
        return interval;
    }

public:

    /**
     * Handler cannot be default constructed.
     *
     * @param  that    The object to not move or copy
     */
    LibBoostAsioHandler2() = delete;

    /**
     * Constructor
     * @param  io_context    The boost io_context to wrap
     */
    explicit LibBoostAsioHandler2(boost::asio::io_context &io_context) :
        _iocontext(io_context),
        _strand(std::make_shared<boost::asio::io_context::strand>(_iocontext))
    {

    }

    /**
     * Handler cannot be copied or moved
     *
     * @param  that    The object to not move or copy
     */
    LibBoostAsioHandler2(LibBoostAsioHandler2 &&that) = delete;
    LibBoostAsioHandler2(const LibBoostAsioHandler2 &that) = delete;

    /**
     * Returns a reference to the boost io_context object that is being used.
     * @return The boost io_context object.
     */
    boost::asio::io_context &service()
    {
        return _iocontext;
    }

    /**
     * Destructor
     */
    ~LibBoostAsioHandler2() override = default;
};


/**
 * End of namespace
 */
}