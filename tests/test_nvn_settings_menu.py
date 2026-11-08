"""Exercise the actual native settings controls against temporary personal settings.

The harness owns its Win32 window and fake pad. It never controls the running game.
INI parsing/writes are the real hle_settings.c implementation.
"""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class NativeSettingsTest(unittest.TestCase):
    def test_controls_and_frame_queue(self):
        compiler = ROOT / 'deps/llvm/bin/clang-cl.exe'
        if not compiler.exists():
            self.skipTest('Local compiler missing')
        source = r'''
#include <windows.h>
#include <cassert>
#include <cstring>
#include <cstdio>
static int notices;
static int test_message(HWND,const char *,const char *,UINT){notices++;return IDYES;}
#define MessageBoxA test_message
#include "nvn/settings_controls.cpp"
#include "nvn/frame_queue.h"
static PadInfo fake;
static DisplaySettings ds={0,1280,720,1,1,0,PACING_CLOCK,"auto",1};
extern "C" {
void display_settings(DisplaySettings *p){*p=ds;}
const char *display_ini_path(){return "display.ini";}
void display_set_mode(int mode){ds.mode=mode;}
void display_set_hide_cursor(int on){ds.hide_cursor=on;}
int pad_info(u32 slot,PadInfo *p){*p={};if(slot!=0)return 0;*p=fake;return p->connected;}
const char *pad_ini_path(){return "controllers.ini";}
int pad_keyboard_slot(){return 0;}
int pad_background(){return 0;}
int pad_rumble_on(){return 1;}
void pad_reload(){}
void pad_rumble(u32,int,float){}
const char *pad_button_name(int i){static const char *s[]={"A","B","X","Y","LSTICK","RSTICK","L","R","ZL","ZR","PLUS","MINUS","LEFT","UP","RIGHT","DOWN"};return s[i];}
const char *pad_raw_name(int i){return i==0?"south":i==30?"lefttrigger":"none";}
void gfx_closing(){assert(false);}
}
static std::string get(const char *file,const char *section,const char *key){return saved(file,section,key,"MISSING");}
int main(){
    for(int limit=1;limit<=3;limit++){
        NvnFrameQueue q={};
        for(int i=0;i<100;i++)assert(nvn_frame_queue_push(&q,100+i*7,limit)==(i+1>=limit?100+(i+1-limit)*7:0));
    }
    NvnFrameQueue q={};assert(!nvn_frame_queue_push(&q,100,0)&&q.count==0);
    assert(!nvn_frame_queue_push(&q,100,4)&&q.count==0);
    settings_ini_set("display.ini",nullptr,"custom","keep");
    settings_ini_set("controllers.ini","device Test Pad","unrelated","keep");
    fake.connected=1;fake.deadzone=15;fake.rumble=80;fake.trigger=25;
    strcpy(fake.name,"Test Pad");strcpy(fake.guid,"test-guid");
    nvn_settings_gpu(2,"Test Vulkan GPU",1,1);
    WNDCLASSW cls={};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"SettingsTest";RegisterClassW(&cls);
    HWND h=CreateWindowW(cls.lpszClassName,L"",WS_OVERLAPPEDWINDOW,0,0,640,700,nullptr,nullptr,cls.hInstance,nullptr);assert(h);
    // Actual controls and save callbacks, including the real comment-preserving writer.
    select(mode,2);select(cursor,0);select(present,3);select(gpu,1);select(fps,2);select(size,3);select(queue,2);save_display();
    assert(ds.mode==2&&ds.hide_cursor==0);
    assert(get("nvn_graphics.ini",nullptr,"gpu")=="2");
    assert(get("nvn_graphics.ini",nullptr,"present")=="immediate");
    assert(get("nvn_graphics.ini",nullptr,"frame_queue")=="2");
    assert(get("display.ini",nullptr,"width")=="1920"&&get("display.ini",nullptr,"height")=="1080");
    assert(get("display.ini",nullptr,"custom")=="keep");
    show_page(1);assert(SendMessageW(deadzone,CB_GETCOUNT,0,0)==51);select(player,2);select(layout,1);select(deadzone,25);select(rumble,60);select(trigger,39);select(keyboard,0);select(background,1);save_pad();
    assert(get("controllers.ini","device Test Pad","deadzone")=="25");
    assert(get("controllers.ini","device Test Pad","trigger_threshold")=="40");
    assert(get("controllers.ini","players","2")=="test-guid");
    assert(get("controllers.ini","general","keyboard_player")=="0");
    assert(get("controllers.ini","device Test Pad","unrelated")=="keep");
    fake.raw=1;select(button,0);SendMessageW(h,WM_COMMAND,CAPTURE,0);capture_tick();
    assert(get("controllers.ini","device Test Pad","A")=="MISSING");
    fake.raw=0;capture_tick();fake.raw=1u<<30;capture_tick();
    assert(get("controllers.ini","device Test Pad","A")=="lefttrigger");
    SendMessageW(h,WM_COMMAND,DEFAULT_BUTTON,0);assert(get("controllers.ini","device Test Pad","A")=="MISSING");
    SendMessageW(h,WM_COMMAND,CAPTURE,0);capture_deadline=0;capture_tick();assert(capture_button==-1);
    fake.connected=0;SendMessageW(h,WM_COMMAND,CAPTURE,0);assert(selected.connected==0);
    // Keyboard/background/global rumble remain editable without a connected controller.
    assert(IsWindowEnabled(GetDlgItem(h,SAVE_PAD)));select(keyboard,3);save_pad();
    assert(get("controllers.ini","general","keyboard_player")=="3");
    char text[160];nvn_settings_presented();assert(nvn_settings_fps_text(text,sizeof text));
    nvn_settings_hotkey(VK_F1);assert(!nvn_settings_fps_text(text,sizeof text));
    nvn_settings_hotkey(VK_F2);assert(nvn_settings_fps_text(text,sizeof text)&&strstr(text,"MS"));
    DestroyWindow(h);assert(notices>=3);
}
'''
        with tempfile.TemporaryDirectory(prefix='nvn menu test ') as tmp:
            work = pathlib.Path(tmp)
            (work / 'check.cpp').write_text(source)
            flags = [str(compiler), '/nologo', '-fuse-ld=lld', '/winsysroot', str(ROOT / 'deps/msvc'),
                     '/I', str(ROOT / 'runtime')]
            subprocess.run(flags + ['/TC', '/c', str(ROOT / 'runtime/hle_settings.c'), '/Fosettings.obj'],
                           cwd=work, check=True, capture_output=True, text=True)
            result = subprocess.run(flags + ['/std:c++17', '/EHsc', 'check.cpp', 'settings.obj',
                                              '/Fecheck.exe', '/link', 'user32.lib', 'gdi32.lib', 'shell32.lib'],
                                    cwd=work, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            subprocess.run([str(work / 'check.exe')], cwd=work, check=True, timeout=30,
                           capture_output=True, text=True)


if __name__ == '__main__':
    unittest.main()
