#include "MaxSvr.h"
#pragma warning(disable:4018)

void MaxSvr::_ListenThread(MaxSvr* pThis)
{
	//在监听套接字上投递Accept请求
	for (int nIndex = 0; nIndex < pThis->m_nInitAcceptCount; nIndex++)
	{
		PIOBuffer pBuffer = pThis->AllocateBuffer();
		if (pBuffer)
		{
			if (pThis->InsertAccept(pBuffer))
			{
				pThis->PostAccept(pBuffer);
			}
		}
	}

	//创建工作者线程
	for (int nIndex = 0; nIndex < pThis->m_nHandleThreadCount; nIndex++)
	{
		std::thread cThread(_WorkerThread, pThis);
		cThread.detach();
	}

	while (true)
	{
		//一分钟检测一次
		int nEvent = WSAWaitForMultipleEvents(2, pThis->m_hEventArray, 
			false, 60 * 1000, false);

		//线程执行失败或者要结束线程
		if (nEvent == WSA_WAIT_FAILED || pThis->m_bListenExit)
		{
			//通知工作线程退出
			for (int nIndex = 0; nIndex < pThis->m_nHandleThreadCount; nIndex++)
			{
				PostQueuedCompletionStatus(pThis->m_hIOComplete, -1, 0, 0);
			}
			//关闭全部连接
			pThis->CloseAllClientConnect();
			//关闭监听套接字
			closesocket(pThis->m_sock);
			pThis->m_sock = INVALID_SOCKET;
			//关闭完成端口
			CloseHandle(pThis->m_hIOComplete);
			pThis->m_hIOComplete = 0;
			//释放空闲内存
			pThis->ReleaseFreeBuffer();
			pThis->ReleaseFreeContext();
			//退出监听线程
			return;
		}

		//超时了就检测连接时间
		if (nEvent == WSA_WAIT_TIMEOUT)
		{
			int nTime = 0, nLen = sizeof(int);
			pThis->m_cMutexBufferAccept.lock();
			for (std::list<PIOBuffer>::iterator it = pThis->m_cListBufferAccept.begin();
				it != pThis->m_cListBufferAccept.end(); it++)
			{
				//获取连接时间	超过时间了就是踢
				getsockopt((*it)->sock, SOL_SOCKET, SO_CONNECT_TIME, (char*)&nTime, &nLen);
				if (nTime >= pThis->m_nTimerOut)
				{
					closesocket((*it)->sock);
					(*it)->sock = INVALID_SOCKET;
				}
			}
			pThis->m_cMutexBufferAccept.unlock();
		}
		else
		{
			nEvent -= WAIT_OBJECT_0;
			int nCount = 0;
			if (nEvent == 0) //m_hAcceptEvent
			{
				WSANETWORKEVENTS stNetwork;
				memset(&stNetwork, 0, sizeof(WSANETWORKEVENTS));
				WSAEnumNetworkEvents(pThis->m_sock, 
					pThis->m_hEventArray[nEvent], &stNetwork);
				if (stNetwork.lNetworkEvents & FD_ACCEPT)
					nCount = 1;
			}
			else if (nEvent == 1)//m_hRepostEvent
			{
				//将m_lRepostCount返回给nCount后将m_lRepostCount设置为0
				nCount = InterlockedExchange(&pThis->m_lRepostCount, 0);
			}
			else if (nEvent > 1)//发生错误
			{
				pThis->m_bListenExit = true;
				continue;
			}

			int nMaxListBufferAccept = pThis->m_cListBufferAccept.size();
			for (int nIndex = 0; nIndex < nCount; nIndex++, nMaxListBufferAccept++)
			{
				//超过Accept请求最大值了
				if (nMaxListBufferAccept >= pThis->m_nMaxBufferAccept)
					break;
				PIOBuffer pBuffer = pThis->AllocateBuffer();
				if (pBuffer)
				{
					if (pThis->InsertAccept(pBuffer))
					{
						pThis->PostAccept(pBuffer);
					}
				}
			}
		}
	}
}

