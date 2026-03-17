#include <stdio.h>
#include "server.h"
#include <unistd.h>
#include <stdlib.h>


int main(int argc, char* argv[])//用户输入命令行参数 ：可执行程序名 （两个参数）PORT 资源目录
{
    if(argc < 3)//参数个数小于三，直接退出程序，让用户输入正确的命令
    {
        printf("./a.out port path\n");
        return -1;
    }
    unsigned short port = atoi(argv[1]);
    //切换服务器的工作路径
    chdir(argv[2]);//用户指定的资源目录
    //初始化用于监听的套接字
    int lfd = initListenFd(port);//短整型：0~65535取值，最好不找5000以下的PORT
    //启动服务器程序
    epollRun(lfd);
    return 0;
}