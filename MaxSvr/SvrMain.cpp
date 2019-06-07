
#include "MaxSvr.h"

class MyServer:public MaxSvr
{
public:
	MyServer(){}
	~MyServer(){}

	void virtual OnClientConnect(PIOContext pContext, PIOBuffer pBuffer)
	{
		cout << "[�ͻ��������� ]: "
			<< inet_ntoa(pContext->stRemoteAddr.sin_addr)
			<< " ��ǰ��������: "
			<< m_vConnectClient.size()
			<< endl;
	}

	void virtual OnClientClose(PIOContext pContext, PIOBuffer pBuffer)
	{
		cout << "[�ͻ��ر�����]: "
			<< inet_ntoa(pContext->stRemoteAddr.sin_addr)
			<< endl;
	}

	void virtual OnConnectErro(PIOContext pContext, PIOBuffer pBuffer, int nError)
	{
		cout << "[���ӷ�������]: "
			<< inet_ntoa(pContext->stRemoteAddr.sin_addr)
			<< ": "
			<< nError
			<< endl;
	}

	void virtual OnSendFinish(PIOContext pContext, PIOBuffer pBuffer)
	{
		cout << "[���Ͳ������]: "
			<< inet_ntoa(pContext->stRemoteAddr.sin_addr)
			<< ": "
			<< pBuffer->szBuffer
			<< endl;
	}

	void virtual OnRecvFinish(PIOContext pContext, PIOBuffer pBuffer)
	{
		cout << "[���ղ������]: "
			<< inet_ntoa(pContext->stRemoteAddr.sin_addr)
			<< ": "
			<< pBuffer->szBuffer
			<< endl;
	}
};

int main(_In_ int argc,
	_In_reads_(argc) _Pre_z_ char** argv,
	_In_z_ char** envp)
{
	MyServer MyTest;
	MyTest.SetThreadCount(4);
	if (MyTest.StartServer())
	{
		cout << "�������ɹ�" << endl;
		HANDLE hEvent = CreateEventA(0, false, false, 0);
		WaitForSingleObject(hEvent, INFINITE);
	}
	else
		cout << "������ʧ��" << endl;
	return 0;
}
