#pragma once

#include "MaxSvr.h"
#include "resource.h"

#include <Windowsx.h>
#include <CommCtrl.h>

namespace cgsvr
{
	//ͨ�Ű����Ĺ�������
	typedef enum _PackageType
	{
		Package_Connect = 100,	//���ӷ����
		Package_Close,			//�Ͽ����Ӱ�
		Package_Heartbeat,		//������
		Package_Login,			//��½��
		Package_Logout,			//�ǳ���
		Package_JoinRoom,		//������Ϸ�����
		Package_CreateRoom,		//������Ϸ�����
		Package_StartGame,		//��ʼ��Ϸ�Ծְ�
		Package_GameState,		//��Ϸ�Ծ�״̬��
		Package_LeaveGame,		//�뿪��Ϸ�Ծְ�
	}PackageType;

	//ͨ�Ű�
	typedef struct _SvrPackage
	{
		PackageType eType;		//��������
		union 
		{

		};
	}SvrPackage,*PSvrPackage;


	class CGServer : public MaxSvr
	{
	private:
		HWND m_hDlg;			//�Ի�����

	public:
		CGServer();
		~CGServer();

		//�¼�֪ͨ
		virtual void OnEvent(EventType eType, void* pData,
			PIOBuffer pBuffer, PIOContext pContext);

		inline void SethDlghWnd(HWND hDlg)//���þ��
		{
			m_hDlg = hDlg;
		}

		bool StartCGServer(int nPort);	//�������װһ����������������
		bool StopCGServer();			//�������װһ��ֹͣ����������

		void OnError(
			int nError,PIOBuffer pBuffer,PIOContext pContext);	//�����¼�
		void OnConnect(
			PIOBuffer pBuffer,PIOContext pContext);				//�����¼�
		void OnClose(
			PIOBuffer pBuffer,PIOContext pContext);				//�ر��¼�
		void OnRecv(
			PIOBuffer pBuffer,PIOContext pContext);				//�����¼�
		void OnSend(
			PIOBuffer pBuffer,PIOContext pContext);				//�����¼�

		void AddMessage(
			const char* szSender, const char* szInfo);			//�����Ϣ

	};
}

