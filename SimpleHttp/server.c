#include "server.h"
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <assert.h>
#include <sys/sendfile.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>

int initListenFd(unsigned short port)
{
    //1.创建监听的套接字
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if(lfd == -1)
    {
        perror("socket");
        return -1;
    }
    //2.设置端口复用
    int opt = 1;//设置可以复用
    int ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if(ret == -1)
    {
        perror("setsockopt");
        return -1;
    }    
    //3.绑定
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;//地址族协议
    addr.sin_port = htons(port);//转成网络字节序
    addr.sin_addr.s_addr = INADDR_ANY;//(是零地址的宏)绑定本地IP地址（s_addr是个整形数）（0表示绑定本地任一个IP地址）
    ret = bind(lfd, (struct sockaddr*)&addr, sizeof addr);
    if(ret == -1)
    {
        perror("bind");
        return -1;
    }    
    //4.设置监听
    ret = listen(lfd, 128);
    if(ret == -1)
    {
        perror("listen");
        return -1;
    }      
    //返回fd
    return lfd;
}
int epollRun(int lfd)
{
    //1.创建epoll实例
    int epfd = epoll_create(1);//参数：>0即可，该参数已被弃用//得到epoll树的根节点，它也是个文件描述符
    if(epfd == -1)
    {
        perror("epoll_create");
        return -1;
    }
    //2.lfd上树
    struct epoll_event ev;
    ev.data.fd = lfd;//联合体：成员只能用一个
    ev.events = EPOLLIN;//读事件
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
    if(ret == -1)
    {
        perror("epoll_ctl");
        return -1;
    }
    //3.检测
    struct epoll_event evs[1024];
    int size = sizeof(evs)/sizeof(struct epoll_event);
    while(1)
    {
        int num = epoll_wait(epfd, evs, size, -1);//-1表示未触发时一直阻塞
        for(int i = 0;i<num;++i)
        {
            int fd = evs[i].data.fd;
            if(fd == lfd)
            {
                //建立新连接accept（已经到达，accept不会阻塞）
                acceptClient(lfd, epfd);//把lfd添加到epoll树上
            }
            else
            {
                //主要是接收对端的数据（不用判断写OR读了，就是读）<--搞懂HTTP协议
                recvHttpRequest(fd, epfd);
            }
        }
    }
    return 0;
}
int acceptClient(int lfd, int epfd)
{
    //1.建立连接
    int cfd = accept(lfd, NULL, NULL);
    if(cfd == -1)
    {
        perror("accept");
        return -1;
    }
    //2.设置非阻塞-->设置边沿触发模式（ET）
    int flag = fcntl(cfd, F_GETFL);
    flag |= O_NONBLOCK;//追加非阻塞属性
    fcntl(cfd, F_SETFL, flag);//设置属性

    //3.cfd添加到epoll中
    struct epoll_event ev;
    ev.data.fd = cfd;
    ev.events = EPOLLIN | EPOLLET;//读事件,ET模式
    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
    if(ret == -1)
    {
        perror("epoll_ctl");
        return -1;
    }
    return 0;
}
int recvHttpRequest(int cfd, int epfd)
{
    printf("开始接收数据了...\n");
    int len = 0, total = 0;
    char tmp[1024] = {0};//为了防止每次读数据覆盖之前的
    char buf[4096] = {0};
    //因为是ET模式，只触发一次，所以要一次性读完
    while((len = recv(cfd, tmp, sizeof tmp, 0))>0)
    {
        if((total+len) < sizeof(buf))
        {
            memcpy(buf+total, tmp, len);
        }
        total += len;
    }
    //判断数据是否被接收完毕
    if(len == -1 && errno == EAGAIN)
    {
        //解析请求行(先只用get请求：请求头 请求行 空 空)
        // 修改后
        char* pt = strstr(buf, "\r\n");
        if (pt == NULL) {
            return -1; // 没有找到请求行结束符，直接丢弃或等待下次读取
        }
        int reqLen = pt - buf;
        buf[reqLen] = '\0';//人为分为两段(读到\0会认为字符串结束)
        parseRequestLine(buf, cfd);
        // 【加上这两行！！！】
        // 告诉浏览器：我的目录发完了，主动断开连接，你赶紧渲染吧！
        epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
        close(cfd);
    }
    else if(len == 0)
    {
        //客户端断开了连接
        epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
        close(cfd);
    }
    else
    {
        perror("recv");
    }
    return 0;
}