void MaxSvr::_WorkerThread(MaxSvr* pThis)
{
	DWORD dwKey = 0, dwTrans = 0;
	LPOVERLAPPED pOverlapped = 0;
	while (true)
	{
		//等待IO事件
		bool bState = GetQueuedCompletionStatus(pThis->m_hIOComplete,
			&dwTrans, (LPDWORD)&dwKey, (LPOVERLAPPED*)&pOverlapped, WSA_INFINITE);

		//通知退出工作线程
		if (dwTrans == -1)
			return;

		//获取Buffer结构
		PIOBuffer pBuffer = CONTAINING_RECORD(pOverlapped, IOBuffer, stOverlapped);
		int nError = NO_ERROR;
		if (bState==false)//套接字发生错误
		{
			SOCKET sock = INVALID_SOCKET;
			if (pBuffer->eType == IO_ACCEPT)//服务器的监听套接字
				sock = pThis->m_sock;
			else//IO_RECV || IO_SEND
			{
				if (!dwKey)//Context结构为空
					return;
				sock = ((PIOContext)dwKey)->sock;
			}
			DWORD dwFlag = 0;
			if (!WSAGetOverlappedResult(sock, 
				&pBuffer->stOverlapped,&dwTrans, false, &dwFlag))
				nError = WSAGetLastError();//获取重叠操作结果失败
		}
		pThis->HandleIOEvent(dwKey, pBuffer, dwTrans, nError);
	}
}

bool MaxSvr::CloseConnect(PIOContext pContext)
{
	//指针为空
	if (!pContext)
		return false;

	bool bState = false;
	m_cMutexContextClient.lock();
	for (std::list<PIOContext>::iterator it = m_cListContextClient.begin();
		it != m_cListContextClient.end(); it++)
	{
		if (pContext == (*it))
		{
			m_cListContextClient.erase(it);
			bState = true;
			break;
		}
	}
	m_cMutexContextClient.unlock();

	//将套接字删除
	pContext->cMutex.lock();
	closesocket(pContext->sock);
	pContext->sock = INVALID_SOCKET;
	pContext->bClose = true;
	pContext->cMutex.unlock();
	return bState;
}

bool MaxSvr::AddConnect(PIOContext pContext)
{
	if (!pContext)
		return false;

	bool bState = false;
	m_cMutexContextClient.lock();
	if (m_cListContextClient.size() <= m_nMaxContextClient)
	{
		m_cListContextClient.emplace_back(pContext);
		bState = true;
	}
	m_cMutexContextClient.unlock();
	return bState;
}

bool MaxSvr::SendBuffer(PIOContext pContext, void* pData,int nSize)
{
	PIOBuffer pBuffer = AllocateBuffer();
	if (pBuffer)
	{
		memcpy(pBuffer->pBuf, pData, nSize);
		return PostSend(pContext, pBuffer, nSize);
	}
	return false;
}

PIOBuffer MaxSvr::AllocateBuffer()
{
	PIOBuffer pBuffer = 0;
	bool bState = false;

	//如果空闲链表里面有内存的话
	m_cMutexBufferFree.lock();
	if (!m_cListBufferFree.empty())
	{
		pBuffer = m_cListBufferFree.back();//拿出最后一个地址
		m_cListBufferFree.pop_back();//删除最后一个地址
		bState = true;//拿到地址了
	}
	m_cMutexBufferFree.unlock();

	if (!bState)//上面没有拿到地址，就申请
	{
		pBuffer = new IOBuffer;
		if (pBuffer)//成功申请到了内存
		{
			memset(pBuffer, 0, sizeof(IOBuffer));
			pBuffer->pBuf = new byte[m_nBufferSize];//申请缓冲区内存
			if (pBuffer->pBuf)
				memset(pBuffer->pBuf, 0, m_nBufferSize);
			else
			{
				delete pBuffer;
				pBuffer = 0;
			}
		}
	}
	return pBuffer;
}

void MaxSvr::ReleaseBuffer(PIOBuffer pBuffer)
{
	//空指针的话
	if (!pBuffer)
		return;

	bool bState = false;
	//还没满就放进容器
	m_cMutexBufferFree.lock();
	if (m_cListBufferFree.size() <= m_nMaxBufferFree)
	{
		//保存缓冲区指针
		void* pData = pBuffer->pBuf;
		memset(pBuffer, 0, sizeof(IOBuffer));
		memset(pData, 0, m_nBufferSize);
		pBuffer->pBuf = pData;
		//放进容器
		m_cListBufferFree.emplace_back(pBuffer);
		bState = true;
	}
	m_cMutexBufferFree.unlock();

	//如果没有放进容器就释放内存
	if (!bState)
	{
		delete[] pBuffer->pBuf;
		delete pBuffer;
		pBuffer = 0;
	}
}

