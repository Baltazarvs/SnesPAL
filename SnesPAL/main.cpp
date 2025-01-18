/*
 * SnesPAL 2025 by Nitrocell.
 * 
 * SnesPAL is tool used for editing SNES palettes, mainly Super Mario World's.
 * It includes needed functions that speed up and ease editing.
 * 
*/

#include "util.h"
#include "resource.h"

#define ID_FILE_NEW					10100
#define ID_FILE_OPEN				10101
#define ID_FILE_SAVE				10102
#define ID_FILE_SAVE				10103
#define ID_FILE_SAS					10104
#define ID_FILE_EXIT				10105
#define ID_HELP_ABOUT				10301

#define ID_BUTTON_CLOSE				20001
#define ID_BUTTON_SHOW_GRID			20002
#define ID_BUTTON_COPY_PAL			20003
#define ID_BUTTON_DRAW_MODE			20004

#define ID_BUTTON_UNDO				20005
#define ID_BUTTON_REDO				20006

#define ID_BUTTON_SIDE_ROTATE		20100
#define ID_BUTTON_SIDE_PLUS			20200
#define ID_BUTTON_SIDE_MINUS		20300

// Working palette in editor.
word pPaletteTable[0x100] = { 0x0000 };
// Picked color RGB.
COLORREF preservedCol = RGB(0x00, 0x00, 0x00);
// Picked color in SNES format.
word preservedColw = 0x0000;
// Used for preserved colors in custom color picker.
COLORREF preservedCols[16];

HINSTANCE hInstance = GetModuleHandle(nullptr);
bool bDisplayGrid = false;
bool bFileOpened = false;
bool bDrawMode = false;
bool bRecord = false;
bool bDrawnOneClick = false; // Used as flag to prevent running record per each LBUTTONDOWN message.
wchar_t pOpenedFilename[MAX_PATH] = { 0 };

bool bCursorInEditor = false;
bool bCursorInCustom = false;
HWND hMainWindow = nullptr;
HWND hPALEditor = nullptr;
HWND hGridCBX = nullptr;
HWND hStatusBar = nullptr;
HWND hCustomCol = nullptr;
HWND hCustomColTxt = nullptr;
HWND hCbxDraw = nullptr;
HWND hUndo = nullptr, hRedo = nullptr;

