/******************************************************************************
    Copyright (C) 2014 by Ruwen Hahn <palana@stunned.de>

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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t obs_hotkey_id;
#define OBS_INVALID_HOTKEY_ID (~(obs_hotkey_id)0)
typedef size_t obs_hotkey_pair_id;
#define OBS_INVALID_HOTKEY_PAIR_ID (~(obs_hotkey_pair_id)0)

enum obs_key {
#define OBS_HOTKEY(x) x,
#include "obs-hotkeys.h"
#undef OBS_HOTKEY
	OBS_KEY_LAST_VALUE, //not an actual key

	OBS_KEY_FIRST_MOUSE = OBS_KEY_MOUSE1,
	OBS_KEY_LAST_MOUSE = OBS_KEY_MOUSE29,
};
typedef enum obs_key obs_key_t;

struct obs_key_combination {
	uint32_t  modifiers;
	obs_key_t key;
};
typedef struct obs_key_combination obs_key_combination_t;

typedef struct obs_hotkey obs_hotkey_t;
typedef struct obs_hotkey_binding obs_hotkey_binding_t;

enum obs_hotkey_registerer_type {
	OBS_HOTKEY_REGISTERER_FRONTEND,
	OBS_HOTKEY_REGISTERER_SOURCE,
	OBS_HOTKEY_REGISTERER_OUTPUT,
	OBS_HOTKEY_REGISTERER_ENCODER,
	OBS_HOTKEY_REGISTERER_SERVICE,
};
typedef enum obs_hotkey_registerer_type obs_hotkey_registerer_t;

EXPORT obs_hotkey_id obs_hotkey_get_id(const obs_hotkey_t *key);
EXPORT const char *obs_hotkey_get_name(const obs_hotkey_t *key);
EXPORT const char *obs_hotkey_get_description(const obs_hotkey_t *key);
//EXPORT obs_key_combination_t obs_hotkey_get_key_combination(
//		const obs_hotkey_t *key);
EXPORT obs_hotkey_registerer_t obs_hotkey_get_registerer_type(
		const obs_hotkey_t *key);
EXPORT void *obs_hotkey_get_registerer(const obs_hotkey_t *key);


EXPORT obs_key_combination_t obs_hotkey_binding_get_key_combination(
		obs_hotkey_binding_t *binding);
EXPORT obs_hotkey_id obs_hotkey_binding_get_hotkey_id(
		obs_hotkey_binding_t *binding);
EXPORT obs_hotkey_t *obs_hotkey_binding_get_hotkey(
		obs_hotkey_binding_t *binding);

struct obs_hotkeys_translations {
	const char *insert;
	const char *del;
	const char *home;
	const char *end;
	const char *page_up;
	const char *page_down;
	const char *num_lock;
	const char *scroll_lock;
	const char *caps_lock;
	const char *backspace;
	const char *tab;
	const char *print;
	const char *pause;
	const char *shift;
	const char *alt;
	const char *control;
	const char *hyper_left; /* windows key left */
	const char *hyper_right; /* windows key right */
	const char *menu;
	const char *numpad; /* For example, "Numpad %1" */
	const char *mouse; /* For example, "Mouse %1" */
};

/* This function is an optional way to provide translations for specific keys
 * that may not have translations.  If the operating system can provide
 * translations for these keys, it will use the operating system's translation
 * over these translations.  If no translations are specified, it will use
 * the default english translations for that specific operating system. */
EXPORT void obs_hotkeys_set_translations_s(
		struct obs_hotkeys_translations *translations, size_t size);

#define obs_hotkeys_set_translations(translations) \
	obs_hotkeys_set_translations_s(translations, \
			sizeof(struct obs_hotkeys_translations))


/* registering hotkeys (giving hotkeys a name and a function) */

typedef void (*obs_hotkey_func)(obs_hotkey_id id, obs_hotkey_t *hotkey,
		bool pressed, void *data);

