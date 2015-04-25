/******************************************************************************
    Copyright (C) 2013 by Hugh Bailey <obs.jim@gmail.com>
    Copyright (C) 2014 by Zachary Lund <admin@computerquip.com>

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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <xcb/xcb.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xlib-xcb.h>
#include <X11/keysym.h>
#include <inttypes.h>
#include "util/dstr.h"
#include "obs-internal.h"

const char *get_module_extension(void)
{
	return ".so";
}

#ifdef __LP64__
#define BIT_STRING "64bit"
#else
#define BIT_STRING "32bit"
#endif

static const char *module_bin[] = {
	"../../obs-plugins/" BIT_STRING,
	OBS_INSTALL_PREFIX "/" OBS_PLUGIN_DESTINATION
};

static const char *module_data[] = {
	OBS_DATA_PATH "/obs-plugins/%module%",
	OBS_INSTALL_DATA_PATH "/obs-plugins/%module%",
};

static const int module_patterns_size =
	sizeof(module_bin)/sizeof(module_bin[0]);

void add_default_module_paths(void)
{
	for (int i = 0; i < module_patterns_size; i++)
		obs_add_module_path(module_bin[i], module_data[i]);
}

/*
 *   /usr/local/share/libobs
 *   /usr/share/libobs
 */
char *find_libobs_data_file(const char *file)
{
	struct dstr output;
		dstr_init(&output);

	if (check_path(file, OBS_DATA_PATH "/libobs/", &output))
		return output.array;

	if (OBS_INSTALL_PREFIX [0] != 0) {
		if (check_path(file, OBS_INSTALL_DATA_PATH "/libobs/",
					&output))
			return output.array;
	}

	dstr_free(&output);
	return NULL;
}

static void log_processor_info(void)
{
	FILE *fp;
	int physical_id = -1;
	int last_physical_id = -1;
	char *line = NULL;
	size_t linecap = 0;
	struct dstr processor;

	blog(LOG_INFO, "Processor: %lu logical cores",
	     sysconf(_SC_NPROCESSORS_ONLN));

	fp = fopen("/proc/cpuinfo", "r");
	if (!fp)
		return;

	dstr_init(&processor);

	while (getline(&line, &linecap, fp) != -1) {
		if (!strncmp(line, "model name", 10)) {
			char *start = strchr(line, ':');
			if (!start || *(++start) == '\0')
				continue;
			dstr_copy(&processor, start);
			dstr_resize(&processor, processor.len - 1);
			dstr_depad(&processor);
		}

		if (!strncmp(line, "physical id", 11)) {
			char *start = strchr(line, ':');
			if (!start || *(++start) == '\0')
				continue;
			physical_id = atoi(start);
		}

		if (*line == '\n' && physical_id != last_physical_id) {
			last_physical_id = physical_id;
			blog(LOG_INFO, "Processor: %s", processor.array);
		}
	}

	fclose(fp);
	dstr_free(&processor);
	free(line);
}

static void log_memory_info(void)
{
	struct sysinfo info;
	if (sysinfo(&info) < 0)
		return;

	blog(LOG_INFO, "Physical Memory: %"PRIu64"MB Total",
			(uint64_t)info.totalram * info.mem_unit / 1024 / 1024);
}

static void log_kernel_version(void)
{
	struct utsname info;
	if (uname(&info) < 0)
		return;

	blog(LOG_INFO, "Kernel Version: %s %s", info.sysname, info.release);
}