LRESULT __stdcall WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT __stdcall SubclassProc_Editor(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
LRESULT __stdcall SubclassProc_CustomCol(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
LRESULT __stdcall DlgProc_CopyPAL(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
LRESULT __stdcall DlgProc_About(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam);

BOOL __stdcall EnumProc_Main(HWND hWnd, LPARAM lParam);
void DrawToEditor(HDC);
void RedrawPalettes(bool bChanged = false);
int Loop();
bool OpenPAL(const wchar_t* fn);
bool SavePAL(const wchar_t* fn);
void ShowGrid(bool bShow);
void UpdateStatusInfo(const wchar_t* _1, const wchar_t* _2, const wchar_t* _3, const wchar_t* _4 = nullptr);
void Undo(); void Redo();

word Color_ConvertToSNES(byte r, byte g, byte b);
COLORREF Color_ConvertFromSNES(word rgb);
word GetEditorPositionIndex(POINTS& pts);

struct SnesPAL_Operation
{
	unsigned int n;
	word palette[0x100];
	std::wstring info;
	bool bDrawMode;
};

// Working palette record using for undo/redo.
std::deque<SnesPAL_Operation> lastOperations, lastOperationsRedo;
// Current index in records. Using to draw undid/redid palettes.
std::size_t historyIndex = 0;
std::size_t operationNumber = 0;

void RecordOperation(const wchar_t* pInfo);
bool CheckUndo();
bool CheckRedo();

int __stdcall wWinMain(HINSTANCE hInst, HINSTANCE, wchar_t*, int)
{
	hInstance = hInst;

	INITCOMMONCONTROLSEX iccex = {};
	iccex.dwSize = sizeof(iccex);
	iccex.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&iccex);

	WNDCLASSEX wcex;
	HWND hWnd = nullptr;
	ZeroMemory(&wcex, sizeof(wcex));

	wcex.cbSize = sizeof(wcex);
	wcex.style = 0;
	wcex.lpfnWndProc = &::WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInst;
	wcex.hIcon = LoadIcon(hInst, IDI_APPLICATION);
	wcex.hCursor = LoadCursor(hInst, IDC_ARROW);
	wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW);
	wcex.lpszMenuName = nullptr;
	wcex.lpszClassName = LP_CLASS_NAME;
	wcex.hIconSm = LoadIcon(hInst, IDI_APPLICATION);

	if (!RegisterClassEx(&wcex))
	{
		ERROR_MBX(nullptr, TEXT("Cannot register window."))
		return 0;
	}
	hWnd = CreateWindowEx(
		WS_EX_CLIENTEDGE,
		LP_CLASS_NAME,
		TEXT("SnesPAL 1.00"),
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		0, 0, 640, 480,
		nullptr, nullptr, hInst, nullptr
	);

	hMainWindow = hWnd;
	UpdateWindow(hWnd);

	if (!hWnd)
	{
		ERROR_MBX(nullptr, TEXT("Cannot create window."))
		return -1;
	}

	return Loop();
}

LRESULT __stdcall WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	static HBRUSH hBrush = nullptr;
	static HDC hdcMem;
	static BITMAPINFOHEADER bi;
	static HBITMAP hBitmap;
	static RECT rect;
	static int cxClient = 256, cyClient = 256;

	switch (Msg)
	{
		case WM_CREATE:
		{
			hdcMem = CreateCompatibleDC(GetDC(hWnd));
			bi.biSize = sizeof(BITMAPINFOHEADER);
			bi.biWidth = cxClient = GetSystemMetrics(SM_CXSCREEN);
			bi.biHeight = cyClient = GetSystemMetrics(SM_CYSCREEN);
			bi.biPlanes = 1;
			bi.biBitCount = 32;
			bi.biCompression = BI_RGB;
			bi.biSizeImage = 0;
			bi.biXPelsPerMeter = 0;
			bi.biYPelsPerMeter = 0;
			bi.biClrUsed = 0;
			bi.biClrImportant = 0;
			hBitmap = CreateDIBSection(hdcMem, (BITMAPINFO*)&bi, DIB_RGB_COLORS, NULL, NULL, 0);
			SelectObject(hdcMem, hBitmap);

			RECT clRect;
			GetClientRect(hWnd, &clRect);
			int wWidth = clRect.right - clRect.left;
			int wHeight = clRect.bottom - clRect.top;

			HMENU hMb = CreateMenu();
			HMENU hFile = CreateMenu();
			HMENU hEdit = CreateMenu();
			HMENU hHelp = CreateMenu();

			AppendMenu(hFile, MF_STRING, (UINT_PTR)1, TEXT("&New Palette"));
			AppendMenu(hFile, MF_STRING, (UINT_PTR)ID_FILE_OPEN, TEXT("&Open Palette"));
			AppendMenu(hFile, MF_STRING, (UINT_PTR)ID_FILE_SAVE, TEXT("&Save"));
			AppendMenu(hFile, MF_STRING, (UINT_PTR)ID_FILE_SAS, TEXT("&Save As"));
			AppendMenu(hFile, MF_STRING, (UINT_PTR)ID_FILE_EXIT, TEXT("E&xit"));

			AppendMenu(hEdit, MF_STRING, (UINT_PTR)3, TEXT("&Copy Color"));
			AppendMenu(hEdit, MF_STRING, (UINT_PTR)4, TEXT("&Paste Color"));
			AppendMenu(hEdit, MF_STRING, (UINT_PTR)5, TEXT("&Delete Color"));

			AppendMenu(hHelp, MF_STRING, (UINT_PTR)ID_HELP_ABOUT, TEXT("&About"));

			AppendMenu(hMb, MF_POPUP, (UINT_PTR)hFile, TEXT("&File"));
			AppendMenu(hMb, MF_POPUP, (UINT_PTR)hEdit, TEXT("E&dit"));
			AppendMenu(hMb, MF_POPUP, (UINT_PTR)hHelp, TEXT("&Help"));
			SetMenu(hWnd, hMb);

			hPALEditor = CreateWindow(WC_STATIC, nullptr, WS_VISIBLE | WS_CHILD, 0, 0, 256, 256, hWnd, nullptr, nullptr, nullptr);
			CreateWindow(WC_BUTTON, TEXT("Close"), WS_VISIBLE | WS_CHILD, 0, 256, 85, 30, hWnd, (HMENU)ID_BUTTON_CLOSE, nullptr, nullptr);
			hGridCBX = CreateWindow(WC_BUTTON, TEXT("Show Grid"), WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 5, 286, 85, 30, hWnd, (HMENU)ID_BUTTON_SHOW_GRID, nullptr, nullptr);
			hCbxDraw = CreateWindow(WC_BUTTON, TEXT("Draw Mode"), WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 90, 286, 85, 30, hWnd, (HMENU)ID_BUTTON_DRAW_MODE, nullptr, nullptr);
			CreateWindow(WC_BUTTON, TEXT("Copy Palette"), WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 85, 256, 85, 30, hWnd, (HMENU)ID_BUTTON_COPY_PAL, nullptr, nullptr);
			hStatusBar = CreateWindow(STATUSCLASSNAME, nullptr, WS_VISIBLE | WS_CHILD | SBARS_SIZEGRIP, 0, 0, 0, 0, hWnd, nullptr, nullptr, nullptr);
			hCustomCol = CreateWindowEx(WS_EX_CLIENTEDGE, WC_STATIC, nullptr, WS_VISIBLE | WS_CHILD | WS_BORDER, 5, 320, 45, 45, hWnd, nullptr, nullptr, nullptr);

			hUndo = CreateWindow(WC_BUTTON, TEXT("Undo"), WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 170, 256, 42, 30, hWnd, (HMENU)ID_BUTTON_UNDO, nullptr, nullptr);
			hRedo = CreateWindow(WC_BUTTON, TEXT("Redo"), WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 212, 256, 45, 30, hWnd, (HMENU)ID_BUTTON_REDO, nullptr, nullptr);

			hCustomColTxt = CreateWindow(WC_STATIC, TEXT("$0000"), WS_VISIBLE | WS_CHILD, 12, 367, 35, 15, hWnd, nullptr, nullptr, nullptr);

			for (int i = 0; i < 0x10; ++i)
			{
				CreateWindow(WC_BUTTON, TEXT(">"), WS_VISIBLE | WS_CHILD, 256, 16 * i, 16, 16, hWnd, (HMENU)(ID_BUTTON_SIDE_ROTATE+i), nullptr, nullptr);
				CreateWindow(WC_BUTTON, TEXT("+"), WS_VISIBLE | WS_CHILD, 256 + 16, 16 * i, 16, 16, hWnd, (HMENU)(ID_BUTTON_SIDE_PLUS+i), nullptr, nullptr);
				CreateWindow(WC_BUTTON, TEXT("-"), WS_VISIBLE | WS_CHILD, 256 + 32, 16 * i, 16, 16, hWnd, (HMENU)(ID_BUTTON_SIDE_MINUS+i), nullptr, nullptr);
			}


			SetWindowLongPtr(hPALEditor, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(hdcMem));

			SetWindowSubclass(hPALEditor, &SubclassProc_Editor, 0u, 0u);
			SetWindowSubclass(hCustomCol, &SubclassProc_CustomCol, 0u, 0u);
			
			std::vector<HWND> vecWindows;
			EnumChildWindows(hWnd, &EnumProc_Main, reinterpret_cast<LPARAM>(&vecWindows));
			HFONT hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
			for (auto& hwnd : vecWindows)
				SendMessage(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), static_cast<LPARAM>(true));

			break;
		}
		case WM_CTLCOLORSTATIC:
		{
			HWND hControl = (HWND)lParam;
			HDC hdc = (HDC)wParam;
			break;
		}
		case WM_COMMAND:
		{
			for (int i = 0; i < 0x10; ++i)
			{
				int row_index = (ID_BUTTON_SIDE_ROTATE+i)-ID_BUTTON_SIDE_ROTATE;
				POINTS ppp = { };
				ppp.x = 0x10*0x03;
				ppp.y = 0x10*i+8;

				if (LOWORD(wParam) == (ID_BUTTON_SIDE_ROTATE+i))
				{
					// TODO:
					word indexes = GetEditorPositionIndex(ppp);
					byte col = indexes, row = (indexes >> 8);
					std::uint16_t singleIndex = row * 0x10 + col;

					word* pFirst = &pPaletteTable[(singleIndex-0x10)+(ppp.x / 3) - 1];
					word* pLast = pFirst+0x0D;
					word firstVal = *pFirst;

					for (int j = 0; j < 0x0D; ++j)
						pFirst[j] = pFirst[j+1];
					*pLast = firstVal;
					RedrawPalettes();
				}
			}

			switch (LOWORD(wParam))
			{
				case ID_FILE_OPEN:
				{
					wchar_t* buffer = new wchar_t[MAX_PATH];
					OPENFILENAME ofn = { };
					ofn.lStructSize = sizeof(ofn);
					ofn.hwndOwner = hWnd;
					ofn.hInstance = ::hInstance;
					ofn.lpstrInitialDir = L".";
					ofn.nMaxFile = MAX_PATH;
					ofn.lpstrFile = buffer;
					ofn.lpstrFile[0] = '\0';
					ofn.lpstrFilter = TEXT("TPL Palette\0*.tpl\0PAL Palette\0*.pal\0");
					ofn.nFilterIndex = -1;
					ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;

					if (GetOpenFileName(&ofn))
					{
						if (!OpenPAL(buffer))
						{
							ERROR_MBX(hWnd, TEXT("Cannot open requested file."))
						}
						else
						{
							SendMessage(hStatusBar, SB_SETTEXT, (WPARAM)LOBYTE(3), (LPARAM)TEXT("File opened successfully."));
						}
					}

					delete[] buffer;
					break;
				}
				case ID_FILE_SAVE:
				{
					if (!bFileOpened)
					{
						wchar_t* buffer = new wchar_t[MAX_PATH];
						OPENFILENAME ofn = { };
						ofn.lStructSize = sizeof(ofn);
						ofn.hwndOwner = hWnd;
						ofn.hInstance = ::hInstance;
						ofn.lpstrInitialDir = L".";
						ofn.nMaxFile = MAX_PATH;
						ofn.lpstrFile = buffer;
						ofn.lpstrFile[0] = '\0';
						ofn.lpstrFilter = L"TPL File\0*.tpl\0PAL File\0*.pal\0";
						ofn.nFilterIndex = -1;
						ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;
						if (GetSaveFileName(&ofn))
						{
							if (!::SavePAL(buffer))
							{
								ERROR_MBX(hWnd, TEXT("Cannot save requested file."))
							}
							else
							{
								SendMessage(hStatusBar, SB_SETTEXT, (WPARAM)LOBYTE(3), (LPARAM)TEXT("File saved."));
							}
						}

						delete[] buffer;
						break;
					}
					else
					{
						if (!::SavePAL(nullptr))
						{
							ERROR_MBX(hWnd, TEXT("Cannot save requested file."))
						}
						else
						{
							SendMessage(hStatusBar, SB_SETTEXT, (WPARAM)LOBYTE(3), (LPARAM)TEXT("File saved."));
						}
					}
					
					break;
				}
				case ID_FILE_SAS:
				{
					wchar_t* buffer = new wchar_t[MAX_PATH];
					OPENFILENAME ofn = { };
					ofn.lStructSize = sizeof(ofn);
					ofn.hwndOwner = hWnd;
					ofn.hInstance = ::hInstance;
					ofn.lpstrInitialDir = L".";
					ofn.nMaxFile = MAX_PATH;
					ofn.lpstrFile = buffer;
					ofn.lpstrFile[0] = '\0';
					ofn.lpstrFilter = L"TPL File\0*.tpl\0PAL File\0*.pal\0";
					ofn.nFilterIndex = -1;
					ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER;
					if (GetSaveFileName(&ofn))
					{
						if (!::SavePAL(buffer))
						{
							ERROR_MBX(hWnd, TEXT("Cannot save requested file."))
						}
					}
					delete[] buffer;
					break;
				}
				case ID_FILE_EXIT:
				{
					DestroyWindow(hWnd);
					break;
				}
				case ID_HELP_ABOUT:
				{
					DialogBox(hInstance, MAKEINTRESOURCE(IDD_ABOUT), hWnd, &::DlgProc_About);
					break;
				}

				case ID_BUTTON_CLOSE:
				{
					if (bFileOpened)
					{
						bFileOpened = false;
						ZeroMemory(pOpenedFilename, sizeof(char)* MAX_PATH);
						ZeroMemory(pPaletteTable, sizeof(word) * 256);
						SendMessage(hStatusBar, SB_SETTEXT, (WPARAM)LOBYTE(3), (LPARAM)TEXT("File closed."));
						SetWindowText(hWnd, TEXT("SnesPAL v1.00"));
						RedrawPalettes();
					}
					break;
				}
				case ID_BUTTON_SHOW_GRID:
				{
					ShowGrid(!bDisplayGrid);
					break;
				}
				case ID_BUTTON_COPY_PAL:
				{
					DialogBox(hInstance, MAKEINTRESOURCE(IDD_COPYPAL), hWnd, &DlgProc_CopyPAL);
					break;
				}
				case ID_BUTTON_DRAW_MODE:
				{
					bDrawMode = !bDrawMode;
					if (bDrawMode)
					{
						UpdateStatusInfo(nullptr, nullptr, nullptr, TEXT("Drawing mode turned ON."));
						RecordOperation(TEXT("Drawing mode turned ON."));
					}
					else
					{
						UpdateStatusInfo(nullptr, nullptr, nullptr, TEXT("Drawing mode turned OFF."));
						RecordOperation(TEXT("Drawing mode turned OFF."));
					}
					break;
				}
				case ID_BUTTON_UNDO:
				{
					if (!lastOperations.empty())
					{
						auto oper = lastOperations.back();
						memcpy(pPaletteTable, oper.palette, sizeof(word) * 0x100);
						RedrawPalettes();
						::bDrawMode = oper.bDrawMode;
						Button_SetCheck(hCbxDraw, ::bDrawMode);
						lastOperations.pop_back();
						lastOperationsRedo.push_back(oper);
						std::wstring statMsg = std::to_wstring(lastOperations.size()).append(TEXT(" step(s) back.")).append(TEXT(" => ")).append(oper.info);
						UpdateStatusInfo(nullptr, nullptr, nullptr, statMsg.c_str());
						Button_Enable(hRedo, TRUE);
						if (lastOperations.empty()) Button_Enable(hUndo, FALSE);
					}
					break;
				}
				case ID_BUTTON_REDO:
				{
					if (!lastOperationsRedo.empty())
					{
						auto oper = lastOperationsRedo.back();
						memcpy(pPaletteTable, oper.palette, sizeof(word) * 0x100);
						RedrawPalettes();
						UpdateStatusInfo(nullptr, nullptr, nullptr, oper.info.c_str());
						::bDrawMode = oper.bDrawMode;
						Button_SetCheck(hCbxDraw, ::bDrawMode);
						lastOperationsRedo.pop_back();
						lastOperations.push_back(oper);
						std::wstring statMsg = std::to_wstring(lastOperationsRedo.size()).append(TEXT(" step(s) forward.")).append(TEXT(" => ")).append(oper.info);
						UpdateStatusInfo(nullptr, nullptr, nullptr, statMsg.c_str());
						Button_Enable(hUndo, TRUE);
						if (lastOperationsRedo.empty()) Button_Enable(hRedo, FALSE);
					}
					break;
				}
			}
			break;
		}
		case WM_MOUSEMOVE:
		{
			POINTS cursorPos = MAKEPOINTS(lParam);

			RECT wRectCC;
			GetWindowRect(hCustomCol, &wRectCC);

			POINT pts;
			GetCursorPos(&pts);

			bool bChk = (pts.x >= wRectCC.left && pts.x <= wRectCC.right && pts.y >= wRectCC.top && pts.y <= wRectCC.bottom);
			if (bChk)
			{
				bCursorInCustom = true;
			}
			else
			{
				bCursorInCustom = false;
			}

			if (((cursorPos.x > 0 && cursorPos.x < 256) && (cursorPos.y > 0 && cursorPos.y < 256)) || bChk)
			{
				if (bChk)
				{
					wchar_t pStr[14];
					wsprintf(pStr, L"Color: $%04X", ::preservedColw);
					UpdateStatusInfo(nullptr, pStr, nullptr);
				}
				else
				{
					// If cursor is withing editor bounds...
					bCursorInEditor = true;
					word indexes = GetEditorPositionIndex(cursorPos);
					byte col = indexes, row = (indexes >> 8);
					std::uint16_t singleIndex = row * 16 + col;

					wchar_t pStatusInfo[150];
					wsprintf(pStatusInfo, TEXT("Palette [%02X]"), row);
					wsprintf(pStatusInfo + 50, TEXT("Color [%02X]: $%04X"), col, pPaletteTable[singleIndex]);
					wsprintf(pStatusInfo + 100, TEXT("PAL-Index: [%02X]"), singleIndex);
					UpdateStatusInfo(pStatusInfo, pStatusInfo + 50, pStatusInfo + 100);

					if (wParam & MK_LBUTTON)
					{
						if (bDrawMode)
						{
							bRecord = true;
							pPaletteTable[singleIndex] = ::preservedColw;
							RedrawPalettes();
						}
					}
				}
			}
			else
			{
				bCursorInEditor = false;
				SendMessage(hStatusBar, SB_SETTEXT, (WPARAM)LOBYTE(0), (LPARAM)(nullptr));
				SendMessage(hStatusBar, SB_SETTEXT, (WPARAM)LOBYTE(1), (LPARAM)(nullptr));
				SendMessage(hStatusBar, SB_SETTEXT, (WPARAM)LOBYTE(2), (LPARAM)(nullptr));
				UpdateStatusInfo(nullptr, nullptr, nullptr);
			}
			break;
		}
		case WM_LBUTTONDOWN:
		{
			CHOOSECOLOR cc;
			ZeroMemory(&cc, sizeof(cc));
			cc.lStructSize = sizeof(CHOOSECOLOR);
			cc.Flags = CC_FULLOPEN | CC_RGBINIT;
			cc.hwndOwner = GetParent(hWnd);
			cc.hInstance = nullptr;
			cc.lpCustColors = ::preservedCols;

			// If user clicks on editor...
			if (bCursorInEditor)
			{
				POINTS cursorPos = MAKEPOINTS(lParam);
				word indexes = GetEditorPositionIndex(cursorPos);
				byte col = indexes, row = (indexes >> 8);
				int index = row * 16 + col;

				word currCol = 0x0000;
				
				//if (bDrawMode)
				//{
				//	pPaletteTable[index] = ::preservedColw;
				//	bDrawnOneClick = true;
				//	RedrawPalettes(true);
				//}
				//else
				//{
				//	bDrawnOneClick = false;
				//	currCol = pPaletteTable[index];
				//}

				

				if (!bDrawMode)
				{
					cc.rgbResult = Color_ConvertFromSNES(pPaletteTable[index]);
					if (ChooseColor(&cc))
					{
						COLORREF tempCol = cc.rgbResult;
						word tempColw = Color_ConvertToSNES(GetRValue(tempCol), GetGValue(tempCol), GetBValue(tempCol));
						pPaletteTable[index] = tempColw;
						RedrawPalettes();
						RecordOperation(TEXT("Change color."));
					}
				}
				break;
			}
			if (bCursorInCustom)
			{
				if (ChooseColor(&cc))
				{
					::preservedCol = cc.rgbResult;
					::preservedColw = Color_ConvertToSNES(GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult));
					InvalidateRect(hCustomCol, nullptr, TRUE);
					UpdateWindow(hCustomCol);

					wchar_t pStr[6];
					wsprintf(pStr, L"$%04X", ::preservedColw);
					SetWindowText(hCustomColTxt, pStr);
				}
			}
			break;
		}
		case WM_LBUTTONUP:
		{
			if (bDrawMode && bRecord)
			{
				bRecord = false;
				RecordOperation(TEXT("Draw to palette."));
			}

			break;
		}
		case WM_RBUTTONDOWN:
		{
			if (bCursorInEditor)
			{
				POINTS cursorPos = MAKEPOINTS(lParam);
				word indexes = GetEditorPositionIndex(cursorPos);
				byte col = indexes, row = (indexes >> 8);
				int index = row * 16 + col;

				preservedCol = Color_ConvertFromSNES(pPaletteTable[index]);
				preservedColw = pPaletteTable[index];

				InvalidateRect(hCustomCol, nullptr, TRUE);
				UpdateWindow(hCustomCol);

				wchar_t pStr[6];
				wsprintf(pStr, L"$%04X", ::preservedColw);
				SetWindowText(hCustomColTxt, pStr);
			}
			break;
		}
		case WM_SIZE:
		{
			RECT clRect;
			GetClientRect(hWnd, &clRect);
			int wWidth = clRect.right - clRect.left;
			int wHeight = clRect.bottom - clRect.top;

			RECT sbRect;
			GetWindowRect(hStatusBar, &sbRect);

			int pParts[] = { 100, 200, 300, -1};
			SendMessage(hStatusBar, SB_SETPARTS, (WPARAM)(sizeof(pParts)/sizeof(int)), (LPARAM)(pParts));
			MoveWindow(hStatusBar, 0, wHeight - (sbRect.bottom - sbRect.top), wWidth, 30, TRUE);
			break;
		}
		case WM_CLOSE:
			DestroyWindow(hWnd);
			break;
		case WM_DESTROY:
		{
			RemoveWindowSubclass(hPALEditor, &SubclassProc_Editor, 0u);
			RemoveWindowSubclass(hCustomCol, &SubclassProc_CustomCol, 0u);
			PostQuitMessage(0);
			break;
		}
		default:
			return DefWindowProc(hWnd, Msg, wParam, lParam);
	}
	return 0;
}

