#define _CRT_SECURE_NO_WARNINGS
#include "settings_controls.h"
#include "display_adapter.h"
#include <shellapi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
extern "C" {
#include "display.h"
#include "pad.h"
#include "settings.h"
void gfx_closing(void);
}

namespace {
HWND panel, owner;
HFONT font;
UINT dpi=96;
int page;
std::vector<HWND> controls[3];
enum { DISPLAY_PAGE=800, PAD_PAGE, GAME_PAGE, MODE, CURSOR, PRESENT, GPU, FPS, SIZE, SAVE_DISPLAY, QUEUE,
       PAD, PLAYER, LAYOUT, DEADZONE, RUMBLE, TRIGGER, KEYBOARD, BACKGROUND, ALL_RUMBLE,
       SAVE_PAD, BUTTON, CAPTURE, DEFAULT_BUTTON, RESET_PAD, TEST_RUMBLE, RESTART, QUIT, CLOSE };
HWND mode, cursor, present, gpu, fps, size, queue, pad_list, player, layout, deadzone, rumble, trigger,
     keyboard, background, all_rumble, button, binding, pad_status;
struct Device {std::string name; bool usable=false;};
Device devices[16];
int running_gpu=-1;
SRWLOCK device_lock=SRWLOCK_INIT;
PadInfo selected={};
int selected_slot=-1, capture_button=-1;
u32 previous_raw;
ULONGLONG capture_deadline=0, rumble_deadline=0;
int rumble_slot=-1;
volatile LONG fps_mode=0, fps_initialized=0;
SRWLOCK timing_lock=SRWLOCK_INIT;
LARGE_INTEGER frequency={}, first_time={}, last_time={};
unsigned frame_count;
double measured_fps, last_ms, worst_ms, measured_worst;

int choice(HWND h){return (int)SendMessageW(h,CB_GETCURSEL,0,0);}
void select(HWND h,int i){SendMessageW(h,CB_SETCURSEL,i,0);}
HWND item(int tab,const char *kind,const char *label,DWORD style,int x,int y,int w,int h,int id=0){
    HWND result=CreateWindowExA(0,kind,label,WS_CHILD|style,MulDiv(x,dpi,96),MulDiv(y,dpi,96),MulDiv(w,dpi,96),MulDiv(h,dpi,96),panel,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
    SendMessageW(result,WM_SETFONT,(WPARAM)font,TRUE);
    if(tab>=0)controls[tab].push_back(result);else ShowWindow(result,SW_SHOW);
    return result;
}
HWND combo(int tab,const char *label,int y,int id,const std::vector<std::string> &values){
    item(tab,"STATIC",label,0,18,y+4,208,22);
    HWND h=item(tab,"COMBOBOX","",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,230,y,360,250,id);
    for(const auto &value:values)SendMessageA(h,CB_ADDSTRING,0,(LPARAM)value.c_str());
    select(h,0);return h;
}
void info(int tab,const char *text,int y,int height=42){item(tab,"STATIC",text,0,18,y,572,height);}
void error(){MessageBoxA(panel,"Could not save all settings. Check that the folder is writable.","Save failed",MB_ICONERROR);}
std::string saved(const char *file,const char *section,const char *key,const char *fallback){char text[256];return settings_ini_get(file,section,key,text,sizeof text)?text:fallback;}
void changed(const char *text){MessageBoxA(panel,text,"WindowSmash settings",MB_OK);}
void initialize_fps(){if(InterlockedCompareExchange(&fps_initialized,1,0)==0){const char *e=getenv("SSBU_NVN_FPS");int mode=e?atoi(e):0;InterlockedExchange(&fps_mode,mode>=0&&mode<=2?mode:0);}}
void load_display(){
    DisplaySettings ds;display_settings(&ds);select(mode,ds.mode);select(cursor,ds.hide_cursor?1:0);
    const char *modes[]={"auto","mailbox","fifo","immediate"};auto value=saved("nvn_graphics.ini",nullptr,"present","mailbox");
    for(int i=0;i<4;i++)if(value==modes[i])select(present,i);
    initialize_fps();select(fps,(int)InterlockedCompareExchange(&fps_mode,0,0));
    int frames=atoi(saved("nvn_graphics.ini",nullptr,"frame_queue","0").c_str());select(queue,frames>=0&&frames<=3?frames:0);
    SendMessageW(gpu,CB_RESETCONTENT,0,0);SendMessageA(gpu,CB_ADDSTRING,0,(LPARAM)"Auto (prefer a usable discrete GPU)");SendMessageW(gpu,CB_SETITEMDATA,0,-1);
    auto configured=saved("nvn_graphics.ini",nullptr,"gpu","0");int wanted=configured=="auto"?-1:atoi(configured.c_str()), selected_index=0;
    AcquireSRWLockShared(&device_lock);
    for(int i=0;i<16;i++)if(devices[i].usable){char label[320];snprintf(label,sizeof label,"%d: %s%s",i,devices[i].name.c_str(),i==running_gpu?" (running)":"");int n=(int)SendMessageA(gpu,CB_ADDSTRING,0,(LPARAM)label);SendMessageW(gpu,CB_SETITEMDATA,n,i);if(wanted==i)selected_index=n;}
    ReleaseSRWLockShared(&device_lock);select(gpu,selected_index);
    // Preserve a custom window size unless the user explicitly picks a preset.
    SendMessageW(size,CB_RESETCONTENT,0,0);char label[80];snprintf(label,sizeof label,"Keep current (%d x %d)",ds.width,ds.height);SendMessageA(size,CB_ADDSTRING,0,(LPARAM)label);
    for(const char *s:{"1280 x 720","1600 x 900","1920 x 1080","2560 x 1440","3840 x 2160"})SendMessageA(size,CB_ADDSTRING,0,(LPARAM)s);select(size,0);
}
void save_display(){
    const char *modes[]={"windowed","borderless","fullscreen"};const char *presents[]={"auto","mailbox","fifo","immediate"};
    bool ok=settings_ini_set(display_ini_path(),nullptr,"window",modes[choice(mode)])!=0;
    ok=settings_ini_set(display_ini_path(),nullptr,"hide_cursor",choice(cursor)?"1":"0")&&ok;
    char number[24];int device=(int)SendMessageW(gpu,CB_GETITEMDATA,choice(gpu),0);snprintf(number,sizeof number,"%d",device);
    ok=settings_ini_set("nvn_graphics.ini",nullptr,"gpu",device<0?"auto":number)&&ok;
    ok=settings_ini_set("nvn_graphics.ini",nullptr,"present",presents[choice(present)])&&ok;
    snprintf(number,sizeof number,"%d",choice(fps));ok=settings_ini_set("nvn_graphics.ini",nullptr,"fps_overlay",number)&&ok;
    snprintf(number,sizeof number,"%d",choice(queue));ok=settings_ini_set("nvn_graphics.ini",nullptr,"frame_queue",number)&&ok;
    if(choice(size)>0){const int widths[]={1280,1600,1920,2560,3840},heights[]={720,900,1080,1440,2160};int i=choice(size)-1;
        snprintf(number,sizeof number,"%d",widths[i]);ok=settings_ini_set(display_ini_path(),nullptr,"width",number)&&ok;
        snprintf(number,sizeof number,"%d",heights[i]);ok=settings_ini_set(display_ini_path(),nullptr,"height",number)&&ok;}
    if(!ok){error();return;}
    display_set_mode(choice(mode));display_set_hide_cursor(choice(cursor));InterlockedExchange(&fps_mode,choice(fps));
    changed("Window mode, cursor and FPS display applied. Restart the game to apply GPU, presentation mode, frame queue or window-size changes.");
}
void section(char *text,size_t count){snprintf(text,count,"device %s",selected.section[0]?selected.section:selected.name);}
bool device_value(const char *key,const char *value){char s[160];section(s,sizeof s);return settings_ini_set(pad_ini_path(),s,key,value)!=0;}
void binding_text(){
    if(!selected.connected){SetWindowTextA(binding,"No controller selected.");return;}
    PadInfo current;if(pad_info(selected_slot,&current)&&!strcmp(current.guid,selected.guid)){
        char text[160];snprintf(text,sizeof text,"Current binding: %s",current.map[choice(button)]);SetWindowTextA(binding,text);
    }
}
void load_pad(){
    int slot=choice(pad_list);selected_slot=slot>=0?(int)SendMessageW(pad_list,CB_GETITEMDATA,slot,0):-1;
    selected={};if(selected_slot>=0)pad_info(selected_slot,&selected);
    for(HWND h:{player,layout,deadzone,rumble,trigger,button,GetDlgItem(panel,CAPTURE),GetDlgItem(panel,DEFAULT_BUTTON),GetDlgItem(panel,RESET_PAD),GetDlgItem(panel,TEST_RUMBLE)})EnableWindow(h,selected.connected);
    EnableWindow(player,selected.connected&&!selected.xinput);
    if(selected.connected){
        select(layout,selected.layout?1:0);select(deadzone,selected.deadzone);select(rumble,selected.rumble);select(trigger,selected.trigger-1);
        int pin=0;for(int i=1;i<=PAD_SLOTS;i++){char key[8];snprintf(key,sizeof key,"%d",i);auto p=saved(pad_ini_path(),"players",key,"");if(!p.empty()&&(!_stricmp(p.c_str(),selected.guid)||!_stricmp(p.c_str(),selected.name)||!_stricmp(p.c_str(),selected.type)))pin=i;}select(player,pin);
    }
    select(keyboard,pad_keyboard_slot()+1);select(background,pad_background()?1:0);select(all_rumble,pad_rumble_on()?1:0);
    SetWindowTextA(pad_status,selected.xinput?"XInput fallback: player assignment follows the controller index.":"Settings apply after Save. Remapping captures a button or trigger; Esc cancels.");
    capture_button=-1;binding_text();
}
void refresh_pads(){
    int old=selected_slot;SendMessageW(pad_list,CB_RESETCONTENT,0,0);int chosen=0;
    for(int i=0;i<PAD_SLOTS;i++){PadInfo p;if(!pad_info(i,&p))continue;char text[160];snprintf(text,sizeof text,"Player %d: %s",i+1,p.name);int n=(int)SendMessageA(pad_list,CB_ADDSTRING,0,(LPARAM)text);SendMessageW(pad_list,CB_SETITEMDATA,n,i);if(i==old)chosen=n;}
    if(SendMessageW(pad_list,CB_GETCOUNT,0,0)==0){SendMessageA(pad_list,CB_ADDSTRING,0,(LPARAM)"No controller connected");SendMessageW(pad_list,CB_SETITEMDATA,0,-1);}select(pad_list,chosen);load_pad();
}
void save_pad(){
    bool ok=true;char number[32];
    if(selected.connected){
        PadInfo current;if(!pad_info(selected_slot,&current)||strcmp(current.guid,selected.guid)){changed("Controller disconnected or changed. Select it again before saving.");refresh_pads();return;}
        ok=device_value("layout",choice(layout)?"labels":"positional");
        for(auto pair:{std::pair<const char *,int>{"deadzone",choice(deadzone)},{"rumble",choice(rumble)},{"trigger_threshold",choice(trigger)+1}}){snprintf(number,sizeof number,"%d",pair.second);ok=device_value(pair.first,number)&&ok;}
        if(!selected.xinput){
            for(int i=1;i<=PAD_SLOTS;i++){snprintf(number,sizeof number,"%d",i);auto p=saved(pad_ini_path(),"players",number,"");if(!p.empty()&&(!_stricmp(p.c_str(),selected.guid)||!_stricmp(p.c_str(),selected.name)||!_stricmp(p.c_str(),selected.type)))ok=settings_ini_set(pad_ini_path(),"players",number,nullptr)&&ok;}
            if(choice(player)>0){snprintf(number,sizeof number,"%d",choice(player));ok=settings_ini_set(pad_ini_path(),"players",number,selected.guid)&&ok;}
        }
    }
    snprintf(number,sizeof number,"%d",choice(keyboard));ok=settings_ini_set(pad_ini_path(),"general","keyboard_player",number)&&ok;
    ok=settings_ini_set(pad_ini_path(),"general","background",choice(background)?"1":"0")&&ok;
    ok=settings_ini_set(pad_ini_path(),"general","rumble",choice(all_rumble)?"1":"0")&&ok;
    pad_reload();if(!ok)error();else changed("Controller settings saved and applied. Changing player assignment may require reconnecting the controller.");
}
void stop_rumble(){if(rumble_slot>=0){pad_rumble(rumble_slot,1,0);pad_rumble(rumble_slot,2,0);rumble_slot=-1;}}
void capture_tick(){
    if(rumble_slot>=0&&GetTickCount64()>=rumble_deadline)stop_rumble();
    if(capture_button<0)return;
    PadInfo p;if(!pad_info(selected_slot,&p)||strcmp(p.guid,selected.guid)||GetTickCount64()>=capture_deadline){capture_button=-1;SetWindowTextA(binding,"Capture cancelled (timeout or controller disconnected).");return;}
    u32 pressed=p.raw&~previous_raw;previous_raw=p.raw;
    // Guide/Home is reserved for opening settings, not a game-button binding.
    pressed&=~(1u<<PAD_RAW_GUIDE_BIT);
    for(int bit=0;bit<32;bit++)if((pressed>>bit&1)&&strcmp(pad_raw_name(bit),"none")){
        bool ok=device_value(pad_button_name(capture_button),pad_raw_name(bit));capture_button=-1;pad_reload();if(!ok)error();else SetWindowTextA(binding,"Binding saved. Select another game button to remap.");break;
    }
}
void game_action(bool restart){
    if(MessageBoxA(panel,restart?"Restart the game? Any match in progress will end.":"Quit the game? Any match in progress will end.","WindowSmash",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
    if(restart){
        wchar_t root[32768],exe[32768],script[32768],args[32768],ps[MAX_PATH];
        if(!GetCurrentDirectoryW(32768,root)||!GetModuleFileNameW(nullptr,exe,32768))return;
        wchar_t *slash=wcsrchr(exe,L'\\');if(!slash)return;*slash=0;
        swprintf_s(script,L"%s\\tools\\Restart-NvnGame.ps1",root);
        UINT n=GetSystemDirectoryW(ps,MAX_PATH);if(!n||n>=MAX_PATH)return;wcscat_s(ps,L"\\WindowsPowerShell\\v1.0\\powershell.exe");
        const wchar_t *cache=_wgetenv(L"SSBU_VK_CACHE");
        if(swprintf_s(args,L"-NoProfile -ExecutionPolicy Bypass -File \"%s\" -GameProcessId %lu -BuildDirectory \"%s\" -CacheDirectory \"%s\"%s",script,GetCurrentProcessId(),exe,cache?cache:L"",settings_online()?L" -Online":L"")<0)return;
        if((INT_PTR)ShellExecuteW(panel,L"open",ps,args,root,SW_HIDE)<=32){changed("Could not schedule restart. Close and reopen the game manually.");return;}
    }
    stop_rumble();gfx_closing();fflush(stdout);fflush(stderr);ExitProcess(0);
}
void show_page(int next){
    stop_rumble();capture_button=-1;page=next;
    for(int i=0;i<3;i++)for(HWND h:controls[i])ShowWindow(h,i==page?SW_SHOW:SW_HIDE);
    if(page==0)load_display();if(page==1)refresh_pads();
}
LRESULT CALLBACK procedure(HWND h,UINT message,WPARAM w,LPARAM l){
    switch(message){
    case WM_CREATE:{
        panel=h;dpi=(UINT)(UINT_PTR)((CREATESTRUCTW *)l)->lpCreateParams;if(!dpi)dpi=GetDpiForWindow(h);if(!dpi)dpi=96;
        font=CreateFontW(-MulDiv(9,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        item(-1,"BUTTON","Display",WS_TABSTOP,18,12,175,30,DISPLAY_PAGE);item(-1,"BUTTON","Controllers",WS_TABSTOP,208,12,175,30,PAD_PAGE);item(-1,"BUTTON","Game",WS_TABSTOP,398,12,192,30,GAME_PAGE);
        mode=combo(0,"Window mode",65,MODE,{"Windowed","Borderless","Fullscreen (borderless, always on top)"});
        size=combo(0,"Window size (next start)",105,SIZE,{});
        cursor=combo(0,"Hide idle mouse cursor",145,CURSOR,{"Off","On"});
        present=combo(0,"Presentation (next start)",185,PRESENT,{"Auto (60 Hz game clock)","Mailbox (default)","VSync FIFO (monitor refresh)","Immediate (may tear)"});
        gpu=combo(0,"Vulkan GPU (next start)",225,GPU,{});
        fps=combo(0,"FPS overlay (F1 / F2)",265,FPS,{"Off","FPS","FPS and frame interval"});
        info(0,"Game simulation keeps its existing 60 Hz clock in every presentation mode.\nFIFO can add latency or stutter on monitors whose refresh does not match.",310);
        queue=combo(0,"GPU frame-ahead limit",365,QUEUE,{"Backend default (unchanged)","1 frame (lowest queue latency)","2 frames","3 frames"});
        info(0,"Rendering resolution is in the main Settings window.\nSave display applies window mode, cursor and overlay now; other changes need Restart.",420);
        item(0,"BUTTON","Save display settings",WS_TABSTOP,18,490,280,30,SAVE_DISPLAY);
        pad_list=combo(1,"Controller",60,PAD,{});
        std::vector<std::string> players={"Auto"};for(int i=1;i<=8;i++)players.push_back("Player "+std::to_string(i));player=combo(1,"Assigned player",96,PLAYER,players);
        layout=combo(1,"Face buttons",132,LAYOUT,{"By position (Switch layout)","By printed label"});
        std::vector<std::string> percent;for(int i=0;i<=100;i++)percent.push_back(std::to_string(i)+"%");
        deadzone=combo(1,"Stick dead zone",168,DEADZONE,std::vector<std::string>(percent.begin(),percent.begin()+51));rumble=combo(1,"Rumble strength",204,RUMBLE,percent);
        percent.erase(percent.begin());percent.pop_back();trigger=combo(1,"Trigger press point",240,TRIGGER,percent);
        players[0]="Disabled";keyboard=combo(1,"Keyboard plays as",276,KEYBOARD,players);
        background=combo(1,"Pads when not focused",312,BACKGROUND,{"Off","On"});all_rumble=combo(1,"All controller rumble",348,ALL_RUMBLE,{"Off","On"});
        std::vector<std::string> buttons;for(int i=0;i<16;i++)buttons.push_back(pad_button_name(i));button=combo(1,"Game button to remap",384,BUTTON,buttons);
        binding=item(1,"STATIC","",0,18,420,572,22);
        item(1,"BUTTON","Capture input",WS_TABSTOP,18,448,180,28,CAPTURE);item(1,"BUTTON","Default binding",WS_TABSTOP,212,448,180,28,DEFAULT_BUTTON);item(1,"BUTTON","Test rumble",WS_TABSTOP,406,448,184,28,TEST_RUMBLE);
        item(1,"BUTTON","Save controller settings",WS_TABSTOP,18,486,280,30,SAVE_PAD);item(1,"BUTTON","Reset selected controller",WS_TABSTOP,310,486,280,30,RESET_PAD);
        pad_status=item(1,"STATIC","",0,18,530,572,44);
        info(2,"Restart applies saved graphics and network settings.\nThe old process finishes cache saving before the replacement starts.",70);
        item(2,"BUTTON","Restart game",WS_TABSTOP,18,135,280,32,RESTART);item(2,"BUTTON","Quit game",WS_TABSTOP,310,135,280,32,QUIT);
        info(2,"Shortcuts\nEsc / controller Home: settings\nF1: FPS overlay on/off\nF2: FPS details\nF11 / Alt+Enter: fullscreen/windowed",210,140);
        info(2,"Controller edits are written to controllers.ini. Custom multi-input or stick-axis\nbindings can still be edited there by hand; this capture tool records buttons/triggers.",390);
        item(-1,"BUTTON","Back to Online / resolution",WS_TABSTOP,18,590,572,30,CLOSE);
        SetTimer(h,1,100,nullptr);show_page(0);return 0;
    }
    case WM_TIMER:if(IsWindowVisible(h))capture_tick();return 0;
    case WM_COMMAND:switch(LOWORD(w)){
        case DISPLAY_PAGE:show_page(0);break;case PAD_PAGE:show_page(1);break;case GAME_PAGE:show_page(2);break;
        case SAVE_DISPLAY:save_display();break;
        case PAD:if(HIWORD(w)==CBN_SELCHANGE){stop_rumble();load_pad();}break;
        case BUTTON:if(HIWORD(w)==CBN_SELCHANGE){capture_button=-1;binding_text();}break;
        case SAVE_PAD:save_pad();break;
        case CAPTURE:{PadInfo p;if(pad_info(selected_slot,&p)&&!strcmp(p.guid,selected.guid)){capture_button=choice(button);previous_raw=p.raw;capture_deadline=GetTickCount64()+10000;SetWindowTextA(binding,"Release held controls, then press the desired button/trigger (10 seconds).");}else refresh_pads();break;}
        case DEFAULT_BUTTON:if(selected.connected){if(!device_value(pad_button_name(choice(button)),nullptr))error();pad_reload();SetWindowTextA(binding,"Default binding restored.");}break;
        case RESET_PAD:if(selected.connected&&MessageBoxA(h,"Reset this controller's buttons, layout, dead zone, rumble and trigger settings?","Reset controller",MB_YESNO)==IDYES){bool ok=true;for(int i=0;i<16;i++)ok=device_value(pad_button_name(i),nullptr)&&ok;for(const char *key:{"layout","deadzone","rumble","trigger_threshold"})ok=device_value(key,nullptr)&&ok;pad_reload();if(!ok)error();else changed("Controller overrides removed. Reopen this page after the input thread reloads.");}break;
        case TEST_RUMBLE:if(selected.connected){stop_rumble();rumble_slot=selected_slot;pad_rumble(rumble_slot,1,0.5f);pad_rumble(rumble_slot,2,0.5f);rumble_deadline=GetTickCount64()+500;}break;
        case RESTART:game_action(true);break;case QUIT:game_action(false);break;
        case CLOSE:case IDCANCEL:SendMessageW(h,WM_CLOSE,0,0);break;
        }return 0;
    case WM_CLOSE:if(capture_button>=0){capture_button=-1;binding_text();return 0;}stop_rumble();ShowWindow(h,SW_HIDE);SetForegroundWindow(owner);return 0;
    case WM_DESTROY:stop_rumble();KillTimer(h,1);if(font)DeleteObject(font);font=nullptr;panel=nullptr;for(auto &list:controls)list.clear();return 0;
    }
    return DefWindowProcW(h,message,w,l);
}
}
void nvn_settings_open(HWND parent){
    owner=parent;
    if(!panel){WNDCLASSW cls={};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorA(nullptr,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);cls.lpszClassName=L"WindowSmash-NVN-Controls";RegisterClassW(&cls);
        UINT d=GetDpiForWindow(parent);if(!d)d=96;
        MONITORINFO monitor={sizeof monitor};bool have_monitor=GetMonitorInfoW(MonitorFromWindow(parent,MONITOR_DEFAULTTONEAREST),&monitor)!=0;
        if(have_monitor){int cap=MulDiv(monitor.rcWork.bottom-monitor.rcWork.top-60,96,636);int width_cap=MulDiv(monitor.rcWork.right-monitor.rcWork.left-40,96,610);if(width_cap<cap)cap=width_cap;if(cap>=72&&(UINT)cap<d)d=(UINT)cap;}
        RECT r={0,0,MulDiv(610,d,96),MulDiv(636,d,96)};AdjustWindowRectExForDpi(&r,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_CONTROLPARENT,d);
        int x=have_monitor?monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-(r.right-r.left))/2:CW_USEDEFAULT;
        int y=have_monitor?monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-(r.bottom-r.top))/2:CW_USEDEFAULT;
        panel=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"WindowSmash NVN - Display, controllers and game",WS_CAPTION|WS_SYSMENU,x,y,r.right-r.left,r.bottom-r.top,parent,nullptr,cls.hInstance,(void *)(UINT_PTR)d);}
    if(panel){show_page(page);ShowWindow(panel,SW_SHOW);SetForegroundWindow(panel);SetFocus(GetDlgItem(panel,DISPLAY_PAGE));}
}
void nvn_settings_close(){capture_button=-1;stop_rumble();if(panel)ShowWindow(panel,SW_HIDE);}
int nvn_settings_message(MSG *message){return panel&&IsWindowVisible(panel)&&IsDialogMessageW(panel,message);}
void nvn_settings_hotkey(int key){initialize_fps();LONG mode=InterlockedCompareExchange(&fps_mode,0,0);InterlockedExchange(&fps_mode,key==VK_F1?(mode?0:1):(mode==2?1:2));}
extern "C" void nvn_settings_gpu(int index,const char *name,int usable,int selected){
    if(index<0||index>=16)return;AcquireSRWLockExclusive(&device_lock);if(*name){devices[index].name=name;devices[index].usable=usable!=0;}if(selected)running_gpu=index;ReleaseSRWLockExclusive(&device_lock);
}
extern "C" void nvn_settings_presented(){
    LARGE_INTEGER now;QueryPerformanceCounter(&now);AcquireSRWLockExclusive(&timing_lock);
    if(!frequency.QuadPart)QueryPerformanceFrequency(&frequency);
    if(last_time.QuadPart){last_ms=1000.0*(now.QuadPart-last_time.QuadPart)/frequency.QuadPart;if(last_ms>worst_ms)worst_ms=last_ms;}
    last_time=now;if(!first_time.QuadPart)first_time=now;else{frame_count++;double elapsed=(double)(now.QuadPart-first_time.QuadPart)/frequency.QuadPart;if(elapsed>=1){measured_fps=frame_count/elapsed;measured_worst=worst_ms;worst_ms=0;frame_count=0;first_time=now;}}
    ReleaseSRWLockExclusive(&timing_lock);
}
extern "C" int nvn_settings_fps_text(char *text,int count){
    initialize_fps();LONG mode=InterlockedCompareExchange(&fps_mode,0,0);if(!mode)return 0;
    AcquireSRWLockShared(&timing_lock);if(mode==2)snprintf(text,count,"%.1f FPS  %.2f MS  MAX %.2f MS",measured_fps,last_ms,measured_worst);else snprintf(text,count,"%.1f FPS",measured_fps);ReleaseSRWLockShared(&timing_lock);return 1;
}
