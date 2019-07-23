#include "CGServer.h"

cgsvr::CGServer::CGServer():MaxSvr()
{

}

cgsvr::CGServer::~CGServer()
{

}

void cgsvr::CGServer::OnEvent(
	EventType eType, void* pData, PIOBuffer pBuffer, PIOContext pContext)
{
	switch (eType)
	{
	case Event_Error:	//��������
		OnError((int)pData, pBuffer, pContext);
		break;
	case Event_Connect:	//�ͻ�����
		OnConnect(pBuffer, pContext);
		break;
	case Event_Close:	//�ͻ��ر�
		OnClose(pBuffer, pContext);
		break;
	case Event_Recv:	//��������
		OnRecv(pBuffer, pContext);
		break;
	case Event_Send:	//��������
		OnSend(pBuffer, pContext);
		break;
	}
}

bool cgsvr::CGServer::StartCGServer(int nPort)
{
	MaxSvr::SetPort(nPort);
	bool bState = MaxSvr::StartServer();
	if (bState)
		AddMessage("������", "�����������ɹ�");
	else
		AddMessage("������", "����������ʧ��");
	return bState;
}

bool cgsvr::CGServer::StopCGServer()
{
	MaxSvr::StopServer();
	AddMessage("������", "�رշ������ɹ�");
	return true;
}

void cgsvr::CGServer::OnError(int nError, PIOBuffer pBuffer, PIOContext pContext)
{
	if (m_hDlg)
	{

	}
}

void cgsvr::CGServer::OnConnect(PIOBuffer pBuffer, PIOContext pContext)
{

}

void cgsvr::CGServer::OnClose(PIOBuffer pBuffer, PIOContext pContext)
{

}

void cgsvr::CGServer::OnRecv(PIOBuffer pBuffer, PIOContext pContext)
{
	AddMessage(inet_ntoa(pContext->stRemoteAddr.sin_addr), (char*)pBuffer->pBuf);



}

void cgsvr::CGServer::OnSend(PIOBuffer pBuffer, PIOContext pContext)
{

}

void cgsvr::CGServer::AddMessage(const char* szSender,const char* szInfo)
{
	if (m_hDlg == 0)
		return;

	//��¼������������1000��ʱ����������
	static int nIndex = 0;
	if (nIndex >= 1000)
	{
		ListView_DeleteAllItems(GetDlgItem(m_hDlg, LIST_STATE));
	}

	LVITEMA stItem;
	memset(&stItem, 0, sizeof(LVITEMA));
	stItem.mask = LVIF_TEXT;
	stItem.iItem = 0;
	stItem.iSubItem = 0;
	stItem.pszText = (LPSTR)szSender;
	ListView_InsertItem(GetDlgItem(m_hDlg, LIST_STATE), &stItem);
	stItem.iSubItem = 1;
	stItem.pszText = (LPSTR)szInfo;
	ListView_SetItem(GetDlgItem(m_hDlg, LIST_STATE), &stItem);
}
