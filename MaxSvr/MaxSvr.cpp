#include "MaxSvr.h"



unsigned int _stdcall MaxSvr::_ListenThread(void* p)
{
	MaxSvr* pThis = (MaxSvr*)p;//���ת��

	//���ڼ����׽�������Ͷ�ݼ���Accept����
	for (int nIndex = 0; nIndex < pThis->m_nInitAccept; nIndex++)
	{
		PIOBuffer pBuffer = pThis->AllocateBuffer();
		if (pBuffer)
		{
			if (pThis->InsertAccept(pBuffer))
			{
				if (!pThis->PostAccept(pBuffer))
					pThis->DisplayErrorString("[�����߳�]:Ͷ��Acceptʧ��");
			}
			else
				pThis->DisplayErrorString("[�����߳�]:����Acceptʧ��");
		}
		else
			pThis->DisplayErrorString("[�����߳�]:����Bufferʧ��");
	}

	//ʹ������ָ���ֹ�ڴ��й¶����Ȼ���������鷳
	shared_ptr<HANDLE> pShareEvent(new HANDLE[pThis->m_nThreadCount + 2]);
	//��ȡԴָ����ܹ�����
	HANDLE* pEvent = pShareEvent.get();
	if (!pEvent)
	{
		//�¼����鶼ʧ���˾Ͳ���Ҫ����ִ����
		pThis->DisplayErrorString("[�����߳�]:�����¼�����ʧ��");
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
			//�����̶߳�ʧ�ܾͲ���Ҫ������
			pThis->DisplayErrorString("[�����߳�]:�����������߳�ʧ��");
			exit(0);
		}
	}

	while (true)
	{
		int nEvent = WSAWaitForMultipleEvents(nIndexEvent, pEvent, false, 60 * 1000, false);

		//�߳�ִ��ʧ�ܻ���Ҫ�����߳�
		if (nEvent == WSA_WAIT_FAILED || pThis->m_bListenExit)
		{
			//�ر�ȫ������
			pThis->CloseConnects();
			Sleep(10);
			//�رռ����׽���
			closesocket(pThis->m_sock);
			pThis->m_sock = INVALID_SOCKET;
			Sleep(10);
			//֪ͨ�������߳��˳�
			for (int nIndex = 0; nIndex < pThis->m_nThreadCount; nIndex++)
			{
				PostQueuedCompletionStatus(pThis->m_hIOComplete, -1, 0, 0);
			}
			//�ȴ����й������̵߳��˳�
			WaitForMultipleObjects(pThis->m_nThreadCount, &pEvent[2], true, 10 * 1000);
			for (int nIndex = 2; nIndex < pThis->m_nThreadCount + 2; nIndex++)
			{
				CloseHandle(pEvent[nIndex]);
			}
			//�ر���ɶ˿�
			CloseHandle(pThis->m_hIOComplete);
			pThis->m_hIOComplete = 0;
			pThis->ReleaseFreeBuffer();
			pThis->ReleaseFreeContext();
			return 0;
		}

		//��ʱ�˾ͼ������ʱ��
		if (nEvent == WSA_WAIT_TIMEOUT)
		{
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
			else if (nEvent > 1)//��������
			{
				pThis->m_bListenExit = true;
				continue;
			}

			for (int nIndex = 0; nIndex < nCount; nIndex++)
			{
				//����Accept�������ֵ��
				if (pThis->m_vPostAccept.size() >= pThis->m_nMaxAccept)
					break;
				PIOBuffer pBuffer = pThis->AllocateBuffer();
				if (pBuffer)
				{
					if (pThis->InsertAccept(pBuffer))
					{
						if (!pThis->PostAccept(pBuffer))
							pThis->DisplayErrorString("[�����߳�]:�׳�Acceptʧ��");
					}
					else
						pThis->DisplayErrorString("[�����߳�]:����Acceptʧ��");
				}
				else
					pThis->DisplayErrorString("[�����߳�]:����Bufferʧ��");
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

		if (dwTrans == -1)//֪ͨ�˳�
			return 0;

		PIOBuffer pBuffer = CONTAINING_RECORD(pOverlapped, IOBuffer, stOverlapped);
		int nError = NO_ERROR;
		if (!bHave)//�׽��ַ�������
		{
			SOCKET sock = INVALID_SOCKET;
			if (pBuffer->eType == IO_ACCEPT)
				sock = pThis->m_sock;
			else//IO_RECV || IO_SEND
			{
				if (!dwKey)
					return -1;
				sock = ((PIOContext)dwKey)->sock;
			}
			DWORD dwFlag = 0;
			if (!WSAGetOverlappedResult(sock, &pBuffer->stOverlapped, &dwTrans, false, &dwFlag))
				nError = WSAGetLastError();//��ȡ�ص��������ʧ��
		}
		pThis->HandleIOEvent(dwKey, pBuffer, dwTrans, nError);
	}
	return 0;
}

bool MaxSvr::CloseConnect(PIOContext pContext)
{
	if (!pContext)
		return false;

	bool bHave = false;
	//��Context�ӿͻ��б�����ɾ��
	EnterCriticalSection(&m_stClientLock);
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
	LeaveCriticalSection(&m_stClientLock);
	//���׽���ɾ��
	EnterCriticalSection(&pContext->stLock);
	if (pContext->sock != INVALID_SOCKET)
	{
		closesocket(pContext->sock);
		pContext->sock = INVALID_SOCKET;
	}
	pContext->bClose = true;
	LeaveCriticalSection(&pContext->stLock);
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

	::EnterCriticalSection(&m_stBufferLock);
	if (m_vFreeBuffer.size())
	{
		pBuffer = m_vFreeBuffer.back();//�ó����һ����ַ
		m_vFreeBuffer.pop_back();//ɾ��
		bHave = true;//�õ���ַ��
	}
	::LeaveCriticalSection(&m_stBufferLock);

	if (!bHave)//����û���õ���ַ��������
	{
		pBuffer = new IOBuffer;
		if (pBuffer)//�ɹ����뵽���ڴ�
		{
			memset(pBuffer, 0, sizeof(IOBuffer));
			pBuffer->szBuffer = new char[m_nBufferSize];//���뻺�����ڴ�
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
	//��ָ��Ļ�
	if (!pBuffer)
		return;

	bool bHave = false;

	EnterCriticalSection(&m_stBufferLock);
	if (m_vFreeBuffer.size() <= m_nMaxFreeBuffer)
	{
		//���滺����ָ��
		char* szBuffer = pBuffer->szBuffer;
		//ȫ�����
		memset(pBuffer, 0, sizeof(IOBuffer));
		memset(szBuffer, 0, sizeof(char)*m_nBufferSize);
		pBuffer->szBuffer = szBuffer;
		pBuffer->sock = INVALID_SOCKET;
		//�Ž�����
		m_vFreeBuffer.emplace_back(pBuffer);
		bHave = true;

	}
	LeaveCriticalSection(&m_stBufferLock);

	//���û�зŽ��������ͷ��ڴ�
	if (!bHave)
	{
		delete[] pBuffer->szBuffer;
		delete pBuffer;
	}
}

void MaxSvr::ReleaseFreeBuffer()
{
	EnterCriticalSection(&m_stBufferLock);
	for (vector<PIOBuffer>::iterator it=m_vFreeBuffer.begin();
		it!=m_vFreeBuffer.end();it++)
	{
		delete[](*it)->szBuffer;
		delete (*it);
	}
	m_vFreeBuffer.clear();
	LeaveCriticalSection(&m_stBufferLock);
}

PIOContext MaxSvr::AllocateContext(SOCKET sock)
{
	if (sock == INVALID_SOCKET)
		return nullptr;

	PIOContext pContext = nullptr;
	bool bHave = false;
	EnterCriticalSection(&m_stContextLock);
	if (m_vFreeContext.size())
	{
		pContext = m_vFreeContext.back();
		m_vFreeContext.pop_back();
		bHave = true;
	}
	LeaveCriticalSection(&m_stContextLock);

	if (!bHave)
	{
		pContext = new IOContext;
		if (pContext)
		{
			memset(pContext, 0, sizeof(IOContext));
			InitializeCriticalSection(&pContext->stLock);
		}
	}

	if (pContext)
		pContext->sock = sock;

	return pContext;
}

void MaxSvr::ReleaseContext(PIOContext pContext)
{
	if (!pContext)
		return;

	if (pContext->sock != INVALID_SOCKET)
		closesocket(pContext->sock);
	bool bHave = false;

	//����������Buffer���Ǿ����ͷ�
	for (vector<PIOBuffer>::iterator it = pContext->vOutOrderReadBuffer.begin();
		it != pContext->vOutOrderReadBuffer.end(); it++)
	{
		ReleaseBuffer((*it));
	}
	pContext->vOutOrderReadBuffer.clear();

	EnterCriticalSection(&m_stContextLock);
	if (m_vFreeContext.size() <= m_nMaxFreeContext)
	{
		//����һ������ؼ���
		CRITICAL_SECTION stLock = pContext->stLock;
		memset(pContext, 0, sizeof(IOContext));
		pContext->stLock = stLock;
		m_vFreeContext.emplace_back(pContext);
		bHave = true;
	}
	LeaveCriticalSection(&m_stContextLock);

	if (!bHave)
	{
		DeleteCriticalSection(&pContext->stLock);//ɾ���ؼ���
		delete pContext;
	}

}

void MaxSvr::ReleaseFreeContext()
{
	EnterCriticalSection(&m_stContextLock);
	for (vector<PIOContext>::iterator it = m_vFreeContext.begin();
		it != m_vFreeContext.end(); it++)
	{
		DeleteCriticalSection(&(*it)->stLock);
		delete (*it);
	}
	m_vFreeContext.clear();
	LeaveCriticalSection(&m_stContextLock);
}

bool MaxSvr::AddConnect(PIOContext pContext)
{
	if (!pContext)
		return false;

	bool bHave = false;
	EnterCriticalSection(&m_stClientLock);
	if (m_vConnectClient.size() <= m_nMaxConnectClient)
	{
		m_vConnectClient.emplace_back(pContext);
		bHave = true;
	}
	LeaveCriticalSection(&m_stClientLock);

	return bHave;
}

bool MaxSvr::InsertAccept(PIOBuffer pBuffer)
{
	if (!pBuffer)
		return false;

	EnterCriticalSection(&m_stAcceptLock);
	m_vPostAccept.emplace_back(pBuffer);
	LeaveCriticalSection(&m_stAcceptLock);

	return true;
}

bool MaxSvr::RemoveAccept(PIOBuffer pBuffer)
{
	bool bHave = false;
	EnterCriticalSection(&m_stAcceptLock);
	for (vector<PIOBuffer>::iterator it = m_vPostAccept.begin();
		it != m_vPostAccept.end(); it++) 
	{
		if (pBuffer == (*it))//ָ���ַ���ж�
		{
			m_vPostAccept.erase(it);
			bHave = true;
			break;
		}
	}
	LeaveCriticalSection(&m_stAcceptLock);
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
	pBuffer->sock = WSASocketA(AF_INET, SOCK_STREAM, 0, 0, 0, WSA_FLAG_OVERLAPPED);
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
		EnterCriticalSection(&pContext->stLock);
		pContext->nOutstandingSend++;
		LeaveCriticalSection(&pContext->stLock);
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
	EnterCriticalSection(&pContext->stLock);
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
	LeaveCriticalSection(&pContext->stLock);
	return bHave;
}

void MaxSvr::HandleIOEvent(DWORD dwKey, PIOBuffer pBuffer, DWORD dwTran, int nError)
{
	PIOContext pContext = (PIOContext)dwKey;

	if (pContext)
	{
		EnterCriticalSection(&pContext->stLock);
		if (pBuffer->eType == IO_RECV)
			pContext->nOutstandingRecv--;
		else if (pBuffer->eType == IO_SEND)
			pContext->nOutstandingSend--;
		LeaveCriticalSection(&pContext->stLock);
		if (pContext->bClose)//��������׽��ֱ��ر���
		{
			if (!pContext->nOutstandingRecv && !pContext->nOutstandingSend)
				ReleaseContext(pContext);
			ReleaseBuffer(pBuffer);
			return;
		}
	}
	else
		RemoveAccept(pBuffer);

	if (nError != NO_ERROR)//��������
	{
		if (pBuffer->eType != IO_ACCEPT)//���ǳ���
		{
			OnConnectErro(pContext, pBuffer, nError);
			CloseConnect(pContext);
			if (!pContext->nOutstandingRecv && !pContext->nOutstandingSend)
				ReleaseContext(pContext);
		}
		else//�ͻ�����
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

	if (pBuffer->eType == IO_ACCEPT)
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

					m_fGetAcceptExSockaddrs(pBuffer->szBuffer,
						m_nBufferSize - ((sizeof(sockaddr_in) + 16) * 2),
						sizeof(sockaddr_in) + 16,
						sizeof(sockaddr_in) + 16,
						(sockaddr**)&pLocalAddr, &nLocalAddrLen,
						(sockaddr**)&pRemoteAdd, &nRemoteAddrLen);

					memcpy(&pNewClient->stLocalAddr, pLocalAddr, nLocalAddrLen);
					memcpy(&pNewClient->stRemoteAddr, pRemoteAdd, nRemoteAddrLen);

					CreateIoCompletionPort((HANDLE)pNewClient->sock, m_hIOComplete, (DWORD)pNewClient, 0);
					OnClientConnect(pNewClient, pBuffer);

					for (int nIndex = 0; nIndex < m_nInitRecv; nIndex++)
					{
						PIOBuffer pTemp = AllocateBuffer();
						if (pTemp)
						{
							if (!PostRecv(pNewClient, pTemp))
							{
								DisplayErrorString("[IO����]:�׳�Recvʧ��");
								CloseConnect(pNewClient);
							}
						}
						else
							DisplayErrorString("[IO����]:����Bufferʧ��");
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
		SetEvent(m_hRepostEvent);
	}
	else if (pBuffer->eType == IO_RECV)
	{
		if (!dwTran)
		{
			OnClientClose(pContext, pBuffer);
			CloseConnect(pContext);
			if (!pContext->nOutstandingRecv && !pContext->nOutstandingSend)
				ReleaseContext(pContext);
			ReleaseBuffer(pBuffer);
		}
		else
		{
			PIOBuffer pTemp = GetNextReadBuffer(pContext, pBuffer);
			while (pTemp)
			{
				OnRecvFinish(pContext, pBuffer);
				InterlockedIncrement((long*)&pContext->nCurrentId);
				ReleaseBuffer(pTemp);
				pTemp = GetNextReadBuffer(pContext, 0);
			}
			pTemp = AllocateBuffer();
			if (pTemp)
			{
				if (!PostRecv(pContext, pTemp))
				{
					DisplayErrorString("[IO����]:�׳�Recvʧ��");
					CloseConnect(pContext);
				}
			}
			else
				DisplayErrorString("[IO����]:����Bufferʧ��");
		}
	}
	else if (pBuffer->eType == IO_SEND)
	{
		if (!dwTran)
		{
			OnClientClose(pContext, pBuffer);
			CloseConnect(pContext);
			if (!pContext->nOutstandingRecv && !pContext->nOutstandingSend)
				ReleaseContext(pContext);
			ReleaseBuffer(pBuffer);
		}
		else
		{
			OnSendFinish(pContext, pBuffer);
			ReleaseBuffer(pBuffer);
		}
	}
}

void MaxSvr::OnClientConnect(PIOContext pContext, PIOBuffer pBuffer)
{
	cout << "[�ͻ���������]: " << inet_ntoa(pContext->stRemoteAddr.sin_addr) << endl;
	SendBuffer(pContext, "Welcode join IOCP Server");
}

void MaxSvr::OnClientClose(PIOContext pContext, PIOBuffer pBuffer)
{
	cout << "[�ͻ��ر�����]: " << inet_ntoa(pContext->stRemoteAddr.sin_addr) << endl;
}

void MaxSvr::OnConnectErro(PIOContext pContext, PIOBuffer pBuffer,int nError)
{
	cout << "[���ӷ�������]: " << inet_ntoa(pContext->stRemoteAddr.sin_addr) << ": " << nError << endl;
}

void MaxSvr::OnSendFinish(PIOContext pContext, PIOBuffer pBuffer)
{
	cout << "[���Ͳ������]: " << inet_ntoa(pContext->stRemoteAddr.sin_addr) << ": " << pBuffer->szBuffer << endl;
}

void MaxSvr::OnRecvFinish(PIOContext pContext, PIOBuffer pBuffer)
{
	cout << "[���ղ������]: " << inet_ntoa(pContext->stRemoteAddr.sin_addr) << ": " << pBuffer->szBuffer << endl;
}

MaxSvr::MaxSvr()
{
	InitializeCriticalSection(&m_stBufferLock);
	m_nMaxFreeBuffer = 200;

	InitializeCriticalSection(&m_stContextLock);
	m_nMaxFreeContext = 200;

	InitializeCriticalSection(&m_stAcceptLock);
	m_nMaxAccept = 200;

	InitializeCriticalSection(&m_stClientLock);
	m_nMaxConnectClient = 20000;

	m_hAcceptEvent = CreateEventA(0, false, false, 0);
	m_hRepostEvent = CreateEventA(0, false, false, 0);
	m_lRepostCount = 0;

	m_nThreadCount = 4;
	m_nBufferSize = 4 * 1024;
	m_nPort = 9999;
	m_nInitAccept = 10;
	m_nInitRecv = 10;
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
		DisplayErrorString("��ʼ�����纯��ʧ��");
		exit(0);
	}
}


MaxSvr::~MaxSvr()
{
	StopServer();
	ReleaseFreeBuffer();
	ReleaseFreeContext();
	WSACleanup();
}

bool MaxSvr::StartServer()
{
	//��ֹ�ظ���������
	if (m_bWorkStart)
		return false;

	if (m_sock != INVALID_SOCKET)
		closesocket(m_sock);

	m_sock = WSASocketA(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
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

	if (m_hIOComplete)
		CloseHandle(m_hIOComplete);

	//������ɶ˿�
	m_hIOComplete = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	if (!m_hIOComplete)
		return false;

	//��ȡ���������纯��
	DWORD dwByte;
	GUID GAcceptEx = WSAID_ACCEPTEX;
	WSAIoctl(m_sock, SIO_GET_EXTENSION_FUNCTION_POINTER, &GAcceptEx, sizeof(GAcceptEx),
		&m_fAcceptEx, sizeof(m_fAcceptEx), &dwByte, 0, 0);
	if (!dwByte)
		return false;

	GUID GGetAcceptSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
	WSAIoctl(m_sock, SIO_GET_EXTENSION_FUNCTION_POINTER, &GGetAcceptSockaddrs, sizeof(GGetAcceptSockaddrs),
		&m_fGetAcceptExSockaddrs, sizeof(m_fGetAcceptExSockaddrs), &dwByte, 0, 0);
	if (!dwByte)
		return false;

	CreateIoCompletionPort((HANDLE)m_sock, m_hIOComplete, (DWORD)0, 0);

	//ע�������¼�
	if (WSAEventSelect(m_sock, m_hAcceptEvent, FD_ACCEPT))
		return false;
	
	if (m_hListen)
		CloseHandle(m_hListen);

	//���������߳�
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

	m_bListenExit = true;//֪ͨ�����߳��˳�
	SetEvent(m_hAcceptEvent);

	//�ȴ��̵߳��˳�
	WaitForSingleObject(m_hListen, INFINITE);
	CloseHandle(m_hListen);
	m_hListen = 0;

	m_bWorkStart = false;
}

void MaxSvr::CloseConnects()
{
	EnterCriticalSection(&m_stClientLock);
	for(vector<PIOContext>::iterator it=m_vConnectClient.begin();
		it!=m_vConnectClient.end();it++)
	{
		EnterCriticalSection(&(*it)->stLock);
		if ((*it)->sock != INVALID_SOCKET)
		{
			closesocket((*it)->sock);
			(*it)->sock = INVALID_SOCKET;
		}
		(*it)->bClose = true;
		LeaveCriticalSection(&(*it)->stLock);
	}
	m_vConnectClient.clear();//�����еĿͻ�ȫ��ɾ��
	LeaveCriticalSection(&m_stClientLock);
}
