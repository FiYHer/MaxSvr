
#include "MaxSvr.h"

class MyClass : public MaxSvr
{
public:
	MyClass():MaxSvr()
	{

	}
	~MyClass()
	{

	}
	virtual void OnEvent(EventType eType,void* pData,
		PIOBuffer pBuffer, PIOContext pContext)
	{
		switch (eType)
		{
		case Event_Error:
			printf("[��������] IP:%s - Sock:%d - Count:%d - Error:%d\n",
				inet_ntoa(pContext->stRemoteAddr.sin_addr), pContext->sock,
				GetConnectCount(), (int)pData);
			break;
		case Event_Connect:
			printf("[�ͻ�����] IP:%s - Sock:%d - Count:%d \n",
				inet_ntoa(pContext->stRemoteAddr.sin_addr), pContext->sock,
				GetConnectCount());
			break;
		case Event_Close:
			printf("[�ͻ��˳�] IP:%s - Sock:%d - Count:%d \n",
				inet_ntoa(pContext->stRemoteAddr.sin_addr), pContext->sock,
				GetConnectCount());
			break;
		case Event_Recv:
			printf("[��������] Buffer:%s - IP:%s - Sock:%d - Count:%d \n",
				(char*)pBuffer->pBuf, inet_ntoa(pContext->stRemoteAddr.sin_addr),
				pContext->sock, GetConnectCount());
			break;
		case Event_Send:
			printf("[��������] Buffer:%s - IP:%s - Sock:%d \n", (char*)pBuffer->pBuf,
				inet_ntoa(pContext->stRemoteAddr.sin_addr), pContext->sock);
			break;
		}
	}
};

int main(_In_ int argc,
	_In_reads_(argc) _Pre_z_ char** argv,
	_In_z_ char** envp)
{
	MyClass Test;
	//Test.SetHandleThreadCount(1);
	if (Test.StartServer())
	{
		HANDLE hEvent = CreateEventA(0, 0, 0, 0);
		WaitForSingleObject(hEvent, INFINITE);
	}

	return 0;
}