void MaxSvr::ReleaseFreeBuffer()
{
	m_cMutexBufferFree.lock();
	for (std::list<PIOBuffer>::iterator it = m_cListBufferFree.begin();
		it != m_cListBufferFree.end(); it++)
	{
		delete[] (*it)->pBuf;//释放字符串
		delete (*it);//释放内存
	}
	m_cListBufferFree.clear();
	m_cMutexBufferFree.unlock();
}

PIOContext MaxSvr::AllocateContext(SOCKET sock)
{
	//套接字都没有创建什么连接??
	if (sock == INVALID_SOCKET)
		return 0;

	PIOContext pContext = 0;
	bool bState = false;
	//如果空闲链表里面有内存的话，直接拿来使用
	m_cMutexContextFree.lock();
	if (!m_cListContextFree.empty())
	{
		//先拿出，再删除
		pContext = m_cListContextFree.back();
		m_cListContextFree.pop_back();
		bState = true;
	}
	m_cMutexContextFree.unlock();

	//如果没拿到的话，直接申请
	if (!bState)
	{
		pContext = new IOContext;
		if (pContext)
		{
			pContext->nOutstandingRecv = pContext->nOutstandingSend = 0;
		}
	}

	//拿到内存或者申请成功的话
	if (pContext)
		pContext->sock = sock;
	return pContext;
}

void MaxSvr::ReleaseContext(PIOContext pContext)
{
	//不能为0
	if (!pContext)
		return;

	closesocket(pContext->sock);
	pContext->sock = INVALID_SOCKET;

	bool bState = false;
	m_cMutexContextFree.lock();
	if (m_cListContextFree.size() <= m_nMaxContextFree)
	{
		pContext->bClose = false;
		pContext->nOutstandingRecv = pContext->nOutstandingSend = 0;
		m_cListContextFree.emplace_back(pContext);
		bState = true;
	}
	m_cMutexContextFree.unlock();

	if (!bState)
	{
		delete pContext;
		pContext = 0;
	}
}

void MaxSvr::ReleaseFreeContext()
{
	m_cMutexContextFree.lock();
	for (std::list<PIOContext>::iterator it = m_cListContextFree.begin();
		it != m_cListContextFree.end(); it++)
	{
		delete (*it);
	}
	m_cListContextFree.clear();
	m_cMutexContextFree.unlock();
}

bool MaxSvr::InsertAccept(PIOBuffer pBuffer)
{
	if (!pBuffer)
		return false;
	bool bState = false;
	m_cMutexBufferAccept.lock();
	if (m_cListBufferAccept.size() < m_nMaxBufferAccept)
	{
		m_cListBufferAccept.emplace_back(pBuffer);
		bState = true;
	}
	m_cMutexBufferAccept.unlock();
	return bState;
}

bool MaxSvr::RemoveAccept(PIOBuffer pBuffer)
{
	if (!pBuffer)
		return false;
	bool bState = false;
	m_cMutexBufferAccept.lock();
	for (std::list<PIOBuffer>::iterator it = m_cListBufferAccept.begin();
		it != m_cListBufferAccept.end(); it++) 
	{
		if (pBuffer == (*it))//指针地址的判断
		{
			m_cListBufferAccept.erase(it);
			bState = true;
			break;
		}
	}
	m_cMutexBufferAccept.unlock();
	return bState;
}

bool MaxSvr::PostAccept(PIOBuffer pBuffer)
{
	if (!pBuffer)
		return false;

	pBuffer->eType = IO_ACCEPT;
	DWORD dwByte = 0;
	pBuffer->sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
	if (pBuffer->sock == INVALID_SOCKET)
		return false;

	bool bState = m_fAcceptEx(m_sock,pBuffer->sock,pBuffer->pBuf, 
		m_nBufferSize - ((sizeof(sockaddr_in) + 16) * 2),
		sizeof(sockaddr_in) + 16, 
		sizeof(sockaddr_in) + 16, 
		&dwByte, &pBuffer->stOverlapped);
	if (!bState && WSAGetLastError() != WSA_IO_PENDING)
		return false;
	return true;
}

