/******************************************************************************
    Copyright (C) 2013 by Hugh Bailey <obs.jim@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "util/platform.h"
#include "util/dstr.h"
#include "obs.h"
#include "obs-internal.h"

#include <windows.h>

const char *get_module_extension(void)
{
	return ".dll";
}

#ifdef _WIN64
#define BIT_STRING "64bit"
#else
#define BIT_STRING "32bit"
#endif

static const char *module_bin[] = {
	"obs-plugins/" BIT_STRING,
	"../../obs-plugins/" BIT_STRING,
};

static const char *module_data[] = {
	"data/%module%",
	"../../data/obs-plugins/%module%"
};

static const int module_patterns_size =
	sizeof(module_bin)/sizeof(module_bin[0]);

void add_default_module_paths(void)
{
	for (int i = 0; i < module_patterns_size; i++)
		obs_add_module_path(module_bin[i], module_data[i]);
}

/* on windows, points to [base directory]/data/libobs */
char *find_libobs_data_file(const char *file)
{
	struct dstr path;
	dstr_init(&path);

	if (check_path(file, "data/libobs/", &path))
		return path.array;

	if (check_path(file, "../../data/libobs/", &path))
		return path.array;

	dstr_free(&path);
	return NULL;
}

static void log_processor_info(void)
{
	HKEY    key;
	wchar_t data[1024];
	char    *str = NULL;
	DWORD   size, speed;
	LSTATUS status;

	memset(data, 0, 1024);

	status = RegOpenKeyW(HKEY_LOCAL_MACHINE,
			L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
			&key);
	if (status != ERROR_SUCCESS)
		return;

	size = 1024;
	status = RegQueryValueExW(key, L"ProcessorNameString", NULL, NULL,
			(LPBYTE)data, &size);
	if (status == ERROR_SUCCESS) {
		os_wcs_to_utf8_ptr(data, 0, &str);
		blog(LOG_INFO, "CPU Name: %s", str);
		bfree(str);
	}

	size = sizeof(speed);
	status = RegQueryValueExW(key, L"~MHz", NULL, NULL, (LPBYTE)&speed,
			&size);
	if (status == ERROR_SUCCESS)
		blog(LOG_INFO, "CPU Speed: %ldMHz", speed);

	RegCloseKey(key);
}

static DWORD num_logical_cores(ULONG_PTR mask)
{
	DWORD     left_shift    = sizeof(ULONG_PTR) * 8 - 1;
	DWORD     bit_set_count = 0;
	ULONG_PTR bit_test      = (ULONG_PTR)1 << left_shift;

	for (DWORD i = 0; i <= left_shift; ++i) {
		bit_set_count += ((mask & bit_test) ? 1 : 0);
		bit_test      /= 2;
	}

	return bit_set_count;
}

static void log_processor_cores(void)
{
	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION info = NULL, temp = NULL;
	DWORD len = 0;

	GetLogicalProcessorInformation(info, &len);
	if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		return;

	info = malloc(len);

	if (GetLogicalProcessorInformation(info, &len)) {
		DWORD num            = len / sizeof(*info);
		int   physical_cores = 0;
		int   logical_cores  = 0;

		temp = info;

		for (DWORD i = 0; i < num; i++) {
			if (temp->Relationship == RelationProcessorCore) {
				ULONG_PTR mask = temp->ProcessorMask;

				physical_cores++;
				logical_cores += num_logical_cores(mask);
			}

			temp++;
		}

		blog(LOG_INFO, "Physical Cores: %d, Logical Cores: %d",
				physical_cores, logical_cores);
	}

	free(info);
}

static void log_available_memory(void)
{
	MEMORYSTATUS ms;
	GlobalMemoryStatus(&ms);

#ifdef _WIN64
	const char *note = "";
#else
	const char *note = " (NOTE: 4 gigs max is normal for 32bit programs)";
#endif

	blog(LOG_INFO, "Physical Memory: %luMB Total, %luMB Free%s",
			(DWORD)(ms.dwTotalPhys / 1048576),
			(DWORD)(ms.dwAvailPhys / 1048576),
			note);
}

static void log_windows_version(void)
{
	OSVERSIONINFOW osvi;
	char           *build = NULL;

	osvi.dwOSVersionInfoSize = sizeof(osvi);
	GetVersionExW(&osvi);

	os_wcs_to_utf8_ptr(osvi.szCSDVersion, 0, &build);
	blog(LOG_INFO, "Windows Version: %ld.%ld Build %ld %s",
			osvi.dwMajorVersion,
			osvi.dwMinorVersion,
			osvi.dwBuildNumber,
			build);

	bfree(build);
}

void log_system_info(void)
{
	log_processor_info();
	log_processor_cores();
	log_available_memory();
	log_windows_version();
}


