/**
 *  amqpcpp-boostasio2.cpp
 * 
 *  Test program to check AMQP functionality based on Boost's asio io_service.
 * 
 *  @author Gavin Smith <gavin.smith@coralbay.tv>
 *
 *  Compile with g++ -std=c++17 amqpcpp-boostasio2.cpp -o boost_test -lpthread -lboost_system -lamqpcpp
 */
#include <iostream>

#include <amqpcpp.h>
#include <amqpcpp-boostasio2.hpp> 

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

int main()
{
    // 1. Use io_context (modern Boost), not io_service
    boost::asio::io_context context(1);

    // 2. Set up signal handling to exit cleanly on Ctrl+C
    boost::asio::signal_set signals(context, SIGINT, SIGTERM);
    signals.async_wait([&](const boost::system::error_code&, int) {
        context.stop();
    });

    // 3. Instantiate your new handler
    AMQP::LibBoostAsioHandler2 handler(context);
    
    // 4. Create connection and channel as usual
    AMQP::TcpConnection connection(&handler, AMQP::Address("amqp://guest:guest@localhost/"));
    AMQP::TcpChannel channel(&connection);
    
    // 5. Run a simple test
    channel.declareQueue(AMQP::exclusive).onSuccess([&connection](const std::string &name, uint32_t messagecount, uint32_t consumercount) {
        std::cout << "SUCCESS: Declared queue " << name << " using boostasio_handler2." << std::endl;
        connection.close();
    });

    std::cout << "Running io_context..." << std::endl;
    return context.run();
}