LRESULT __stdcall SubclassProc_Editor(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
	HDC hdcMem = (HDC)GetWindowLongPtr(hWnd, GWLP_USERDATA);
	switch (Msg)
	{
		case WM_CREATE:
		{
			break;
		}
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			BeginPaint(hWnd, &ps);
			DrawToEditor(hdcMem);
			BitBlt(ps.hdc, 0, 0, 256, 256, hdcMem, 0, 0, SRCCOPY);
			EndPaint(hWnd, &ps);
			break;
		}
		default:
			return DefSubclassProc(hWnd, Msg, wParam, lParam);
	}
	return 0;
}

LRESULT __stdcall SubclassProc_CustomCol(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
	switch (Msg)
	{
		case WM_PAINT:
		{
			RECT clRect;
			GetClientRect(hWnd, &clRect);
			PAINTSTRUCT ps;
			BeginPaint(hWnd, &ps);
			word snesCol = Color_ConvertToSNES(GetRValue(preservedCol), GetGValue(preservedCol), GetBValue(preservedCol));
			HBRUSH hbr = CreateSolidBrush(preservedCol);
			HBRUSH hbrOld = (HBRUSH)SelectObject(ps.hdc, hbr);
			FillRect(ps.hdc, &clRect, hbr);
			SelectObject(ps.hdc, hbrOld);
			DeleteObject(hbr);
			EndPaint(hWnd, &ps);
			break;
		}
		default:
			return DefSubclassProc(hWnd, Msg, wParam, lParam);
	}
}

