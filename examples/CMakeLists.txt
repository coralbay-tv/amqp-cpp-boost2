
add_executable(amqpcpp_boost2_example amqpcpp-boostasio2.cpp)

# Link against the core amqpcpp library
add_dependencies(amqpcpp_boost2_example amqpcpp)

# Link: 
# 1. amqpcpp (core)
# 2. amqp-cpp-boost2 (the handler, provides headers)
# 3. boost_system (required by ASIO)
# 4. pthread/dl/ssl (system requirements)
target_link_libraries(amqpcpp_boost2_example 
	amqpcpp 
	amqp-cpp-boost2
	boost_system 
	pthread 
	dl 
	ssl
)
