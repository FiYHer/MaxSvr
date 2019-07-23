#pragma once

#include "MaxSvr.h"
#include "resource.h"

#include <Windowsx.h>
#include <CommCtrl.h>

namespace cgsvr
{
	//通信包方的功能类型
	typedef enum _PackageType
	{
		Package_Connect = 100,	//连接服务包
		Package_Close,			//断开连接包
		Package_Heartbeat,		//心跳包
		Package_Login,			//登陆包
		Package_Logout,			//登出包
		Package_JoinRoom,		//加入游戏房间包
		Package_CreateRoom,		//创建游戏房间包
		Package_StartGame,		//开始游戏对局包
		Package_GameState,		//游戏对局状态包
		Package_LeaveGame,		//离开游戏对局包
	}PackageType;

	//通信包
	typedef struct _SvrPackage
	{
		PackageType eType;		//功能类型
		union 
		{

		};
	}SvrPackage,*PSvrPackage;


	class CGServer : public MaxSvr
	{
	private:
		HWND m_hDlg;			//对话框句柄

	public:
		CGServer();
		~CGServer();

		//事件通知
		virtual void OnEvent(EventType eType, void* pData,
			PIOBuffer pBuffer, PIOContext pContext);

		inline void SethDlghWnd(HWND hDlg)//设置句柄
		{
			m_hDlg = hDlg;
		}

		bool StartCGServer(int nPort);	//在这里封装一下启动服务器函数
		bool StopCGServer();			//在这里封装一下停止服务器函数

		void OnError(
			int nError,PIOBuffer pBuffer,PIOContext pContext);	//错误事件
		void OnConnect(
			PIOBuffer pBuffer,PIOContext pContext);				//连接事件
		void OnClose(
			PIOBuffer pBuffer,PIOContext pContext);				//关闭事件
		void OnRecv(
			PIOBuffer pBuffer,PIOContext pContext);				//接收事件
		void OnSend(
			PIOBuffer pBuffer,PIOContext pContext);				//发送事件

		void AddMessage(
			const char* szSender, const char* szInfo);			//添加消息

	};
}

