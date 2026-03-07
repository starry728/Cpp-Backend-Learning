#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>

int main()
{
    //1.创建通信的套接字
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd==-1)
    {
        perror("socket");
        return -1;
    }
    //2.连接服务器IP port
    struct sockaddr_in saddr;
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(9999);//找一个本地的未被占用的端口，一般5000以上占用较少
    inet_pton(AF_INET, "172.31.192.239", &saddr.sin_addr.s_addr);
    int ret = connect(fd, (struct sockaddr*)&saddr, sizeof(saddr));
    if(ret==-1)
    {
        perror("connect");
        return -1;
    }    
    int number = 0;
    //3.通信
    while(1)
    {
        //发送数据
        char buff[1024];
        sprintf(buff,"你好，hello cxy,%d...\n",number++);
        send(fd, buff, strlen(buff)+1, 0);

        //接收数据
        memset(buff, 0, sizeof(buff));//清空缓冲区
        int len = recv(fd, buff, sizeof(buff), 0);
        if(len > 0)
        {
            printf("server say: %s\n", buff);
        }
        else if(len==0)
        {
            printf("服务器已经断开了连接\n");
        }
        else
        {
            perror("recv");
            break;
        }
        sleep(1);//每隔一秒发一次数据
    }
    //通信结束，关闭文件描述符
    close(fd);

    return 0;
}
