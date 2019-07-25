#include "MaxSvr.h"
#pragma warning(disable:4018)

void MaxSvr::_ListenThread(MaxSvr* pThis)
{
	//�ڼ����׽�����Ͷ��Accept����
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

	//�����������߳�
	for (int nIndex = 0; nIndex < pThis->m_nHandleThreadCount; nIndex++)
	{
		std::thread cThread(_WorkerThread, pThis);
		cThread.detach();
	}

	while (true)
	{
		//һ���Ӽ��һ��
		int nEvent = WSAWaitForMultipleEvents(2, pThis->m_hEventArray, 
			false, 60 * 1000, false);

		//�߳�ִ��ʧ�ܻ���Ҫ�����߳�
		if (nEvent == WSA_WAIT_FAILED || pThis->m_bListenExit)
		{
			//֪ͨ�����߳��˳�
			for (int nIndex = 0; nIndex < pThis->m_nHandleThreadCount; nIndex++)
			{
				PostQueuedCompletionStatus(pThis->m_hIOComplete, -1, 0, 0);
			}
			//�ر�ȫ������
			pThis->CloseAllClientConnect();
			//�رռ����׽���
			closesocket(pThis->m_sock);
			pThis->m_sock = INVALID_SOCKET;
			//�ر���ɶ˿�
			CloseHandle(pThis->m_hIOComplete);
			pThis->m_hIOComplete = 0;
			//�ͷſ����ڴ�
			pThis->ReleaseFreeBuffer();
			pThis->ReleaseFreeContext();
			//�˳������߳�
			return;
		}

		//��ʱ�˾ͼ������ʱ��
		if (nEvent == WSA_WAIT_TIMEOUT)
		{
			int nTime = 0, nLen = sizeof(int);
			pThis->m_cMutexBufferAccept.lock();
			for (std::list<PIOBuffer>::iterator it = pThis->m_cListBufferAccept.begin();
				it != pThis->m_cListBufferAccept.end(); it++)
			{
				//��ȡ����ʱ��	����ʱ���˾�����
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
				//��m_lRepostCount���ظ�nCount��m_lRepostCount����Ϊ0
				nCount = InterlockedExchange(&pThis->m_lRepostCount, 0);
			}
			else if (nEvent > 1)//��������
			{
				pThis->m_bListenExit = true;
				continue;
			}

			int nMaxListBufferAccept = pThis->m_cListBufferAccept.size();
			for (int nIndex = 0; nIndex < nCount; nIndex++, nMaxListBufferAccept++)
			{
				//����Accept�������ֵ��
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
		//�ȴ�IO�¼�
		bool bState = GetQueuedCompletionStatus(pThis->m_hIOComplete,
			&dwTrans, (LPDWORD)&dwKey, (LPOVERLAPPED*)&pOverlapped, WSA_INFINITE);

		//֪ͨ�˳������߳�
		if (dwTrans == -1 || pThis->m_hIOComplete == 0)
			return;

		//��ȡBuffer�ṹ
		PIOBuffer pBuffer = CONTAINING_RECORD(pOverlapped, IOBuffer, stOverlapped);
		int nError = NO_ERROR;
		if (bState==false)//�׽��ַ�������
		{
			SOCKET sock = INVALID_SOCKET;
			if (pBuffer->eType == IO_ACCEPT)//�������ļ����׽���
				sock = pThis->m_sock;
			else//IO_RECV || IO_SEND
			{
				if (!dwKey)//Context�ṹΪ��
					return;
				sock = ((PIOContext)dwKey)->sock;
			}
			DWORD dwFlag = 0;
			if (!WSAGetOverlappedResult(sock, 
				&pBuffer->stOverlapped,&dwTrans, false, &dwFlag))
				nError = WSAGetLastError();//��ȡ�ص��������ʧ��
		}
		pThis->HandleIOEvent(dwKey, pBuffer, dwTrans, nError);
	}
}

bool MaxSvr::CloseConnect(PIOContext pContext)
{
	//ָ��Ϊ��
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

	//���׽���ɾ��
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

	//������������������ڴ�Ļ�
	m_cMutexBufferFree.lock();
	if (!m_cListBufferFree.empty())
	{
		pBuffer = m_cListBufferFree.back();//�ó����һ����ַ
		m_cListBufferFree.pop_back();//ɾ�����һ����ַ
		bState = true;//�õ���ַ��
	}
	m_cMutexBufferFree.unlock();

	if (!bState)//����û���õ���ַ��������
	{
		pBuffer = new IOBuffer;
		if (pBuffer)//�ɹ����뵽���ڴ�
		{
			memset(pBuffer, 0, sizeof(IOBuffer));
			pBuffer->pBuf = new byte[m_nBufferSize];//���뻺�����ڴ�
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
	//��ָ��Ļ�
	if (!pBuffer)
		return;

	bool bState = false;
	//��û���ͷŽ�����
	m_cMutexBufferFree.lock();
	if (m_cListBufferFree.size() <= m_nMaxBufferFree)
	{
		//���滺����ָ��
		void* pData = pBuffer->pBuf;
		memset(pBuffer, 0, sizeof(IOBuffer));
		memset(pData, 0, m_nBufferSize);
		pBuffer->pBuf = pData;
		//�Ž�����
		m_cListBufferFree.emplace_back(pBuffer);
		bState = true;
	}
	m_cMutexBufferFree.unlock();

	//���û�зŽ��������ͷ��ڴ�
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
		delete[] (*it)->pBuf;//�ͷ��ַ���
		delete (*it);//�ͷ��ڴ�
	}
	m_cListBufferFree.clear();
	m_cMutexBufferFree.unlock();
}

PIOContext MaxSvr::AllocateContext(SOCKET sock)
{
	//�׽��ֶ�û�д���ʲô����??
	if (sock == INVALID_SOCKET)
		return 0;

	PIOContext pContext = 0;
	bool bState = false;
	//������������������ڴ�Ļ���ֱ������ʹ��
	m_cMutexContextFree.lock();
	if (!m_cListContextFree.empty())
	{
		//���ó�����ɾ��
		pContext = m_cListContextFree.back();
		m_cListContextFree.pop_back();
		bState = true;
	}
	m_cMutexContextFree.unlock();

	//���û�õ��Ļ���ֱ������
	if (!bState)
	{
		pContext = new IOContext;
		if (pContext)
		{
			pContext->nOutstandingRecv = pContext->nOutstandingSend = 0;
		}
	}

	//�õ��ڴ��������ɹ��Ļ�
	if (pContext)
		pContext->sock = sock;
	return pContext;
}

void MaxSvr::ReleaseContext(PIOContext pContext)
{
	//����Ϊ0
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
		if (pBuffer == (*it))//ָ���ַ���ж�
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

	/*�����Accept�Ļ�����һ����NULL
	��Ϊ��Accept��ʱ��û�н�����ɶ˿ڹ���*/
	if (pContext)
	{
		pContext->cMutex.lock();
		if (pBuffer->eType == IO_RECV)
			pContext->nOutstandingRecv--;
		else if (pBuffer->eType == IO_SEND)
			pContext->nOutstandingSend--;
		pContext->cMutex.unlock();

		if (pContext->bClose)//����׽��ֱ��ر���
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

	if (nError)//��������
	{
		if (pBuffer->eType != IO_ACCEPT)//���ǳ���
		{
			//֪ͨ�������� 
			//10054���������Զ������ǿ�ȹر���һ�����е����ӡ� 
			//Ҳ����һ�����ӱ�ǿ�ƹرն���
			OnEvent(Event_Error, (void*)nError, pBuffer, pContext);
			//�ر�����
			CloseConnect(pContext);
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

				//��ȡ��ַ��Ϣ
				m_fGetAcceptExSockaddrs(pBuffer->pBuf,
					m_nBufferSize - ((sizeof(sockaddr_in) + 16) * 2),
					sizeof(sockaddr_in) + 16,
					sizeof(sockaddr_in) + 16,
					(sockaddr**)&pLocalAddr, &nLocalAddrLen,
					(sockaddr**)&pRemoteAdd, &nRemoteAddrLen);

				//�����ַ��Ϣ
				memcpy(&pNewClient->stLocalAddr, pLocalAddr, nLocalAddrLen);
				memcpy(&pNewClient->stRemoteAddr, pRemoteAdd, nRemoteAddrLen);

				//�׽��ֹ�������ɶ˿�
				CreateIoCompletionPort((HANDLE)pNewClient->sock, m_hIOComplete,
					(DWORD)pNewClient, 0);

				//֪ͨ
				OnEvent(Event_Connect, 0, pBuffer, pNewClient);

				//Ͷ�ݼ���Recv
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
	//Ĭ�����Buffer����
	m_nMaxBufferFree = 200;
	//Ĭ�����Context����
	m_nMaxContextFree = 200;
	//Ĭ�����Ͷ��Accept
	m_nMaxBufferAccept = 200;
	//Ĭ�����ͻ�Context
	m_nMaxContextClient = 20000;

	m_hEventArray[0] = CreateEventA(0, false, false, 0);	//Accept�¼�
	m_hEventArray[1] = CreateEventA(0, false, false, 0);	//Repost�¼�
	m_lRepostCount = 0;										//����Recv������

	m_nHandleThreadCount = 1;			//�����߳�����
	m_nBufferSize = 4 * 1024;			//��������С
	m_nPort = 9999;						//�����˿�
	m_nTimerOut = 2 * 60;				//���ӳ�ʱʱ��

	m_nInitAcceptCount = 5;				//��ʼ��Accept����
	m_nInitRecvCount = 5;				//��ʼ��Recv����

	m_hIOComplete = 0;					//IO��ɶ˿�
	m_sock = INVALID_SOCKET;			//�׽���

	m_fAcceptEx = 0;					//AcceptEx
	m_fGetAcceptExSockaddrs = 0;		//GetAcceptExSockaddrs

	m_bWorkStart = false;				//�����߳�û�п�ʼ����
	m_bListenExit = true;				//�����߳��˳���

	WSADATA data;						//��ʼ�����纯��
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
	//��ֹ�ظ�����
	if (m_bWorkStart)
		return false;

	//�����׽���
	m_sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
	if (m_sock == INVALID_SOCKET)
		return false;

	//���׽���
	sockaddr_in stAddr;
	memset(&stAddr,0,sizeof(sockaddr_in));
	stAddr.sin_family = AF_INET;
	stAddr.sin_port = ntohs(m_nPort);
	stAddr.sin_addr.S_un.S_addr = INADDR_ANY;
	if (bind(m_sock, (sockaddr*)&stAddr, sizeof(sockaddr_in)) == SOCKET_ERROR)
		return false;

	//�����׽���
	if (listen(m_sock, 200) == SOCKET_ERROR)
		return false;

	//����IO��ɶ˿�
	m_hIOComplete = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
	if (!m_hIOComplete)
		return false;

	//��ȡ���纯��
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

	//�������׽�����IO��ɶ˿ڰ�
	CreateIoCompletionPort((HANDLE)m_sock, m_hIOComplete, 0, 0);

	//ע�������¼�
	if (WSAEventSelect(m_sock, m_hEventArray[1], FD_ACCEPT) == SOCKET_ERROR)
		return false;

	//���������߳�
	std::thread cThread(_ListenThread,this);
	cThread.detach();

	m_bListenExit = false;	//�����߳�û���˳�
	m_bWorkStart = true;	//�����߳̿�ʼ����
	return true;
}

bool MaxSvr::StopServer()
{
	if (!m_bWorkStart)
		return false;

	//֪ͨ�����߳��˳�
	m_bListenExit = true;
	SetEvent(m_hEventArray[1]);
	//�����߳�û�й���
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
