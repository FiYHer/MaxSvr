#pragma once
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <mswsock.h>
#include <Windows.h>
#include <process.h>

#include <vector>
#include <iostream>
using namespace std;
#pragma comment(lib,"ws2_32.lib")

typedef enum _IOType
{
	IO_ACCEPT=0x10,	//�����׽���
	IO_RECV,		//��������
	IO_SEND			//��������
}IOType;

typedef struct _IOBuffer
{
	WSAOVERLAPPED stOverlapped;	//IO�������ص��ṹ��
	SOCKET sock;				//�ͻ��˵��׽���
	char *szBuffer;				//���� �������ݻ�����
	long long nId;				//���к�
	IOType eType;				//��������
	_IOBuffer()
	{
		memset(&stOverlapped, 0, sizeof(WSAOVERLAPPED));
		sock = INVALID_SOCKET;
		szBuffer = nullptr;
		nId = 0;
		eType = IO_ACCEPT;
	}
	void clear()
	{
		memset(&stOverlapped, 0, sizeof(WSAOVERLAPPED));
		sock = INVALID_SOCKET;
		szBuffer = nullptr;
		nId = 0;
		eType = IO_ACCEPT;
	}
}IOBuffer,*PIOBuffer;

typedef struct _IOContext
{
	SOCKET sock;							//�׽���
	sockaddr_in stLocalAddr;				//���ص�ַ
	sockaddr_in stRemoteAddr;				//Զ�̵�ַ
	bool bClose;							//�׽����Ƿ�ر�
	int nOutstandingRecv;					//�׳���Recv����
	int nOutstandingSend;					//�׳���Send����
	long long nCurrentId;					//��ǰ��ID
	long long nNextId;						//��һ����ID
	vector<PIOBuffer> vOutOrderReadBuffer;	//û�а�˳����ɵĶ�ȡIO
	CRITICAL_SECTION stLock;				//�ؼ���
	_IOContext()
	{
		sock = INVALID_SOCKET;
		memset(&stLocalAddr, 0, sizeof(sockaddr_in));
		memset(&stRemoteAddr, 0, sizeof(sockaddr_in));
		bClose = false;
		nOutstandingRecv = 0;
		nOutstandingSend = 0;
		nCurrentId = 0;
		nNextId = 0;
		vOutOrderReadBuffer.clear();
		memset(&stLock, 0, sizeof(CRITICAL_SECTION));
	}
	void clear()
	{
		sock = INVALID_SOCKET;
		memset(&stLocalAddr, 0, sizeof(sockaddr_in));
		memset(&stRemoteAddr, 0, sizeof(sockaddr_in));
		bClose = false;
		nOutstandingRecv = 0;
		nOutstandingSend = 0;
		nCurrentId = 0;
		nNextId = 0;
		vOutOrderReadBuffer.clear();
		memset(&stLock, 0, sizeof(CRITICAL_SECTION));
	}
}IOContext,*PIOContext;

class MaxSvr
{
protected:
	//����Buffer�ڴ��
	vector<PIOBuffer> m_vFreeBuffer;
	CRITICAL_SECTION m_stFreeBufferLock;
	int m_nMaxFreeBuffer;

	//����Context�ڴ��
	vector<PIOContext> m_vFreeContext;
	CRITICAL_SECTION m_stFreeContextLock;
	int m_nMaxFreeContext;

	//��¼�׳���accept��������
	vector<PIOBuffer> m_vPostAccept;
	CRITICAL_SECTION m_stAcceptLock;
	int m_nMaxAccept;

	//�ͻ������б�
	vector<PIOContext> m_vConnectClient;
	CRITICAL_SECTION m_stClientLock;
	int m_nMaxConnectClient;
protected:
	HANDLE m_hAcceptEvent;//accept�¼�
	HANDLE m_hRepostEvent;//��Ҫ����Ͷ��accept�¼�
	long m_lRepostCount;//Ͷ�ݵ�����

	int m_nThreadCount;//�������̵߳�����
	int m_nBufferSize;//����/���ջ�������С
	int m_nPort;//�������ļ����˿�

	int m_nInitAccept;//��ʼ���׳���accept����
	int m_nInitRecv;//��ʼ���׳���Recv����

	int m_nMaxSend;//��������ݵ���������ֹһֱ���Ͳ�����

	HANDLE m_hListen;//�����߳�
	HANDLE m_hIOComplete;//��ɶ˿�
	SOCKET m_sock;//�����׽���

	//�йص����纯��
	LPFN_ACCEPTEX m_fAcceptEx;
	LPFN_GETACCEPTEXSOCKADDRS m_fGetAcceptExSockaddrs;