LRESULT __stdcall DlgProc_CopyPAL(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	HWND hSrcEdit = GetDlgItem(hDlg, IDC_EDIT_SRC_PAL);
	HWND hDestEdit = GetDlgItem(hDlg, IDC_EDIT_DEST_PAL);

	switch (Msg)
	{
		case WM_INITDIALOG:
		{
			SetWindowText(hSrcEdit, TEXT("00"));
			SetWindowText(hDestEdit, TEXT("00"));
			break;
		}
		case WM_COMMAND:
		{
			switch (LOWORD(wParam))
			{
				case IDOK:
				{
					// Proceed to copying.
					wchar_t srcStr[3], destStr[3];
					GetWindowText(hSrcEdit, srcStr, 3); GetWindowText(hDestEdit, destStr, 3);
					wchar_t* pEnd1, *pEnd2;
					int srcIndex = wcstol(srcStr, &pEnd1, 16);
					int destIndex = wcstol(destStr, &pEnd2, 16);

					if (*pEnd1 != '\0' || *pEnd2 != '\0')
					{
						MessageBox(hDlg, TEXT("Invalid Input. Check source or destination."), TEXT("Copy Palette"), MB_OK | MB_ICONEXCLAMATION);
						break;
					}


					if (srcIndex > 0x0F || destIndex > 0x0F)
					{
						MessageBox(hDlg, TEXT("Palette number must be [$00-$0F]"), TEXT("Copy Palette"), MB_OK | MB_ICONEXCLAMATION);
						break;
					}

					int realSrcIndex = srcIndex * 16;
					int realDestIndex = destIndex * 16;

					word palSrc[0x10], palDest[0x10];
					memcpy(palSrc, pPaletteTable+realSrcIndex,  sizeof(word) * 0x10);
					memcpy(pPaletteTable+realDestIndex, palSrc, sizeof(word) * 0x10);
					RedrawPalettes();
					wchar_t temBuff[35];
					wsprintf(temBuff, L"Palette $%02X copied to $%02X", srcIndex, destIndex);
					SendMessage(hStatusBar, SB_SETTEXT, (WPARAM)LOBYTE(3), (LPARAM)temBuff);

					RecordOperation(temBuff);
				}
				case IDCANCEL:
				{
					EndDialog(hDlg, 0);
					break;
				}
			}
			break;
		}
	}
	return 0;
}

