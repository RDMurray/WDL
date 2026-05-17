/*
    swell_myapp

    This software is provided 'as-is', without any express or implied
    warranty.  In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
       claim that you wrote the original software. If you use this software
       in a product, an acknowledgment in the product documentation would be
       appreciated but is not required.
    2. Altered source versions must be plainly marked as such, and must not be
       misrepresented as being the original software.
    3. This notice may not be removed or altered from any source distribution.
*/

#ifdef _WIN32
#include <windows.h>
#include "../WDL/win32_utf8.h"
#endif

#include "../../swell/swell.h"

#include "../../wingui/wndsize.h"

#include "resource.h"

#if !defined(_WIN32) && !defined(__APPLE__)
bool g_quit;
#endif

HINSTANCE g_hInstance;
HWND g_hwnd;

static HMENU g_menu;
static HMENU g_options_menu;
static int g_progress;

static void setStatus(HWND hwndDlg, const char *text)
{
  SetDlgItemText(hwndDlg,IDC_STATIC_STATUS,text ? text : "Status");
}

static void initMenu(HWND hwndDlg)
{
  HMENU submenu = CreatePopupMenu();
  AddMenuItem(submenu,0,"Submenu action",ID_SAMPLE_SUBITEM);

  g_options_menu = CreatePopupMenu();
  AddMenuItem(g_options_menu,0,"Normal action",ID_SAMPLE_ACTIVATE);
  AddMenuItem(g_options_menu,1,"Checked item",ID_SAMPLE_CHECKED);
  AddMenuItem(g_options_menu,2,"Radio A",ID_SAMPLE_RADIO_A);
  AddMenuItem(g_options_menu,3,"Radio B",ID_SAMPLE_RADIO_B);
  AddMenuItem(g_options_menu,4,"Disabled item",ID_SAMPLE_DISABLED);
  InsertMenu(g_options_menu,5,MF_BYPOSITION|MF_SEPARATOR,0,NULL);
  InsertMenu(g_options_menu,6,MF_BYPOSITION|MF_POPUP,(UINT_PTR)submenu,"Submenu");
  CheckMenuItem(g_options_menu,ID_SAMPLE_CHECKED,MF_BYCOMMAND|MF_CHECKED);
  CheckMenuItem(g_options_menu,ID_SAMPLE_RADIO_A,MF_BYCOMMAND|MF_CHECKED);
  MENUITEMINFO mi = { sizeof(mi), MIIM_TYPE };
  mi.fType = MFT_RADIOCHECK;
  SetMenuItemInfo(g_options_menu,ID_SAMPLE_RADIO_A,FALSE,&mi);
  SetMenuItemInfo(g_options_menu,ID_SAMPLE_RADIO_B,FALSE,&mi);
  EnableMenuItem(g_options_menu,ID_SAMPLE_DISABLED,MF_BYCOMMAND|MF_GRAYED);

  g_menu = CreatePopupMenu();
  InsertMenu(g_menu,0,MF_BYPOSITION|MF_POPUP,(UINT_PTR)g_options_menu,"Harness");
  AddMenuItem(g_menu,1,"Quit",ID_QUIT);
  SetMenu(hwndDlg,g_menu);
}

static bool menuItemChecked(HMENU menu, int command_id)
{
  MENUITEMINFO mi = { sizeof(mi), MIIM_STATE };
  return GetMenuItemInfo(menu,command_id,FALSE,&mi) && (mi.fState & MF_CHECKED);
}

