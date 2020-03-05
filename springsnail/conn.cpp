#include <exception>
#include <errno.h>
#include <string.h>
#include "conn.h"
#include "log.h"
#include "fdwrapper.h"

conn::conn()
{
	m_srvfd = -1; //初始化，表示并未设置服务端
	m_clt_buf = new char[BUF_SIZE];				//客户端缓冲区
	if (!m_clt_buf)
	{
		throw std::exception();  //分配空间失败
	}
	m_srv_buf = new char[BUF_SIZE];
	if (!m_srv_buf)
	{
		throw std::exception();  //分配空间失败
	}
	reset();//重置缓冲区
}

conn::~conn()
{
    delete [] m_clt_buf;
    delete [] m_srv_buf;
}
//初始化客户端socket地址与文件描述符
void conn::init_clt( int sockfd, const sockaddr_in& client_addr )
{
    m_cltfd = sockfd;
    m_clt_address = client_addr;
}
//初始化服务端socket地址与文件描述符
void conn::init_srv( int sockfd, const sockaddr_in& server_addr )
{
    m_srvfd = sockfd;
    m_srv_address = server_addr;
}
//重置缓冲区
void conn::reset()
{
    m_clt_read_idx = 0;
    m_clt_write_idx = 0;
    m_srv_read_idx = 0;
    m_srv_write_idx = 0;
    m_srv_closed = false;
    m_cltfd = -1;
    memset( m_clt_buf, '\0', BUF_SIZE );
    memset( m_srv_buf, '\0', BUF_SIZE );
}
//将客户端读入的信息写入m_clt_buf
RET_CODE conn::read_clt()
{
    int bytes_read = 0;
    while( true )
    {
		//如果缓冲区满，打印信息log并返回
        if( m_clt_read_idx >= BUF_SIZE )
        {
            log( LOG_ERR, __FILE__, __LINE__, "%s", "the client read buffer is full, let server write" );
            return BUFFER_FULL;
        }
		//recv函数用于TCP数据读写,读取m_cltfd上的数据到m_clt_buf缓冲区中
        bytes_read = recv( m_cltfd, m_clt_buf + m_clt_read_idx, BUF_SIZE - m_clt_read_idx, 0 );
		//recv成功时返回的是读取到的数据长度，不成功返回-1或0
        if ( bytes_read == -1 )
        {
            if( errno == EAGAIN || errno == EWOULDBLOCK )// 非阻塞情况下： EAGAIN表示没有数据可读，请尝试再次调用,而在阻塞情况下，如果被中断，则返回EINTR;  EWOULDBLOCK等同于EAGAIN
            {
                break;
            }
            return IOERR;
        }
        else if ( bytes_read == 0 ) //连接被关闭
        {
            return CLOSED;
        }

        m_clt_read_idx += bytes_read;
    }
	//返回是否读入数据成功，防止写数据出错
    return ( ( m_clt_read_idx - m_clt_write_idx ) > 0 ) ? OK : NOTHING;
}
//服务器读函数，与客户端如出一辙，从服务端读入的信息写入m_srv_buf
RET_CODE conn::read_srv()
{
    int bytes_read = 0;
    while( true )
    {
        if( m_srv_read_idx >= BUF_SIZE )
        {
            log( LOG_ERR, __FILE__, __LINE__, "%s", "the server read buffer is full, let client write" );
            return BUFFER_FULL;
        }

        bytes_read = recv( m_srvfd, m_srv_buf + m_srv_read_idx, BUF_SIZE - m_srv_read_idx, 0 );
        if ( bytes_read == -1 )
        {
            if( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                break;
            }
            return IOERR;
        }
        else if ( bytes_read == 0 )
        {
            log( LOG_ERR, __FILE__, __LINE__, "%s", "the server should not close the persist connection" );
            return CLOSED;
        }

        m_srv_read_idx += bytes_read;
    }
    return ( ( m_srv_read_idx - m_srv_write_idx ) > 0 ) ? OK : NOTHING;
}
//把从客户端读入m_clt_buf的内容写入服务端
RET_CODE conn::write_srv()
{
    int bytes_write = 0;
    while( true )
    {
		//m_clt_buf为空，写失败
        if( m_clt_read_idx <= m_clt_write_idx )
        {
            m_clt_read_idx = 0;
            m_clt_write_idx = 0;
            return BUFFER_EMPTY;
        }
		//send函数用于TCP发送：将m_clt_buf的内容发送给文件描述符m_srvfd
        bytes_write = send( m_srvfd, m_clt_buf + m_clt_write_idx, m_clt_read_idx - m_clt_write_idx, 0 );
        if ( bytes_write == -1 )
        {
			//返回值<0但有如下情况认为连接是正常的，继续接受，但非阻塞模式会立即返回，所以需要循环读取
            if( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                return TRY_AGAIN;
            }
            log( LOG_ERR, __FILE__, __LINE__, "write server socket failed, %s", strerror( errno ) );
            return IOERR;
        }
        else if ( bytes_write == 0 )
        {
            return CLOSED;
        }

        m_clt_write_idx += bytes_write;
    }
}
//把服务器读入m_srv_buf的信息写入客户端的
RET_CODE conn::write_clt()
{
    int bytes_write = 0;
    while( true )
    {
        if( m_srv_read_idx <= m_srv_write_idx )
        {
            m_srv_read_idx = 0;
            m_srv_write_idx = 0;
            return BUFFER_EMPTY;
        }

        bytes_write = send( m_cltfd, m_srv_buf + m_srv_write_idx, m_srv_read_idx - m_srv_write_idx, 0 );
        if ( bytes_write == -1 )
        {
            if( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                return TRY_AGAIN;
            }
            log( LOG_ERR, __FILE__, __LINE__, "write client socket failed, %s", strerror( errno ) );
            return IOERR;
        }
        else if ( bytes_write == 0 )
        {
            return CLOSED;
        }

        m_srv_write_idx += bytes_write;
    }
}