struct obs_hotkeys_platform {
	bool blank;
};

bool obs_hotkeys_platform_init(struct obs_core_hotkeys *hotkeys)
{
	return true;
}

void obs_hotkeys_platform_free(struct obs_core_hotkeys *hotkeys)
{
}

bool obs_hotkeys_platform_is_pressed(obs_hotkeys_platform_t *context,
		obs_key_t key)
{
	short state = GetAsyncKeyState(obs_key_to_virtual_key(key));
	bool down = (state & 0x8000) != 0;
	bool was_down = (state & 0x1) != 0;
	return down || was_down;
}

void obs_key_to_str(obs_key_t key, struct dstr *str)
{
	wchar_t name[128] = L"";
	UINT scan_code = MapVirtualKey(obs_key_to_virtual_key(key), 0) << 16;

	GetKeyNameTextW(scan_code, name, 128);

	dstr_from_wcs(str, name);
}

obs_key_t obs_key_from_virtual_key(int code)
{
	switch (code) {
	case VK_TAB: return OBS_KEY_TAB;
	case VK_BACK: return OBS_KEY_BACKSPACE;
	case VK_INSERT: return OBS_KEY_INSERT;
	case VK_DELETE: return OBS_KEY_DELETE;
	case VK_PAUSE: return OBS_KEY_PAUSE;
	case VK_HOME: return OBS_KEY_HOME;
	case VK_END: return OBS_KEY_END;
	case VK_LEFT: return OBS_KEY_LEFT;
	case VK_UP: return OBS_KEY_UP;
	case VK_RIGHT: return OBS_KEY_RIGHT;
	case VK_DOWN: return OBS_KEY_DOWN;
	case VK_PRIOR: return OBS_KEY_PAGEUP;
	case VK_NEXT: return OBS_KEY_PAGEDOWN;

	case VK_SHIFT: return OBS_KEY_SHIFT;
	case VK_CONTROL: return OBS_KEY_CONTROL;
	case VK_MENU: return OBS_KEY_ALT;
	case VK_CAPITAL: return OBS_KEY_CAPSLOCK;
	case VK_NUMLOCK: return OBS_KEY_NUMLOCK;
	case VK_SCROLL: return OBS_KEY_SCROLLLOCK;

	case VK_F1: return OBS_KEY_F1;
	case VK_F2: return OBS_KEY_F2;
	case VK_F3: return OBS_KEY_F3;
	case VK_F4: return OBS_KEY_F4;
	case VK_F5: return OBS_KEY_F5;
	case VK_F6: return OBS_KEY_F6;
	case VK_F7: return OBS_KEY_F7;
	case VK_F8: return OBS_KEY_F8;
	case VK_F9: return OBS_KEY_F9;
	case VK_F10: return OBS_KEY_F10;
	case VK_F11: return OBS_KEY_F11;
	case VK_F12: return OBS_KEY_F12;
	case VK_F13: return OBS_KEY_F13;
	case VK_F14: return OBS_KEY_F14;
	case VK_F15: return OBS_KEY_F15;
	case VK_F16: return OBS_KEY_F16;
	case VK_F17: return OBS_KEY_F17;
	case VK_F18: return OBS_KEY_F18;
	case VK_F19: return OBS_KEY_F19;
	case VK_F20: return OBS_KEY_F20;
	case VK_F21: return OBS_KEY_F21;
	case VK_F22: return OBS_KEY_F22;
	case VK_F23: return OBS_KEY_F23;
	case VK_F24: return OBS_KEY_F24;

	case VK_SPACE: return OBS_KEY_SPACE;

	case VK_OEM_7: return OBS_KEY_APOSTROPHE;
	case VK_OEM_PLUS: return OBS_KEY_PLUS;
	case VK_OEM_COMMA: return OBS_KEY_COMMA;
	case VK_OEM_MINUS: return OBS_KEY_MINUS;
	case VK_OEM_PERIOD: return OBS_KEY_PERIOD;
	case VK_OEM_2: return OBS_KEY_SLASH;
	case '0': return OBS_KEY_0;
	case '1': return OBS_KEY_1;
	case '2': return OBS_KEY_2;
	case '3': return OBS_KEY_3;
	case '4': return OBS_KEY_4;
	case '5': return OBS_KEY_5;
	case '6': return OBS_KEY_6;
	case '7': return OBS_KEY_7;
	case '8': return OBS_KEY_8;
	case '9': return OBS_KEY_9;
	case VK_MULTIPLY: return OBS_KEY_NUMASTERISK;
	case VK_ADD: return OBS_KEY_NUMPLUS;
	case VK_SUBTRACT: return OBS_KEY_NUMMINUS;
	case VK_DECIMAL: return OBS_KEY_NUMPERIOD;
	case VK_DIVIDE: return OBS_KEY_NUMSLASH;
	case VK_NUMPAD0: return OBS_KEY_NUM0;
	case VK_NUMPAD1: return OBS_KEY_NUM1;
	case VK_NUMPAD2: return OBS_KEY_NUM2;
	case VK_NUMPAD3: return OBS_KEY_NUM3;
	case VK_NUMPAD4: return OBS_KEY_NUM4;
	case VK_NUMPAD5: return OBS_KEY_NUM5;
	case VK_NUMPAD6: return OBS_KEY_NUM6;
	case VK_NUMPAD7: return OBS_KEY_NUM7;
	case VK_NUMPAD8: return OBS_KEY_NUM8;
	case VK_NUMPAD9: return OBS_KEY_NUM9;
	case VK_OEM_1: return OBS_KEY_SEMICOLON;
	case 'A': return OBS_KEY_A;
	case 'B': return OBS_KEY_B;
	case 'C': return OBS_KEY_C;
	case 'D': return OBS_KEY_D;
	case 'E': return OBS_KEY_E;
	case 'F': return OBS_KEY_F;
	case 'G': return OBS_KEY_G;
	case 'H': return OBS_KEY_H;
	case 'I': return OBS_KEY_I;
	case 'J': return OBS_KEY_J;
	case 'K': return OBS_KEY_K;
	case 'L': return OBS_KEY_L;
	case 'M': return OBS_KEY_M;
	case 'N': return OBS_KEY_N;
	case 'O': return OBS_KEY_O;
	case 'P': return OBS_KEY_P;
	case 'Q': return OBS_KEY_Q;
	case 'R': return OBS_KEY_R;
	case 'S': return OBS_KEY_S;
	case 'T': return OBS_KEY_T;
	case 'U': return OBS_KEY_U;
	case 'V': return OBS_KEY_V;
	case 'W': return OBS_KEY_W;
	case 'X': return OBS_KEY_X;
	case 'Y': return OBS_KEY_Y;
	case 'Z': return OBS_KEY_Z;
	case VK_OEM_4: return OBS_KEY_BRACKETLEFT;
	case VK_OEM_5: return OBS_KEY_BACKSLASH;
	case VK_OEM_6: return OBS_KEY_BRACKETRIGHT;
	case VK_OEM_3: return OBS_KEY_ASCIITILDE;

	case VK_LBUTTON: return OBS_KEY_MOUSE1;
	case VK_RBUTTON: return OBS_KEY_MOUSE2;
	case VK_MBUTTON: return OBS_KEY_MOUSE3;
	case VK_XBUTTON1: return OBS_KEY_MOUSE4;
	case VK_XBUTTON2: return OBS_KEY_MOUSE5;

	/* TODO: Implement keys for non-US keyboards */
	default:;
	}
	return OBS_KEY_NONE;
}

