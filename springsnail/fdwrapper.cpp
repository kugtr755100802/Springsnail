#ifndef FDWRAPPER_H
#define FDWRAPPER_H

#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
//���ļ�������fdע��ɷ�������
int setnonblocking( int fd )
{
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}
//��epoll�ں��¼�����ע����ļ�������fd�ϵ�EPOLLIN�¼���EPOLLET�����Ƿ�ʹ��ET����ģʽ
void add_read_fd( int epollfd, int fd )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}
//��epoll�ں��¼�����ע��д�ļ�������fd�ϵ�EPOLLOUT�¼�
void add_write_fd( int epollfd, int fd )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLOUT | EPOLLET;
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}
//��epoll�¼�����ɾ��fd���¼����ر��ļ�������fd
void closefd( int epollfd, int fd )
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    close( fd );
}
//��epoll�¼�����ɾ��fd���¼�
void removefd( int epollfd, int fd )
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
}
/*
//�޸�������fd�ϵ��¼�Ϊev|EPOLLET

���������Ҫ�ص���ܣ�EPOLL_CTL_MOD�Ĳ���ͨ����ET������ģʽ�µ�����������
��epoll������ETģʽ��ʱ�����ڶ����������readһ��û�ж���buffer�е����ݣ�
��ô�´ν��ò�����������֪ͨ�����buffer�����е������޻���������������µ������ٴε��
����д��������Ҫ����ΪETģʽ��fdͨ��Ϊ��������ɵ�һ�����⡪����α�֤���û�Ҫ��д������д�ꡣ

����������Ҫʵ������������
a. ���ڶ���ֻҪbuffer�л������ݾ�һֱ����

b. ����д��ֻҪbuffer���пռ����û�����д�����ݻ�δд�꣬��һֱд��

���ԣ�����������ǣ�
(1) ÿ�ζ��������read��recv�����û�����epoll_mod IN�¼�����ʱֻҪ��fd�Ļ��廹�����ݿ��Զ���
��epoll_wait�᷵�ض�������

(2) ÿ�����������write��send�����û�����epoll_mod OUT�¼�����ʱֻҪ�ø�fd�Ļ�����Է������ݣ�����buffer��������
��epoll_wait�ͻ᷵��д��������ʱ����øû���֪ͨepoll_wai�ѹ�������


*/
void modfd( int epollfd, int fd, int ev )
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET;
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );
}

#endif