static void log_distribution_info(void)
{
	FILE *fp;
	char *line = NULL;
	size_t linecap = 0;
	struct dstr distro;
	struct dstr version;

	fp = fopen("/etc/os-release", "r");
	if (!fp) {
		blog(LOG_INFO, "Distribution: Missing /etc/os-release !");
		return;
	}

	dstr_init_copy(&distro, "Unknown");
	dstr_init_copy(&version, "Unknown");

	while (getline(&line, &linecap, fp) != -1) {
		if (!strncmp(line, "NAME", 4)) {
			char *start = strchr(line, '=');
			if (!start || *(++start) == '\0')
				continue;
			dstr_copy(&distro, start);
			dstr_resize(&distro, distro.len - 1);
		}

		if (!strncmp(line, "VERSION_ID", 10)) {
			char *start = strchr(line, '=');
			if (!start || *(++start) == '\0')
				continue;
			dstr_copy(&version, start);
			dstr_resize(&version, version.len - 1);
		}
	}

	blog(LOG_INFO, "Distribution: %s %s", distro.array, version.array);

	fclose(fp);
	dstr_free(&version);
	dstr_free(&distro);
	free(line);
}

void log_system_info(void)
{
	log_processor_info();
	log_memory_info();
	log_kernel_version();
	log_distribution_info();
}

/* So here's how linux works with key mapping:
 *
 * First, there's a global key symbol enum (xcb_keysym_t) which has unique
 * values for all possible symbols keys can have (e.g., '1' and '!' are
 * different values).
 *
 * Then there's a key code (xcb_keycode_t), which is basically an index to the
 * actual key itself on the keyboard (e.g., '1' and '!' will share the same
 * value).
 *
 * xcb_keysym_t values should be given to libobs, and libobs will translate it
 * to an obs_key_t, and although xcb_keysym_t can differ ('!' vs '1'), it will
 * get the obs_key_t value that represents the actual key pressed; in other
 * words it will be based on the key code rather than the key symbol.  The same
 * applies to checking key press states.
 */

struct obs_hotkeys_platform {
	Display *display;
	xcb_keysym_t base_keysyms[OBS_KEY_LAST_VALUE];
	xcb_keycode_t keycodes[OBS_KEY_LAST_VALUE];
	xcb_keycode_t min_keycode;

	/* stores a copy of the keysym map for keycodes */
	xcb_keysym_t *keysyms;
	int num_keysyms;
	int syms_per_code;
};

#define MOUSE_1 (1<<16)
#define MOUSE_2 (2<<16)
#define MOUSE_3 (3<<16)
#define MOUSE_4 (4<<16)
#define MOUSE_5 (5<<16)

