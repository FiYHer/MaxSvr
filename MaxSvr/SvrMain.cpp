
#include "MaxSvr.h"

class MyServer:public MaxSvr
{
public:
	MyServer(){}
	~MyServer(){}

	void virtual OnClientConnect(PIOContext pContext, PIOBuffer pBuffer)
	{
		cout << "[客户加入连接 ]: "
			<< inet_ntoa(pContext->stRemoteAddr.sin_addr)
			<< " 当前连接数量: "
			<< m_vConnectClient.size()
			<< endl;
	}

	void virtual OnClientClose(PIOContext pContext, PIOBuffer pBuffer)
	{
		cout << "[客户关闭连接]: "
			<< inet_ntoa(pContext->stRemoteAddr.sin_addr)
			<< endl;
	}

	void virtual OnConnectErro(PIOContext pContext, PIOBuffer pBuffer, int nError)
	{
		cout << "[连接发生错误]: "
			<< inet_ntoa(pContext->stRemoteAddr.sin_addr)
			<< ": "
			<< nError
			<< endl;
	}

	void virtual OnSendFinish(PIOContext pContext, PIOBuffer pBuffer)
	{
		cout << "[发送操作完成]: "
			<< inet_ntoa(pContext->stRemoteAddr.sin_addr)
			<< ": "
			<< pBuffer->szBuffer
			<< endl;
	}

	void virtual OnRecvFinish(PIOContext pContext, PIOBuffer pBuffer)
	{
		cout << "[接收操作完成]: "
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
		cout << "服务开启成功" << endl;
		HANDLE hEvent = CreateEventA(0, false, false, 0);
		WaitForSingleObject(hEvent, INFINITE);
	}
	else
		cout << "服务开启失败" << endl;
	return 0;
}
