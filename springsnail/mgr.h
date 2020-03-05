#ifndef SRVMGR_H
#define SRVMGR_H

#include <map>
#include <arpa/inet.h>
#include "fdwrapper.h"
#include "conn.h"
/*
���ؾ����������ܣ���������
*/
using std::map;

//����
class host
{
public:
	char m_hostname[1024];//����ip��ַ
	int m_port;//����˿ں�
	int m_conncnt;//�������Ӹ���connect_cnt
};
//����������Ϣ�������������ӣ���ɸ��ؾ���Ŀ��
class mgr
{
public:
	mgr(int epollfd, const host& srv);  //�ڹ���mgr��ͬʱ����conn2srv�ͷ���˽�������
	~mgr();
	int conn2srv(const sockaddr_in& address);  //�ͷ���˽�������ͬʱ����socket������
	conn* pick_conn(int cltfd);  //�����Ӻõ������У�m_conn�У��ó�һ������������У�m_used����
	void free_conn(conn* connection);//// �ͷ����ӣ������ӹرջ����жϺ󣬽���fd���ں��¼���ɾ�������ر�fd��������ͬsrv�������ӵķ���m_freed��
	int get_used_conn_cnt();//��ȡ��ǰ������(��notify_parent_busy_ratio()���ã�
	void recycle_conns();//��m_freed�л������ӣ����������Ѿ����رգ���˻�Ҫ����conn2srv()���ŵ�m_conn��
	RET_CODE process(int fd, OP_TYPE type);//ͨ��fd��type�����ƶԷ���˺Ϳͻ��˵Ķ�д�����������ؾ���ĺ��Ĺ���

private:
	static int m_epollfd;//�ں��¼���
	map< int, conn* > m_conns;   //�����������Ϣ,׼���õ�����
	map< int, conn* > m_used;    //Ҫ��ʹ�õ�����
	map< int, conn* > m_freed;   //ʹ�ú��ͷŵ�����
	host m_logic_srv;            //�������˵���Ϣ
};

#endif
