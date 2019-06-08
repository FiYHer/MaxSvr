#include "MaxSvr.h"

unsigned int _stdcall MaxSvr::_ListenThread(void* p)
{
	MaxSvr* pThis = (MaxSvr*)p;//类的转化

	//现在监听套接字上面投递几个Accept请求
	for (int nIndex = 0; nIndex < pThis->m_nInitAccept; nIndex++)
	{
		PIOBuffer pBuffer = pThis->AllocateBuffer();
		if (pBuffer)
		{
			if (pThis->InsertAccept(pBuffer))
			{
				if (!pThis->PostAccept(pBuffer))
					pThis->DisplayErrorString("[监听线程]:投递Accept失败");
			}
			else
				pThis->DisplayErrorString("[监听线程]:插入Accept失败");
		}
		else
			pThis->DisplayErrorString("[监听线程]:申请Buffer失败");
	}

	//使用智能指针防止内存的泄露，不然管理起来麻烦
	shared_ptr<HANDLE> pShareEvent(new HANDLE[pThis->m_nThreadCount + 2]);
	//获取源指针才能够操作
	HANDLE* pEvent = pShareEvent.get();
	if (!pEvent)
	{
		//事件数组都失败了就不必要继续执行了
		pThis->DisplayErrorString("[监听线程]:申请事件数组失败");
		exit(0);
	}

	int nIndexEvent = 0;
	pEvent[nIndexEvent++] = pThis->m_hAcceptEvent;//0
	pEvent[nIndexEvent++] = pThis->m_hRepostEvent;//1

	for (int nIndex = 0; nIndex < pThis->m_nThreadCount; nIndex++)
	{
		pEvent[nIndexEvent++] = (HANDLE)_beginthreadex(0, 0, _WorkThread, pThis, 0, 0);
		if (!pEvent[nIndexEvent - 1])
		{
			//监听线程都失败就不必要继续了
			pThis->DisplayErrorString("[监听线程]:创建工作者线程失败");
			exit(0);
		}
	}

	while (true)
	{
		int nEvent = WSAWaitForMultipleEvents(nIndexEvent, pEvent, false, 60 * 1000, false);

		//线程执行失败或者要结束线程
		if (nEvent == WSA_WAIT_FAILED || pThis->m_bListenExit)
		{
			//关闭全部连接
			pThis->CloseConnects();
			Sleep(10);
			//关闭监听套接字
			closesocket(pThis->m_sock);
			pThis->m_sock = INVALID_SOCKET;
			Sleep(10);
			//通知工作中线程退出
			for (int nIndex = 0; nIndex < pThis->m_nThreadCount; nIndex++)
			{
				PostQueuedCompletionStatus(pThis->m_hIOComplete, -1, 0, 0);
			}
			//等待所有工作者线程的退出
			WaitForMultipleObjects(pThis->m_nThreadCount, &pEvent[2], true, 10 * 1000);
			for (int nIndex = 2; nIndex < (pThis->m_nThreadCount + 2); nIndex++)
			{
				CloseHandle(pEvent[nIndex]);
			}
			//关闭完成端口
			CloseHandle(pThis->m_hIOComplete);
			pThis->m_hIOComplete = 0;
			pThis->ReleaseFreeBuffer();
			pThis->ReleaseFreeContext();
			return 0;
		}

		//超时了就检测连接时间
		if (nEvent == WSA_WAIT_TIMEOUT)
		{
			CLock cLock(&pThis->m_stAcceptLock);
			int nTime;
			int nLen = sizeof(int);
			for (vector<PIOBuffer>::iterator it = pThis->m_vPostAccept.begin();
				it != pThis->m_vPostAccept.end(); it++)
			{
				getsockopt((*it)->sock, SOL_SOCKET, SO_CONNECT_TIME, (char*)&nTime, &nLen);
				if (nTime >= 2 * 60)
				{
					closesocket((*it)->sock);
					(*it)->sock = INVALID_SOCKET;
				}
			}
		}
		else
		{
			nEvent -= WAIT_OBJECT_0;
			int nCount = 0;
			if (nEvent == 0) //m_hAcceptEvent
			{
				WSANETWORKEVENTS stNetwork;
				WSAEnumNetworkEvents(pThis->m_sock, pEvent[nEvent], &stNetwork);
				if (stNetwork.lNetworkEvents & FD_ACCEPT)
					nCount = 20;
			}
			else if (nEvent == 1)//m_hRepostEvent
			{
				nCount = InterlockedExchange(&pThis->m_lRepostCount, 0);
			}
			else if (nEvent > 1)//发生错误
			{
				pThis->m_bListenExit = true;
				continue;
			}

			for (int nIndex = 0; nIndex < nCount; nIndex++)
			{
				//超过Accept请求最大值了
				if (pThis->m_vPostAccept.size() >= pThis->m_nMaxAccept)
					break;
				PIOBuffer pBuffer = pThis->AllocateBuffer();
				if (pBuffer)
				{
					if (pThis->InsertAccept(pBuffer))
					{
						if (!pThis->PostAccept(pBuffer))
							pThis->DisplayErrorString("[监听线程]:抛出Accept失败");
					}
					else
						pThis->DisplayErrorString("[监听线程]:插入Accept失败");
				}
				else
					pThis->DisplayErrorString("[监听线程]:申请Buffer失败");
			}
		}
	}
	return 0;
}

