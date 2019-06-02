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
	IO_ACCEPT=0x10,	//接受套接字
	IO_RECV,		//接收数据
	IO_SEND			//发送数据
}IOType;

typedef struct _IOBuffer
{
	WSAOVERLAPPED stOverlapped;//IO操作的重叠结构体
	SOCKET sock;//客户端的套接字
	char *szBuffer;//接收/发送数据缓冲区
	int nBuffer;//缓冲区大小
	long long nId;//序列号
	IOType eType;//操作类型
	_IOBuffer()
	{
		memset(&stOverlapped, 0, sizeof(WSAOVERLAPPED));
		sock = INVALID_SOCKET;
		szBuffer = nullptr;
		nBuffer = 0;
		nId = 0;
		eType = IO_ACCEPT;
	}
}IOBuffer,*PIOBuffer;

typedef struct _IOContext
{
	SOCKET sock;//套接字
	sockaddr_in stLocalAddr;//本地地址
	sockaddr_in stRemoteAddr;//远程地址
	bool bClose;//套接字是否关闭
	int nOutstandingRecv;//抛出的Recv数量
	int nOutstandingSend;//抛出的Send数量
	long long nCurrentId;//当前的ID
	long long nNextId;//下一个的ID
	vector<PIOBuffer> vOutOrderReadBuffer;//没有按顺序完成的读取IO
	CRITICAL_SECTION stLock;//关键段
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
		memset(&stLock, 0, sizeof(CRITICAL_SECTION));
	}
}IOContext,*PIOContext;

class MaxSvr
{
protected:
	//空闲Buffer内存池
	vector<PIOBuffer> m_vFreeBuffer;
	CRITICAL_SECTION m_stBufferLock;
	int m_nMaxFreeBuffer;

	//空闲Context内存池
	vector<PIOContext> m_vFreeContext;
	CRITICAL_SECTION m_stContextLock;
	int m_nMaxFreeContext;

	//记录抛出的accept连接请求
	vector<PIOBuffer> m_vPostAccept;
	CRITICAL_SECTION m_stAcceptLock;
	int m_nMaxAccept;

	//客户连接列表
	vector<PIOContext> m_vConnectClient;
	CRITICAL_SECTION m_stClientLock;
	int m_nMaxConnectClient;

protected:
	HANDLE m_hAcceptEvent;//accept事件
	HANDLE m_hRepostEvent;//需要重新投递accept事件
	long m_lRepostCount;//投递的数量

	int m_nThreadCount;//工作者线程的数量
	int m_nBufferSize;//发送/接收缓冲区大小
	int m_nPort;//服务器的监听端口

	int m_nInitAccept;//初始化抛出的accept请求
	int m_nInitRecv;//初始化抛出的Recv请求

	int m_nMaxSend;//最大发送数据的数量，防止一直发送不接收

	HANDLE m_hListen;//监听线程
	HANDLE m_hIOComplete;//完成端口
	SOCKET m_sock;//监听套接字

	//有关的网络函数
	LPFN_ACCEPTEX m_fAcceptEx;
	LPFN_GETACCEPTEXSOCKADDRS m_fGetAcceptExSockaddrs;

	bool m_bListenExit;//监听线程的退出
	bool m_bWorkStart;//工作线程的开始

private:
	//监听线程
	static unsigned int _stdcall _ListenThread(void* p);

	//工作线程
	static unsigned int _stdcall _WorkThread(void* p);

private:
	//关闭一个连接
	bool CloseConnect(PIOContext pContext);

	//向指定的客户发送数据
	bool SendBuffer(PIOContext pContext, const char* szBuffer);

	//显示错误信息
	void DisplayErrorString(const char* szInfo);
private:
	//申请Buffer
	PIOBuffer AllocateBuffer();

	//释放Buffer
	void ReleaseBuffer(PIOBuffer pBuffer);

	//清空空闲Buffer
	void ReleaseFreeBuffer();

	//申请Context
	PIOContext AllocateContext(SOCKET sock);

	//释放Context
	void ReleaseContext(PIOContext pContext);

	//清空空闲Context
	void ReleaseFreeContext();

	//添加一个新的连接
	bool AddConnect(PIOContext pContext);

	//插入一个Accept请求
	bool InsertAccept(PIOBuffer pBuffer);

	//删除一个Accept请求
	bool RemoveAccept(PIOBuffer pBuffer);

	//取得下一个Read IO
	PIOBuffer GetNextReadBuffer(PIOContext pContext,PIOBuffer pBuffer);

	//抛出一个Accept
	bool PostAccept(PIOBuffer pBuffer);

	//抛出一个Send
	bool PostSend(PIOContext pContext,PIOBuffer pBuffer);

	//抛出一个Recv
	bool PostRecv(PIOContext pContext,PIOBuffer pBuffer);

	//IO的处理
	void HandleIOEvent(DWORD dwKey,PIOBuffer pBuffer,DWORD dwTran,int nError);

private:
	//客户加入连接
	void OnClientConnect(PIOContext pContext,PIOBuffer pBuffer);

	//客户关闭连接
	void OnClientClose(PIOContext pContext,PIOBuffer pBuffer);

	//连接发生错误
	void OnConnectErro(PIOContext pContext,PIOBuffer pBuffer,int nError);

	//发送操作完成
	void OnSendFinish(PIOContext pContext,PIOBuffer pBuffer);

	//接收操作完成
	void OnRecvFinish(PIOContext pContext,PIOBuffer pBuffer);

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
	//开始服务
	bool StartServer();

	//结束服务
	void StopServer();

	//关闭所有连接
	void CloseConnects();

	//获取当前连接客户数量
	inline int GetConnectCount()
	{
		return m_vConnectClient.size();
	}
};

