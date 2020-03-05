#ifndef FDWRAPPER_H
#define FDWRAPPER_H

#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
//将文件描述符fd注册成非阻塞的
int setnonblocking( int fd )
{
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}
//向epoll内核事件表中注册读文件描述符fd上的EPOLLIN事件，EPOLLET表明是否使用ET工作模式
void add_read_fd( int epollfd, int fd )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}
//向epoll内核事件表中注册写文件描述符fd上的EPOLLOUT事件
void add_write_fd( int epollfd, int fd )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLOUT | EPOLLET;
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}
//从epoll事件表中删除fd的事件并关闭文件描述符fd
void closefd( int epollfd, int fd )
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    close( fd );
}
//从epoll事件表中删除fd的事件
void removefd( int epollfd, int fd )
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
}
/*
//修改描述符fd上的事件为ev|EPOLLET

这个函数需要重点介绍，EPOLL_CTL_MOD的操作通常是ET非阻塞模式下的问题解决方案
当epoll工作在ET模式下时，对于读操作，如果read一次没有读尽buffer中的数据，
那么下次将得不到读就绪的通知，造成buffer中已有的数据无机会读出，除非有新的数据再次到达。
对于写操作，主要是因为ET模式下fd通常为非阻塞造成的一个问题——如何保证将用户要求写的数据写完。

所以我们需要实现两个方案：
a. 对于读，只要buffer中还有数据就一直读；

b. 对于写，只要buffer还有空间且用户请求写的数据还未写完，就一直写。

所以，解决方案就是：
(1) 每次读入操作后（read，recv），用户主动epoll_mod IN事件，此时只要该fd的缓冲还有数据可以读，
则epoll_wait会返回读就绪。

(2) 每次输出操作后（write，send），用户主动epoll_mod OUT事件，此时只要该该fd的缓冲可以发送数据（发送buffer不满），
则epoll_wait就会返回写就绪（有时候采用该机制通知epoll_wai醒过来）。


*/
void modfd( int epollfd, int fd, int ev )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET;
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}

#endif