LRESULT __stdcall DlgProc_About(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg)
	{
		case WM_INITDIALOG:
			SendMessage(hStatusBar, SB_SETTEXT, (WPARAM)LOBYTE(3), (LPARAM)TEXT("Displaying about."));
			break;
		case WM_COMMAND:
		case WM_CLOSE:
			EndDialog(hDlg, 0);
			break;
	}
	return 0;
}

BOOL __stdcall EnumProc_Main(HWND hWnd, LPARAM lParam)
{
	std::vector<HWND>* pVec = reinterpret_cast<std::vector<HWND>*>(lParam);
	if (!pVec)
		return FALSE;
	pVec->push_back(hWnd);
	return TRUE;
}

int Loop()
{
	MSG Msg = { };
	while (GetMessage(&Msg, nullptr, 0, 0) > 0)
	{
		TranslateMessage(&Msg);
		DispatchMessage(&Msg);
	}
	return 0;
}

bool OpenPAL(const wchar_t* fn)
{
	if (!fn) return false;

	const wchar_t* ext = nullptr;
	const wchar_t* del = wcschr(fn, '.');
	if (del || del != fn)
		ext = del + 1;

	if (!ext)
	{
		ERROR_MBX(nullptr, TEXT("Invalid File"))
		return false;
	}

	if (wcscmp(ext, L"pal") && wcscmp(ext, L"tpl"))
	{
		ERROR_MBX(nullptr, TEXT("Unknown extension."))
		return false;
	}

	FILE* file = _wfopen(fn, L"rb");
	if (file)
	{
		if (!wcscmp(ext, L"pal"))
		{
			// Load PAL...
			byte rgb_curr[0x100 * 3];
			fread(rgb_curr, sizeof(byte), 0x100 * 0x03, file);

			int currPalIndex = 0;
			for (int i = 0; i < 0x300; i += 3)
			{
				pPaletteTable[i/0x03] = Color_ConvertToSNES(rgb_curr[i], rgb_curr[i + 1], rgb_curr[i + 2]);
			}

			CheckUndo(); CheckRedo();
			RedrawPalettes();
			lastOperations.clear();
			lastOperationsRedo.clear();
			operationNumber = 0ull;
			RecordOperation(TEXT("Loaded palette."));

			wchar_t* titleBuff = new wchar_t[MAX_PATH + 17];
			wcscpy(titleBuff, TEXT("SnesPAL v1.00"));
			wcscat(titleBuff, TEXT(" - "));
			wcscat(titleBuff, fn);
			SetWindowText(hMainWindow, titleBuff);
			delete[] titleBuff;
			::bFileOpened = true;
			wcscpy(::pOpenedFilename, fn);
			return true;
		}
		else if (!wcscmp(ext, L"tpl"))
		{
			// Load TPL...
			byte signature[4];
			fread(signature, sizeof(byte) * 4, 1, file);
			word palData[256];
			ZeroMemory(palData, sizeof(word) * 256);
			fread(pPaletteTable, sizeof(word), 256, file);
			
			CheckUndo(); CheckRedo();
			RedrawPalettes();
			lastOperations.clear();
			lastOperationsRedo.clear();
			operationNumber = 0ull;
			RecordOperation(TEXT("Loaded palette."));

			::bFileOpened = true;
			wcscpy(::pOpenedFilename, fn);
		}
		fclose(file);
		return true;
	}
	else
	{
		ERROR_MBX(nullptr, TEXT("Cannot open requested file."))
		return false;
	}

	return false;
}

