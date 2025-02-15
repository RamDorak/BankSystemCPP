cmake_minimum_required(VERSION 3.10)
project(BankApp)

set(CMAKE_CXX_STANDARD 17)

# Find MySQL
include_directories(/usr/local/mysql-8.1.0-macos13-arm64/include)
link_directories(/usr/local/mysql-8.1.0-macos13-arm64/lib)

# Create executable
add_executable(BankApp main.cpp SHA512.cpp)

# Link MySQL to the executable
target_link_libraries(BankApp mysqlclient)
