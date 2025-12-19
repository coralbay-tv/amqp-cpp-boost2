cmake_minimum_required(VERSION 3.14)
project(amqp-cpp-boost2)

# 1. Define the library target (Header only = INTERFACE)
add_library(amqp-cpp-boost2 INTERFACE)

# 2. Define where the headers are. 
# The "." means "the directory this file is in" (the root).
# This tells consumers: "Add my root folder to your include path."
target_include_directories(amqp-cpp-boost2 INTERFACE ".")

# 3. (Optional) Propagate dependencies if this header includes them
# target_link_libraries(amqp-cpp-boost2 INTERFACE amqpcpp boost_asio boost_system)
