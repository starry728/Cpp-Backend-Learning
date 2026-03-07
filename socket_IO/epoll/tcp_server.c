#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/epoll.h>
//epoll 单线程
//线程同步：无

int main()
{
    // 1. 创建监听的fd
    int lfd = socket(AF_INET, SOCK_STREAM, 0);

    // ==== 端口复用设置 ====
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    // ====================

    // 2. 绑定
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9999);
    addr.sin_addr.s_addr = INADDR_ANY;
    int ret = bind(lfd, (struct sockaddr*)&addr, sizeof(addr));
    if(ret==-1)
    {
        perror("bind");
        exit(1);
    }

    // 3. 设置监听
    ret = listen(lfd, 128);
    if(ret==-1)
    {
        perror("listen");
        exit(1);
    }
    //创建epoll实例
    int epfd = epoll_create(1);//指定一个>0的数字即可，无实际意义
    if(epfd==-1)
    {
        perror("epoll_create");
        exit(1);
    }    
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = lfd;
    epoll_ctl(epfd,EPOLL_CTL_ADD,lfd,&ev);

    struct epoll_event evs[1024];
    int size = sizeof(evs)/sizeof(evs[0]);

    while(1)
    {
        int num = epoll_wait(epfd,evs,size,-1);
        printf("num = %d\n",num);
        for(int i=0;i<num;++i)
        {
            int fd = evs[i].data.fd;
            if(fd==lfd)//监听fd
            {
                int cfd = accept(lfd, NULL, NULL);
                ev.events = EPOLLIN;
                ev.data.fd = cfd;
                epoll_ctl(epfd,EPOLL_CTL_ADD,cfd,&ev);
            }
            else//通信fd
            {
                char buf[5] = {0};
                int len = read(fd, buf, sizeof(buf));
                if(len == -1)
                {
                    // 异常
                    perror("read");
                    exit(1);
                }
                else if(len == 0)
                {
                    printf("客户端关闭了连接...\n");
                    epoll_ctl(epfd,EPOLL_CTL_DEL,fd,NULL);//删除节点：先删除
                    close(fd);//再关闭fd
                    break;
                }
                printf("read buf = %s\n",buf);
                 
                //小写转大写,发给客户端
                for(int j=0;j<len;++j)
                {
                    buf[j]=toupper(buf[j]);
                }
                printf("after buf = %s\n",buf);
                len = send(fd,buf,len,0);
                if(len==-1)
                {
                    perror("send error");
                    exit(1);
                }
            }
        }
    }
    close(lfd);
    return 0;
}