unsigned int _stdcall MaxSvr::_WorkThread(void* p)
{
	MaxSvr* pThis = (MaxSvr*)p;
	DWORD dwKey = 0;
	DWORD dwTrans = 0;
	LPOVERLAPPED pOverlapped = nullptr;
	while (true)
	{
		bool bHave = GetQueuedCompletionStatus(pThis->m_hIOComplete,
			&dwTrans, (LPDWORD)&dwKey, (LPOVERLAPPED*)&pOverlapped, WSA_INFINITE);

		if (dwTrans == -1)//通知退出
		{
			pThis->DisplayErrorString("接到通知，工作线程退出");
			return 0;
		}

		PIOBuffer pBuffer = CONTAINING_RECORD(pOverlapped, IOBuffer, stOverlapped);
		int nError = NO_ERROR;
		if (!bHave)//套接字发生错误
		{
			SOCKET sock = INVALID_SOCKET;
			if (pBuffer->eType == IO_ACCEPT)
				sock = pThis->m_sock;
			else//IO_RECV || IO_SEND
			{
				if (!dwKey)
				{
					pThis->DisplayErrorString("发生错误，工作线程退出");
					return -1;
				}
				sock = ((PIOContext)dwKey)->sock;
			}
			DWORD dwFlag = 0;
			if (!WSAGetOverlappedResult(sock, &pBuffer->stOverlapped, &dwTrans, false, &dwFlag))
				nError = WSAGetLastError();//获取重叠操作结果失败
		}
		pThis->HandleIOEvent(dwKey, pBuffer, dwTrans, nError);
	}
	return 0;
}

bool MaxSvr::CloseConnect(PIOContext pContext)
{
	//指针为空
	if (!pContext)
		return false;

	//客户数量为0
	if (!m_vConnectClient.size())
		return false;

	bool bHave = false;
	//将Context从客户列表里面删除
	CLock cLock(&m_stClientLock);
	for (vector<PIOContext>::iterator it = m_vConnectClient.begin();
		it != m_vConnectClient.end(); it++)
	{
		if (pContext == (*it))
		{
			m_vConnectClient.erase(it);
			bHave = true;
			break;
		}
	}
	cLock.UnLock();

	//将套接字删除
	CLock ccLock(&pContext->stLock);
	if (pContext->sock != INVALID_SOCKET)
	{
		closesocket(pContext->sock);
		pContext->sock = INVALID_SOCKET;
	}
	pContext->bClose = true;
	return bHave;
}

bool MaxSvr::SendBuffer(PIOContext pContext, const char* szBuffer)
{
	PIOBuffer pBuffer = AllocateBuffer();
	if (pBuffer)
	{
		int nLen = strlen(szBuffer);
		memcpy(pBuffer->szBuffer, szBuffer, nLen);
		return PostSend(pContext, pBuffer);
	}
	return false;
}

void MaxSvr::DisplayErrorString(const char* szInfo)
{
#if DEBUG | _DEBUG
	if(szInfo)
		MessageBoxA(0, szInfo, 0, MB_OK);
#endif
}

