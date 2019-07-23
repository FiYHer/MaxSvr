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
	IO_ACCEPT,		//接受套接字
	IO_RECV,		//接收数据
	IO_SEND			//发送数据
}IOType;

typedef enum _EventType
{
	Event_Error,	//发生错误
	Event_Connect,	//客户连接
	Event_Close,	//客户关闭
	Event_Recv,		//接收数据
	Event_Send		//发生数据
}EventType;

typedef struct _IOBuffer
{
	WSAOVERLAPPED stOverlapped;	//重叠操作
	SOCKET sock;				//套接字
	void* pBuf;					//缓冲区
	IOType eType;				//操作类型
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
	SOCKET sock;								//套接字
	sockaddr_in stLocalAddr;					//本地地址
	sockaddr_in stRemoteAddr;					//远程地址
	int nOutstandingRecv;						//抛出Recv的数量
	int nOutstandingSend;						//抛出Send的数量
	bool bClose;								//套接字是否关闭
	std::mutex cMutex;							//互斥体
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
protected:
	std::list<PIOBuffer> m_cListBufferFree;		//空闲Buffer列表
	std::mutex m_cMutexBufferFree;				//空闲Buffer列表互斥体
	int m_nMaxBufferFree;						//空闲Buffer列表最大数量

	std::list<PIOContext> m_cListContextFree;	//空闲Context列表
	std::mutex m_cMutexContextFree;				//空闲Context列表互斥体	
	int m_nMaxContextFree;						//空闲Context列表最大数量

	std::list<PIOBuffer> m_cListBufferAccept;	//抛出Accept列表
	std::mutex m_cMutexBufferAccept;			//抛出Accept列表互斥体
	int m_nMaxBufferAccept;						//抛出Accept列表最大数量

	std::list<PIOContext> m_cListContextClient;	//连接Client列表
	std::mutex m_cMutexContextClient;			//连接Client列表互斥体
	int m_nMaxContextClient;					//连接Client列表最大数量

	HANDLE m_hEventArray[2];	//Accept事件和Repost事件
	long m_lRepostCount;		//投递的数量

	int m_nHandleThreadCount;	//工作者线程数量
	int m_nBufferSize;			//缓冲区大小
	int m_nPort;				//服务器监听端口
	int m_nTimerOut;			//设置超时时间

	int m_nInitAcceptCount;		//初始化时抛出Accept请求的数量
	int m_nInitRecvCount;		//初始化时抛出Recv请求的数量

	HANDLE m_hIOComplete;		//完成端口
	SOCKET m_sock;				//监听套接字

	LPFN_ACCEPTEX m_fAcceptEx;
	LPFN_GETACCEPTEXSOCKADDRS m_fGetAcceptExSockaddrs;

	bool m_bListenExit;			//监听线程的退出
	bool m_bWorkStart;			//工作线程的开始

protected:
	static void _ListenThread(MaxSvr* pThis);			//监听线程
	static void _WorkerThread(MaxSvr* pThis);			//工作线程

	bool CloseConnect(PIOContext pContext);				//关闭一个连接
	bool AddConnect(PIOContext pContext);				//添加一个连接

	bool SendBuffer(PIOContext pContext,
		void* pData,int nSize);							//向指定的客户发送数据

	PIOBuffer AllocateBuffer();							//申请Buffer
	void ReleaseBuffer(PIOBuffer pBuffer);				//释放Buffer
	void ReleaseFreeBuffer();							//清空空闲Buffer

	PIOContext AllocateContext(SOCKET sock);			//申请Context
	void ReleaseContext(PIOContext pContext);			//释放Context
	void ReleaseFreeContext();							//清空空闲Context
		
	bool InsertAccept(PIOBuffer pBuffer);				//插入一个Accept请求
	bool RemoveAccept(PIOBuffer pBuffer);				//删除一个Accept请求

	bool PostAccept(PIOBuffer pBuffer);					//抛出一个Accept
	bool PostSend(PIOContext pContext,
		PIOBuffer pBuffer,int nSize);					//抛出一个Send
	bool PostRecv(PIOContext pContext,
		PIOBuffer pBuffer);								//抛出一个Recv

	//IO的处理
	void HandleIOEvent(DWORD dwKey,PIOBuffer pBuffer,DWORD dwTran,int nError);
	void HandleAcceptEvent(DWORD dwTran, PIOBuffer pBuffer);
	void HandleRecvEvent(DWORD dwTran, PIOContext pContext, PIOBuffer pBuffer);
	void HandleSendEvent(DWORD dwTran, PIOContext pContext, PIOBuffer pBuffer);

	//事件通知
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

	bool StartServer();											//开始服务
	bool StopServer();											//结束服务
	bool CloseClientConnect(SOCKET sClientsock);				//关闭一个客户连接
	bool CloseAllClientConnect();								//关闭所有连接
	bool SendBufferToClent(SOCKET sClientSock,
		void* pData,int nSize);									//发送数据给客户
};
