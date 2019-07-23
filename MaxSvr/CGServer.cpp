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
	case Event_Error:	//发生错误
		OnError((int)pData, pBuffer, pContext);
		break;
	case Event_Connect:	//客户连接
		OnConnect(pBuffer, pContext);
		break;
	case Event_Close:	//客户关闭
		OnClose(pBuffer, pContext);
		break;
	case Event_Recv:	//接收数据
		OnRecv(pBuffer, pContext);
		break;
	case Event_Send:	//发生数据
		OnSend(pBuffer, pContext);
		break;
	}
}

bool cgsvr::CGServer::StartCGServer(int nPort)
{
	MaxSvr::SetPort(nPort);
	bool bState = MaxSvr::StartServer();
	if (bState)
		AddMessage("服务器", "启动服务器成功");
	else
		AddMessage("服务器", "启动服务器失败");
	return bState;
}

bool cgsvr::CGServer::StopCGServer()
{
	MaxSvr::StopServer();
	AddMessage("服务器", "关闭服务器成功");
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

	//记录索引，当超过1000的时候就清空再来
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