static void initControls(HWND hwndDlg)
{
  SetDlgItemText(hwndDlg,IDC_EDIT1,"Sample text");
  SetDlgItemText(hwndDlg,IDC_EDIT_MULTI,"First line\nSecond editable line");
  SendDlgItemMessage(hwndDlg,IDC_CHECK1,BM_SETCHECK,BST_CHECKED,0);
  SendDlgItemMessage(hwndDlg,IDC_RADIO1,BM_SETCHECK,BST_CHECKED,0);
  SendDlgItemMessage(hwndDlg,IDC_RADIO2,BM_SETCHECK,BST_UNCHECKED,0);

  SWELL_CB_AddString(hwndDlg,IDC_COMBO1,"Alpha");
  SWELL_CB_AddString(hwndDlg,IDC_COMBO1,"Beta");
  SWELL_CB_AddString(hwndDlg,IDC_COMBO1,"Gamma");
  SWELL_CB_SetCurSel(hwndDlg,IDC_COMBO1,0);

  SendDlgItemMessage(hwndDlg,IDC_LIST1,LB_ADDSTRING,0,(LPARAM)"List row one");
  SendDlgItemMessage(hwndDlg,IDC_LIST1,LB_ADDSTRING,0,(LPARAM)"List row two");
  SendDlgItemMessage(hwndDlg,IDC_LIST1,LB_ADDSTRING,0,(LPARAM)"List row three");
  SendDlgItemMessage(hwndDlg,IDC_LIST1,LB_SETCURSEL,0,0);

  SWELL_TB_SetRange(hwndDlg,IDC_SLIDER1,0,100);
  SWELL_TB_SetPos(hwndDlg,IDC_SLIDER1,40);
  SendDlgItemMessage(hwndDlg,IDC_PROGRESS1,PBM_SETRANGE,0,MAKELONG(0,100));
  SendDlgItemMessage(hwndDlg,IDC_PROGRESS1,PBM_SETPOS,g_progress,0);

  HWND tab = GetDlgItem(hwndDlg,IDC_TAB1);
  if (tab)
  {
    TCITEM item = { TCIF_TEXT, 0, 0, (char *)"Tab one", 0, 0, 0 };
    TabCtrl_InsertItem(tab,0,&item);
    item.pszText = (char *)"Tab two";
    TabCtrl_InsertItem(tab,1,&item);
    TabCtrl_SetCurSel(tab,0);
  }
}

static void updateProgress(HWND hwndDlg)
{
  int slider = SWELL_TB_GetPos(hwndDlg,IDC_SLIDER1);
  if (slider < 0) slider = 0;
  if (slider > 100) slider = 100;
  g_progress = slider;
  SendDlgItemMessage(hwndDlg,IDC_PROGRESS1,PBM_SETPOS,g_progress,0);
  char buf[128];
  snprintf(buf,sizeof(buf),"Status: progress set to %d",g_progress);
  setStatus(hwndDlg,buf);
}

static void handleRadio(HWND hwndDlg, int which)
{
  SendDlgItemMessage(hwndDlg,IDC_RADIO1,BM_SETCHECK,which == IDC_RADIO1 ? BST_CHECKED : BST_UNCHECKED,0);
  SendDlgItemMessage(hwndDlg,IDC_RADIO2,BM_SETCHECK,which == IDC_RADIO2 ? BST_CHECKED : BST_UNCHECKED,0);
  setStatus(hwndDlg,which == IDC_RADIO1 ? "Status: radio alpha selected" : "Status: radio beta selected");
}

WDL_DLGRET mainProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static WDL_WndSizer resize;
  switch (uMsg)
  {
    case WM_INITDIALOG:
      g_hwnd=hwndDlg;
#ifdef _WIN32
      {
        HICON icon=LoadIcon(g_hInstance,MAKEINTRESOURCE(IDI_ICON1));
        SetClassLongPtr(hwndDlg,GCLP_HICON,(LPARAM)icon);
      }
#endif

      resize.init(hwndDlg);
      resize.init_item(IDCANCEL,0,1,0,1);
      initMenu(hwndDlg);
      initControls(hwndDlg);
    return 1;
    case WM_CLOSE:
      DestroyWindow(hwndDlg);
    return 1;
    case WM_DESTROY:
      g_hwnd=NULL;
#ifdef __APPLE__
      SWELL_PostQuitMessage(0);
#elif defined(_WIN32)
      PostQuitMessage(0);
#else
      g_quit = true;
#endif
    break;
    case WM_SIZE:
      if (wParam != SIZE_MINIMIZED)
        resize.onResize();
    break;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDC_BUTTON1:
        case ID_SAMPLE_ACTIVATE:
          setStatus(hwndDlg,"Status: action activated");
        break;
        case IDC_BUTTON_STATE:
          SendDlgItemMessage(hwndDlg,IDC_CHECK1,BM_SETCHECK,
              SendDlgItemMessage(hwndDlg,IDC_CHECK1,BM_GETCHECK,0,0) == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED,0);
          SetDlgItemText(hwndDlg,IDC_EDIT1,"State changed");
          setStatus(hwndDlg,"Status: checkbox and edit text changed");
        break;
        case IDC_BUTTON_PROGRESS:
          g_progress = (g_progress + 10) % 110;
          SendDlgItemMessage(hwndDlg,IDC_PROGRESS1,PBM_SETPOS,g_progress,0);
          setStatus(hwndDlg,"Status: progress stepped");
        break;
        case IDC_CHECK1:
          setStatus(hwndDlg,"Status: checkbox toggled");
        break;
        case IDC_RADIO1:
        case IDC_RADIO2:
          handleRadio(hwndDlg,LOWORD(wParam));
        break;
        case IDC_COMBO1:
          if (HIWORD(wParam) == CBN_SELCHANGE) setStatus(hwndDlg,"Status: combo selection changed");
        break;
        case IDC_LIST1:
          if (HIWORD(wParam) == LBN_SELCHANGE) setStatus(hwndDlg,"Status: list selection changed");
        break;
        case ID_SAMPLE_CHECKED:
          CheckMenuItem(g_options_menu,ID_SAMPLE_CHECKED,MF_BYCOMMAND|
              (menuItemChecked(g_options_menu,ID_SAMPLE_CHECKED) ? MF_UNCHECKED : MF_CHECKED));
          setStatus(hwndDlg,"Status: checked menu item activated");
        break;
        case ID_SAMPLE_RADIO_A:
        case ID_SAMPLE_RADIO_B:
          CheckMenuItem(g_options_menu,ID_SAMPLE_RADIO_A,MF_BYCOMMAND|(LOWORD(wParam) == ID_SAMPLE_RADIO_A ? MF_CHECKED : MF_UNCHECKED));
          CheckMenuItem(g_options_menu,ID_SAMPLE_RADIO_B,MF_BYCOMMAND|(LOWORD(wParam) == ID_SAMPLE_RADIO_B ? MF_CHECKED : MF_UNCHECKED));
          setStatus(hwndDlg,LOWORD(wParam) == ID_SAMPLE_RADIO_A ? "Status: menu radio A" : "Status: menu radio B");
        break;
        case ID_SAMPLE_SUBITEM:
          setStatus(hwndDlg,"Status: submenu item activated");
        break;
        case ID_QUIT:
        case IDCANCEL:
          DestroyWindow(hwndDlg);
        break;
      }
    break;
    case WM_HSCROLL:
      updateProgress(hwndDlg);
    break;
  }
  return 0;
}

