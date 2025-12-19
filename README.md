
# amqp-cpp-boost2

**A modern, maintained Boost ASIO handler for the AMQP-CPP library.**

This repository provides a robust `ConnectionHandler` implementation for integrating [AMQP-CPP](https://github.com/CopernicaMarketingSoftware/AMQP-CPP) with `boost::asio`. It is designed to replace the unmaintained example handler found in the core AMQP-CPP repository.

## Overview

AMQP-CPP is a C++ library for communicating with a RabbitMQ message broker. By design, AMQP-CPP operates in a network-agnostic fashion and does not do IO by itself. Instead, it uses a layered architecture where the user (or a handler) must manage the network layer and socket connections.

While AMQP-CPP provides a native TCP module for Linux and examples for `libev`, `libuv`, and `libevent`, the original documentation notes that the included Boost ASIO handler is "debatable" in quality. It was not developed by the original maintainers and has known open issues.

**amqp-cpp-boost2** addresses this by providing a clean, modern, and maintained implementation of the `AMQP::TcpHandler` interface specifically for Boost ASIO.

## Prerequisites

To use this handler, you must have the following dependencies available in your build environment:

* **C++17 Compiler:** The AMQP-CPP library uses C++17 features.
* **AMQP-CPP:** The core library (version 4+ recommended).
* **Boost:** Specifically the `boost::asio`.

## Installation

This is a header-only (or lightweight static) library implementation. You can integrate it into your project using CMake or by directly including the source files.

### 1. Using CMake (FetchContent)

You can include this repository directly in your `CMakeLists.txt`:

```cmake
include(FetchContent)

FetchContent_Declare(
  amqp-cpp-boost2
  GIT_REPOSITORY https://github.com/coralbay-tv/amqp-cpp-boost2.git
  GIT_TAG main
)
FetchContent_MakeAvailable(amqp-cpp-boost2)
```

### 2. Manual Installation

Clone this repository and add the `include` directory to your project's include path.

## Usage

Using `amqp-cpp-boost2` is similar to using the standard TCP module provided by AMQP-CPP, but you pass the Boost IO context to the handler.

### Basic Example

Here is how to set up a connection using `boost::asio::io_context` and the `amqp-cpp-boost2` handler:

```
#include <boost/asio.hpp>
#include <amqpcpp.h>
#include <amqpcpp-boostasio2.hpp> // The header from this repo

int main()
{
    // 1. Create the Boost ASIO IO context
    boost::asio::io_context io_context;

    // 2. Create the handler provided by this library
    // Pass the io_context to the handler so it can manage the socket
    AMQP::LibBoostAsioHandler2 handler(io_context);

    // 3. Create the connection
    // We use TcpConnection just like standard AMQP-CPP usage.
    // The library manages the parsing of incoming data and generating frames.
    AMQP::TcpConnection connection(&handler, AMQP::Address("amqp://guest:guest@localhost/"));

    // 4. Create a channel
    AMQP::TcpChannel channel(&connection);

    // 5. Use the channel to declare queues, exchanges, etc.
    channel.declareQueue("hello-world").onSuccess([&connection](const std::string &name, uint32_t messagecount, uint32_t consumercount) {
        
        std::cout << "Queue " << name << " declared successfully." << std::endl;
        
        // Optional: Close connection to exit
        // connection.close(); 
    });

    // 6. Run the IO context
    // This will block until the connection is closed and all work is done
    io_context.run();

    return 0;
}
```
## Features & Improvements

-   **Modern Boost Support:** Compatible with recent versions of Boost.
    
-   **Reliable Monitoring:** Correctly implements the `monitor()` method required by AMQP-CPP to interact with the main event loop.
    
-   **Heartbeat Support:** Supports AMQP heartbeats to keep connections alive during idle periods.
    

## License

This project is licensed under the Apache License, Version 2.0 - see the LICENSE file for details.

## Acknowledgments

-   **Copernica Marketing Software** for creating the [AMQP-CPP library](https://github.com/CopernicaMarketingSoftware/AMQP-CPP).


