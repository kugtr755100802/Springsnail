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

int mgr::m_epollfd = -1;//-1表示未初始化
//和服务端建立连接同时返回socket描述符
int mgr::conn2srv( const sockaddr_in& address )
{
    int sockfd = socket( PF_INET, SOCK_STREAM, 0 );
    if( sockfd < 0 )
    {
        return -1;
    }

    if ( connect( sockfd, ( struct sockaddr* )&address, sizeof( address ) ) != 0  )//同逻辑服务器相连接
    {
        close( sockfd );
        return -1;
    }
    return sockfd;
}
/*
manager构造函数：在创建manager时，初始化内核表，逻辑服务器，然后与
				 逻辑服务器创建连接，并且将创建好的连接插入m_conn中，
				 表示已经有一个准备就绪的连接等待处理
*/
mgr::mgr( int epollfd, const host& srv ) : m_logic_srv( srv )
{
	//初始化内核事件表
    m_epollfd = epollfd;
	//初始化为0
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
		//服务端socket描述符
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
			//初始化
            tmp->init_srv( sockfd, address );
			//准备好的连接
            m_conns.insert( pair< int, conn* >( sockfd, tmp ) );
        }
    }
}

mgr::~mgr()
{
}
//已经使用的连接个数
int mgr::get_used_conn_cnt()
{
    return m_used.size();
}
//从连接好的连接中（m_conn中）拿出一个放入任务队列（m_used）中
/*
pick_conn:从已经建立好的连接中(m_conn)取出当前的第一个放入任务队列(m_used)
从m_conn到m_used
*/
conn* mgr::pick_conn( int cltfd  )
{
	//如果没有连接好的任务
    if( m_conns.empty() )
    {
        log( LOG_ERR, __FILE__, __LINE__, "%s", "not enough srv connections to server" );
        return NULL;
    }
	//迭代器，选择m_conn的第一个元素放入队列中
    map< int, conn* >::iterator iter =  m_conns.begin();
    int srvfd = iter->first;
    conn* tmp = iter->second;
    if( !tmp )
    {
        log( LOG_ERR, __FILE__, __LINE__, "%s", "empty server connection object" );
        return NULL;
    }
    m_conns.erase( iter );
	m_used.insert(pair< int, conn* >(cltfd, tmp));//插入一个客户端的信息，客户端信息由输入得到
	m_used.insert(pair< int, conn* >(srvfd, tmp));//插入一个服务端的信息，服务端信息是我们从m_conn中得到的
	//向内核事件表中epoll两个读事件
    add_read_fd( m_epollfd, cltfd );
    add_read_fd( m_epollfd, srvfd );
    log( LOG_INFO, __FILE__, __LINE__, "bind client sock %d with server sock %d", cltfd, srvfd );
    return tmp;//返回服务端连接信息
}
// free_conn：释放连接（当连接关闭或者中断后，将其fd从内核事件表删除，并关闭fd），并将同srv进行连接的放入m_freed中
//从m_used到m_freed
void mgr::free_conn( conn* connection )
{
    int cltfd = connection->m_cltfd;
    int srvfd = connection->m_srvfd;
    closefd( m_epollfd, cltfd );
    closefd( m_epollfd, srvfd );
    m_used.erase( cltfd );
    m_used.erase( srvfd );
    connection->reset();//重置connection的读写缓冲
    m_freed.insert( pair< int, conn* >( srvfd, connection ) );
}
//从m_freed中回收连接（由于连接已经被关闭，因此还要调用conn2srv()）放到m_conn中
//即将连接修复，重新放入m_conn中,从m_freed到m_conn
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
		//连接已经被关闭，需要重新建立连接
        srvfd = conn2srv( tmp->m_srv_address );
        if( srvfd < 0 )
        {
            log( LOG_ERR, __FILE__, __LINE__, "%s", "fix connection failed");
        }
        else
        {
            log( LOG_INFO, __FILE__, __LINE__, "%s", "fix connection success" );
			//重新初始化连接信息tmp
            tmp->init_srv( srvfd, tmp->m_srv_address );
			//将连接信息插入m_conns
            m_conns.insert( pair< int, conn* >( srvfd, tmp ) );
        }
    }
    m_freed.clear();
}
//通过fd和type来控制对服务端和客户端的读写，是整个负载均衡的核心功能
/*
fd用于判断是服务端还是客户端连接
type用于选择接下来的操作

*/
RET_CODE mgr::process( int fd, OP_TYPE type )
{
    conn* connection = m_used[ fd ];
    if( !connection )
    {
        return NOTHING;
    }
	//说明是客户端连接的信息
    if( connection->m_cltfd == fd )
    {
		//得到与客户端连接的服务端
        int srvfd = connection->m_srvfd;
        switch( type )
        {
			//客户端读入信息，并且写入客户端缓存区m_clt_buf
            case READ:
            {
                RET_CODE res = connection->read_clt();//写入缓存
				//判断返回状态是否成功
                switch( res )
                {
                    case OK:
                    {
                        log( LOG_DEBUG, __FILE__, __LINE__, "content read from client: %s", connection->m_clt_buf );
                    }
                    case BUFFER_FULL:
                    {
						//具体看modfd函数的函数体注释，为什么需要在缓存区满的情况下使用modfd操作
					   /*
						   因为在ET模式下，如果一次没有读尽数据，下一次将得不到可读的就绪通知
						   modfd函数就是通过epoll_ctl_mod和EPOLLOUT解决此问题的
					   */
                        modfd( m_epollfd, srvfd, EPOLLOUT );
                        break;
                    }
                    case IOERR:
					//关闭连接，将连接从m_used转移到m_freed。
                    case CLOSED:
                    {
                        free_conn( connection );
                        return CLOSED;
                    }
                    default:
                        break;
                }
				//如果连接的服务端终止，也需要关闭连接
                if( connection->m_srv_closed )
                {
                    free_conn( connection );
                    return CLOSED;
                }
                break;
            }
			//写类型，把服务器读入m_srv_buf的信息写入客户端的
            case WRITE:
            {
                RET_CODE res = connection->write_clt();
                switch( res )
                {
                    case TRY_AGAIN:
                    {
						//调用modfd以重新载入写就绪事件
                        modfd( m_epollfd, fd, EPOLLOUT );
                        break;
                    }
                    case BUFFER_EMPTY:
                    {
						//服务端并没有数据，需要调用modfd重新载入读就绪事件，读入未读尽的数据，从而让BUFFER非空
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
				//如果服务端终止，一样需要close
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
	//如果是服务端的连接信息
    else if( connection->m_srvfd == fd )
    {
		//得到与之连接的客户端socket
        int cltfd = connection->m_cltfd;
        switch( type )
        {
			//得到与之连接的客户端socket
            case READ:
            {
                RET_CODE res = connection->read_srv();//读入信息写m_srv_buf
                switch( res )
                {
                    case OK:
                    {
                        log( LOG_DEBUG, __FILE__, __LINE__, "content read from server: %s", connection->m_srv_buf );//此处的break不能加，在读完消息之后
                                                                                                                      //应该继续去触发BUFFER_FULL从而通知可写
                    }
					//如果已满，和上面一样，需要调用modfd一直保持写就绪事件，让数据写完
                    case BUFFER_FULL:
                    {
                        modfd( m_epollfd, cltfd, EPOLLOUT );
                        break;
                    }
                    case IOERR:
                    case CLOSED:
                    {
						//当服务端关闭的时候，应当调用modfd保持写就绪事件尽可能让数据写完
                        modfd( m_epollfd, cltfd, EPOLLOUT );
                        connection->m_srv_closed = true;
                        break;
                    }
                    default:
                        break;
                }
                break;
            }
			//把从客户端读入m_clt_buf的内容写入服务端
            case WRITE:
            {
                RET_CODE res = connection->write_srv();
                switch( res )
                {
                    case TRY_AGAIN:
                    {
						//调用modfd保持写就绪事件，尽可能让数据写完
                        modfd( m_epollfd, fd, EPOLLOUT );
                        break;
                    }
                    case BUFFER_EMPTY:
                    {
						//m_clt_buf为空，应当调用modfd保持读就绪事件，看看是否有数据没有读完
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
						//调用modfd保持写就绪事件，让数据写完
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
