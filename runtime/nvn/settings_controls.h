#pragma once
#include <windows.h>
void nvn_settings_open(HWND owner);
void nvn_settings_close();
int nvn_settings_message(MSG *message);
void nvn_settings_hotkey(int key);
extern "C" {
void nvn_settings_gpu(int index,const char *name,int usable,int selected);
void nvn_settings_presented();
int nvn_settings_fps_text(char *text,int size);
}
