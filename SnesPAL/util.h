#define _WIN32_WINNT 0x501
#define _WIN32_IE 0x0300

#include <Windows.h>
#include <windowsx.h>
#include <CommCtrl.h>

//#define ENABLE_VISUAL_STYLE

#ifdef _MSC_VER
	#pragma comment(lib, "Comctl32.lib")
	#ifdef ENABLE_VISUAL_STYLE
		#pragma comment(linker,"\"/manifestdependency:type='win32' \
		name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
		processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
	#endif
#endif

#pragma warning(disable: 4996)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <sstream>
#include <vector>
#include <deque>

#define LP_CLASS_NAME TEXT("SnesPAL")
#define ERROR_MBX(parent,msg) MessageBox(parent, msg, L"ERROR", MB_OK | MB_ICONERROR);

typedef unsigned char byte;
typedef unsigned short word;
typedef unsigned int dword;