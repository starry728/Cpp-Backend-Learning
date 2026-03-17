#pragma once
//初始化监听的套接字
int initListenFd(unsigned short port);
//启动epoll
int epollRun(int lfd);
//和客户端建立连接
int acceptClient(int lfd, int epfd);
//接收HTTP请求
int recvHttpRequest(int cfd, int epfd);
//解析请求行
int parseRequestLine(const char* line, int cfd);
//发送文件
int sendFile(const char* fileName, int cfd);
//发送响应头（状态行+响应头）
int sendHeadMsg(int cfd, int status, const char* descr, const char* type, int length);
//根据文件后缀得到对应的content-type
const char* getFileType(const char* name);
//发送目录
int sendDir(const char* dirName, int cfd);
//将字符转换成整形数
int hexToDec(char c);
//解码
//to存储解码之后的数据，传出参数， from被解码的数据，传入参数
void decodeMsg(char* to, char* from);