static int get_keysym(obs_key_t key)
{
	switch (key) {
	case OBS_KEY_TAB: return XK_Tab;
	case OBS_KEY_BACKSPACE: return XK_BackSpace;
	case OBS_KEY_INSERT: return XK_Insert;
	case OBS_KEY_DELETE: return XK_Delete;
	case OBS_KEY_PAUSE: return XK_Pause;
	case OBS_KEY_HOME: return XK_Home;
	case OBS_KEY_END: return XK_End;
	case OBS_KEY_LEFT: return XK_Left;
	case OBS_KEY_UP: return XK_Up;
	case OBS_KEY_RIGHT: return XK_Right;
	case OBS_KEY_DOWN: return XK_Down;
	case OBS_KEY_PAGEUP: return XK_Prior;
	case OBS_KEY_PAGEDOWN: return XK_Next;

	case OBS_KEY_SHIFT: return XK_Shift_L;
	case OBS_KEY_CONTROL: return XK_Control_L;
	case OBS_KEY_ALT: return XK_Alt_L;
	case OBS_KEY_CAPSLOCK: return XK_Caps_Lock;
	case OBS_KEY_NUMLOCK: return XK_Num_Lock;
	case OBS_KEY_SCROLLLOCK: return XK_Scroll_Lock;

	case OBS_KEY_F1: return XK_F1;
	case OBS_KEY_F2: return XK_F2;
	case OBS_KEY_F3: return XK_F3;
	case OBS_KEY_F4: return XK_F4;
	case OBS_KEY_F5: return XK_F5;
	case OBS_KEY_F6: return XK_F6;
	case OBS_KEY_F7: return XK_F7;
	case OBS_KEY_F8: return XK_F8;
	case OBS_KEY_F9: return XK_F9;
	case OBS_KEY_F10: return XK_F10;
	case OBS_KEY_F11: return XK_F11;
	case OBS_KEY_F12: return XK_F12;
	case OBS_KEY_F13: return XK_F13;
	case OBS_KEY_F14: return XK_F14;
	case OBS_KEY_F15: return XK_F15;
	case OBS_KEY_F16: return XK_F16;
	case OBS_KEY_F17: return XK_F17;
	case OBS_KEY_F18: return XK_F18;
	case OBS_KEY_F19: return XK_F19;
	case OBS_KEY_F20: return XK_F20;
	case OBS_KEY_F21: return XK_F21;
	case OBS_KEY_F22: return XK_F22;
	case OBS_KEY_F23: return XK_F23;
	case OBS_KEY_F24: return XK_F24;

	case OBS_KEY_SPACE: return XK_space;

	case OBS_KEY_APOSTROPHE: return XK_apostrophe;
	case OBS_KEY_PLUS: return XK_plus;
	case OBS_KEY_COMMA: return XK_comma;
	case OBS_KEY_MINUS: return XK_minus;
	case OBS_KEY_PERIOD: return XK_period;
	case OBS_KEY_SLASH: return XK_slash;
	case OBS_KEY_0: return XK_0;
	case OBS_KEY_1: return XK_1;
	case OBS_KEY_2: return XK_2;
	case OBS_KEY_3: return XK_3;
	case OBS_KEY_4: return XK_4;
	case OBS_KEY_5: return XK_5;
	case OBS_KEY_6: return XK_6;
	case OBS_KEY_7: return XK_7;
	case OBS_KEY_8: return XK_8;
	case OBS_KEY_9: return XK_9;
	case OBS_KEY_NUMASTERISK: return XK_KP_Multiply;
	case OBS_KEY_NUMPLUS: return XK_KP_Add;
	case OBS_KEY_NUMMINUS: return XK_KP_Subtract;
	case OBS_KEY_NUMPERIOD: return XK_KP_Decimal;
	case OBS_KEY_NUMSLASH: return XK_KP_Divide;
	case OBS_KEY_NUM0: return XK_KP_0;
	case OBS_KEY_NUM1: return XK_KP_1;
	case OBS_KEY_NUM2: return XK_KP_2;
	case OBS_KEY_NUM3: return XK_KP_3;
	case OBS_KEY_NUM4: return XK_KP_4;
	case OBS_KEY_NUM5: return XK_KP_5;
	case OBS_KEY_NUM6: return XK_KP_6;
	case OBS_KEY_NUM7: return XK_KP_7;
	case OBS_KEY_NUM8: return XK_KP_8;
	case OBS_KEY_NUM9: return XK_KP_9;
	case OBS_KEY_SEMICOLON: return XK_semicolon;
	case OBS_KEY_A: return XK_A;
	case OBS_KEY_B: return XK_B;
	case OBS_KEY_C: return XK_C;
	case OBS_KEY_D: return XK_D;
	case OBS_KEY_E: return XK_E;
	case OBS_KEY_F: return XK_F;
	case OBS_KEY_G: return XK_G;
	case OBS_KEY_H: return XK_H;
	case OBS_KEY_I: return XK_I;
	case OBS_KEY_J: return XK_J;
	case OBS_KEY_K: return XK_K;
	case OBS_KEY_L: return XK_L;
	case OBS_KEY_M: return XK_M;
	case OBS_KEY_N: return XK_N;
	case OBS_KEY_O: return XK_O;
	case OBS_KEY_P: return XK_P;
	case OBS_KEY_Q: return XK_Q;
	case OBS_KEY_R: return XK_R;
	case OBS_KEY_S: return XK_S;
	case OBS_KEY_T: return XK_T;
	case OBS_KEY_U: return XK_U;
	case OBS_KEY_V: return XK_V;
	case OBS_KEY_W: return XK_W;
	case OBS_KEY_X: return XK_X;
	case OBS_KEY_Y: return XK_Y;
	case OBS_KEY_Z: return XK_Z;
	case OBS_KEY_BRACKETLEFT: return XK_bracketleft;
	case OBS_KEY_BACKSLASH: return XK_backslash;
	case OBS_KEY_BRACKETRIGHT: return XK_bracketright;
	case OBS_KEY_ASCIITILDE: return XK_grave;

	case OBS_KEY_MOUSE1: return MOUSE_1;
	case OBS_KEY_MOUSE2: return MOUSE_2;
	case OBS_KEY_MOUSE3: return MOUSE_3;
	case OBS_KEY_MOUSE4: return MOUSE_4;
	case OBS_KEY_MOUSE5: return MOUSE_5;

	/* TODO: Implement keys for non-US keyboards */
	default:;
	}
	return 0;
}