bool MaxSvr::PostSend(PIOContext pContext, PIOBuffer pBuffer,int nSize)
{
	if (!pContext || !pBuffer)
		return false;

	if (pContext->sock == INVALID_SOCKET)
		return false;

	pBuffer->eType = IO_SEND;
	DWORD dwByte = 0, dwFlag = 0;
	WSABUF stBuf;
	stBuf.buf = (char*)pBuffer->pBuf;
	stBuf.len = nSize;

	int nRet = WSASend(pContext->sock, &stBuf, 
		1, &dwByte, dwFlag, &pBuffer->stOverlapped, 0);
	if (!nRet || WSAGetLastError() == WSA_IO_PENDING)
	{
		pContext->cMutex.lock();
		pContext->nOutstandingSend++;
		pContext->cMutex.unlock();
		return true;
	}
	return false;
}

bool MaxSvr::PostRecv(PIOContext pContext, PIOBuffer pBuffer)
{
	if (!pContext || !pBuffer)
		return false;

	if (pContext->sock == INVALID_SOCKET)
		return false;

	pBuffer->eType = IO_RECV;
	bool bState = false;
	DWORD dwByte = 0, dwFalg = 0;
	WSABUF stBuf;
	stBuf.buf = (char*)pBuffer->pBuf;
	stBuf.len = m_nBufferSize;
	pContext->cMutex.lock();
	int nRet = WSARecv(pContext->sock, &stBuf, 1, &dwByte,
		&dwFalg, &pBuffer->stOverlapped, 0);
	if (!nRet || WSAGetLastError() == WSA_IO_PENDING)
	{
		pContext->nOutstandingRecv++;
		bState = true;
	}
	pContext->cMutex.unlock();
	return bState;
}

void MaxSvr::HandleIOEvent(DWORD dwKey, PIOBuffer pBuffer, 
	DWORD dwTran, int nError)
{
	PIOContext pContext = (PIOContext)dwKey;

	/*如果是Accept的话这里一定是NULL
	因为是Accept的时候还没有进行完成端口关联*/
	if (pContext)
	{
		pContext->cMutex.lock();
		if (pBuffer->eType == IO_RECV)
			pContext->nOutstandingRecv--;
		else if (pBuffer->eType == IO_SEND)
			pContext->nOutstandingSend--;
		pContext->cMutex.unlock();

		if (pContext->bClose)//这个套接字被关闭了
		{
			if (!pContext->nOutstandingRecv && !pContext->nOutstandingSend)
			{
				ReleaseContext(pContext);
			}
			ReleaseBuffer(pBuffer);
			return;
		}
	}
	else
	{
		RemoveAccept(pBuffer);
	}

	if (nError)//发生错误
	{
		if (pBuffer->eType != IO_ACCEPT)//我们出错
		{
			//通知发生错误 
			//10054错误代码是远程主机强迫关闭了一个现有的连接。 
			//也就是一个连接被强制关闭而已
			OnEvent(Event_Error, (void*)nError, pBuffer, pContext);
			//关闭连接
			CloseConnect(pContext);
		}
		else//客户出错
		{
			if (pBuffer->sock != INVALID_SOCKET)
			{
				closesocket(pBuffer->sock);
				pBuffer->sock = INVALID_SOCKET;
			}
		}
		ReleaseBuffer(pBuffer);
		return;
	}

	switch (pBuffer->eType)
	{
	case IO_ACCEPT:
		HandleAcceptEvent(dwTran,pBuffer);
		break;
	case IO_RECV:
		HandleRecvEvent(dwTran,pContext,pBuffer);
		break;
	case IO_SEND:
		HandleSendEvent(dwTran, pContext, pBuffer);
		break;
	}
}

