#pragma once
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <mswsock.h>
#include <Windows.h>
#include <process.h>
#pragma comment(lib,"ws2_32.lib")

#include <list>
#include <mutex>

typedef enum _IOType
{
	IO_ACCEPT,		//�����׽���
	IO_RECV,		//��������
	IO_SEND			//��������
}IOType;

typedef enum _EventType
{
	Event_Error,	//��������
	Event_Connect,	//�ͻ�����
	Event_Close,	//�ͻ��ر�
	Event_Recv,		//��������
	Event_Send		//��������
}EventType;

typedef struct _IOBuffer
{
	WSAOVERLAPPED stOverlapped;	//�ص�����
	SOCKET sock;				//�׽���
	void* pBuf;					//������
	IOType eType;				//��������
	_IOBuffer()
	{
		memset(&stOverlapped, 0, sizeof(WSAOVERLAPPED));
		sock = INVALID_SOCKET;
		pBuf = 0;
		eType = IO_ACCEPT;
	}
}IOBuffer,*PIOBuffer;

typedef struct _IOContext
{
	SOCKET sock;								//�׽���
	sockaddr_in stLocalAddr;					//���ص�ַ
	sockaddr_in stRemoteAddr;					//Զ�̵�ַ
	int nOutstandingRecv;						//�׳�Recv������
	int nOutstandingSend;						//�׳�Send������
	bool bClose;								//�׽����Ƿ�ر�
	std::mutex cMutex;							//������
	_IOContext()
	{
		sock = INVALID_SOCKET;
		memset(&stLocalAddr, 0, sizeof(sockaddr_in));
		memset(&stRemoteAddr, 0, sizeof(sockaddr_in));
		bClose = false;
	}
}IOContext,*PIOContext;

class MaxSvr
{
private:
	std::list<PIOBuffer> m_cListBufferFree;		//����Buffer�б�
	std::mutex m_cMutexBufferFree;				//����Buffer�б�����
	int m_nMaxBufferFree;						//����Buffer�б��������

	std::list<PIOContext> m_cListContextFree;	//����Context�б�
	std::mutex m_cMutexContextFree;				//����Context�б�����	
	int m_nMaxContextFree;						//����Context�б��������

	std::list<PIOBuffer> m_cListBufferAccept;	//�׳�Accept�б�
	std::mutex m_cMutexBufferAccept;			//�׳�Accept�б�����
	int m_nMaxBufferAccept;						//�׳�Accept�б��������

	std::list<PIOContext> m_cListContextClient;	//����Client�б�
	std::mutex m_cMutexContextClient;			//����Client�б�����
	int m_nMaxContextClient;					//����Client�б��������

	HANDLE m_hEventArray[2];	//Accept�¼���Repost�¼�
	long m_lRepostCount;		//Ͷ�ݵ�����

	int m_nHandleThreadCount;	//�������߳�����
	int m_nBufferSize;			//��������С
	int m_nPort;				//�����������˿�

	int m_nInitAcceptCount;		//��ʼ��ʱ�׳�Accept���������
	int m_nInitRecvCount;		//��ʼ��ʱ�׳�Recv���������

	HANDLE m_hIOComplete;		//��ɶ˿�
	SOCKET m_sock;				//�����׽���

	LPFN_ACCEPTEX m_fAcceptEx;
	LPFN_GETACCEPTEXSOCKADDRS m_fGetAcceptExSockaddrs;

	bool m_bListenExit;			//�����̵߳��˳�
	bool m_bWorkStart;			//�����̵߳Ŀ�ʼ

protected:
	static void _ListenThread(MaxSvr* pThis);			//�����߳�
	static void _WorkerThread(MaxSvr* pThis);			//�����߳�

	bool CloseConnect(PIOContext pContext);				//�ر�һ������
	bool AddConnect(PIOContext pContext);				//���һ������

	bool SendBuffer(PIOContext pContext,
		void* pData,int nSize);							//��ָ���Ŀͻ���������

	PIOBuffer AllocateBuffer();							//����Buffer
	void ReleaseBuffer(PIOBuffer pBuffer);				//�ͷ�Buffer
	void ReleaseFreeBuffer();							//��տ���Buffer

	PIOContext AllocateContext(SOCKET sock);			//����Context
	void ReleaseContext(PIOContext pContext);			//�ͷ�Context
	void ReleaseFreeContext();							//��տ���Context
		
	bool InsertAccept(PIOBuffer pBuffer);				//����һ��Accept����
	bool RemoveAccept(PIOBuffer pBuffer);				//ɾ��һ��Accept����

	bool PostAccept(PIOBuffer pBuffer);					//�׳�һ��Accept
	bool PostSend(PIOContext pContext,
		PIOBuffer pBuffer,int nSize);					//�׳�һ��Send
	bool PostRecv(PIOContext pContext,
		PIOBuffer pBuffer);								//�׳�һ��Recv

	//IO�Ĵ���
	void HandleIOEvent(DWORD dwKey,PIOBuffer pBuffer,DWORD dwTran,int nError);
	void HandleAcceptEvent(DWORD dwTran, PIOBuffer pBuffer);
	void HandleRecvEvent(DWORD dwTran, PIOContext pContext, PIOBuffer pBuffer);
	void HandleSendEvent(DWORD dwTran, PIOContext pContext, PIOBuffer pBuffer);

	//�¼�֪ͨ
	virtual void OnEvent(EventType eType, void* pData,
		PIOBuffer pBuffer, PIOContext pContext) = 0;
public:
	MaxSvr();
	virtual ~MaxSvr();

public:
	inline void SetMaxBufferFree(int nValue)
	{
		m_nMaxBufferFree = nValue;
	}
	inline void SetMaxContextFree(int nValue)
	{
		m_nMaxContextFree = nValue;
	}
	inline void SetMaxBufferAccept(int nValue)
	{
		m_nMaxBufferAccept = nValue;
	}
	inline void SetMaxContextClient(int nValue)
	{
		m_nMaxContextClient = nValue;
	}
	inline void SetHandleThreadCount(int nValue)
	{
		m_nHandleThreadCount = nValue;
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
		m_nInitAcceptCount = nValue;
	}
	inline void SetInitRecv(int nValue)
	{
		m_nInitRecvCount = nValue;
	}
	inline int  GetConnectCount()
	{
		return m_cListContextClient.size();
	}

	bool StartServer();											//��ʼ����
	bool StopServer();											//��������
	bool CloseClientConnect(SOCKET sClientsock);				//�ر�һ���ͻ�����
	bool CloseAllClientConnect();								//�ر���������
	bool SendBufferToClent(SOCKET sClientSock,
		void* pData,int nSize);									//�������ݸ��ͻ�
};