static inline void fill_base_keysyms(struct obs_core_hotkeys *hotkeys)
{
	for (size_t i = 0; i < OBS_KEY_LAST_VALUE; i++)
		hotkeys->platform_context->base_keysyms[i] = get_keysym(i);
}

static obs_key_t key_from_base_keysym(obs_hotkeys_platform_t *context,
		xcb_keysym_t code)
{
	for (size_t i = 0; i < OBS_KEY_LAST_VALUE; i++) {
		if (context->base_keysyms[i] == (xcb_keysym_t)code) {
			return (obs_key_t)i;
		}
	}

	return OBS_KEY_NONE;
}

static inline bool fill_keycodes(struct obs_core_hotkeys *hotkeys)
{
	obs_hotkeys_platform_t *context = hotkeys->platform_context;
	xcb_connection_t *connection = XGetXCBConnection(context->display);
	const struct xcb_setup_t *setup = xcb_get_setup(connection);
	xcb_get_keyboard_mapping_cookie_t cookie;
	xcb_get_keyboard_mapping_reply_t *reply;
	xcb_generic_error_t *error = NULL;
	int code;

	int mincode = setup->min_keycode;
	int maxcode = setup->max_keycode;

	context->min_keycode = setup->min_keycode;

	cookie = xcb_get_keyboard_mapping(connection,
			mincode, maxcode - mincode - 1);

	reply = xcb_get_keyboard_mapping_reply(connection, cookie, &error);

	if (error || !reply) {
		blog(LOG_WARNING, "xcb_get_keyboard_mapping_reply failed");
		goto error1;
	}

	const xcb_keysym_t *keysyms = xcb_get_keyboard_mapping_keysyms(reply);
	int syms_per_code = (int)reply->keysyms_per_keycode;

	context->num_keysyms = (maxcode - mincode) * syms_per_code;
	context->syms_per_code = syms_per_code;
	context->keysyms = bmemdup(keysyms,
			sizeof(xcb_keysym_t) * context->num_keysyms);

	for (code = mincode; code <= maxcode; code++) {
		const xcb_keysym_t *sym;
		obs_key_t key;

		sym = &keysyms[(code - mincode) * syms_per_code];

		for (int i = 0; i < syms_per_code; i++) {
			if (!sym[i])
				break;

			key = key_from_base_keysym(context, sym[i]);

			if (key != OBS_KEY_NONE) {
				context->keycodes[key] = (xcb_keycode_t)code;
				break;
			}
		}

	}

error1:
	free(reply);
	free(error);

	return error != NULL || reply == NULL;
}

bool obs_hotkeys_platform_init(struct obs_core_hotkeys *hotkeys)
{
	Display *display = XOpenDisplay(NULL);
	if (!display)
		return false;

	hotkeys->platform_context = bzalloc(sizeof(obs_hotkeys_platform_t));
	hotkeys->platform_context->display = display;

	fill_base_keysyms(hotkeys);
	fill_keycodes(hotkeys);
	return true;
}

