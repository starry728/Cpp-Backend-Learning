#pragma once
#include <arpa/inet.h> //基于Linux系统的文件。socket需要
#include <stdbool.h>//添加bool需要

//数据包类型
enum Type{Heart, Message};
//数据包类型：‘H’：心跳包，‘M’：数据包
//数据包格式：数据长度|数据包类型|数据块
//             int      char    char*
//            4字节     1字节   N字节

//初始化一个套接字
int initSocket();
//初始化sockaddr结构体
void initSockaddr(struct sockaddr* addr, unsigned short port, const char* ip);
//设置监听
int setListen(int lfd, unsigned short port);
//接收客户端连接
int acceptConnect(int lfd, struct sockaddr *addr);//结构体作用：取出所连客户端的IP和端口
//连接服务器
int connectToHost(int fd, unsigned short port, const char* ip);
//读出指定的字节数（接收）
int readn(int fd, char* buffer, int size);
//写入指定的字节数(发送)
int writen(int fd, const char* buffer, int length);
//发送数据
bool sendMessage(int fd, const char* buffer, int length, enum Type t);
//接收数据
int recvMessage(int fd, char** buffer, enum Type* t);//返回值：buffer指向的内存存储的数据有多少个字节