int obs_key_to_virtual_key(obs_key_t key)
{
	switch (key) {
	case OBS_KEY_TAB: return VK_TAB;
	case OBS_KEY_BACKSPACE: return VK_BACK;
	case OBS_KEY_INSERT: return VK_INSERT;
	case OBS_KEY_DELETE: return VK_DELETE;
	case OBS_KEY_PAUSE: return VK_PAUSE;
	case OBS_KEY_HOME: return VK_HOME;
	case OBS_KEY_END: return VK_END;
	case OBS_KEY_LEFT: return VK_LEFT;
	case OBS_KEY_UP: return VK_UP;
	case OBS_KEY_RIGHT: return VK_RIGHT;
	case OBS_KEY_DOWN: return VK_DOWN;
	case OBS_KEY_PAGEUP: return VK_PRIOR;
	case OBS_KEY_PAGEDOWN: return VK_NEXT;

	case OBS_KEY_SHIFT: return VK_SHIFT;
	case OBS_KEY_CONTROL: return VK_CONTROL;
	case OBS_KEY_ALT: return VK_MENU;
	case OBS_KEY_CAPSLOCK: return VK_CAPITAL;
	case OBS_KEY_NUMLOCK: return VK_NUMLOCK;
	case OBS_KEY_SCROLLLOCK: return VK_SCROLL;

	case OBS_KEY_F1: return VK_F1;
	case OBS_KEY_F2: return VK_F2;
	case OBS_KEY_F3: return VK_F3;
	case OBS_KEY_F4: return VK_F4;
	case OBS_KEY_F5: return VK_F5;
	case OBS_KEY_F6: return VK_F6;
	case OBS_KEY_F7: return VK_F7;
	case OBS_KEY_F8: return VK_F8;
	case OBS_KEY_F9: return VK_F9;
	case OBS_KEY_F10: return VK_F10;
	case OBS_KEY_F11: return VK_F11;
	case OBS_KEY_F12: return VK_F12;
	case OBS_KEY_F13: return VK_F13;
	case OBS_KEY_F14: return VK_F14;
	case OBS_KEY_F15: return VK_F15;
	case OBS_KEY_F16: return VK_F16;
	case OBS_KEY_F17: return VK_F17;
	case OBS_KEY_F18: return VK_F18;
	case OBS_KEY_F19: return VK_F19;
	case OBS_KEY_F20: return VK_F20;
	case OBS_KEY_F21: return VK_F21;
	case OBS_KEY_F22: return VK_F22;
	case OBS_KEY_F23: return VK_F23;
	case OBS_KEY_F24: return VK_F24;

	case OBS_KEY_SPACE: return VK_SPACE;

	case OBS_KEY_APOSTROPHE: return VK_OEM_7;
	case OBS_KEY_PLUS: return VK_OEM_PLUS;
	case OBS_KEY_COMMA: return VK_OEM_COMMA;
	case OBS_KEY_MINUS: return VK_OEM_MINUS;
	case OBS_KEY_PERIOD: return VK_OEM_PERIOD;
	case OBS_KEY_SLASH: return VK_OEM_2;
	case OBS_KEY_0: return '0';
	case OBS_KEY_1: return '1';
	case OBS_KEY_2: return '2';
	case OBS_KEY_3: return '3';
	case OBS_KEY_4: return '4';
	case OBS_KEY_5: return '5';
	case OBS_KEY_6: return '6';
	case OBS_KEY_7: return '7';
	case OBS_KEY_8: return '8';
	case OBS_KEY_9: return '9';
	case OBS_KEY_NUMASTERISK: return VK_MULTIPLY;
	case OBS_KEY_NUMPLUS: return VK_ADD;
	case OBS_KEY_NUMMINUS: return VK_SUBTRACT;
	case OBS_KEY_NUMPERIOD: return VK_DECIMAL;
	case OBS_KEY_NUMSLASH: return VK_DIVIDE;
	case OBS_KEY_NUM0: return VK_NUMPAD0;
	case OBS_KEY_NUM1: return VK_NUMPAD1;
	case OBS_KEY_NUM2: return VK_NUMPAD2;
	case OBS_KEY_NUM3: return VK_NUMPAD3;
	case OBS_KEY_NUM4: return VK_NUMPAD4;
	case OBS_KEY_NUM5: return VK_NUMPAD5;
	case OBS_KEY_NUM6: return VK_NUMPAD6;
	case OBS_KEY_NUM7: return VK_NUMPAD7;
	case OBS_KEY_NUM8: return VK_NUMPAD8;
	case OBS_KEY_NUM9: return VK_NUMPAD9;
	case OBS_KEY_SEMICOLON: return VK_OEM_1;
	case OBS_KEY_A: return 'A';
	case OBS_KEY_B: return 'B';
	case OBS_KEY_C: return 'C';
	case OBS_KEY_D: return 'D';
	case OBS_KEY_E: return 'E';
	case OBS_KEY_F: return 'F';
	case OBS_KEY_G: return 'G';
	case OBS_KEY_H: return 'H';
	case OBS_KEY_I: return 'I';
	case OBS_KEY_J: return 'J';
	case OBS_KEY_K: return 'K';
	case OBS_KEY_L: return 'L';
	case OBS_KEY_M: return 'M';
	case OBS_KEY_N: return 'N';
	case OBS_KEY_O: return 'O';
	case OBS_KEY_P: return 'P';
	case OBS_KEY_Q: return 'Q';
	case OBS_KEY_R: return 'R';
	case OBS_KEY_S: return 'S';
	case OBS_KEY_T: return 'T';
	case OBS_KEY_U: return 'U';
	case OBS_KEY_V: return 'V';
	case OBS_KEY_W: return 'W';
	case OBS_KEY_X: return 'X';
	case OBS_KEY_Y: return 'Y';
	case OBS_KEY_Z: return 'Z';
	case OBS_KEY_BRACKETLEFT: return VK_OEM_4;
	case OBS_KEY_BACKSLASH: return VK_OEM_5;
	case OBS_KEY_BRACKETRIGHT: return VK_OEM_6;
	case OBS_KEY_ASCIITILDE: return VK_OEM_3;

	case OBS_KEY_MOUSE1: return VK_LBUTTON;
	case OBS_KEY_MOUSE2: return VK_RBUTTON;
	case OBS_KEY_MOUSE3: return VK_MBUTTON;
	case OBS_KEY_MOUSE4: return VK_XBUTTON1;
	case OBS_KEY_MOUSE5: return VK_XBUTTON2;

	/* TODO: Implement keys for non-US keyboards */
	default:;
	}
	return 0;
}