void MaxSvr::HandleAcceptEvent(DWORD dwTran, PIOBuffer pBuffer)
{
	if (!dwTran)
	{
		if (pBuffer->sock != INVALID_SOCKET)
		{
			closesocket(pBuffer->sock);
			pBuffer->sock = INVALID_SOCKET;
		}
	}
	else
	{
		PIOContext pNewClient = AllocateContext(pBuffer->sock);
		if (pNewClient)
		{
			if (AddConnect(pNewClient))
			{
				int nLocalAddrLen = 0;
				int nRemoteAddrLen = 0;
				LPSOCKADDR pLocalAddr = 0;
				LPSOCKADDR pRemoteAdd = 0;

				//获取地址信息
				m_fGetAcceptExSockaddrs(pBuffer->pBuf,
					m_nBufferSize - ((sizeof(sockaddr_in) + 16) * 2),
					sizeof(sockaddr_in) + 16,
					sizeof(sockaddr_in) + 16,
					(sockaddr**)&pLocalAddr, &nLocalAddrLen,
					(sockaddr**)&pRemoteAdd, &nRemoteAddrLen);

				//保存地址信息
				memcpy(&pNewClient->stLocalAddr, pLocalAddr, nLocalAddrLen);
				memcpy(&pNewClient->stRemoteAddr, pRemoteAdd, nRemoteAddrLen);

				//套接字关联到完成端口
				CreateIoCompletionPort((HANDLE)pNewClient->sock, m_hIOComplete,
					(DWORD)pNewClient, 0);

				//通知
				OnEvent(Event_Connect, 0, pBuffer, pNewClient);

				//投递几个Recv
				for (int nIndex = 0; nIndex < m_nInitRecvCount; nIndex++)
				{
					PIOBuffer pTemp = AllocateBuffer();
					if (pTemp)
					{
						if (!PostRecv(pNewClient, pTemp))
						{
							CloseConnect(pNewClient);
						}
					}
				}
			}
			else
			{
				CloseConnect(pNewClient);
				ReleaseContext(pNewClient);
			}
		}
		else
		{
			closesocket(pBuffer->sock);
			pBuffer->sock = INVALID_SOCKET;
		}
	}
	ReleaseBuffer(pBuffer);
	InterlockedIncrement(&m_lRepostCount);
	SetEvent(m_hEventArray[1]);
}

void MaxSvr::HandleRecvEvent(DWORD dwTran, PIOContext pContext, PIOBuffer pBuffer)
{
	if (!dwTran)
	{
		OnEvent(Event_Close, 0, pBuffer, pContext);
		CloseConnect(pContext);
	}
	else
	{
		OnEvent(Event_Recv, 0, pBuffer, pContext);
		PIOBuffer pPostRecv = AllocateBuffer();
		if (pPostRecv)
		{
			PostRecv(pContext, pPostRecv);
		}
	}
	ReleaseBuffer(pBuffer);
}

void MaxSvr::HandleSendEvent(DWORD dwTran, PIOContext pContext, PIOBuffer pBuffer)
{
	if (!dwTran)
	{
		OnEvent(Event_Close, 0, pBuffer, pContext);
		CloseConnect(pContext);
	}
	else
	{
		OnEvent(Event_Send, 0, pBuffer, pContext);
	}
	ReleaseBuffer(pBuffer);
}

MaxSvr::MaxSvr()
{
	//默认最大Buffer空闲
	m_nMaxBufferFree = 200;
	//默认最大Context空闲
	m_nMaxContextFree = 200;
	//默认最大投递Accept
	m_nMaxBufferAccept = 200;
	//默认最大客户Context
	m_nMaxContextClient = 20000;

	m_hEventArray[0] = CreateEventA(0, false, false, 0);	//Accept事件
	m_hEventArray[1] = CreateEventA(0, false, false, 0);	//Repost事件
	m_lRepostCount = 0;										//重新Recv的数量

	m_nHandleThreadCount = 1;			//工作线程数量
	m_nBufferSize = 4 * 1024;			//缓冲区大小
	m_nPort = 9999;						//监听端口
	m_nTimerOut = 2 * 60;				//连接超时时间

	m_nInitAcceptCount = 5;				//初始化Accept数量
	m_nInitRecvCount = 5;				//初始化Recv数量

	m_hIOComplete = 0;					//IO完成端口
	m_sock = INVALID_SOCKET;			//套接字

	m_fAcceptEx = 0;					//AcceptEx
	m_fGetAcceptExSockaddrs = 0;		//GetAcceptExSockaddrs

	m_bWorkStart = false;				//工作线程没有开始工作
	m_bListenExit = true;				//监听线程退出了

	WSADATA data;						//初始化网络函数
	if (WSAStartup(MAKEWORD(2, 2), &data))
		exit(0);
}

