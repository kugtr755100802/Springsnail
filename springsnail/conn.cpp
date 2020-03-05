#include <exception>
#include <errno.h>
#include <string.h>
#include "conn.h"
#include "log.h"
#include "fdwrapper.h"

conn::conn()
{
	m_srvfd = -1; //��ʼ������ʾ��δ���÷����
	m_clt_buf = new char[BUF_SIZE];				//�ͻ��˻�����
	if (!m_clt_buf)
	{
		throw std::exception();  //����ռ�ʧ��
	}
	m_srv_buf = new char[BUF_SIZE];
	if (!m_srv_buf)
	{
		throw std::exception();  //����ռ�ʧ��
	}
	reset();//���û�����
}

conn::~conn()
{
    delete [] m_clt_buf;
    delete [] m_srv_buf;
}
//��ʼ���ͻ���socket��ַ���ļ�������
void conn::init_clt( int sockfd, const sockaddr_in& client_addr )
{
    m_cltfd = sockfd;
    m_clt_address = client_addr;
}
//��ʼ�������socket��ַ���ļ�������
void conn::init_srv( int sockfd, const sockaddr_in& server_addr )
{
    m_srvfd = sockfd;
    m_srv_address = server_addr;
}
//���û�����
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
//���ͻ��˶������Ϣд��m_clt_buf
RET_CODE conn::read_clt()
{
    int bytes_read = 0;
    while( true )
    {
		//���������������ӡ��Ϣlog������
        if( m_clt_read_idx >= BUF_SIZE )
        {
            log( LOG_ERR, __FILE__, __LINE__, "%s", "the client read buffer is full, let server write" );
            return BUFFER_FULL;
        }
		//recv��������TCP���ݶ�д,��ȡm_cltfd�ϵ����ݵ�m_clt_buf��������
        bytes_read = recv( m_cltfd, m_clt_buf + m_clt_read_idx, BUF_SIZE - m_clt_read_idx, 0 );
		//recv�ɹ�ʱ���ص��Ƕ�ȡ�������ݳ��ȣ����ɹ�����-1��0
        if ( bytes_read == -1 )
        {
            if( errno == EAGAIN || errno == EWOULDBLOCK )// ����������£� EAGAIN��ʾû�����ݿɶ����볢���ٴε���,������������£�������жϣ��򷵻�EINTR;  EWOULDBLOCK��ͬ��EAGAIN
            {
                break;
            }
            return IOERR;
        }
        else if ( bytes_read == 0 ) //���ӱ��ر�
        {
            return CLOSED;
        }

        m_clt_read_idx += bytes_read;
    }
	//�����Ƿ�������ݳɹ�����ֹд���ݳ���
    return ( ( m_clt_read_idx - m_clt_write_idx ) > 0 ) ? OK : NOTHING;
}
//����������������ͻ������һ�ޣ��ӷ���˶������Ϣд��m_srv_buf
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
//�Ѵӿͻ��˶���m_clt_buf������д������
RET_CODE conn::write_srv()
{
    int bytes_write = 0;
    while( true )
    {
		//m_clt_bufΪ�գ�дʧ��
        if( m_clt_read_idx <= m_clt_write_idx )
        {
            m_clt_read_idx = 0;
            m_clt_write_idx = 0;
            return BUFFER_EMPTY;
        }
		//send��������TCP���ͣ���m_clt_buf�����ݷ��͸��ļ�������m_srvfd
        bytes_write = send( m_srvfd, m_clt_buf + m_clt_write_idx, m_clt_read_idx - m_clt_write_idx, 0 );
        if ( bytes_write == -1 )
        {
			//����ֵ<0�������������Ϊ�����������ģ��������ܣ���������ģʽ���������أ�������Ҫѭ����ȡ
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
//�ѷ���������m_srv_buf����Ϣд��ͻ��˵�
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
