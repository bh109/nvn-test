#define _CRT_SECURE_NO_WARNINGS
#include "online_menu.h"
#include "online_settings.h"
#include <shellapi.h>
#include <cstdio>
extern "C" {
#include "settings.h"
void net_status(char *,size_t);
}

namespace {
HWND panel, server_edit, name_edit, direct_check, status_label;
HFONT font;
UINT dpi=96;
volatile LONG opened;
enum { SAVE=100, PUBLIC_SERVER, LOCAL_SERVER, HOST_SERVER };
const char *public_server="smash.recomps.wtf:39421";
HWND control(const char *kind,const char *label,DWORD style,int x,int y,int w,int h,int id=0) {
    HWND item=CreateWindowExA(0,kind,label,WS_CHILD|WS_VISIBLE|style,MulDiv(x,dpi,96),MulDiv(y,dpi,96),MulDiv(w,dpi,96),MulDiv(h,dpi,96),panel,
        (HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
    SendMessageW(item,WM_SETFONT,(WPARAM)font,TRUE);
    return item;
}
void refresh_status() {
    char text[2048]={};net_status(text,sizeof text);
    SetWindowTextA(status_label,*text?text:"Offline. Start the game with Play Intel NVN Vulkan.bat to use Online settings.");
}
void load_settings() {
    char text[512]={};
    if (!settings_line_read("online_server.txt",text,sizeof text)) snprintf(text,sizeof text,"%s",public_server);
    SetWindowTextA(server_edit,text);
    text[0]=0;settings_line_read("online_name.txt",text,sizeof text);SetWindowTextA(name_edit,text);
    text[0]=0;settings_line_read("online_direct.txt",text,sizeof text);
    SendMessageW(direct_check,BM_SETCHECK,_strnicmp(text,"off",3)?BST_CHECKED:BST_UNCHECKED,0);
}
void save_settings() {
    char server[512],name[128];GetWindowTextA(server_edit,server,sizeof server);GetWindowTextA(name_edit,name,sizeof name);
    std::string normalized;
    if(!nvn_online::server_line(server,normalized)) {
        MessageBoxA(panel,"Use hostname:port or IPv4:port, optionally followed by a lobby code.\nExample: 192.168.100.10:39421\nUse off for offline mode.","Invalid lobby address",MB_ICONWARNING);return;
    }
    if(!nvn_online::player_name(name)) {MessageBoxA(panel,"Use 1-32 printable English/ASCII characters for your player name.","Invalid player name",MB_ICONWARNING);return;}
    const char *direct=SendMessageW(direct_check,BM_GETCHECK,0,0)==BST_CHECKED?"on":"off";
    if(!settings_line_write("online_server.txt",normalized.c_str()) || !settings_line_write("online_name.txt",name) || !settings_line_write("online_direct.txt",direct)) {
        MessageBoxA(panel,"Could not save all settings. Check that the project folder and settings files are writable.","Save failed",MB_ICONERROR);return;
    }
    fprintf(stderr,"[NVN online] settings saved; restart required\n");
    MessageBoxA(panel,"Saved. Close and reopen the game to connect with these settings.\nThe connection status still describes this running session.","Online settings saved",MB_OK);
}
void start_server() {
    wchar_t root[32768],script[32768],powershell[32768],arguments[32768];
    DWORD count=GetCurrentDirectoryW(32768,root);
    if(!count || count>=32768) return;
    if(swprintf_s(script,L"%s\\tools\\Start-NvnLobby.ps1",root)<0 || GetFileAttributesW(script)==INVALID_FILE_ATTRIBUTES) {
        MessageBoxA(panel,"tools/Start-NvnLobby.ps1 is missing. Keep the source tools with this installation.","Cannot start server",MB_ICONERROR);return;
    }
    UINT length=GetSystemDirectoryW(powershell,32768);
    if(!length || length>=32768) return;
    if(wcscat_s(powershell,L"\\WindowsPowerShell\\v1.0\\powershell.exe") ||
       swprintf_s(arguments,L"-NoProfile -ExecutionPolicy Bypass -File \"%s\"",script)<0) return;
    if((INT_PTR)ShellExecuteW(panel,L"open",powershell,arguments,root,SW_SHOWNORMAL)<=32) {
        MessageBoxA(panel,"Could not open the server console. Use Host WindowSmash NVN Lobby.bat instead.","Cannot start server",MB_ICONERROR);return;
    }
    SetWindowTextA(server_edit,"127.0.0.1:39421");
    fprintf(stderr,"[NVN online] local server console requested\n");
}
LRESULT CALLBACK procedure(HWND h,UINT message,WPARAM w,LPARAM l) {
    switch(message) {
    case WM_CREATE:
        panel=h;
        dpi=GetDpiForWindow(h);if(!dpi)dpi=96;
        font=CreateFontW(-MulDiv(9,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        control("STATIC","Online / lobby settings",0,18,16,590,22);
        control("STATIC","Lobby address (host:port, optional lobby code):",0,18,47,590,18);
        server_edit=control("EDIT","",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,18,69,590,25);
        SendMessageW(server_edit,EM_SETLIMITTEXT,288,0);
        control("BUTTON","Public lobby",WS_TABSTOP,18,105,145,28,PUBLIC_SERVER);
        control("BUTTON","Use this PC",WS_TABSTOP,173,105,145,28,LOCAL_SERVER);
        control("BUTTON","Start local Online server",WS_TABSTOP,328,105,280,28,HOST_SERVER);
        control("STATIC","Player name:",0,18,149,115,22);
        name_edit=control("EDIT","",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,135,145,300,25);
        SendMessageW(name_edit,EM_SETLIMITTEXT,32,0);
        direct_check=control("BUTTON","Allow direct peer connections (peers learn your IP address)",BS_AUTOCHECKBOX|WS_TABSTOP,18,184,590,24);
        control("STATIC","Current connection (updates every second):",0,18,223,590,20);
        status_label=control("STATIC","",SS_LEFT,18,246,590,80);
        control("STATIC","A local server has its own players/ratings. Other laptops use the host's LAN IP.\nFor an Asia internet lobby, run the server on a reachable machine in Asia.\nSave settings, then close and reopen the game. Esc closes this window.",0,18,329,590,62);
        control("BUTTON","Save for next start",BS_DEFPUSHBUTTON|WS_TABSTOP,18,407,270,30,SAVE);
        control("BUTTON","Close",WS_TABSTOP,470,407,138,30,IDCANCEL);
        load_settings();refresh_status();SetTimer(h,1,1000,nullptr);return 0;
    case WM_TIMER: if(nvn_online_menu_open())refresh_status();return 0;
    case WM_COMMAND:
        switch(LOWORD(w)) {
        case SAVE: save_settings();break;
        case PUBLIC_SERVER: SetWindowTextA(server_edit,public_server);break;
        case LOCAL_SERVER: SetWindowTextA(server_edit,"127.0.0.1:39421");break;
        case HOST_SERVER: start_server();break;
        case IDCANCEL: SendMessageW(h,WM_CLOSE,0,0);break;
        }return 0;
    case WM_CLOSE: ShowWindow(h,SW_HIDE);InterlockedExchange(&opened,0);SetForegroundWindow(GetWindow(h,GW_OWNER));return 0;
    case WM_DESTROY: KillTimer(h,1);if(font){DeleteObject(font);font=nullptr;}panel=nullptr;InterlockedExchange(&opened,0);return 0;
    }
    return DefWindowProcW(h,message,w,l);
}
}
void nvn_online_menu_toggle(HWND owner) {
    if(nvn_online_menu_open()) {SendMessageW(panel,WM_CLOSE,0,0);return;}
    if(!panel) {
        WNDCLASSW cls={};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);
        cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);cls.lpszClassName=L"WindowSmash-NVN-Online";
        RegisterClassW(&cls);
        UINT owner_dpi=GetDpiForWindow(owner);if(!owner_dpi)owner_dpi=96;
        RECT rectangle={0,0,MulDiv(630,owner_dpi,96),MulDiv(458,owner_dpi,96)};AdjustWindowRectExForDpi(&rectangle,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_CONTROLPARENT,owner_dpi);
        panel=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"WindowSmash NVN - Online settings",WS_CAPTION|WS_SYSMENU,
            CW_USEDEFAULT,CW_USEDEFAULT,rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,owner,nullptr,cls.hInstance,nullptr);
        if(!panel){MessageBoxA(owner,"Could not create Online settings window.","WindowSmash NVN",MB_ICONERROR);return;}
    }
    load_settings();InterlockedExchange(&opened,1);ShowWindow(panel,SW_SHOW);SetForegroundWindow(panel);SetFocus(server_edit);
    fprintf(stderr,"[NVN online] Online settings opened\n");
}
int nvn_online_menu_open(){return InterlockedCompareExchange(&opened,0,0)!=0;}
int nvn_online_menu_message(MSG *message){return nvn_online_menu_open()&&IsDialogMessageW(panel,message);}
