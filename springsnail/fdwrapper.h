#ifndef FDWRAPPER_H
#define FDWRAPPER_H

enum RET_CODE { OK = 0, NOTHING = 1, IOERR = -1, CLOSED = -2, BUFFER_FULL = -3, BUFFER_EMPTY = -4, TRY_AGAIN };
enum OP_TYPE { READ = 0, WRITE, ERROR };
int setnonblocking(int fd);//将文件描述符fd注册成非阻塞的
void add_read_fd(int epollfd, int fd);//向epoll内核事件表中注册文件描述符fd上的EPOLLIN事件
void add_write_fd(int epollfd, int fd);//向epoll内核事件表中注册写文件描述符fd上的EPOLLIN事件
void removefd(int epollfd, int fd);//从epoll事件表中删除fd的事件
void closefd(int epollfd, int fd);//从epoll事件表中删除fd的事件并关闭文件描述符fd
void modfd(int epollfd, int fd, int ev);//修改描述符fd上的事件为ev|EPOLLET

#endif