PIOBuffer MaxSvr::AllocateBuffer()
{
	PIOBuffer pBuffer = nullptr;
	bool bHave = false;

	CLock cLock(&m_stFreeBufferLock);
	if (m_vFreeBuffer.size())
	{
		pBuffer = m_vFreeBuffer.back();//拿出最后一个地址
		m_vFreeBuffer.pop_back();//删除
		bHave = true;//拿到地址了
	}
	cLock.UnLock();

	if (!bHave)//上面没有拿到地址，就申请
	{
		pBuffer = new IOBuffer;
		if (pBuffer)//成功申请到了内存
		{
			pBuffer->clear();
			pBuffer->szBuffer = new char[m_nBufferSize];//申请缓冲区内存
			if (pBuffer->szBuffer)
			{
				memset(pBuffer->szBuffer, 0, sizeof(char)*m_nBufferSize);
			}
			else
			{
				delete pBuffer;
				pBuffer = nullptr;
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

	CLock cLock(&m_stFreeBufferLock);
	bool bHave = false;
	if (m_vFreeBuffer.size() <= m_nMaxFreeBuffer)
	{
		//保存缓冲区指针
		char* szBuffer = pBuffer->szBuffer;
		//全部清空
		pBuffer->clear();
		memset(szBuffer, 0, sizeof(char)*m_nBufferSize);
		pBuffer->szBuffer = szBuffer;
		//放进容器
		m_vFreeBuffer.emplace_back(pBuffer);
		bHave = true;
	}
	cLock.UnLock();

	//如果没有放进容器就释放内存
	if (!bHave)
	{
		delete[] pBuffer->szBuffer;
		delete pBuffer;
		pBuffer = nullptr;
	}
}

void MaxSvr::ReleaseFreeBuffer()
{
	CLock cLock(&m_stFreeBufferLock);
	for (vector<PIOBuffer>::iterator it = m_vFreeBuffer.begin();
		it != m_vFreeBuffer.end(); it++)
	{
		delete[] (*it)->szBuffer;//释放字符串
		delete (*it);//释放内存
	}
	m_vFreeBuffer.clear();//清空
}

PIOContext MaxSvr::AllocateContext(SOCKET sock)
{
	//套接字都没有创建什么连接??
	if (sock == INVALID_SOCKET)
		return nullptr;

	PIOContext pContext = nullptr;
	bool bHave = false;
	//如果空闲链表里面有内存的话，直接拿来使用
	CLock cLock(&m_stFreeContextLock);
	int nSize = m_vFreeContext.size();
	if (nSize)
	{
		//先拿出，再删除
		pContext = m_vFreeContext[nSize - 1];
		m_vFreeContext.pop_back();
		bHave = true;
	}
	cLock.UnLock();

	//如果没拿到的话，直接申请
	if (!bHave)
	{
		pContext = new IOContext;
		if (pContext)
		{
			pContext->clear();
			InitializeCriticalSection(&pContext->stLock);
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

	//没有关闭就先关闭套接字
	if (pContext->sock != INVALID_SOCKET)
		closesocket(pContext->sock);

	bool bHave = false;

	//如果有乱序的Buffer，那就先释放
	CLock cLock(&pContext->stLock);
	for (vector<PIOBuffer>::iterator it = pContext->vOutOrderReadBuffer.begin();
		it != pContext->vOutOrderReadBuffer.end(); it++)
	{
		ReleaseBuffer((*it));
	}
	pContext->vOutOrderReadBuffer.clear();
	cLock.UnLock();

	CLock ccLock(&m_stFreeContextLock);
	cout << "地址 [" << pContext << "] 入栈:" << GetCurrentThreadId() << endl;
	if (m_vFreeContext.size() <= m_nMaxFreeContext)
	{
		//保存一下这个关键段
		CRITICAL_SECTION stLock = pContext->stLock;
		pContext->clear();
		pContext->stLock = stLock;
		m_vFreeContext.emplace_back(pContext);
		bHave = true;
	}

	if (!bHave)
	{
		DeleteCriticalSection(&pContext->stLock);//删除关键段
		delete pContext;
		pContext = nullptr;
	}
}

void MaxSvr::ReleaseFreeContext()
{
	CLock cLock(&m_stFreeContextLock);
	for (vector<PIOContext>::iterator it = m_vFreeContext.begin();
		it != m_vFreeContext.end(); it++)
	{
		DeleteCriticalSection(&(*it)->stLock);
		delete (*it);
	}
	m_vFreeContext.clear();//清空所有
}

bool MaxSvr::AddConnect(PIOContext pContext)
{
	if (!pContext)
		return false;

	bool bHave = false;
	CLock cLock(&m_stClientLock);
	if (m_vConnectClient.size() <= m_nMaxConnectClient)
	{
		m_vConnectClient.emplace_back(pContext);
		bHave = true;
	}
	return bHave;
}

bool MaxSvr::InsertAccept(PIOBuffer pBuffer)
{
	if (!pBuffer)
		return false;
	CLock cLock(&m_stAcceptLock);
	m_vPostAccept.emplace_back(pBuffer);
	return true;
}

bool MaxSvr::RemoveAccept(PIOBuffer pBuffer)
{
	bool bHave = false;
	CLock cLock(&m_stAcceptLock);;
	for (vector<PIOBuffer>::iterator it = m_vPostAccept.begin();
		it != m_vPostAccept.end(); it++) 
	{
		if (pBuffer == (*it))//指针地址的判断
		{
			m_vPostAccept.erase(it);
			bHave = true;
			break;
		}
	}
	return bHave;
}

PIOBuffer MaxSvr::GetNextReadBuffer(PIOContext pContext, PIOBuffer pBuffer)
{
	if (!pContext)
		return nullptr;

	if (pBuffer)
	{
		if (pBuffer->nId == pContext->nCurrentId)
			return pBuffer;

		pContext->vOutOrderReadBuffer.emplace_back(pBuffer);
	}

	PIOBuffer pRetBuffer = nullptr;
	for (vector<PIOBuffer>::iterator it = pContext->vOutOrderReadBuffer.begin();
		it != pContext->vOutOrderReadBuffer.end(); it++)
	{
		if ((*it)->nId == pContext->nCurrentId)
		{
			pRetBuffer = (*it);
			pContext->vOutOrderReadBuffer.erase(it);
			break;
		}
	}
	return pRetBuffer;
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

	bool bHave = m_fAcceptEx(m_sock,
		pBuffer->sock,
		pBuffer->szBuffer, 
		m_nBufferSize - ((sizeof(sockaddr_in) + 16) * 2),
		sizeof(sockaddr_in) + 16, 
		sizeof(sockaddr_in) + 16, 
		&dwByte, &pBuffer->stOverlapped);
	if (!bHave && WSAGetLastError() != WSA_IO_PENDING)
		return false;
	return true;
}

bool MaxSvr::PostSend(PIOContext pContext, PIOBuffer pBuffer)
{
	if (!pContext || !pBuffer)
		return false;

	if (pContext->nOutstandingSend >= m_nMaxSend)
		return false;

	if (pContext->sock == INVALID_SOCKET)
		return false;

	pBuffer->eType = IO_SEND;

	DWORD dwByte = 0;
	DWORD dwFlag = 0;
	WSABUF stBuf;
	stBuf.buf = pBuffer->szBuffer;
	stBuf.len = m_nBufferSize;

	int nRet = WSASend(pContext->sock, &stBuf, 1, &dwByte, dwFlag, &pBuffer->stOverlapped, 0);
	if (!nRet || WSAGetLastError() == WSA_IO_PENDING)
	{
		CLock cLock(&pContext->stLock);
		pContext->nOutstandingSend++;
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
	bool bHave = false;
	CLock cLock(&pContext->stLock);
	pBuffer->nId = pContext->nNextId;
	DWORD dwByte = 0;
	DWORD dwFalg = 0;
	WSABUF stBuf;
	stBuf.buf = pBuffer->szBuffer;
	stBuf.len = m_nBufferSize;
	int nRet = WSARecv(pContext->sock, &stBuf, 1, &dwByte, &dwFalg, &pBuffer->stOverlapped, 0);
	if (!nRet || WSAGetLastError() == WSA_IO_PENDING)
	{
		pContext->nOutstandingRecv++;
		pContext->nNextId++;
		bHave = true;
	}
	return bHave;
}

void MaxSvr::HandleIOEvent(DWORD dwKey, PIOBuffer pBuffer, DWORD dwTran, int nError)
{
	PIOContext pContext = (PIOContext)dwKey;

	//如果是Accept的话这里一定是NULL
	//因为是Accept的时候还没有进行完成端口关联
	if (pContext)
	{
		//相关的请求数量进行减少
		CLock cLock(&pContext->stLock);
		if (pBuffer->eType == IO_RECV)
			pContext->nOutstandingRecv--;
		else if (pBuffer->eType == IO_SEND)
			pContext->nOutstandingSend--;
		cLock.UnLock();

		if (pContext->bClose)//但是这个套接字被关闭了
		{
			if (!pContext->nOutstandingRecv && !pContext->nOutstandingSend)
			{
				cout << "---------------------------582释放的" << endl;
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
			OnConnectErro(pContext, pBuffer, nError);
			//关闭连接
			CloseConnect(pContext);
			if (!pContext->nOutstandingRecv && !pContext->nOutstandingSend)
			{
				cout << "604释放的" << endl;
				ReleaseContext(pContext);
			}
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
	default:
		DisplayErrorString("未知请求");
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
				LPSOCKADDR pLocalAddr = nullptr;
				LPSOCKADDR pRemoteAdd = nullptr;
				//获取地址信息
				m_fGetAcceptExSockaddrs(pBuffer->szBuffer,
					m_nBufferSize - ((sizeof(sockaddr_in) + 16) * 2),
					sizeof(sockaddr_in) + 16,
					sizeof(sockaddr_in) + 16,
					(sockaddr**)&pLocalAddr, &nLocalAddrLen,
					(sockaddr**)&pRemoteAdd, &nRemoteAddrLen);

				memcpy(&pNewClient->stLocalAddr, pLocalAddr, nLocalAddrLen);
				memcpy(&pNewClient->stRemoteAddr, pRemoteAdd, nRemoteAddrLen);
				//套接字关联到完成端口
				CreateIoCompletionPort((HANDLE)pNewClient->sock, m_hIOComplete,
					(DWORD)pNewClient, 0);
				//通知
				OnClientConnect(pNewClient, pBuffer);
				//投递几个RECV
				for (int nIndex = 0; nIndex < m_nInitRecv; nIndex++)
				{
					PIOBuffer pTemp = AllocateBuffer();
					if (pTemp)
					{
						if (!PostRecv(pNewClient, pTemp))
						{
							DisplayErrorString("[IO处理]:抛出Recv失败");
							CloseConnect(pNewClient);
						}
					}
					else
						DisplayErrorString("[IO处理]:申请Buffer失败");
				}
			}
			else
			{
				CloseConnect(pNewClient);
				cout << "//////////////////////////////691释放的" << endl;
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
	SetEvent(m_hRepostEvent);
}

void MaxSvr::HandleRecvEvent(DWORD dwTran, PIOContext pContext, PIOBuffer pBuffer)
{
	if (!dwTran)
	{
		OnClientClose(pContext, pBuffer);
		CloseConnect(pContext);
		//if (!pContext->nOutstandingRecv && !pContext->nOutstandingSend)
		//{
		//	cout << "(714释放的)" << endl;
		//	ReleaseContext(pContext);
		//}
		ReleaseBuffer(pBuffer);
	}
	else
	{
		PIOBuffer pRecv = GetNextReadBuffer(pContext, pBuffer);
		while (pRecv)
		{
			OnRecvFinish(pContext, pRecv);
			InterlockedIncrement((long*)&pContext->nCurrentId);
			ReleaseBuffer(pRecv);
			pRecv = GetNextReadBuffer(pContext, 0);
		}

		PIOBuffer pPostRecv = AllocateBuffer();
		if (pPostRecv)
		{
			if (!PostRecv(pContext, pPostRecv))
			{
				DisplayErrorString("[IO处理]:抛出Recv失败");
				CloseConnect(pContext);
			}
		}
		else
			DisplayErrorString("[IO处理]:申请Buffer失败");
	}
}

void MaxSvr::HandleSendEvent(DWORD dwTran, PIOContext pContext, PIOBuffer pBuffer)
{
	if (!dwTran)
	{
		OnClientClose(pContext, pBuffer);
		CloseConnect(pContext);
		//if (!pContext->nOutstandingRecv && !pContext->nOutstandingSend)
		//{
		//	cout << "752释放的" << endl;
		//	ReleaseContext(pContext);
		//}
		ReleaseBuffer(pBuffer);
	}
	else
	{
		OnSendFinish(pContext, pBuffer);
		ReleaseBuffer(pBuffer);
	}
}

MaxSvr::MaxSvr()
{
	InitializeCriticalSection(&m_stFreeBufferLock);
	m_nMaxFreeBuffer = 200;

	InitializeCriticalSection(&m_stFreeContextLock);
	m_nMaxFreeContext = 200;

	InitializeCriticalSection(&m_stAcceptLock);
	m_nMaxAccept = 200;

	InitializeCriticalSection(&m_stClientLock);
	m_nMaxConnectClient = 20000;

	m_hAcceptEvent = CreateEventA(0, false, false, 0);
	m_hRepostEvent = CreateEventA(0, false, false, 0);
	m_lRepostCount = 0;

	m_nThreadCount = 1;
	m_nBufferSize = 4 * 1024;
	m_nPort = 9999;
	m_nInitAccept = 5;
	m_nInitRecv = 5;
	m_nMaxSend = 100;

	m_hListen = 0;
	m_hIOComplete = 0;
	m_sock = INVALID_SOCKET;

	m_fAcceptEx = nullptr;
	m_fGetAcceptExSockaddrs = nullptr;

	m_bWorkStart = false;
	m_bListenExit = false;

	WSADATA data;
	if (WSAStartup(MAKEWORD(2, 2), &data))
	{
		DisplayErrorString("初始化网络函数失败");
		exit(0);
	}
}


MaxSvr::~MaxSvr()
{
	StopServer();
	WSACleanup();
	DeleteCriticalSection(&m_stFreeBufferLock);
	DeleteCriticalSection(&m_stFreeContextLock);
	DeleteCriticalSection(&m_stAcceptLock);
	DeleteCriticalSection(&m_stClientLock);
}

bool MaxSvr::StartServer()
{
	//防止重复启动服务
	if (m_bWorkStart)
		return false;

	m_sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
	if (m_sock == INVALID_SOCKET)
		return false;

	sockaddr_in stAddr;
	memset(&stAddr,0,sizeof(sockaddr_in));
	stAddr.sin_family = AF_INET;
	stAddr.sin_port = ntohs(m_nPort);
	stAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	if(bind(m_sock, (sockaddr*)&stAddr, sizeof(sockaddr_in)))
		return false;

	if (listen(m_sock, 200))
		return false;

	//创建完成端口
	m_hIOComplete = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	if (!m_hIOComplete)
		return false;

	//获取这两个网络函数
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

	CreateIoCompletionPort((HANDLE)m_sock, m_hIOComplete, (DWORD)0, 0);

	//注册网络事件
	if (WSAEventSelect(m_sock, m_hAcceptEvent, FD_ACCEPT))
		return false;

	//创建监听线程
	m_hListen = (HANDLE)_beginthreadex(0, 0, _ListenThread, this, 0, 0);
	if (!m_hListen)
		return false;

	m_bListenExit = false;
	m_bWorkStart = true;
	return true;
}

void MaxSvr::StopServer()
{
	if (!m_bWorkStart)
		return;

	m_bListenExit = true;//通知监听线程退出
	SetEvent(m_hAcceptEvent);

	//等待线程的退出
	WaitForSingleObject(m_hListen, INFINITE);
	CloseHandle(m_hListen);
	m_hListen = 0;

	m_bWorkStart = false;
}

void MaxSvr::CloseConnects()
{
	CLock cLock(&m_stClientLock);
	for (vector<PIOContext>::iterator it = m_vConnectClient.begin();
		it != m_vConnectClient.end(); it++)
	{
		CLock ccLock(&(*it)->stLock);
		if ((*it)->sock != INVALID_SOCKET)
		{
			closesocket((*it)->sock);
			(*it)->sock = INVALID_SOCKET;
		}
		(*it)->bClose = true;
	}
	m_vConnectClient.clear();//将所有的客户全部删除
}
