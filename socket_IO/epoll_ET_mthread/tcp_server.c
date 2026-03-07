#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

typedef struct socketinfo
{
    int fd;
    int epfd;
} SocketInfo;

// 设置文件描述符为非阻塞
void set_nonblocking(int fd) {
    int flag = fcntl(fd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flag);
}

// 重置 ONESHOT 事件 (让 epoll 能再次检测该 fd)
void reset_oneshot(int epfd, int fd) {
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    ev.data.fd = fd;
    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}

void* acceptConn(void* arg)
{
    printf("acceptConn tid: %ld\n", pthread_self());
    SocketInfo* info = (SocketInfo*)arg;
    
    // 【修正2】ET模式下，accept 必须循环直到空
    while(1)
    {
        int cfd = accept(info->fd, NULL, NULL);
        if(cfd == -1)
        {
            if(errno == EAGAIN) {
                // 处理完了所有连接
                break;
            }
            perror("accept");
            break;
        }

        set_nonblocking(cfd);

        struct epoll_event ev;
        // 【修正3】加入 EPOLLONESHOT，防止多线程竞争同一个fd
        ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
        ev.data.fd = cfd;
        epoll_ctl(info->epfd, EPOLL_CTL_ADD, cfd, &ev);
        printf("New client connected: %d\n", cfd);
    }

    free(info);
    return NULL;
}

void* communication(void* arg)
{
    printf("communication tid: %ld\n", pthread_self());
    SocketInfo* info = (SocketInfo*)arg;
    
    // 【修正1】buf 留一个位置给 \0
    char buf[6]; 
    char temp[1024];
    memset(temp, 0, sizeof(temp));
    
    while(1)
    {
        // 每次只读 sizeof(buf)-1，确保能放进 \0
        memset(buf, 0, sizeof(buf));
        int len = read(info->fd, buf, sizeof(buf) - 1);
        
        if(len == -1)
        {
            if(errno == EAGAIN)
            {
                printf("数据接收完毕，准备回写...\n");
                send(info->fd, temp, strlen(temp)+1, 0);
                
                // 【重要】处理完了，重置 ONESHOT，让 epoll 能再次监听到这个 fd 的下一次消息
                reset_oneshot(info->epfd, info->fd);
                break;
            }
            else
            {
                perror("read");
                epoll_ctl(info->epfd, EPOLL_CTL_DEL, info->fd, NULL);
                close(info->fd);
                break;
            }
        }
        else if(len == 0)
        {
            printf("客户端关闭了连接...\n");
            epoll_ctl(info->epfd, EPOLL_CTL_DEL, info->fd, NULL);
            close(info->fd);
            break;
        }
        
        // 转大写
        for(int j = 0; j < len; ++j)
        {
            buf[j] = toupper(buf[j]);
        }
        
        // 【修正1】现在 buf 是安全的字符串了，可以用 strcat
        // 但要注意 temp 不要溢出，这里简单处理
        if(strlen(temp) + len < sizeof(temp)) {
            strcat(temp, buf);
        }
        
        write(STDOUT_FILENO, buf, len);
        printf("\n");
    }
    
    free(info);
    return NULL;
}

int main()
{
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9999);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(lfd, (struct sockaddr*)&addr, sizeof(addr));
    listen(lfd, 128);

    int epfd = epoll_create(1);
    struct epoll_event ev;
    // 监听 socket 建议也加 ONESHOT，防止多线程同时 accept 造成惊群（虽然这里概率低）
    ev.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    ev.data.fd = lfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);

    struct epoll_event evs[1024];

    while(1)
    {
        int num = epoll_wait(epfd, evs, 1024, -1);
        for(int i = 0; i < num; ++i)
        {
            int fd = evs[i].data.fd;
            SocketInfo* info = (SocketInfo*)malloc(sizeof(SocketInfo));
            info->fd = fd;
            info->epfd = epfd;

            pthread_t tid;
            if(fd == lfd)
            {
                pthread_create(&tid, NULL, acceptConn, info);
                pthread_detach(tid);
            }
            else
            {
                pthread_create(&tid, NULL, communication, info);
                pthread_detach(tid);
            }
        }
    }
    close(lfd);
    return 0;
}