#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <vector>
#include "log.h"
#include "fdwrapper.h"

using std::vector;
//描述一个子进程类，m_pid是目标子进程的PID,m_pipefd是父进程与子进程的通信管道
class process
{
public:
    process() : m_pid( -1 ){}
public:
	int m_busy_ratio;	//给每台实际处理服务器（业务逻辑服务器）分配一个加权比例
	pid_t m_pid;       //目标子进程的PID
	int m_pipefd[2];   //父进程和子进程通信用的管道
};



template< typename C, typename H, typename M >
class processpool
{
private:
    processpool( int listenfd, int process_number = 8 );
public:
    static processpool< C, H, M >* create( int listenfd, int process_number = 8 )
    {
        if( !m_instance ) //单件模式,确保一个类只有一个实例
        {
            m_instance = new processpool< C, H, M >( listenfd, process_number );
        }
        return m_instance;
    }
    ~processpool()
    {
        delete [] m_sub_process;
    }
	//启动进程池
    void run( const vector<H>& arg );


private:
	void notify_parent_busy_ratio(int pipefd, M* manager);  //获取目前连接数量，将其发送给父进程
	int get_most_free_srv();  //找出最空闲的服务器
	void setup_sig_pipe(); //统一事件源
	void run_parent();//启动父进程
	void run_child(const vector<H>& arg);//启动子进程


private:
	static const int MAX_PROCESS_NUMBER = 16;   //进程池允许最大进程数量
	static const int USER_PER_PROCESS = 65536;  //每个子进程最多能处理的客户数量
	static const int MAX_EVENT_NUMBER = 10000;  //EPOLL最多能处理的的事件数
	int m_process_number;  //进程池中的进程总数
	int m_idx;  //子进程在池中的序号（从0开始）
	int m_epollfd;  //当前进程的epoll内核事件表fd
	int m_listenfd;  //监听socket
	int m_stop;      //子进程通过m_stop来决定是否停止运行
	process* m_sub_process;  //保存所有子进程的描述信息
	static processpool< C, H, M >* m_instance;  //进程池静态实例
};
template< typename C, typename H, typename M >
processpool< C, H, M >* processpool< C, H, M >::m_instance = NULL;

static int EPOLL_WAIT_TIME = 5000;
static int sig_pipefd[2];//用于处理信号的管道，以实现统一事件源,后面称之为信号管道
//信号处理函数
static void sig_handler( int sig )
{
    int save_errno = errno;
    int msg = sig;
	//将所得信号送给信号处理管道，统一处理，统一事件源
    send( sig_pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}
//添加信号,设置信号处理函数
static void addsig(int sig, void(handler)(int), bool restart = true)
{
	struct sigaction sa;//设置信号函数的接口
	memset(&sa, '\0', sizeof(sa));
	sa.sa_handler = handler;//指明信号处理函数
	if (restart)
	{
		sa.sa_flags |= SA_RESTART;  //重新调用被该信号终止的系统调用,sa_flags设置程序收到信号的行为
	}
	sigfillset(&sa.sa_mask);  //在信号集函数中设置所有信号，sa_mask的作用是设置信号掩码
	assert(sigaction(sig, &sa, NULL) != -1);
}
//进程池构造函数
template< typename C, typename H, typename M >
processpool< C, H, M >::processpool( int listenfd, int process_number ) 
    : m_listenfd( listenfd ), m_process_number( process_number ), m_idx( -1 ), m_stop( false )
{
    assert( ( process_number > 0 ) && ( process_number <= MAX_PROCESS_NUMBER ) );
	//分配保存所有子进程的描述信息的数组
    m_sub_process = new process[ process_number ];
    assert( m_sub_process );

    for( int i = 0; i < process_number; ++i )
    {
		//socketpair创建一对无名套接字，用于全双工通信
		//这里将每个子进程的m_pipefd连接起来
        int ret = socketpair( PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd );
        assert( ret == 0 );
		//fork函数创建子进程
        m_sub_process[i].m_pid = fork();
        assert( m_sub_process[i].m_pid >= 0 );
        if( m_sub_process[i].m_pid > 0 )//父进程
        {
			//如果是父进程，关闭管道1，因为1只能写数据
            close( m_sub_process[i].m_pipefd[1] );
            m_sub_process[i].m_busy_ratio = 0;
            continue;
        }
        else
        {
			//如果是子进程，关闭管道0，因为0只能读数据
            close( m_sub_process[i].m_pipefd[0] );
            m_idx = i;
            break;
        }
    }
}
//采用轮询的方式得到当前最空闲的服务器
template< typename C, typename H, typename M >
int processpool< C, H, M >::get_most_free_srv()
{
	//每台处理连接的子进程（服务器）都有一个权重busy_ratio
	int ratio = m_sub_process[0].m_busy_ratio;
	int idx = 0;
	//采用轮询的方式得到最小权重的服务器
	for (int i = 0; i < m_process_number; ++i)
	{
		if (m_sub_process[i].m_busy_ratio < ratio)
		{
			idx = i;
			ratio = m_sub_process[i].m_busy_ratio;
		}
	}
	return idx;
}
//统一事件源，将信号事件与I/O事件一同处理
template< typename C, typename H, typename M >
void processpool< C, H, M >::setup_sig_pipe()  //统一事件源
{

	m_epollfd = epoll_create(5);//创建epoll描述符
	assert(m_epollfd != -1);
	//sig_pipefd是处理信号的管道，创建它
	int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);   //全双工管道
	assert(ret != -1);
	//设置描述符为非阻塞模式
	setnonblocking(sig_pipefd[1]);  //非阻塞写
	//向内核事件表中添加读事件，设置为非阻塞
	add_read_fd(m_epollfd, sig_pipefd[0]); //监听管道读端并设置为非阻塞

	addsig(SIGCHLD, sig_handler);  //子进程状态发生变化（退出或暂停）
	addsig(SIGTERM, sig_handler);  //终止进程,kill命令默认发送的即为SIGTERM
	addsig(SIGINT, sig_handler);   //键盘输入中断进程（Ctrl + C）
	addsig(SIGPIPE, SIG_IGN);      /*往被关闭的文件描述符中写数据时触发会使程序退出
									   SIG_IGN可以忽略，在write的时候返回-1,
									   errno设置为SIGPIPE*/
}


