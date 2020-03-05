#ifndef CONN_H
#define CONN_H

#include <arpa/inet.h>
#include "fdwrapper.h"
/*
�ͻ������ӵĸ�����Ϣ
�������ͻ����ļ����������ͻ��˻��������±��
	  ������ļ�������������˻��������±��
	  ���������ӵĿͻ��˷�����ļ�������socket
*/
class conn
{
public:
	conn();
	~conn();
	void init_clt(int sockfd, const sockaddr_in& client_addr);			//��ʼ���ͻ��˵�ַ
	void init_srv(int sockfd, const sockaddr_in& server_addr);			//��ʼ���������˵�ַ
	void reset();  //���ö�д����
	RET_CODE read_clt(); //�ӿͻ��˶������Ϣд��m_clt_buf
	RET_CODE write_clt();//�Ѵӷ���˶���m_srv_buf������д��ͻ���
	RET_CODE read_srv();//�ӷ���˶������Ϣд��m_srv_buf
	RET_CODE write_srv();//�Ѵӿͻ��˶���m_clt_buf������д������

public:
	static const int BUF_SIZE = 2048;  //��������С

	char* m_clt_buf;   //�ͻ����ļ�������
	int m_clt_read_idx;    //�ͻ��˶��±�
	int m_clt_write_idx;    //�ͻ���д�±�
	sockaddr_in m_clt_address;				//�ͻ��˵�ַ
	int m_cltfd;     //�ͻ����ļ�������

	char* m_srv_buf;  //������ļ�������
	int m_srv_read_idx;   //����˶��±�
	int m_srv_write_idx;  //�����д�±�
	sockaddr_in m_srv_address;              //����˵�ַ
	int m_srvfd;    //������ļ�������

	bool m_srv_closed;  //�жϷ�����Ƿ�ر�
};

#endif
