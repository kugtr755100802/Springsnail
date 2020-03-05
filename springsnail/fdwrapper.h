#ifndef FDWRAPPER_H
#define FDWRAPPER_H

enum RET_CODE { OK = 0, NOTHING = 1, IOERR = -1, CLOSED = -2, BUFFER_FULL = -3, BUFFER_EMPTY = -4, TRY_AGAIN };
enum OP_TYPE { READ = 0, WRITE, ERROR };
int setnonblocking(int fd);//���ļ�������fdע��ɷ�������
void add_read_fd(int epollfd, int fd);//��epoll�ں��¼�����ע���ļ�������fd�ϵ�EPOLLIN�¼�
void add_write_fd(int epollfd, int fd);//��epoll�ں��¼�����ע��д�ļ�������fd�ϵ�EPOLLIN�¼�
void removefd(int epollfd, int fd);//��epoll�¼�����ɾ��fd���¼�
void closefd(int epollfd, int fd);//��epoll�¼�����ɾ��fd���¼����ر��ļ�������fd
void modfd(int epollfd, int fd, int ev);//�޸�������fd�ϵ��¼�Ϊev|EPOLLET

#endif