bool SavePAL(const wchar_t* fn)
{
	if (!fn)
		fn = pOpenedFilename;

	const wchar_t* ext = nullptr;
	const wchar_t* del = wcschr(fn, '.');
	if (del || del != fn)
		ext = del + 1;

	if (!ext)
	{
		ERROR_MBX(nullptr, TEXT("Invalid File"))
		return false;
	}

	if (wcscmp(ext, L"pal") && wcscmp(ext, L"tpl"))
	{
		ERROR_MBX(nullptr, TEXT("Unknown extension."))
		return false;
	}

	FILE* file = _wfopen(fn, L"wb");
	if (file)
	{
		if (!wcscmp(ext, L"pal"))
		{
			byte pPaletteTableConv[768];
			for (int i = 0; i < 256; ++i)
			{
				COLORREF rgb = Color_ConvertFromSNES(pPaletteTable[i]);
				byte r = GetRValue(rgb);
				byte g = GetGValue(rgb);
				byte b = GetBValue(rgb);
				byte rgb_b[3] = { r, g, b };
				memcpy(pPaletteTableConv+(i*3), rgb_b, sizeof(byte) * 3);
			}
			fwrite(pPaletteTableConv, sizeof(byte) * 768, 1, file);
		}
		fclose(file);

		wcscpy(pOpenedFilename, fn);
		bFileOpened = true;
		wchar_t* titleBuff = new wchar_t[MAX_PATH + 17];
		wcscpy(titleBuff, TEXT("SnesPAL v1.00"));
		wcscat(titleBuff, TEXT(" - "));
		wcscat(titleBuff, fn);
		SetWindowText(hMainWindow, titleBuff);
		delete[] titleBuff;
		return true;
	}
	else
	{
		ERROR_MBX(nullptr, TEXT("Cannot open requested file."))
		return false;
	}

	return false;
}

