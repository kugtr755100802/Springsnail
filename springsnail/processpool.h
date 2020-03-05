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
//����һ���ӽ����࣬m_pid��Ŀ���ӽ��̵�PID,m_pipefd�Ǹ��������ӽ��̵�ͨ�Źܵ�
class process
{
public:
    process() : m_pid( -1 ){}
public:
	int m_busy_ratio;	//��ÿ̨ʵ�ʴ����������ҵ���߼�������������һ����Ȩ����
	pid_t m_pid;       //Ŀ���ӽ��̵�PID
	int m_pipefd[2];   //�����̺��ӽ���ͨ���õĹܵ�
};



template< typename C, typename H, typename M >
class processpool
{
private:
    processpool( int listenfd, int process_number = 8 );
public:
    static processpool< C, H, M >* create( int listenfd, int process_number = 8 )
    {
        if( !m_instance ) //����ģʽ,ȷ��һ����ֻ��һ��ʵ��
        {
            m_instance = new processpool< C, H, M >( listenfd, process_number );
        }
        return m_instance;
    }
    ~processpool()
    {
        delete [] m_sub_process;
    }
	//�������̳�
    void run( const vector<H>& arg );


private:
	void notify_parent_busy_ratio(int pipefd, M* manager);  //��ȡĿǰ�������������䷢�͸�������
	int get_most_free_srv();  //�ҳ�����еķ�����
	void setup_sig_pipe(); //ͳһ�¼�Դ
	void run_parent();//����������
	void run_child(const vector<H>& arg);//�����ӽ���


private:
	static const int MAX_PROCESS_NUMBER = 16;   //���̳���������������
	static const int USER_PER_PROCESS = 65536;  //ÿ���ӽ�������ܴ���Ŀͻ�����
	static const int MAX_EVENT_NUMBER = 10000;  //EPOLL����ܴ���ĵ��¼���
	int m_process_number;  //���̳��еĽ�������
	int m_idx;  //�ӽ����ڳ��е���ţ���0��ʼ��
	int m_epollfd;  //��ǰ���̵�epoll�ں��¼���fd
	int m_listenfd;  //����socket
	int m_stop;      //�ӽ���ͨ��m_stop�������Ƿ�ֹͣ����
	process* m_sub_process;  //���������ӽ��̵�������Ϣ
	static processpool< C, H, M >* m_instance;  //���̳ؾ�̬ʵ��
};
template< typename C, typename H, typename M >
processpool< C, H, M >* processpool< C, H, M >::m_instance = NULL;

