
#include "MaxSvr.h"

int main(_In_ int argc,
	_In_reads_(argc) _Pre_z_ char** argv,
	_In_z_ char** envp)
{
	MaxSvr MyTest;
	MyTest.StartServer();
	HANDLE hEvent = CreateEventA(0, false, false, 0);
	WaitForSingleObject(hEvent, INFINITE);
	return 0;
}
