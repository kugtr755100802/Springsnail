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
#include "conn.h"
#include "mgr.h"
#include "processpool.h"

using std::vector;

static const char* version = "1.1";

static void usage( const char* prog )
{
    log( LOG_INFO, __FILE__, __LINE__,  "usage: %s [-h] [-v] [-f config_file]", prog );
}
/*
//ANSI C��׼�м�����׼Ԥ�����
// __FILE__ :��Դ�ļ��в��뵱ǰԭ�ļ���
// __LINE__ ����Դ�ļ��в��뵱ǰԴ�����к�
// _DATE_: ��Դ�ļ����뵱ǰ�ı�������
// _TIME_: ��Դ�ļ��в���ı���ʱ��
// _STDC_ : ��Ҫ������ϸ���ѭANSI C ��׼ʱ�̱�ʾ����ֵΪ1
// _cplusplus: ����дC++����ʱ�ñ�ʶ��������

*/

int main( int argc, char* argv[] )
{
    char cfg_file[1024];     //�����ļ�
    memset( cfg_file, '\0', 100 );
    int option;
    while ( ( option = getopt( argc, argv, "f:xvh" ) ) != -1 )    //getopt�����������������в���
    {
        switch ( option )
        {
            case 'x':
            {
                set_loglevel( LOG_DEBUG );     //log.cpp
                break;
            }
            case 'v':  //�汾��Ϣ
            {
                log( LOG_INFO, __FILE__, __LINE__, "%s %s", argv[0], version );
                return 0;
            }
            case 'h':   //����help
            {
                usage( basename( argv[ 0 ] ) );
                return 0;
            }
            case 'f':  //ָ�������ļ�
            {
                memcpy( cfg_file, optarg, strlen( optarg ) );     // cfg_file  ��ʱ�������� config.xml  ��������ʱ��./main -fconfig.xml
                break;
            }
            case '?':    //��Ч�Ĳ�������ȱ�ٲ�����ѡ��ֵ
            {
                log( LOG_ERR, __FILE__, __LINE__, "un-recognized option %c", option );
                usage( basename( argv[ 0 ] ) );
                return 1;
            }
        }
    }    
	//����һ�������������.xml�ļ���
    if( cfg_file[0] == '\0' )
    {
        log( LOG_ERR, __FILE__, __LINE__, "%s", "please specifiy the config file" );
        return 1;
    }
    int cfg_fd = open( cfg_file, O_RDONLY );//�������ļ� config.xml�� cfg_fd��config.xml�ļ����ļ�������
    if( !cfg_fd )
    {
        log( LOG_ERR, __FILE__, __LINE__, "read config file met error: %s", strerror( errno ) );
        return 1;
    }
    struct stat ret_stat;
    if( fstat( cfg_fd, &ret_stat ) < 0 )
    {
        log( LOG_ERR, __FILE__, __LINE__, "read config file met error: %s", strerror( errno ) );
        return 1;
    }
    char* buf = new char [ret_stat.st_size + 1];
    memset( buf, '\0', ret_stat.st_size + 1 );
    ssize_t read_sz = read( cfg_fd, buf, ret_stat.st_size );   //buf��ʱ��������config.xml�ļ�������
    if ( read_sz < 0 )
    {
        log( LOG_ERR, __FILE__, __LINE__, "read config file met error: %s", strerror( errno ) );
        return 1;
    }
	vector< host > balance_srv;	     //host��ǰ���mgr.h�ļ��ж���   //һ���Ǹ��ؾ��������,��һ�����߼�������
	vector< host > logical_srv;     //��xml�ļ��е�host������vector��
    host tmp_host;
    memset( tmp_host.m_hostname, '\0', 1024 );
    char* tmp_hostname;
    char* tmp_port;
    char* tmp_conncnt;
    bool opentag = false;
    char* tmp = buf;               //��ʱtemָ��config.xml�ļ�������
    char* tmp2 = NULL;
    char* tmp3 = NULL;
    char* tmp4 = NULL;
	/*
	
	    
        ��δ���ʹ��������״̬����˼�룬����������< logical_host >��ʱ�����ǽ�optag����Ϊtrue,����ʼ����;
        Ȼ��ͨ��\n����buf�����������Ϣ���д����Ӷ�������Ҫ����ķ�������ip��ַ�˿ں��Լ���Ҫ��������������
        ����< /logical_host >ʱ������һ������������Ϣ�Ѿ����꣬Ȼ��optag����Ϊfalse���������ֱ������buf�Ľ�β��
        ��Listen�Ķ�ȡ��ҿ������в鿴�����д���ģ��������ｫ�������һ�£���
    
	
	*/
    while( tmp2 = strpbrk( tmp, "\n" ) )  //��Դ�ַ���tmp���ҳ����Ⱥ��������ַ���"\n"����һ�ַ���λ�ò����أ���û�ҵ��򷵻ؿ�ָ��
    {
        *tmp2++ = '\0';
        if( strstr( tmp, "<logical_host>" ) )   //strstr(str1,str2)���������ж��ַ���str2�Ƿ���str1���Ӵ�������ǣ���ú�������str2��str1�״γ��ֵĵ�ַ�����򷵻�null
        {
            if( opentag )
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed" );
                return 1;
            }
            opentag = true;
        }
        else if( strstr( tmp, "</logical_host>" ) )     // ��/	����
        {
            if( !opentag )
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed" );
                return 1;
            }
            logical_srv.push_back( tmp_host );
            memset( tmp_host.m_hostname, '\0', 1024 );
            opentag = false;
        }
        else if( tmp3 = strstr( tmp, "<name>" ) )   //��tmp_hostnameָ��ָ��<name>�����IP��ַ���׸���ַ
        {
            tmp_hostname = tmp3 + 6;
            tmp4 = strstr( tmp_hostname, "</name>" );
            if( !tmp4 )
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed" );
                return 1;
            }
            *tmp4 = '\0';
            memcpy( tmp_host.m_hostname, tmp_hostname, strlen( tmp_hostname ) );
        }
        else if( tmp3 = strstr( tmp, "<port>" ) )
        {
            tmp_port = tmp3 + 6;
            tmp4 = strstr( tmp_port, "</port>" );
            if( !tmp4 )
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed" );
                return 1;
            }
            *tmp4 = '\0';
            tmp_host.m_port = atoi( tmp_port );
        }
        else if( tmp3 = strstr( tmp, "<conns>" ) )
        {
            tmp_conncnt = tmp3 + 7;
            tmp4 = strstr( tmp_conncnt, "</conns>" );
            if( !tmp4 )
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed" );
                return 1;
            }
            *tmp4 = '\0';
            tmp_host.m_conncnt = atoi( tmp_conncnt );
        }
		//���Listen�����Ǵ��
        else if( tmp3 = strstr( tmp, "Listen" ) )
        {
            tmp_hostname = tmp3 + 7;
            tmp4 = strstr( tmp_hostname, ":" );
            if( !tmp4 )
            {
                log( LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed" );
                return 1;
            }
            *tmp4++ = '\0';
            tmp_host.m_port = atoi( tmp4 );
			tmp = tmp + 7;//�¼ӵ�
            memcpy( tmp_host.m_hostname, tmp3, strlen( tmp3 ) );
			//��ӡһ��Listen������
			log(LOG_INFO, __FILE__, __LINE__, "Listen host is %s ", tmp3);
            balance_srv.push_back( tmp_host );
            memset( tmp_host.m_hostname, '\0', 1024 );
        }
        tmp = tmp2;
    }

    if( balance_srv.size() == 0 || logical_srv.size() == 0 )
    {
        log( LOG_ERR, __FILE__, __LINE__, "%s", "parse config file failed" );
        return 1;
    }
	//�õ�ip�Ͷ˿�
    const char* ip = balance_srv[0].m_hostname;    //balance_srv������ֻ��һ��Ԫ��
    int port = balance_srv[0].m_port;

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );
 
    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret != -1 );

    ret = listen( listenfd, 5 );
    assert( ret != -1 );

    //memset( cfg_host.m_hostname, '\0', 1024 );
    //memcpy( cfg_host.m_hostname, "127.0.0.1", strlen( "127.0.0.1" ) );
    //cfg_host.m_port = 54321;
    //cfg_host.m_conncnt = 5;
    processpool< conn, host, mgr >* pool = processpool< conn, host, mgr >::create( listenfd, logical_srv.size() );
    if( pool )
    {
        pool->run( logical_srv );
        delete pool;
    }

    close( listenfd );
    return 0;
}