EXPORT obs_hotkey_id obs_hotkey_register_frontend(const char *name,
		const char *description, obs_hotkey_func func, void *data);

EXPORT obs_hotkey_id obs_hotkey_register_encoder(obs_encoder_t *encoder,
		const char *name, const char *description,
		obs_hotkey_func func, void *data);

EXPORT obs_hotkey_id obs_hotkey_register_output(obs_output_t *output,
		const char *name, const char *description,
		obs_hotkey_func func, void *data);

EXPORT obs_hotkey_id obs_hotkey_register_service(obs_service_t *service,
		const char *name, const char *description,
		obs_hotkey_func func, void *data);

EXPORT obs_hotkey_id obs_hotkey_register_source(obs_source_t *source,
		const char *name, const char *description,
		obs_hotkey_func func, void *data);

typedef bool (*obs_hotkey_active_func)(obs_hotkey_pair_id id,
		obs_hotkey_t *hotkey, bool pressed, void *data);

EXPORT obs_hotkey_pair_id obs_hotkey_pair_register_frontend(
		const char *name0, const char *description0,
		const char *name1, const char *description1,
		obs_hotkey_active_func func0, obs_hotkey_active_func func1,
		void *data0, void *data1);

EXPORT void obs_hotkey_unregister(obs_hotkey_id id);

/* loading hotkeys (associating a hotkey with a physical key and modifiers) */

/*EXPORT void obs_hotkey_set(obs_hotkey_id id, obs_key_combination_t hotkey);

EXPORT obs_key_combination_t obs_hotkey_get(obs_hotkey_id id);*/

EXPORT void obs_hotkey_load_bindings(obs_hotkey_id id,
		obs_key_combination_t *combinations, size_t num);

EXPORT void obs_hotkey_load(obs_hotkey_id id, obs_data_array_t *data);

EXPORT void obs_hotkeys_load_encoder(obs_encoder_t *encoder,
		obs_data_t *hotkeys);

EXPORT void obs_hotkeys_load_output(obs_output_t *output, obs_data_t *hotkeys);

EXPORT void obs_hotkeys_load_service(obs_service_t *service,
		obs_data_t *hotkeys);

EXPORT void obs_hotkeys_load_source(obs_source_t *source, obs_data_t *hotkeys);

EXPORT void obs_hotkey_pair_load(obs_hotkey_pair_id id, obs_data_array_t *data0,
		obs_data_array_t *data1);


EXPORT obs_data_array_t *obs_hotkey_save(obs_hotkey_id id);

EXPORT obs_data_t *obs_hotkeys_save_encoder(obs_encoder_t *encoder);

EXPORT obs_data_t *obs_hotkeys_save_output(obs_output_t *output);

EXPORT obs_data_t *obs_hotkeys_save_service(obs_service_t *service);

EXPORT obs_data_t *obs_hotkeys_save_source(obs_source_t *source);

/* enumerating hotkeys */

typedef bool (*obs_hotkey_enum_func)(obs_hotkey_id id, obs_hotkey_t *key,
		void *data);

EXPORT void obs_enum_hotkeys(obs_hotkey_enum_func func, void *data);

/* enumerating bindings */

typedef bool (*obs_hotkey_binding_enum_func)(size_t idx,
		obs_hotkey_binding_t* binding, void *data);

EXPORT void obs_enum_hotkey_bindings(obs_hotkey_binding_enum_func func,
		void *data);

/* hotkey event control */

EXPORT void obs_hotkey_inject_event(obs_key_combination_t hotkey, bool pressed);

EXPORT void obs_hotkey_enable_background_primary(bool enable);

/* misc */



struct dstr;
EXPORT void obs_key_to_str(obs_key_t key, struct dstr *str);

EXPORT obs_key_t obs_key_from_virtual_key(int code);
EXPORT int obs_key_to_virtual_key(obs_key_t key);

EXPORT const char *obs_key_to_name(obs_key_t key);
EXPORT obs_key_t obs_key_from_name(const char *name);

#ifdef __cplusplus
}
#endif

