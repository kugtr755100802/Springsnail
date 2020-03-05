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

#include <exception>
#include "log.h"
#include "mgr.h"

using std::pair;

int mgr::m_epollfd = -1;//-1��ʾδ��ʼ��
//�ͷ���˽�������ͬʱ����socket������
int mgr::conn2srv( const sockaddr_in& address )
{
    int sockfd = socket( PF_INET, SOCK_STREAM, 0 );
    if( sockfd < 0 )
    {
        return -1;
    }

    if ( connect( sockfd, ( struct sockaddr* )&address, sizeof( address ) ) != 0  )//ͬ�߼�������������
    {
        close( sockfd );
        return -1;
    }
    return sockfd;
}
/*
manager���캯�����ڴ���managerʱ����ʼ���ں˱��߼���������Ȼ����
				 �߼��������������ӣ����ҽ������õ����Ӳ���m_conn�У�
				 ��ʾ�Ѿ���һ��׼�����������ӵȴ�����
*/
mgr::mgr( int epollfd, const host& srv ) : m_logic_srv( srv )
{
	//��ʼ���ں��¼���
    m_epollfd = epollfd;
	//��ʼ��Ϊ0
    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, srv.m_hostname, &address.sin_addr );
    address.sin_port = htons( srv.m_port );
    log( LOG_INFO, __FILE__, __LINE__, "logcial srv host info: (%s, %d)", srv.m_hostname, srv.m_port );

    for( int i = 0; i < srv.m_conncnt; ++i )
    {
        sleep( 1 );
		//�����socket������
        int sockfd = conn2srv( address );
        if( sockfd < 0 )
        {
            log( LOG_ERR, __FILE__, __LINE__, "build connection %d failed", i );
        }
        else
        {
            log( LOG_INFO, __FILE__, __LINE__, "build connection %d to server success", i );
            conn* tmp = NULL;
            try
            {
                tmp = new conn;
            }
            catch( ... )
            {
                close( sockfd );
                continue;
            }
			//��ʼ��
            tmp->init_srv( sockfd, address );
			//׼���õ�����
            m_conns.insert( pair< int, conn* >( sockfd, tmp ) );
        }
    }
}