void obs_hotkeys_platform_free(struct obs_core_hotkeys *hotkeys)
{
	XCloseDisplay(hotkeys->platform_context->display);
	bfree(hotkeys->platform_context->keysyms);
	bfree(hotkeys->platform_context);
	hotkeys->platform_context = NULL;
}

static xcb_screen_t *default_screen(obs_hotkeys_platform_t *context,
		xcb_connection_t *connection)
{
	int def_screen_idx = XDefaultScreen(context->display);
	xcb_screen_iterator_t iter;

	iter = xcb_setup_roots_iterator(xcb_get_setup(connection));
	while (iter.rem) {
		if (--def_screen_idx == 0) {
			return iter.data;
			break;
		}

		xcb_screen_next(&iter);
	}

	return NULL;
}

static inline xcb_window_t root_window(obs_hotkeys_platform_t *context,
		xcb_connection_t *connection)
{
	xcb_screen_t *screen = default_screen(context, connection);
	if (screen)
		return screen->root;
	return 0;
}

static bool mouse_button_pressed(xcb_connection_t *connection,
		obs_hotkeys_platform_t *context, obs_key_t key)
{
	xcb_generic_error_t *error = NULL;
	xcb_query_pointer_cookie_t qpc;
	xcb_query_pointer_reply_t *reply;
	bool ret = false;

	qpc = xcb_query_pointer(connection, root_window(context, connection));
	reply = xcb_query_pointer_reply(connection, qpc, &error);

	if (error) {
		blog(LOG_WARNING, "xcb_query_pointer_reply failed");
	} else {
		uint16_t buttons = reply->mask;

		switch (key) {
		case OBS_KEY_MOUSE1: ret = buttons & XCB_BUTTON_MASK_1; break;
		case OBS_KEY_MOUSE2: ret = buttons & XCB_BUTTON_MASK_3; break;
		case OBS_KEY_MOUSE3: ret = buttons & XCB_BUTTON_MASK_2; break;
		default:;
		}
	}

	free(reply);
	free(error);
	return ret;
}

static bool key_pressed(xcb_connection_t *connection,
		obs_hotkeys_platform_t *context, obs_key_t key)
{
	xcb_keycode_t code = context->keycodes[key];
	xcb_generic_error_t *error = NULL;
	xcb_query_keymap_reply_t *reply;
	bool pressed = false;

	reply = xcb_query_keymap_reply(connection,
			xcb_query_keymap(connection), &error);
	if (error) {
		blog(LOG_WARNING, "xcb_query_keymap failed");
	} else {
		pressed = (reply->keys[code / 8] & (1 << (code % 8))) != 0;
	}

	free(reply);
	free(error);
	return pressed;
}

bool obs_hotkeys_platform_is_pressed(obs_hotkeys_platform_t *context,
		obs_key_t key)
{
	xcb_connection_t *connection = XGetXCBConnection(context->display);

	if (key >= OBS_KEY_MOUSE1 && key <= OBS_KEY_MOUSE29)
		return mouse_button_pressed(connection, context, key);
	else
		return key_pressed(connection, context, key);
}

int get_keycode(obs_key_t key)
{
	return (int)obs->hotkeys.platform_context->keycodes[(int)key];
}