void ShowGrid(bool bShow)
{
	::bDisplayGrid = bShow;
	InvalidateRect(hPALEditor, nullptr, TRUE);
	UpdateWindow(hPALEditor);
	return;
}

void UpdateStatusInfo(const wchar_t* _1, const wchar_t* _2, const wchar_t* _3, const wchar_t* _4)
{
	if (_4)
	{
		// Info is used without other statuses...
		SendMessage(hStatusBar, SB_SETTEXT, (WPARAM)LOBYTE(3), (LPARAM)(_4));
		return;
	}
	SendMessage(hStatusBar, SB_SETTEXT, (WPARAM)LOBYTE(0), (LPARAM)(_1));
	SendMessage(hStatusBar, SB_SETTEXT, (WPARAM)LOBYTE(1), (LPARAM)(_2));
	SendMessage(hStatusBar, SB_SETTEXT, (WPARAM)LOBYTE(2), (LPARAM)(_3));
}

void Undo()
{
	RedrawPalettes();
	return;
}

void Redo()
{
	RedrawPalettes();
	return;
}

bool UndoCheck()
{
	if (historyIndex > 0)
	{
		Button_Enable(hUndo, TRUE);
		return true;
	}
	else
	{
		Button_Enable(hUndo, FALSE);
	}
	return false;
}