static int EPOLL_WAIT_TIME = 5000;
static int sig_pipefd[2];//���ڴ����źŵĹܵ�����ʵ��ͳһ�¼�Դ,�����֮Ϊ�źŹܵ�
//�źŴ�����
static void sig_handler( int sig )
{
    int save_errno = errno;
    int msg = sig;
	//�������ź��͸��źŴ���ܵ���ͳһ����ͳһ�¼�Դ
    send( sig_pipefd[1], ( char* )&msg, 1, 0 );
    errno = save_errno;
}
//����ź�,�����źŴ�����
static void addsig(int sig, void(handler)(int), bool restart = true)
{
	struct sigaction sa;//�����źź����Ľӿ�
	memset(&sa, '\0', sizeof(sa));
	sa.sa_handler = handler;//ָ���źŴ�����
	if (restart)
	{
		sa.sa_flags |= SA_RESTART;  //���µ��ñ����ź���ֹ��ϵͳ����,sa_flags���ó����յ��źŵ���Ϊ
	}
	sigfillset(&sa.sa_mask);  //���źż����������������źţ�sa_mask�������������ź�����
	assert(sigaction(sig, &sa, NULL) != -1);
}
//���̳ع��캯��
template< typename C, typename H, typename M >
processpool< C, H, M >::processpool( int listenfd, int process_number ) 
    : m_listenfd( listenfd ), m_process_number( process_number ), m_idx( -1 ), m_stop( false )
{
    assert( ( process_number > 0 ) && ( process_number <= MAX_PROCESS_NUMBER ) );
	//���䱣�������ӽ��̵�������Ϣ������
    m_sub_process = new process[ process_number ];
    assert( m_sub_process );

    for( int i = 0; i < process_number; ++i )
    {
		//socketpair����һ�������׽��֣�����ȫ˫��ͨ��
		//���ｫÿ���ӽ��̵�m_pipefd��������
        int ret = socketpair( PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd );
        assert( ret == 0 );
		//fork���������ӽ���
        m_sub_process[i].m_pid = fork();
        assert( m_sub_process[i].m_pid >= 0 );
        if( m_sub_process[i].m_pid > 0 )//������
        {
			//����Ǹ����̣��رչܵ�1����Ϊ1ֻ��д����
            close( m_sub_process[i].m_pipefd[1] );
            m_sub_process[i].m_busy_ratio = 0;
            continue;
        }
        else
        {
			//������ӽ��̣��رչܵ�0����Ϊ0ֻ�ܶ�����
            close( m_sub_process[i].m_pipefd[0] );
            m_idx = i;
            break;
        }
    }
}
//������ѯ�ķ�ʽ�õ���ǰ����еķ�����
template< typename C, typename H, typename M >
int processpool< C, H, M >::get_most_free_srv()
{
	//ÿ̨�������ӵ��ӽ��̣�������������һ��Ȩ��busy_ratio
	int ratio = m_sub_process[0].m_busy_ratio;
	int idx = 0;
	//������ѯ�ķ�ʽ�õ���СȨ�صķ�����
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
//ͳһ�¼�Դ�����ź��¼���I/O�¼�һͬ����
template< typename C, typename H, typename M >
void processpool< C, H, M >::setup_sig_pipe()  //ͳһ�¼�Դ
{

	m_epollfd = epoll_create(5);//����epoll������
	assert(m_epollfd != -1);
	//sig_pipefd�Ǵ����źŵĹܵ���������
	int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);   //ȫ˫���ܵ�
	assert(ret != -1);
	//����������Ϊ������ģʽ
	setnonblocking(sig_pipefd[1]);  //������д
	//���ں��¼�������Ӷ��¼�������Ϊ������
	add_read_fd(m_epollfd, sig_pipefd[0]); //�����ܵ����˲�����Ϊ������

	addsig(SIGCHLD, sig_handler);  //�ӽ���״̬�����仯���˳�����ͣ��
	addsig(SIGTERM, sig_handler);  //��ֹ����,kill����Ĭ�Ϸ��͵ļ�ΪSIGTERM
	addsig(SIGINT, sig_handler);   //���������жϽ��̣�Ctrl + C��
	addsig(SIGPIPE, SIG_IGN);      /*�����رյ��ļ���������д����ʱ������ʹ�����˳�
									   SIG_IGN���Ժ��ԣ���write��ʱ�򷵻�-1,
									   errno����ΪSIGPIPE*/
}


//��������m_idxֵΪ-1���ӽ�����m_idx>0���ݴ��ж����и����̴�������ӽ��̴���
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
//��ȡĿǰ�������������䷢�͸�������
template< typename C, typename H, typename M >
void processpool< C, H, M >::notify_parent_busy_ratio( int pipefd, M* manager )
{
    int msg = manager->get_used_conn_cnt();
    send( pipefd, ( char* )&msg, 1, 0 );    
}

//�ӽ������й���
//���ݰ�ͬ��/���첽ģ�ͣ����������ڶԼ�����socket����epoll_wait()����¼�
//�ӽ�������Ҫ��������socket������epoll_wait()����¼�

/*
�ڴ�����,CΪconn�ͻ��ˣ�HΪhost�����ˣ�MΪmgr����
mgr����manager������ͱ����߼���������������Ϣ
mgr��������map����ģ��������У�m_conns,m_used,m_freed
m_conns�����Ѿ������õľ������ӣ����½�һ��managerʵ���ͻὨ��һ����������
m_used��һ��������У���ʾ��Ҫʹ�õ����ӣ�pick_conn���Ѿ������õľ������ӷ������������
m_freed����ʹ�ú��ͷŵ�����,��free_conn��ɲ���
recycle_conns���Ѿ�free����������fix�������m_conns��
*/
template< typename C, typename H, typename M >
void processpool< C, H, M >::run_child( const vector<H>& arg )
{
	//��������ź��¼�,ͳһ�¼�Դ
    setup_sig_pipe();
	//ÿ���ӽ��̶�ͨ��m_idx�ҵ����븸���̵�ͨ�Źܵ�
    int pipefd_read = m_sub_process[m_idx].m_pipefd[ 1 ];
    add_read_fd( m_epollfd, pipefd_read );
	//�ӽ�����Ҫ�����ܵ�������m_pipefd����Ϊ����������ӽ�������������Ҫͨ���ܵ�֪ͨ�ӽ���accept����
	//�ں��¼���
    epoll_event events[ MAX_EVENT_NUMBER ];
	//manager�Ǳ���͹���������Ϣ���߼�������
    M* manager = new M( m_epollfd, arg[m_idx] );
    assert( manager );

    int number = 0;
    int ret = -1;

    while( ! m_stop )
    {
		//epoll_wait�õ��Ѿ������¼�
        number = epoll_wait( m_epollfd, events, MAX_EVENT_NUMBER, EPOLL_WAIT_TIME );
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            log( LOG_ERR, __FILE__, __LINE__, "%s", "epoll failure" );
            break;
        }
		//epoll_wait�õ��Ѿ������¼�
        if( number == 0 )
        {
            manager->recycle_conns();//�����Ѿ����ͷŵ�����
            continue;
        }

        for ( int i = 0; i < number; i++ )
        {
			//�õ��о����¼���������
            int sockfd = events[i].data.fd;
			//EPOLLIN:ֻ�е��Զ�������д��ʱ�Żᴥ��,����һ�κ���Ҫ���϶�ȡ��������ֱ������EAGAINΪֹ
			//EPOLLIN�Ƿ�ֹepoll��ETģʽ����Ϊ��һ��û�������ݶ���������һ�������¼����������
			if ((sockfd == pipefd_read) && (events[i].events & EPOLLIN))  //�Ǹ����̷��͵���Ϣ��֪ͨ���µĿͻ����ӵ�����
            {
                int client = 0;
				//�Ӹ���֮��Ĺܵ���recv��ȡ���ݣ������������client��
                ret = recv( sockfd, ( char* )&client, sizeof( client ), 0 );
                if( ( ( ret < 0 ) && ( errno != EAGAIN ) ) || ret == 0 ) 
                {
                    continue;
                }
                else
                {
                    struct sockaddr_in client_address;
                    socklen_t client_addrlength = sizeof( client_address );
					//�Ӽ��������н������ӣ�m_listenfd�ǽ��̼����Ŀͻ�����
                    int connfd = accept( m_listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                    if ( connfd < 0 )
                    {
                        log( LOG_ERR, __FILE__, __LINE__, "errno: %s", strerror( errno ) );
                        continue;
                    }
					//accept�������ص�connfd��һ���µ����ӣ���socketΨһ�ı�ʶ�˱����ܵ�������ӣ�������connfd����ͨ����
				   //ע���¼�
                    add_read_fd( m_epollfd, connfd );
					//���Ѿ�׼���õ�������pickһ�������ط���˵���Ϣ
                    C* conn = manager->pick_conn( connfd );
                    if( !conn )
                    {
                        closefd( m_epollfd, connfd );
                        continue;
                    }
					//��ʼ���ͻ�����Ϣ
                    conn->init_clt( connfd, client_address );
					//��ȡĿǰmanager���������������͸�������
                    notify_parent_busy_ratio( pipefd_read, manager );
                }
            }
			//����������̽��յ����ź�
            else if( ( sockfd == sig_pipefd[0] ) && ( events[i].events & EPOLLIN ) )
            {
                int sig;
                char signals[1024];
				//TCP��recv����sig_pipefd[0]�����ݣ�������signals��
                ret = recv( sig_pipefd[0], signals, sizeof( signals ), 0 );
                if( ret <= 0 )
                {
                    continue;
                }
                else
                {
                    for( int i = 0; i < ret; ++i )
                    {
						//�ж��ź�����
                        switch( signals[i] )
                        {
							//���ӽ����˳�ʱ�����򸸽��̷���SIGCHLD�ź�,��������û���յ�����źŵ��������Ҫ�ȣ������ӽ��̻��ɽ�ʬ����
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
								//��ʾ��������̽��еȴ����Է�������ʽ����ѭ��Ӧ��ͬһʱ�����ӽ����˳�
                                while ( ( pid = waitpid( -1, &stat, WNOHANG ) ) > 0 )//���ռ��˳����ӽ��̣�����������WNOHANG��˲��ȴ�
                                {
                                    continue;
                                }
                                break;
                            }
                            case SIGTERM:
								//�˳��ý���
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
			//����������������ݣ��Ǳ�Ȼ�ǿͻ����������������߼������process����������
			//ǰ��˵���ˣ������߼���Ԫ�����鶼����ģ����������ʵ��ģ�������Ҫ
			//���ﴦ��ͻ������Ӿ��õ���ģ����manager��process����
            else if( events[i].events & EPOLLIN )
            {
				//process����
                 RET_CODE result = manager->process( sockfd, READ );
                 switch( result )
                 {
                     case CLOSED:
                     {
						 //��ȡĿǰmanager���������������͸�������
                         notify_parent_busy_ratio( pipefd_read, manager );
                         break;
                     }
                     default:
                         break;
                 }
            }
            else if( events[i].events & EPOLLOUT )//д�¼�
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
//��������Ҫ��ɵĹ���
template< typename C, typename H, typename M >
void processpool< C, H, M >::run_parent()
{
	//ͬ���ģ�Ҳ��Ҫͳһ�¼�Դ
	setup_sig_pipe();
	//��epoll�¼���ע���������ӽ���֮��Ĺܵ��Ķ��¼�

    for( int i = 0; i < m_process_number; ++i )
    {
        add_read_fd( m_epollfd, m_sub_process[i].m_pipefd[ 0 ] );
    }
	//��epoll�¼���ע��m_listenfd���¼�
    add_read_fd( m_epollfd, m_listenfd );
	//epoll�¼�
    epoll_event events[ MAX_EVENT_NUMBER ];
	//�ӽ��̸���
    int sub_process_counter = 0;
    int new_conn = 1;
    int number = 0;
    int ret = -1;

    while( ! m_stop )
    {
		//�ں��¼����еľ����¼�
        number = epoll_wait( m_epollfd, events, MAX_EVENT_NUMBER, EPOLL_WAIT_TIME );
        if ( ( number < 0 ) && ( errno != EINTR ) )
        {
            log( LOG_ERR, __FILE__, __LINE__, "%s", "epoll failure" );
            break;
        }

        for ( int i = 0; i < number; i++ )
        {
			//�õ��о����¼���������
            int sockfd = events[i].data.fd;
			//����Ǹ����̼�����socket���¿ͻ��������Ͳ��ø��ؾ������Ѱ��һ��������С���ӽ��̴���
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
				//�Һ��ӽ���֮����Ҫ����������¼�֪ͨ���ӽ��̣�ʹ�ø��ӽ��̵Ĺܵ�����
                send( m_sub_process[idx].m_pipefd[0], ( char* )&new_conn, sizeof( new_conn ), 0 );
                log( LOG_INFO, __FILE__, __LINE__, "send request to child %d", idx );
            }
			//������ź�,��������Ҫ����֮
            else if( ( sockfd == sig_pipefd[0] ) && ( events[i].events & EPOLLIN ) )
            {
                int sig;
                char signals[1024];
				//�õ����ݵ��ź�
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
							//������ӽ���֪ͨ�˳�
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
                                while ( ( pid = waitpid( -1, &stat, WNOHANG ) ) > 0 )
                                {
									//������̳��е�i���ӽ����˳��ˣ��������̹ر���Ӧ��ͨ�Źܵ�����������Ӧ��m_pidΪ-1����Ǹ��ӽ����Ѿ��˳���
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
								//��������ӽ��̶��˳��������̲ſ����˳�����ֹ��ʬ��������ڴ�й©
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
							//ֹͣ���Ҳ����Ҫֹͣ�����ӽ���֮�����ֹͣ������
                            case SIGINT:
                            {
                                log( LOG_INFO, __FILE__, __LINE__, "%s", "kill all the clild now" );
                                for( int i = 0; i < m_process_number; ++i )
                                {
                                    int pid = m_sub_process[i].m_pid;
                                    if( pid != -1 )
                                    {
                                        kill( pid, SIGTERM );//kill������ʵ��һ�������źŵĺ���
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
			//�������������Ҳ�����źţ��п������ӽ����򸸽��̷��͵�����
			//���������У��ӽ��̻��򸸽��̷����Լ��Ѿ�ʹ�õ���������
			/*
				��������Ҫ����������ݸ���m_sub_process[index]�ļ�Ȩ������
				�Ե���get_most_free_srv�ҵ�������С�ķ�����
			*/
            else if( events[i].events & EPOLLIN )
            {
                int busy_ratio = 0;
				//�����sockfd�Ǹ��ӽ��̼��ͨ�Źܵ�
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
