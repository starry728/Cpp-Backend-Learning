#include "socket.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>

int main()
{
    // 1. 创建用于通信的套接字
    int fd = createSocket();
    if(fd == -1)
    {
        return -1;
    }

    // 2. 连接服务器IP PORT
    int ret = connectToHost(fd, "127.0.0.1", 10000);
    if(ret == -1)
    {
        return -1;
    }

    // 3.通信
    int fdl = open("english.txt",O_RDONLY);
    int length = 0;
    char tmp[1000];//每次发送的最大字节数
    while((length = read(fdl,tmp,rand()%1000))>0)
    {
        // 发送数据
        sendMsg(fd, tmp, length);

        // 接收数据（初始化后再读）
        memset(tmp, 0, sizeof(tmp));
        usleep(300);
    }
    sleep(10);
    //关闭文件描述符
    closeSocket(fd);

    return 0;
}