int parseRequestLine(const char* line, int cfd)
{
    //解析请求行 get /xxx/1.jpg http/1.1
    char method[12];//get 或 post
    char path[1024];
    // 限制提取长度，防止溢出 (12-1=11, 1024-1=1023)
    sscanf(line,"%11[^ ] %1023[^ ]",method,path);
    printf("method: %s,path: %s\n",method, path);
    if(strcasecmp(method,"get") != 0)
    {
        return -1;
    }
    decodeMsg(path,path);
    //处理客户端请求的静态资源（目录或文件）
    char* file = NULL;
    if (strstr(path, "..") != NULL) 
    {
    // 返回 403 Forbidden 或者 404
    return -1; 
    }
    if(strcmp(path, "/") == 0)//是根目录
    {
        file = "./";//转换成相对路径
    }
    else
    {
        file = path + 1;//去掉第一个斜杠，就表相对路径了
    }
    //获取文件属性
    struct stat st;
    int ret = stat(file, &st);
    if(ret == -1)
    {
        //文件不存在--回复404
        //sendHeadMsg(cfd, 404, "Not Found", getFileType(".html"),-1);//-1表不知道长度
        //sendFile("404.html", cfd);
        //return 0;
        // 【修改开始】：不要去读外部的 404.html 了，直接发送字符串！
        sendHeadMsg(cfd, 404, "Not Found", getFileType(".html"), -1);
        
        const char* err_msg = "<html><head><title>404 Not Found</title></head><body><h1 style=\"color:red;\">404 Not Found</h1><p>The requested file does not exist on this server.</p></body></html>";
        send(cfd, err_msg, strlen(err_msg), 0);
        
        return 0;
        // 【修改结束】
    }
    //判断文件类型
    if(S_ISDIR(st.st_mode))//是目录（=1）
    {
        //把这个目录中的内容发送给客户端
        sendHeadMsg(cfd, 200, "OK", getFileType(".html"),-1);
        sendDir(file, cfd);
    }
    else
    {
        //把文件中的内容发送给客户端
        sendHeadMsg(cfd, 200, "OK", getFileType(file),st.st_size);//st中保存了文件的属性，可以得到文件大小
        sendFile(file, cfd);
    }
    return 0;
}
const char* getFileType(const char* name)
{
    //a.jpg a.mp4 a.html
    //自右向左查找'.'字符，如不存在返回NULL
    const char* dot = strrchr(name, '.');
    if(dot == NULL)
        return "text/plain; charset=utf-8";//纯文本
    if(strcmp(dot, ".html") == 0 || strcmp(dot,".htm") == 0)
        return "text/html; charset=utf-8";
    if(strcmp(dot, ".jpg") == 0 || strcmp(dot,".jpeg") == 0)
        return "image/jpeg";
    if(strcmp(dot, ".gif") == 0)
        return "image/gif";
    if(strcmp(dot, ".png") == 0)
        return "image/png";
    if(strcmp(dot, ".css") == 0)
        return "text/css";
    if(strcmp(dot, ".au") == 0)
        return "audio/basic";
    if(strcmp(dot, ".wav") == 0)
        return "audio/wav";
    if(strcmp(dot, ".avi") == 0)
        return "video/x-msvideo";   
    if(strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
        return "video/quicktime";  
    if(strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
        return "video/mpeg"; 
    if(strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
        return "model/vrml"; 
    if(strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
        return "audio/midi";
    if(strcmp(dot, ".mp3") == 0)
        return "audio/mpeg";  
    if(strcmp(dot, ".mp4") == 0)
        return "video/mp4";  
    if(strcmp(dot, ".ogg") == 0)
        return "application/ogg";   
    if(strcmp(dot, ".pac") == 0)
        return "application/x-ns-proxy-autoconfig";   
    
    return "text/plain; charset=utf-8";
    // 修改后：告诉浏览器这是二进制流，让它直接下载
    //return "application/octet-stream";
}
/*
<html>
    <head>
        <title>test</title>
    </head>
    <body>
        <table>
            <tr>//表示每行
                <td></td>
                <td></td>
            </tr>
            <tr>
                <td></td>
                <td></td>
            </tr>
        </table>
    </body>
</html>
*/
/*当用户在浏览器中访问一个“目录”时，服务器读取这个目录下的所有文件，
并动态生成一个 HTML 网页，把这些文件以表格和超链接的形式展示给用户，
就像一个网页版的文件管理器。*/
int sendDir(const char* dirName, int cfd)
{
    char buf[4096] = {0};
    sprintf(buf, "<html><head><title>%s</title></head><body><table>",dirName);//因为是TCP流式协议，所以可以拼一点传一点
    struct dirent** namelist;//调用 scandir 把 dirName 目录下的所有文件/文件夹扫描出来，存到 namelist 这个数组里。
    int num = scandir(dirName, &namelist, NULL, alphasort);//遍历目录，返回值是目录下文件个数
    for(int i = 0; i < num; ++i)
    {
        //取出文件名   namelist指向的是一个指针数组 struct dirent* tem[]
        char* name = namelist[i]->d_name;
        struct stat st;
        char subPath[1024] = {0};
        sprintf(subPath, "%s/%s", dirName, name);//拼接成完整相对目录
        stat(subPath, &st);
        if(S_ISDIR(st.st_mode))//是目录
        {
            //a标签 <a href="跳转地址">name</a>     -->超链接 跳转
            sprintf(buf+strlen(buf),"<tr><td><a href=\"%s/\">%s</a></td><td>%ld</td></tr>",name, name, st.st_size);
        }
        else//非目录，按文件方式处理
        {
            sprintf(buf+strlen(buf),"<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",name, name, st.st_size);
        }
        send(cfd, buf, strlen(buf), 0);
        memset(buf, 0, sizeof(buf));
        free(namelist[i]);
    }
    sprintf(buf,"</table></body></html>");
    send(cfd, buf, strlen(buf), 0);
    free(namelist);
    return 0;
}
int sendFile(const char* fileName, int cfd)//TCP流式协议，一部分一部分发
{
    int fd = open(fileName, O_RDONLY);
    //assert(fd > 0);//若断言失败、会抛出异常
    // 【修改开始】：删掉 assert，换成安全的错误判断
    // assert(fd > 0); 
    if (fd == -1) 
    {
        perror("open file failed");
        return -1; // 打开失败直接返回，不要让服务器崩溃
    }
    // 【修改结束】
#if 0
    while(1)
    {
        char buf[1024];
        int len = read(fd, buf, sizeof buf);
        if(len > 0)
        {
            send(cfd, buf, len, 0);
            usleep(10);//这非常重要，防止发太快浏览器解析不过来
        }
        else if(len == 0)
        {
            break;
        }
        else
        {
            perror("read");
        }
    }
#else//Linux自带了一个sendfile函数，效率更高不用从内存拷贝
    off_t offset = 0;
    int size = lseek(fd, 0, SEEK_END);//去读文件字节大小;lseek文件指针会移动到尾部
    lseek(fd, 0, SEEK_SET);//把文件指针移动回到头部
    while(offset < size)
    {
        int ret = sendfile(cfd, fd, &offset, size - offset);//sendfile零拷贝、效率高
        printf("ret value: %d\n",ret);
        if(ret == -1 && errno == EAGAIN)
        {
            printf("没数据，发送缓冲区满了，等待中...\n");
            usleep(10000); // 休眠 10ms，防止 CPU 100%
        }
        // 【加上下面这个分支！！！】
        else if (ret == -1)
        {
            // 如果客户端断开了连接，或者发生其他错误，必须跳出循环！
            perror("sendfile error");
            break;
        }
    }
#endif
    close(fd);
    return 0;
}
int sendHeadMsg(int cfd, int status, const char* descr, const char* type, int length)
{
    //状态行
    char buf[4096] = {0};
    sprintf(buf, "http/1.1 %d %s\r\n", status, descr);//拼接字符串
    //响应头
    sprintf(buf + strlen(buf), "content-type: %s\r\n",type);//进行格式化
    //sprintf(buf + strlen(buf), "content-length: %d\r\n\r\n",length);//长度 + 空行
    // 修改 sendHeadMsg 的内容长度拼接：
    if (length == -1) {
        sprintf(buf + strlen(buf), "\r\n"); // 不知道长度时，不要发 content-length，直接跟空行
    } else {
        sprintf(buf + strlen(buf), "content-length: %d\r\n\r\n", length);
    }
    send(cfd, buf, strlen(buf), 0);
    return 0;
}
//将字符转换成整形数(十六进制->十进制)
int hexToDec(char c)
{
    if(c >= '0' && c <= '9')
        return c - '0';
    if(c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if(c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return 0;
}
//解码
//to存储解码之后的数据，传出参数， from被解码的数据，传入参数
void decodeMsg(char* to, char* from)
{
    for(; *from != '\0'; ++to, ++from)
    {
        //isxdigit -> 判断字符是不是16进制格式，取值在0-f
        //Linux%E5%86%85%E6%A0%B8.jpg
        if(from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2]))
        {
            //将16进制的数->十进制 将这个数值赋值给了字符int->char
            //B2==178
            //将3个字符，变成了一个字符，这个字符就是原始数据
            *to = hexToDec(from[1])*16+hexToDec(from[2]);
            from += 2;
        }
        else
        {
            *to = *from;//字符拷贝，赋值
        }
    }
    *to = '\0'; // 必须加上这一句，确保字符串正常结束
}