//父进程中m_idx值为-1，子进程中m_idx>0，据此判断运行父进程代码或者子进程代码
template< typename C, typename H, typename M >
void processpool< C, H, M >::run(const vector<H>& arg)
{
	if (m_idx != -1)
	{
		run_child(arg);
		return;
	}
	run_parent();
}
//获取目前连接数量，将其发送给父进程
template< typename C, typename H, typename M >
void processpool< C, H, M >::notify_parent_busy_ratio( int pipefd, M* manager )
{
    int msg = manager->get_used_conn_cnt();
    send( pipefd, ( char* )&msg, 1, 0 );    
}

//子进程运行过程
//根据半同步/半异步模型，父进程用于对监听的socket调用epoll_wait()添加事件
//子进程则需要监听连接socket并调用epoll_wait()添加事件

/*
在此例中,C为conn客户端，H为host主机端，M为mgr管理
mgr就是manager，管理和保存逻辑服务器的连接信息
mgr中有三个map用于模拟请求队列：m_conns,m_used,m_freed
m_conns保存已经建立好的就绪连接，当新建一个manager实例就会建立一个就绪连接
m_used是一个任务队列，表示将要使用的连接，pick_conn将已经建立好的就绪连接放入任务队列中
m_freed保存使用后被释放的连接,由free_conn完成操作
recycle_conns将已经free的连接重新fix，保存进m_conns中
*/
template< typename C, typename H, typename M >
void processpool< C, H, M >::run_child( const vector<H>& arg )
{
	//首先添加信号事件,统一事件源
    setup_sig_pipe();
	//每个子进程都通过m_idx找到在与父进程的通信管道
    int pipefd_read = m_sub_process[m_idx].m_pipefd[ 1 ];
    add_read_fd( m_epollfd, pipefd_read );
	//子进程需要监听管道描述符m_pipefd，因为如果有新连接进来，父进程需要通过管道通知子进程accept连接
	//内核事件表
    epoll_event events[ MAX_EVENT_NUMBER ];
	//manager是保存和管理连接信息的逻辑服务器
    M* manager = new M( m_epollfd, arg[m_idx] );
    assert( manager );

    int number = 0;
    int ret = -1;

    while( ! m_stop )
    {
		//epoll_wait得到已就绪的事件
        number = epoll_wait( m_epollfd, events, MAX_EVENT_NUMBER, EPOLL_WAIT_TIME );
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            log( LOG_ERR, __FILE__, __LINE__, "%s", "epoll failure" );
            break;
        }
		//epoll_wait得到已就绪的事件
        if( number == 0 )
        {
            manager->recycle_conns();//回收已经被释放的连接
            continue;
        }

        for ( int i = 0; i < number; i++ )
        {
			//得到有就绪事件的描述符
            int sockfd = events[i].data.fd;
			//EPOLLIN:只有当对端有数据写入时才会触发,触发一次后需要不断读取所有数据直到读完EAGAIN为止
			//EPOLLIN是防止epoll在ET模式下因为上一次没读完数据而不会有下一个就绪事件的情况发生
			if ((sockfd == pipefd_read) && (events[i].events & EPOLLIN))  //是父进程发送的消息（通知有新的客户连接到来）
            {
                int client = 0;
				//从父子之间的管道用recv读取数据，将结果保存在client中
                ret = recv( sockfd, ( char* )&client, sizeof( client ), 0 );
                if( ( ( ret < 0 ) && ( errno != EAGAIN ) ) || ret == 0 ) 
                {
                    continue;
                }
                else
                {
                    struct sockaddr_in client_address;
                    socklen_t client_addrlength = sizeof( client_address );
					//从监听队列中接受连接，m_listenfd是进程监听的客户连接
                    int connfd = accept( m_listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                    if ( connfd < 0 )
                    {
                        log( LOG_ERR, __FILE__, __LINE__, "errno: %s", strerror( errno ) );
                        continue;
                    }
					//accept函数返回的connfd是一个新的连接，该socket唯一的标识了被接受的这个连接，可以用connfd进行通信了
				   //注册事件
                    add_read_fd( m_epollfd, connfd );
					//从已经准备好的连接中pick一个，返回服务端的信息
                    C* conn = manager->pick_conn( connfd );
                    if( !conn )
                    {
                        closefd( m_epollfd, connfd );
                        continue;
                    }
					//初始化客户端信息
                    conn->init_clt( connfd, client_address );
					//获取目前manager的连接数量，发送给父进程
                    notify_parent_busy_ratio( pipefd_read, manager );
                }
            }
			//处理自身进程接收到的信号
            else if( ( sockfd == sig_pipefd[0] ) && ( events[i].events & EPOLLIN ) )
            {
                int sig;
                char signals[1024];
				//TCP的recv接受sig_pipefd[0]的内容，保存在signals中
                ret = recv( sig_pipefd[0], signals, sizeof( signals ), 0 );
                if( ret <= 0 )
                {
                    continue;
                }
                else
                {
                    for( int i = 0; i < ret; ++i )
                    {
						//判断信号类型
                        switch( signals[i] )
                        {
							//当子进程退出时，会向父进程发送SIGCHLD信号,父进程在没有收到这个信号的情况下需要等，否则子进程会变成僵尸进程
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
								//表示对任意进程进行等待，以非阻塞方式加轮循，应对同一时间多个子进程退出
                                while ( ( pid = waitpid( -1, &stat, WNOHANG ) ) > 0 )//等收集退出的子进程，由于设置了WNOHANG因此不等待
                                {
                                    continue;
                                }
                                break;
                            }
                            case SIGTERM:
								//退出该进程
                            case SIGINT:
                            {
                                m_stop = true;
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }
			//如果是其他处理数据，那必然是客户端请求到来，调用逻辑对象的process方法来处理
			//前面说到了，所有逻辑单元的事情都交给模板类来处理，实现模板类很重要
			//这里处理客户端连接就用到了模板类manager的process函数
            else if( events[i].events & EPOLLIN )
            {
				//process处理
                 RET_CODE result = manager->process( sockfd, READ );
                 switch( result )
                 {
                     case CLOSED:
                     {
						 //获取目前manager的连接数量，发送给父进程
                         notify_parent_busy_ratio( pipefd_read, manager );
                         break;
                     }
                     default:
                         break;
                 }
            }
            else if( events[i].events & EPOLLOUT )//写事件
            {
                 RET_CODE result = manager->process( sockfd, WRITE );
                 switch( result )
                 {
                     case CLOSED:
                     {
                         notify_parent_busy_ratio( pipefd_read, manager );
                         break;
                     }
                     default:
                         break;
                 }
            }
            else
            {
                continue;
            }
        }
    }

    close( pipefd_read );
    close( m_epollfd );
}
//父进程需要完成的工作
template< typename C, typename H, typename M >
void processpool< C, H, M >::run_parent()
{
	//同样的，也需要统一事件源
	setup_sig_pipe();
	//向epoll事件表注册所有与子进程之间的管道的读事件

    for( int i = 0; i < m_process_number; ++i )
    {
        add_read_fd( m_epollfd, m_sub_process[i].m_pipefd[ 0 ] );
    }
	//向epoll事件表注册m_listenfd的事件
    add_read_fd( m_epollfd, m_listenfd );
	//epoll事件
    epoll_event events[ MAX_EVENT_NUMBER ];
	//子进程个数
    int sub_process_counter = 0;
    int new_conn = 1;
    int number = 0;
    int ret = -1;

    while( ! m_stop )
    {
		//内核事件表中的就绪事件
        number = epoll_wait( m_epollfd, events, MAX_EVENT_NUMBER, EPOLL_WAIT_TIME );
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            log( LOG_ERR, __FILE__, __LINE__, "%s", "epoll failure" );
            break;
        }

        for ( int i = 0; i < number; i++ )
        {
			//得到有就绪事件的描述符
            int sockfd = events[i].data.fd;
			//如果是父进程监听的socket有新客户到来，就采用负载均衡策略寻找一个负载最小的子进程处理
            if( sockfd == m_listenfd )
            {
                /*
                int i =  sub_process_counter;
                do
                {
                    if( m_sub_process[i].m_pid != -1 )
                    {
                        break;
                    }
                    i = (i+1)%m_process_number;
                }
                while( i != sub_process_counter );
                
                if( m_sub_process[i].m_pid == -1 )
                {
                    m_stop = true;
                    break;
                }
                sub_process_counter = (i+1)%m_process_number;
                */
                int idx = get_most_free_srv();
				//找好子进程之后，需要将这个请求事件通知给子进程，使用父子进程的管道即可
                send( m_sub_process[idx].m_pipefd[0], ( char* )&new_conn, sizeof( new_conn ), 0 );
                log( LOG_INFO, __FILE__, __LINE__, "send request to child %d", idx );
            }
			//如果是信号,父进程需要处理之
            else if( ( sockfd == sig_pipefd[0] ) && ( events[i].events & EPOLLIN ) )
            {
                int sig;
                char signals[1024];
				//得到传递的信号
                ret = recv( sig_pipefd[0], signals, sizeof( signals ), 0 );
                if( ret <= 0 )
                {
                    continue;
                }
                else
                {
                    for( int i = 0; i < ret; ++i )
                    {
                        switch( signals[i] )
                        {
							//如果有子进程通知退出
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
                                while ( ( pid = waitpid( -1, &stat, WNOHANG ) ) > 0 )
                                {
									//如果进程池中第i个子进程退出了，则主进程关闭相应的通信管道，并设置相应的m_pid为-1，标记该子进程已经退出。
                                    for( int i = 0; i < m_process_number; ++i )
                                    {
                                        if( m_sub_process[i].m_pid == pid )
                                        {
                                            log( LOG_INFO, __FILE__, __LINE__, "child %d join", i );
                                            close( m_sub_process[i].m_pipefd[0] );
                                            m_sub_process[i].m_pid = -1;
                                        }
                                    }
                                }
                                m_stop = true;
								//如果所有子进程都退出，父进程才可以退出，防止僵尸进程造成内存泄漏
                                for( int i = 0; i < m_process_number; ++i )
                                {
                                    if( m_sub_process[i].m_pid != -1 )
                                    {
                                        m_stop = false;
                                    }
                                }
                                break;
                            }
                            case SIGTERM:
							//停止命令，也是需要停止所有子进程之后才能停止父进程
                            case SIGINT:
                            {
                                log( LOG_INFO, __FILE__, __LINE__, "%s", "kill all the clild now" );
                                for( int i = 0; i < m_process_number; ++i )
                                {
                                    int pid = m_sub_process[i].m_pid;
                                    if( pid != -1 )
                                    {
                                        kill( pid, SIGTERM );//kill命令其实是一个发送信号的函数
                                    }
                                }
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }
			//如果不是新连接也不是信号，有可能是子进程向父进程发送的数据
			//整个程序中，子进程会向父进程发送自己已经使用的连接数量
			/*
				父进程需要根据这个数据更新m_sub_process[index]的加权比例，
				以调用get_most_free_srv找到负载最小的服务器
			*/
            else if( events[i].events & EPOLLIN )
            {
                int busy_ratio = 0;
				//这里的sockfd是父子进程间的通信管道
                ret = recv( sockfd, ( char* )&busy_ratio, sizeof( busy_ratio ), 0 );
                if( ( ( ret < 0 ) && ( errno != EAGAIN ) ) || ret == 0 )
                {
                    continue;
                }
                for( int i = 0; i < m_process_number; ++i )
                {
                    if( sockfd == m_sub_process[i].m_pipefd[0] )
                    {
                        m_sub_process[i].m_busy_ratio = busy_ratio;
                        break;
                    }
                }
                continue;
            }
        }
    }

    for( int i = 0; i < m_process_number; ++i )
    {
        closefd( m_epollfd, m_sub_process[i].m_pipefd[ 0 ] );
    }
    close( m_epollfd );
}

#endif