mgr::~mgr()
{
}
//�Ѿ�ʹ�õ����Ӹ���
int mgr::get_used_conn_cnt()
{
    return m_used.size();
}
//�����Ӻõ������У�m_conn�У��ó�һ������������У�m_used����
/*
pick_conn:���Ѿ������õ�������(m_conn)ȡ����ǰ�ĵ�һ�������������(m_used)
��m_conn��m_used
*/
conn* mgr::pick_conn( int cltfd  )
{
	//���û�����Ӻõ�����
    if( m_conns.empty() )
    {
        log( LOG_ERR, __FILE__, __LINE__, "%s", "not enough srv connections to server" );
        return NULL;
    }
	//��������ѡ��m_conn�ĵ�һ��Ԫ�ط��������
    map< int, conn* >::iterator iter =  m_conns.begin();
    int srvfd = iter->first;
    conn* tmp = iter->second;
    if( !tmp )
    {
        log( LOG_ERR, __FILE__, __LINE__, "%s", "empty server connection object" );
        return NULL;
    }
    m_conns.erase( iter );
	m_used.insert(pair< int, conn* >(cltfd, tmp));//����һ���ͻ��˵���Ϣ���ͻ�����Ϣ������õ�
	m_used.insert(pair< int, conn* >(srvfd, tmp));//����һ������˵���Ϣ���������Ϣ�����Ǵ�m_conn�еõ���
	//���ں��¼�����epoll�������¼�
    add_read_fd( m_epollfd, cltfd );
    add_read_fd( m_epollfd, srvfd );
    log( LOG_INFO, __FILE__, __LINE__, "bind client sock %d with server sock %d", cltfd, srvfd );
    return tmp;//���ط����������Ϣ
}
// free_conn���ͷ����ӣ������ӹرջ����жϺ󣬽���fd���ں��¼���ɾ�������ر�fd��������ͬsrv�������ӵķ���m_freed��
//��m_used��m_freed
void mgr::free_conn( conn* connection )
{
    int cltfd = connection->m_cltfd;
    int srvfd = connection->m_srvfd;
    closefd( m_epollfd, cltfd );
    closefd( m_epollfd, srvfd );
    m_used.erase( cltfd );
    m_used.erase( srvfd );
    connection->reset();//����connection�Ķ�д����
    m_freed.insert( pair< int, conn* >( srvfd, connection ) );
}
//��m_freed�л������ӣ����������Ѿ����رգ���˻�Ҫ����conn2srv()���ŵ�m_conn��
//���������޸������·���m_conn��,��m_freed��m_conn
void mgr::recycle_conns()
{
    if( m_freed.empty() )
    {
        return;
    }
    for( map< int, conn* >::iterator iter = m_freed.begin(); iter != m_freed.end(); iter++ )
    {
        sleep( 1 );
        int srvfd = iter->first;
        conn* tmp = iter->second;
		//�����Ѿ����رգ���Ҫ���½�������
        srvfd = conn2srv( tmp->m_srv_address );
        if( srvfd < 0 )
        {
            log( LOG_ERR, __FILE__, __LINE__, "%s", "fix connection failed");
        }
        else
        {
            log( LOG_INFO, __FILE__, __LINE__, "%s", "fix connection success" );
			//���³�ʼ��������Ϣtmp
            tmp->init_srv( srvfd, tmp->m_srv_address );
			//��������Ϣ����m_conns
            m_conns.insert( pair< int, conn* >( srvfd, tmp ) );
        }
    }
    m_freed.clear();
}
//ͨ��fd��type�����ƶԷ���˺Ϳͻ��˵Ķ�д�����������ؾ���ĺ��Ĺ���
/*
fd�����ж��Ƿ���˻��ǿͻ�������
type����ѡ��������Ĳ���

*/
RET_CODE mgr::process( int fd, OP_TYPE type )
{
    conn* connection = m_used[ fd ];
    if( !connection )
    {
        return NOTHING;
    }
	//˵���ǿͻ������ӵ���Ϣ
    if( connection->m_cltfd == fd )
    {
		//�õ���ͻ������ӵķ����
        int srvfd = connection->m_srvfd;
        switch( type )
        {
			//�ͻ��˶�����Ϣ������д��ͻ��˻�����m_clt_buf
            case READ:
            {
                RET_CODE res = connection->read_clt();//д�뻺��
				//�жϷ���״̬�Ƿ�ɹ�
                switch( res )
                {
                    case OK:
                    {
                        log( LOG_DEBUG, __FILE__, __LINE__, "content read from client: %s", connection->m_clt_buf );
                    }
                    case BUFFER_FULL:
                    {
						//���忴modfd�����ĺ�����ע�ͣ�Ϊʲô��Ҫ�ڻ��������������ʹ��modfd����
					   /*
						   ��Ϊ��ETģʽ�£����һ��û�ж������ݣ���һ�ν��ò����ɶ��ľ���֪ͨ
						   modfd��������ͨ��epoll_ctl_mod��EPOLLOUT����������
					   */
                        modfd( m_epollfd, srvfd, EPOLLOUT );
                        break;
                    }
                    case IOERR:
					//�ر����ӣ������Ӵ�m_usedת�Ƶ�m_freed��
                    case CLOSED:
                    {
                        free_conn( connection );
                        return CLOSED;
                    }
                    default:
                        break;
                }
				//������ӵķ������ֹ��Ҳ��Ҫ�ر�����
                if( connection->m_srv_closed )
                {
                    free_conn( connection );
                    return CLOSED;
                }
                break;
            }
			//д���ͣ��ѷ���������m_srv_buf����Ϣд��ͻ��˵�
            case WRITE:
            {
                RET_CODE res = connection->write_clt();
                switch( res )
                {
                    case TRY_AGAIN:
                    {
						//����modfd����������д�����¼�
                        modfd( m_epollfd, fd, EPOLLOUT );
                        break;
                    }
                    case BUFFER_EMPTY:
                    {
						//����˲�û�����ݣ���Ҫ����modfd��������������¼�������δ���������ݣ��Ӷ���BUFFER�ǿ�
                        modfd( m_epollfd, srvfd, EPOLLIN );
                        modfd( m_epollfd, fd, EPOLLIN );
                        break;
                    }
                    case IOERR:
                    case CLOSED:
                    {
                        free_conn( connection );
                        return CLOSED;
                    }
                    default:
                        break;
                }
				//����������ֹ��һ����Ҫclose
                if( connection->m_srv_closed )
                {
                    free_conn( connection );
                    return CLOSED;
                }
                break;
            }
            default:
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "other operation not support yet" );
                break;
            }
        }
    }
	//����Ƿ���˵�������Ϣ
    else if( connection->m_srvfd == fd )
    {
		//�õ���֮���ӵĿͻ���socket
        int cltfd = connection->m_cltfd;
        switch( type )
        {
			//�õ���֮���ӵĿͻ���socket
            case READ:
            {
                RET_CODE res = connection->read_srv();//������Ϣдm_srv_buf
                switch( res )
                {
                    case OK:
                    {
                        log( LOG_DEBUG, __FILE__, __LINE__, "content read from server: %s", connection->m_srv_buf );//�˴���break���ܼӣ��ڶ�����Ϣ֮��
                                                                                                                      //Ӧ�ü���ȥ����BUFFER_FULL�Ӷ�֪ͨ��д
                    }
					//���������������һ������Ҫ����modfdһֱ����д�����¼���������д��
                    case BUFFER_FULL:
                    {
                        modfd( m_epollfd, cltfd, EPOLLOUT );
                        break;
                    }
                    case IOERR:
                    case CLOSED:
                    {
						//������˹رյ�ʱ��Ӧ������modfd����д�����¼�������������д��
                        modfd( m_epollfd, cltfd, EPOLLOUT );
                        connection->m_srv_closed = true;
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
			//�Ѵӿͻ��˶���m_clt_buf������д������
            case WRITE:
            {
                RET_CODE res = connection->write_srv();
                switch( res )
                {
                    case TRY_AGAIN:
                    {
						//����modfd����д�����¼���������������д��
                        modfd( m_epollfd, fd, EPOLLOUT );
                        break;
                    }
                    case BUFFER_EMPTY:
                    {
						//m_clt_bufΪ�գ�Ӧ������modfd���ֶ������¼��������Ƿ�������û�ж���
                        modfd( m_epollfd, cltfd, EPOLLIN );
                        modfd( m_epollfd, fd, EPOLLIN );
                        break;
                    }
                    case IOERR:
                    case CLOSED:
                    {
                        /*
                        if( connection->m_srv_write_idx == connection->m_srvread_idx )
                        {
                            free_conn( connection );
                        }
                        else
                        {
                            modfd( m_epollfd, cltfd, EPOLLOUT );
                        }
                        */
						//����modfd����д�����¼���������д��
                        modfd( m_epollfd, cltfd, EPOLLOUT );
                        connection->m_srv_closed = true;
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
            default:
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "other operation not support yet" );
                break;
            }
        }
    }
    else
    {
        return NOTHING;
    }
    return OK;
}