	bool m_bListenExit;//�����̵߳��˳�
	bool m_bWorkStart;//�����̵߳Ŀ�ʼ

private:
	//�����߳�
	static unsigned int _stdcall _ListenThread(void* p);

	//�����߳�
	static unsigned int _stdcall _WorkThread(void* p);

private:
	//�ر�һ������
	bool CloseConnect(PIOContext pContext);

	//��ָ���Ŀͻ���������
	bool SendBuffer(PIOContext pContext, const char* szBuffer);

	//��ʾ������Ϣ
	void DisplayErrorString(const char* szInfo);
private:
	//����Buffer
	PIOBuffer AllocateBuffer();

	//�ͷ�Buffer
	void ReleaseBuffer(PIOBuffer pBuffer);

	//��տ���Buffer
	void ReleaseFreeBuffer();

	//����Context
	PIOContext AllocateContext(SOCKET sock);

	//�ͷ�Context
	void ReleaseContext(PIOContext pContext);

	//��տ���Context
	void ReleaseFreeContext();

	//���һ���µ�����
	bool AddConnect(PIOContext pContext);

	//����һ��Accept����
	bool InsertAccept(PIOBuffer pBuffer);

	//ɾ��һ��Accept����
	bool RemoveAccept(PIOBuffer pBuffer);

	//ȡ����һ��Read IO
	PIOBuffer GetNextReadBuffer(PIOContext pContext,PIOBuffer pBuffer);

	//�׳�һ��Accept
	bool PostAccept(PIOBuffer pBuffer);

	//�׳�һ��Send
	bool PostSend(PIOContext pContext,PIOBuffer pBuffer);

	//�׳�һ��Recv
	bool PostRecv(PIOContext pContext,PIOBuffer pBuffer);

	//IO�Ĵ���
	void HandleIOEvent(DWORD dwKey,PIOBuffer pBuffer,DWORD dwTran,int nError);

	//����Accept����
	void HandleAcceptEvent(DWORD dwTran, PIOBuffer pBuffer);

	//����Recv����
	void HandleRecvEvent(DWORD dwTran, PIOContext pContext, PIOBuffer pBuffer);

	//����Send����
	void HandleSendEvent(DWORD dwTran, PIOContext pContext, PIOBuffer pBuffer);

protected:
	//�ͻ���������
	void virtual OnClientConnect(PIOContext pContext, PIOBuffer pBuffer) {};

	//�ͻ��ر�����
	void virtual OnClientClose(PIOContext pContext, PIOBuffer pBuffer) {};

	//���ӷ�������
	void virtual OnConnectErro(PIOContext pContext, PIOBuffer pBuffer, int nError) {};

	//���Ͳ������
	void virtual OnSendFinish(PIOContext pContext, PIOBuffer pBuffer) {};

	//���ղ������
	void virtual OnRecvFinish(PIOContext pContext, PIOBuffer pBuffer) {};

public:
	MaxSvr();
	virtual ~MaxSvr();

public:
	inline void SetMaxFreeBuffer(int nValue)
	{
		m_nMaxFreeBuffer = nValue;
	}

	inline void SetMaxFreeContext(int nValue)
	{
		m_nMaxFreeContext = nValue;
	}

	inline void SetMaxAccept(int nValue)
	{
		m_nMaxAccept = nValue;
	}

	inline void SetMaxConnectClient(int nValue)
	{
		m_nMaxAccept = nValue;
	}

	inline void SetThreadCount(int nValue)
	{
		m_nThreadCount = nValue;
	}

	inline void SetBufferSize(int nValue)
	{
		m_nBufferSize = nValue;
	}

	inline void SetPort(int nValue)
	{
		m_nPort = nValue;
	}

	inline void SetInitAccept(int nValue)
	{
		m_nInitAccept = nValue;
	}

	inline void SetInitRecv(int nValue)
	{
		m_nInitRecv = nValue;
	}

	inline void SetMaxSend(int nValue)
	{
		m_nMaxSend = nValue;
	}

public:
	//��ʼ����
	bool StartServer();

	//��������
	void StopServer();

	//�ر���������
	void CloseConnects();

	//��ȡ��ǰ���ӿͻ�����
	inline int GetConnectCount()
	{
		return m_vConnectClient.size();
	}
};


//Э���߳�ͬ��
class CLock
{
private:
	PCRITICAL_SECTION m_pLock;
	bool m_bUnLock;
public:
	CLock(PCRITICAL_SECTION stLock)
	{
		m_pLock = stLock;
		EnterCriticalSection(m_pLock);
		m_bUnLock = false;
	}
	~CLock()
	{
		UnLock();
	}
public:
	void UnLock()
	{
		if (!m_bUnLock)
		{
			LeaveCriticalSection(m_pLock);
			m_bUnLock = true;
		}
	}
};