MaxSvr::~MaxSvr()
{
	StopServer();
	WSACleanup();
}

bool MaxSvr::StartServer()
{
	//防止重复启动
	if (m_bWorkStart)
		return false;

	//创建套接字
	m_sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
	if (m_sock == INVALID_SOCKET)
		return false;

	//绑定套接字
	sockaddr_in stAddr;
	memset(&stAddr,0,sizeof(sockaddr_in));
	stAddr.sin_family = AF_INET;
	stAddr.sin_port = ntohs(m_nPort);
	stAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	if (bind(m_sock, (sockaddr*)&stAddr, sizeof(sockaddr_in)) == SOCKET_ERROR)
		return false;

	//监听套接字
	if (listen(m_sock, 200) == SOCKET_ERROR)
		return false;

	//创建IO完成端口
	m_hIOComplete = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	if (!m_hIOComplete)
		return false;

	//获取网络函数
	DWORD dwByte = 0;
	GUID GAcceptEx = WSAID_ACCEPTEX;
	WSAIoctl(m_sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GAcceptEx, sizeof(GAcceptEx),
		&m_fAcceptEx, sizeof(m_fAcceptEx),
		&dwByte, 0, 0);
	if (!dwByte)
		return false;
	GUID GGetAcceptSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
	WSAIoctl(m_sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&GGetAcceptSockaddrs, sizeof(GGetAcceptSockaddrs),
		&m_fGetAcceptExSockaddrs, sizeof(m_fGetAcceptExSockaddrs),
		&dwByte, 0, 0);
	if (!dwByte)
		return false;

	//将监听套接字与IO完成端口绑定
	CreateIoCompletionPort((HANDLE)m_sock, m_hIOComplete, 0, 0);

	//注册网络事件
	if (WSAEventSelect(m_sock, m_hEventArray[1], FD_ACCEPT) == SOCKET_ERROR)
		return false;

	//创建监听线程
	std::thread cThread(_ListenThread,this);
	cThread.detach();

	m_bListenExit = false;	//监听线程没有退出
	m_bWorkStart = true;	//工作线程开始工作
	return true;
}

bool MaxSvr::StopServer()
{
	if (!m_bWorkStart)
		return false;

	//通知监听线程退出
	m_bListenExit = true;
	SetEvent(m_hEventArray[1]);
	//工作线程没有工作
	m_bWorkStart = false;

	return true;
}

bool MaxSvr::CloseClientConnect(SOCKET sClientsock)
{
	m_cMutexContextClient.lock();
	for (std::list<PIOContext>::iterator it = m_cListContextClient.begin();
		it != m_cListContextClient.end(); it++)
	{
		if ((*it)->sock == sClientsock)
		{
			m_cListContextClient.erase(it);
			return true;
		}
	}
	m_cMutexContextClient.unlock();
	return false;
}

bool MaxSvr::CloseAllClientConnect()
{
	m_cMutexContextClient.lock();
	for (std::list<PIOContext>::iterator it = m_cListContextClient.begin();
		it != m_cListContextClient.end(); it++)
	{
		(*it)->cMutex.lock();
		closesocket((*it)->sock);
		(*it)->sock = INVALID_SOCKET;
		(*it)->bClose = true;
		(*it)->cMutex.unlock();
	}
	m_cListContextClient.clear();
	m_cMutexContextClient.unlock();
	return true;
}

bool MaxSvr::SendBufferToClent(SOCKET sClientSock,void* pData,int nSize)
{
	bool bState = false;
	m_cMutexContextClient.lock();
	for (std::list<PIOContext>::iterator it = m_cListContextClient.begin();
		it != m_cListContextClient.end(); it++)
	{
		if ((*it)->sock == sClientSock)
		{
			bState = SendBuffer((*it), pData,nSize);
			break;
		}
	}
	m_cMutexContextClient.unlock();
	return bState;
}