void obs_key_to_str(obs_key_t key, struct dstr *dstr)
{
	xcb_connection_t *connection;
	XKeyEvent event = {0};
	char name[128];
	int keycode;

	connection = XGetXCBConnection(obs->hotkeys.platform_context->display);

	if (key >= OBS_KEY_MOUSE1 && key <= OBS_KEY_MOUSE29) {
		if (obs->hotkeys.translations[key]) {
			dstr_copy(dstr, obs->hotkeys.translations[key]);
		} else {
			dstr_printf(dstr, "Mouse %d",
					(int)(key - OBS_KEY_MOUSE1 + 1));
		}
		return;
	}

	if (key >= OBS_KEY_NUM0 && key <= OBS_KEY_NUM9) {
		if (obs->hotkeys.translations[key]) {
			dstr_copy(dstr, obs->hotkeys.translations[key]);
		} else {
			dstr_printf(dstr, "Numpad %d",
					(int)(key - OBS_KEY_NUM0));
		}
		return;
	}

#define translate_key(key, def) obs_get_hotkey_translation(key, def)

	switch (key) {
	case OBS_KEY_INSERT:       return translate_key(key, "Insert");
	case OBS_KEY_DELETE:       return translate_key(key, "Delete");
	case OBS_KEY_HOME:         return translate_key(key, "Home");
	case OBS_KEY_END:          return translate_key(key, "End");
	case OBS_KEY_PAGEUP:       return translate_key(key, "Page Up");
	case OBS_KEY_PAGEDOWN:     return translate_key(key, "Page Down");
	case OBS_KEY_NUMLOCK:      return translate_key(key, "Num Lock");
	case OBS_KEY_SCROLLLOCK:   return translate_key(key, "Scroll Lock");
	case OBS_KEY_CAPSLOCK:     return translate_key(key, "Caps Lock");
	case OBS_KEY_BACKSPACE:    return translate_key(key, "Backspace");
	case OBS_KEY_TAB:          return translate_key(key, "Tab");
	case OBS_KEY_PRINT:        return translate_key(key, "Print");
	case OBS_KEY_PAUSE:        return translate_key(key, "Pause");
	case OBS_KEY_SHIFT:        return translate_key(key, "Shift");
	case OBS_KEY_ALT:          return translate_key(key, "Alt");
	case OBS_KEY_CONTORL:      return translate_key(key, "Control");
	case OBS_KEY_HYPER_L:      return translate_key(key, "Hyper Left");
	case OBS_KEY_HYPER_R:      return translate_key(key, "Hyper Right");
	case OBS_KEY_MENU:         return translate_key(key, "Menu");
	case OBS_KEY_NUMASTERISK:  return translate_key(key, "Numpad *");
	case OBS_KEY_NUMPLUS:      return translate_key(key, "Numpad +");
	case OBS_KEY_NUMCOMMA:     return translate_key(key, "Numpad ,");
	case OBS_KEY_NUMPERIOD:    return translate_key(key, "Numpad .");
	case OBS_KEY_NUMSLASH:     return translate_key(key, "Numpad /");
	default:;
	}

	keycode = get_keycode(key);
	event.type = KeyPress;
	event.display = obs->hotkeys.platform_context->display;
	event.keycode = keycode;
	event.root = root_window(obs->hotkeys.platform_context, connection);
	event.window = event.root;

	if (keycode) {
		int len = XLookupString(&event, name, 128, NULL, NULL);
		if (len) {
			dstr_ncopy(dstr, name, len);
			dstr_to_upper(dstr);
		}
	}
}

static obs_key_t key_from_keycode(obs_hotkeys_platform_t *context,
		xcb_keycode_t code)
{
	for (size_t i = 0; i < OBS_KEY_LAST_VALUE; i++) {
		if (context->keycodes[i] == (xcb_keysym_t)code) {
			return (obs_key_t)i;
		}
	}

	return OBS_KEY_NONE;
}

obs_key_t obs_key_from_virtual_key(int sym)
{
	obs_hotkeys_platform_t *context = obs->hotkeys.platform_context;
	const xcb_keysym_t *keysyms = context->keysyms;
	int syms_per_code = context->syms_per_code;
	int num_keysyms = context->num_keysyms;

	if (sym == 0)
		return OBS_KEY_NONE;

	for (int i = 0; i < num_keysyms; i++) {
		if (keysyms[i] == (xcb_keysym_t)sym) {
			xcb_keycode_t code = (xcb_keycode_t)(i / syms_per_code);
			code += context->min_keycode;
			obs_key_t key = key_from_keycode(context, code);
			return key;
		}
	}

	return OBS_KEY_NONE;
}

int obs_key_to_virtual_key(obs_key_t key)
{
	return (int)obs->hotkeys.platform_context->base_keysyms[(int)key];
}
