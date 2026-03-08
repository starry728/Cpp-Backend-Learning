cmake_minimum_required(VERSION 3.15)
project(socket)

#set(SRC_server socket.c clientlist.c server.c)
set(SRC_client socket.c client.c)
#aux_source_directory(${PROJECT_SOURCE_DIR} SRC)
file(GLOB SRC_server ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
message(${SRC_server})
list(REMOVE_ITEM SRC_server "/home/cxy/MySocket/client.c")
message(${SRC_server})
message("IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII")

set(tmp hello cxy)
set(tmp1 ${tmp} "hahahaha")
message(${tmp})
message(${tmp1})


message(STATUS "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX")
set(EXECUTABLE_OUTPUT_PATH /home/cxy/MySocket/Myapp)#生成可执行程序的输出路径 没有则创建
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/Mylib)#生成可执行程序的输出路径 没有则创建
message("RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR")
#set(CMAKE_CXX_STANDARD 11)#添加标准 C++11新特性

add_executable(server_cm ${SRC_server})
add_executable(client_cm ${SRC_client})
#add_library(server_dll SHARED ${SRC_server})
#add_library(client_dll STATIC ${SRC_client})