INT_PTR SWELLAppMain(int msg, INT_PTR parm1, INT_PTR parm2)
{
  switch (msg)
  {
    case SWELLAPP_ONLOAD:
      {
      }
    break;
    case SWELLAPP_LOADED:
      {
        HWND h=CreateDialog(NULL,MAKEINTRESOURCE(IDD_DIALOG1),NULL,mainProc);
        ShowWindow(h,SW_SHOW);
      }
    break;
    case SWELLAPP_DESTROY:
      if (g_hwnd) DestroyWindow(g_hwnd);
    break;
    case SWELLAPP_ONCOMMAND:
      // this is to catch commands coming from the system menu etc
      if (g_hwnd && parm1) SendMessage(g_hwnd,WM_COMMAND,parm1,0);
    break;

  }
  return 0;
}



#ifdef _WIN32

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
  g_hInstance = hInstance;

  SWELLAppMain(SWELLAPP_ONLOAD,0,0);
  SWELLAppMain(SWELLAPP_LOADED,0,0);

  for(;;)
  {
    MSG msg={0,};
    int vvv = GetMessage(&msg,NULL,0,0);
    if (!vvv) break;

    if (vvv<0)
    {
      Sleep(10);
      continue;
    }
    if (!msg.hwnd)
    {
      DispatchMessage(&msg);
      continue;
    }
    if (SWELLAppMain(SWELLAPP_PROCESSMESSAGE, (INT_PTR) &msg, 0)) continue;

    if (g_hwnd && IsDialogMessage(g_hwnd,&msg)) continue;

    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  SWELLAppMain(SWELLAPP_DESTROY,0,0);

  ExitProcess(0);
  
  return 0;
}

#else

/************** SWELL stuff ********** */

#ifdef __APPLE__
extern "C" {
#endif

const char **g_argv;
int g_argc;

#ifdef __APPLE__
};
#endif


#ifndef __APPLE__

int main(int argc, const char **argv)
{
  g_argc=argc;
  g_argv=argv;
  SWELL_initargs(&argc,(char***)&argv);
  SWELL_Internal_PostMessage_Init();
  SWELL_ExtendedAPI("APPNAME",(void*)"MyApp");
  SWELLAppMain(SWELLAPP_ONLOAD,0,0);
  SWELLAppMain(SWELLAPP_LOADED,0,0);
  while (!g_quit) {
    SWELL_RunMessageLoop();
    Sleep(10);
  }
  SWELLAppMain(SWELLAPP_DESTROY,0,0);
  return 0;
}

#endif


#include "../../swell/swell-dlggen.h"
#include "res.rc_mac_dlg"
#undef BEGIN
#undef END
#include "../../swell/swell-menugen.h"
#include "res.rc_mac_menu"

#endif