bool RedoCheck()
{
	if (historyIndex <= lastOperations.size() - 1)
	{
		Button_Enable(hUndo, TRUE);
		return true;
	}
	else
		Button_Enable(hUndo, FALSE);
	return false;
}

void DrawToEditor(HDC hdc)
{
	word rowDown = 0;
	RECT edRect;
	edRect.left = 0;
	edRect.top = 0;
	edRect.bottom = 256;
	edRect.right = 256;
	
	HBRUSH hbrt = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
	FillRect(hdc, &edRect, hbrt);
	DeleteObject(hbrt);

	for (word y = 0x00; y < 0x10; ++y)
	{
		for (word x = 0x00; x < 0x10; ++x)
		{
			// Get current color from table.
			word currCol = pPaletteTable[y * 0x10 + x];
			if (x == 0)
				currCol = 0x0000;

			// Create brush based on that color.
			HBRUSH hbr = CreateSolidBrush(Color_ConvertFromSNES(currCol));
			// Calculate each palette color rect dimensions.
			RECT colRect;
			colRect.left = (x * 0x10) + (bDisplayGrid ? 0x01 : 0x00);
			colRect.right = (colRect.left + 0x10) - (bDisplayGrid ? 0x01 : 0x00);
			colRect.top = (y * 0x10) + (bDisplayGrid ? 0x01 : 0x00);
			colRect.bottom = (colRect.top + 0x10) - (bDisplayGrid ? 0x01 : 0x00);

			// Draw color squares.
			FillRect(hdc, &colRect, hbr);
			// Release Brush
			DeleteObject(hbr);
		}
	}
	return;
}

void RedrawPalettes(bool bChanged)
{
	InvalidateRect(hPALEditor, nullptr, TRUE);
	UpdateWindow(hPALEditor);
	return;
}

word Color_ConvertToSNES(byte r, byte g, byte b)
{
	word outCol = (b >> 3) << 10 | (g >> 3) << 5 | (r >> 3);
	return outCol;
}

COLORREF Color_ConvertFromSNES(word rgb)
{
	byte blue = (rgb >> 10) & 0x1F;
	byte green = (rgb >> 5) & 0x1F;
	byte red = rgb & 0x1F;
	COLORREF rgbout = RGB((red * 255) / 31, (green * 255) / 31, (blue * 255) / 31);
	return rgbout;
}

word GetEditorPositionIndex(POINTS& pts)
{
	const byte cell = 0x10;
	const byte cols = 0x100/0x10;

	byte col = pts.x / cell;
	byte row = pts.y / cell;

	word resPair = 0x0000;
	resPair |= (row<<8)|col;
	return resPair;
}

void RecordOperation(const wchar_t* pInfo)
{
	SnesPAL_Operation lastOper;
	memcpy(&lastOper.palette, pPaletteTable, sizeof(word) * 0x100);
	lastOper.bDrawMode = ::bDrawMode;
	lastOper.info = pInfo;
	operationNumber = 0; lastOper.n = operationNumber;
	lastOperations.push_back(lastOper);
	lastOperationsRedo.clear();
	Button_Enable(hUndo, TRUE);
	return;
}

bool CheckUndo()
{
	if (lastOperations.empty())
	{
		Button_Enable(hUndo, FALSE);
		return false;
	}
	Button_Enable(hUndo, TRUE);
	return true;
}

bool CheckRedo()
{
	if (lastOperationsRedo.empty())
	{
		Button_Enable(hRedo, FALSE);
		return false;
	}
	Button_Enable(hRedo, TRUE);
	return true;
}