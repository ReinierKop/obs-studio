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

#include "obs-internal.h"

static inline bool lock(void)
{
	if (!obs)
		return false;

	pthread_mutex_lock(&obs->hotkeys.mutex);
	return true;
}

static inline void unlock(void)
{
	pthread_mutex_unlock(&obs->hotkeys.mutex);
}

obs_hotkey_id obs_hotkey_get_id(const obs_hotkey_t *key)
{
	return key->id;
}

const char *obs_hotkey_get_name(const obs_hotkey_t *key)
{
	return key->name;
}

const char *obs_hotkey_get_description(const obs_hotkey_t *key)
{
	return key->description;
}

/*obs_key_combination_t obs_hotkey_get_key_combination(const obs_hotkey_t *key)
{
	return key->key;
}*/

obs_hotkey_registerer_t obs_hotkey_get_registerer_type(const obs_hotkey_t *key)
{
	return key->registerer_type;
}

void *obs_hotkey_get_registerer(const obs_hotkey_t *key)
{
	return key->registerer;
}

obs_key_combination_t obs_hotkey_binding_get_key_combination(
		obs_hotkey_binding_t *binding)
{
	return binding->key;
}

obs_hotkey_id obs_hotkey_binding_get_hotkey_id(obs_hotkey_binding_t *binding)
{
	return binding->hotkey_id;
}

obs_hotkey_t *obs_hotkey_binding_get_hotkey(obs_hotkey_binding_t *binding)
{
	return binding->hotkey;
}

static inline void fixup_pointers(void);
static inline void load_bindings(obs_hotkey_t *hotkey, obs_data_array_t *data);

static inline obs_hotkey_id obs_hotkey_register_internal(
		obs_hotkey_registerer_t type, void *registerer,
		struct obs_context_data *context,
		const char *name, const char *description,
		obs_hotkey_primary_action_t primary,
		obs_hotkey_func func, void *data)
{
	obs_hotkey_id result;

	if (!lock())
		return OBS_INVALID_HOTKEY_ID;

	assert((obs->hotkeys.next_id + 1) < OBS_INVALID_HOTKEY_ID);

	obs_hotkey_t *base_addr = obs->hotkeys.hotkeys.array;
	result                  = obs->hotkeys.next_id++;
	obs_hotkey_t *hotkey    = da_push_back_new(obs->hotkeys.hotkeys);

	hotkey->id              = result;
	hotkey->name            = bstrdup(name);
	hotkey->description     = bstrdup(description);
	hotkey->func            = func;
	hotkey->data            = data;
	hotkey->primary_action  = primary;
	hotkey->registerer_type = type;
	hotkey->registerer      = registerer;

	if (context) {
		obs_data_array_t *data =
			obs_data_get_array(context->hotkey_data, name);
		load_bindings(hotkey, data);
		obs_data_array_release(data);
	}

	if (base_addr != obs->hotkeys.hotkeys.array)
		fixup_pointers();
	unlock();

	return result;
}

static inline void context_add_hotkey(struct obs_context_data *context,
		obs_hotkey_id id)
{
	da_push_back(context->hotkeys, &id);
}

obs_hotkey_id obs_hotkey_register_source(obs_source_t *source, const char *name,
		const char *description, obs_hotkey_primary_action_t primary,
		obs_hotkey_func func, void *data)
{
	if (!source)
		return -1;

	obs_hotkey_id id = obs_hotkey_register_internal(
			OBS_HOTKEY_REGISTERER_SOURCE,
			source, &source->context, name, description,
			primary, func, data);
	context_add_hotkey(&source->context, id);

	return id;
}

obs_hotkey_id obs_hotkey_register_frontend(const char *name,
		const char *description, obs_hotkey_primary_action_t primary,
		obs_hotkey_func func, void *data)
{
	return obs_hotkey_register_internal(OBS_HOTKEY_REGISTERER_FRONTEND,
			NULL, NULL, name, description, primary,
			func, data);
}

typedef bool (*obs_hotkey_internal_enum_func)(size_t idx,
		obs_hotkey_t *hotkey, void *data);

static inline void enum_hotkeys(obs_hotkey_internal_enum_func func, void *data)
{
	const size_t num    = obs->hotkeys.hotkeys.num;
	obs_hotkey_t *array = obs->hotkeys.hotkeys.array;
	for (size_t i = 0; i < num; i++) {
		if (!func(i, &array[i], data))
			break;
	}
}

typedef bool (*obs_hotkey_binding_internal_enum_func)(size_t idx,
		obs_hotkey_binding_t *binding, void *data);

static inline void enum_bindings(obs_hotkey_binding_internal_enum_func func,
		void *data)
{
	const size_t         num    = obs->hotkeys.bindings.num;
	obs_hotkey_binding_t *array = obs->hotkeys.bindings.array;
	for (size_t i = 0; i < num; i++) {
		if (!func(i, &array[i], data))
			break;
	}
}

struct obs_hotkey_internal_find_forward {
	obs_hotkey_id id;
	bool          found;
	size_t        idx;
};

static inline bool find_id_helper(size_t idx, obs_hotkey_t *hotkey,
		void *data)
{
	struct obs_hotkey_internal_find_forward *find = data;
	if (hotkey->id != find->id)
		return true;

	find->idx   = idx;
	find->found = true;
	return false;
}

static inline bool find_id(obs_hotkey_id id, size_t *idx)
{
	struct obs_hotkey_internal_find_forward find = {id, false, 0};
	enum_hotkeys(find_id_helper, &find);
	*idx = find.idx;
	return find.found;
}

static inline bool pointer_fixup_func(size_t idx, obs_hotkey_binding_t *binding,
		void *data)
{
	UNUSED_PARAMETER(idx);
	UNUSED_PARAMETER(data);

	size_t idx_;
	bool found = find_id(binding->hotkey_id, &idx_);
	assert(found);
	binding->hotkey = &obs->hotkeys.hotkeys.array[idx_];

	return true;
}

static inline void fixup_pointers()
{
	enum_bindings(pointer_fixup_func, NULL);
}

static inline void enum_context_hotkeys(struct obs_context_data *context,
		obs_hotkey_internal_enum_func func, void *data)
{
	const size_t num           = context->hotkeys.num;
	const obs_hotkey_id *array = context->hotkeys.array;
	obs_hotkey_t *hotkey_array = obs->hotkeys.hotkeys.array;
	for (size_t i = 0; i < num; i++) {
		size_t idx;
		if (!find_id(array[i], &idx))
			continue;

		if (!func(idx, &hotkey_array[idx], data))
			break;
	}
}

static inline void load_modifier(uint32_t *modifiers, obs_data_t *data,
		const char *name, uint32_t flag)
{
	if (obs_data_get_bool(data, name))
		*modifiers |= flag;
}

static inline void create_binding(obs_hotkey_t *hotkey,
		obs_key_combination_t combo)
{
	obs_hotkey_binding_t *binding = da_push_back_new(obs->hotkeys.bindings);
	if (!binding)
		return;

	binding->key = combo;
	binding->hotkey_id = hotkey->id;
	binding->hotkey    = hotkey;
}

static inline void load_binding(obs_hotkey_t *hotkey, obs_data_t *data)
{
	if (!hotkey || !data)
		return;

	obs_key_combination_t combo = {0};
	uint32_t *modifiers = &combo.modifiers;
	load_modifier(modifiers, data, "shift", INTERACT_SHIFT_KEY);
	load_modifier(modifiers, data, "control", INTERACT_CONTROL_KEY);
	load_modifier(modifiers, data, "alt", INTERACT_ALT_KEY);
	load_modifier(modifiers, data, "command", INTERACT_COMMAND_KEY);

	combo.key = obs_key_from_name(obs_data_get_string(data, "key"));
	if (!modifiers && (combo.key == OBS_KEY_NONE ||
			   combo.key == OBS_KEY_UNKNOWN ||
			   combo.key >= OBS_KEY_LAST_VALUE))
		return;

	create_binding(hotkey, combo);
}

static inline void load_bindings(obs_hotkey_t *hotkey, obs_data_array_t *data)
{
	const size_t count = obs_data_array_count(data);
	for (size_t i = 0; i < count; i++) {
		obs_data_t *item = obs_data_array_item(data, i);
		load_binding(hotkey, item);
		obs_data_release(item);
	}
}

/*void obs_hotkey_set(obs_hotkey_id id, obs_key_combination_t hotkey)
{
	size_t idx;

	if (!lock())
		return;

	if (find_id(id, &idx))
		obs->hotkeys.hotkeys.array[idx].key = hotkey;
	unlock();
}*/

/*obs_key_combination_t obs_hotkey_get(obs_hotkey_id id)
{
	obs_key_combination_t key = {0, OBS_KEY_NONE};
	size_t idx;

	if (!lock())
		return;

	if (find_id(id, &idx))
		key = obs->hotkeys.hotkeys.array[idx].key;
	unlock();

	return key;
}*/

static inline void remove_bindings(obs_hotkey_id id);

void obs_hotkey_load_bindings(obs_hotkey_id id,
		obs_key_combination_t *combinations, size_t num)
{
	size_t idx;

	if (!lock())
		return;

	if (find_id(id, &idx)) {
		remove_bindings(id);
		for (size_t i = 0; i < num; i++)
			create_binding(&obs->hotkeys.hotkeys.array[idx],
					combinations[i]);
	}
	unlock();
}

void obs_hotkey_load(obs_hotkey_id id, obs_data_array_t *data)
{
	size_t idx;

	if (!lock())
		return;

	if (find_id(id, &idx)) {
		remove_bindings(id);
		load_bindings(&obs->hotkeys.hotkeys.array[idx], data);
	}
	unlock();
}

static inline bool enum_load_bindings(size_t idx, obs_hotkey_t *hotkey,
		void *data)
{
	UNUSED_PARAMETER(idx);

	obs_data_array_t *hotkey_data = obs_data_get_array(data, hotkey->name);
	if (!hotkey_data)
		return true;

	load_bindings(hotkey, hotkey_data);
	obs_data_array_release(hotkey_data);
	return true;
}

void obs_hotkeys_load_source(obs_source_t *source, obs_data_t *bindings)
{
	if (!source || !bindings)
		return;
	if (!lock())
		return;

	enum_context_hotkeys(&source->context, enum_load_bindings, bindings);
	unlock();
}

static inline void save_modifier(uint32_t modifiers, obs_data_t *data,
		const char *name, uint32_t flag)
{
	if ((modifiers & flag) == flag)
		obs_data_set_bool(data, name, true);
}

struct save_bindings_helper_t {
	obs_data_array_t *array;
	obs_hotkey_t     *hotkey;
};

static inline bool save_bindings_helper(size_t idx,
		obs_hotkey_binding_t *binding, void *data)
{
	UNUSED_PARAMETER(idx);
	struct save_bindings_helper_t *h = data;

	if (h->hotkey->id != binding->hotkey_id)
		return true;

	obs_data_t *hotkey = obs_data_create();

	uint32_t modifiers = binding->key.modifiers;
	save_modifier(modifiers, hotkey, "shift", INTERACT_SHIFT_KEY);
	save_modifier(modifiers, hotkey, "control", INTERACT_CONTROL_KEY);
	save_modifier(modifiers, hotkey, "alt", INTERACT_ALT_KEY);
	save_modifier(modifiers, hotkey, "command", INTERACT_COMMAND_KEY);

	obs_data_set_string(hotkey, "key", obs_key_to_name(binding->key.key));

	obs_data_array_push_back(h->array, hotkey);

	obs_data_release(hotkey);

	return true;
}

static inline obs_data_array_t *save_hotkey(obs_hotkey_t *hotkey)
{
	obs_data_array_t *data = obs_data_array_create();

	struct save_bindings_helper_t arg = {data, hotkey};
	enum_bindings(save_bindings_helper, &arg);

	return data;
}

obs_data_array_t *obs_hotkey_save(obs_hotkey_id id)
{
	size_t idx;
	obs_data_array_t *result = NULL;

	if (!lock())
		return result;

	if (find_id(id, &idx))
		result = save_hotkey(&obs->hotkeys.hotkeys.array[idx]);
	unlock();

	return result;
}

static inline bool enum_save_hotkey(size_t idx, obs_hotkey_t *hotkey,
		void *data)
{
	UNUSED_PARAMETER(idx);

	obs_data_array_t *hotkey_data = save_hotkey(hotkey);
	obs_data_set_array(data, hotkey->name, hotkey_data);
	obs_data_array_release(hotkey_data);
	return true;
}

static inline obs_data_t *save_context_hotkeys(struct obs_context_data *context)
{
	if (!context->hotkeys.num)
		return NULL;

	obs_data_t *result = obs_data_create();
	enum_context_hotkeys(context, enum_save_hotkey, result);
	return result;
}

obs_data_t *obs_hotkeys_save_source(obs_source_t *source)
{
	obs_data_t *result = NULL;

	if (!lock())
		return result;

	result = save_context_hotkeys(&source->context);
	unlock();

	return result;
}

struct binding_find_data {
	obs_hotkey_id id;
	size_t *idx;
	bool found;
};

static inline bool binding_finder(size_t idx, obs_hotkey_binding_t *binding,
		void *data)
{
	struct binding_find_data *find = data;
	if (binding->hotkey_id != find->id)
		return true;

	*find->idx  = idx;
	find->found = true;
	return false;
}

static inline bool find_binding(obs_hotkey_id id, size_t *idx)
{
	struct binding_find_data data = {id, idx, false};
	enum_bindings(binding_finder, &data);
	return data.found;
}

static inline void release_pressed_binding(obs_hotkey_binding_t *binding);

static inline void remove_bindings(obs_hotkey_id id)
{
	size_t idx;
	while (find_binding(id, &idx)) {
		obs_hotkey_binding_t *binding =
			&obs->hotkeys.bindings.array[idx];

		if (binding->pressed)
			release_pressed_binding(binding);

		da_erase(obs->hotkeys.bindings, idx);
	}
}

static inline bool unregister_hotkey(obs_hotkey_id id)
{
	if (id >= obs->hotkeys.next_id)
		return false;

	size_t idx;
	if (!find_id(id, &idx))
		return false;

	da_erase(obs->hotkeys.hotkeys, idx);
	remove_bindings(id);

	return obs->hotkeys.hotkeys.num >= idx;
}

void obs_hotkey_unregister(obs_hotkey_id id)
{
	if (!lock())
		return;
	if (unregister_hotkey(id))
		fixup_pointers();
	unlock();
}

void obs_hotkeys_context_release(struct obs_context_data *context)
{
	if (!lock())
		return;
	if (!context->hotkeys.num)
		goto cleanup;

	bool need_fixup = false;
	for (size_t i = 0; i < context->hotkeys.num; i++)
		need_fixup = unregister_hotkey(context->hotkeys.array[i]) ||
			need_fixup;

	if (need_fixup)
		fixup_pointers();

cleanup:
	da_free(context->hotkeys);
	obs_data_release(context->hotkey_data);
	unlock();
}

void obs_hotkeys_free(void)
{
	const size_t num      = obs->hotkeys.hotkeys.num;
	obs_hotkey_t *hotkeys = obs->hotkeys.hotkeys.array;
	for (size_t i = 0; i < num; i++) {
		bfree(hotkeys[i].name);
		bfree(hotkeys[i].description);
	}
	da_free(obs->hotkeys.hotkeys);
	da_free(obs->hotkeys.bindings);
}

struct obs_hotkey_internal_enum_forward {
	obs_hotkey_enum_func func;
	void                 *data;
};

static inline bool enum_hotkey(size_t idx, obs_hotkey_t *hotkey,
		void *data)
{
	UNUSED_PARAMETER(idx);

	struct obs_hotkey_internal_enum_forward *forward = data;
	return forward->func(hotkey->id, hotkey, forward->data);
}

void obs_enum_hotkeys(obs_hotkey_enum_func func, void *data)
{
	struct obs_hotkey_internal_enum_forward forwarder = {func, data};
	if (!lock())
		return;

	enum_hotkeys(enum_hotkey, &forwarder);
	unlock();
}

void obs_enum_hotkey_bindings(obs_hotkey_binding_enum_func func, void *data)
{
	if (!lock())
		return;

	enum_bindings(func, data);
	unlock();
}

static inline bool modifiers_match(obs_hotkey_binding_t *binding,
		uint32_t modifiers_)
{
	uint32_t modifiers = binding->key.modifiers;
	return !modifiers || (modifiers & modifiers_) == modifiers;
}

static inline bool is_pressed(obs_key_t key)
{
	return obs_hotkeys_platform_is_pressed(obs->hotkeys.platform_context,
			key);
}

static inline void release_pressed_binding(obs_hotkey_binding_t *binding)
{
	obs_hotkey_t *hotkey = binding->hotkey;

	binding->pressed = false;
	hotkey->pressed -= 1;
	if (!hotkey->pressed)
		hotkey->func(hotkey->id, hotkey, false, hotkey->data);
}

static inline void handle_binding(obs_hotkey_binding_t *binding,
		uint32_t modifiers, bool no_primary, bool *pressed)
{
	bool modifiers_match_ = modifiers_match(binding, modifiers);

	if (!binding->key.modifiers)
		binding->modifiers_match = true;

	if (!binding->key.modifiers && binding->key.key == OBS_KEY_NONE)
		goto reset;

	if (!binding->modifiers_match || !modifiers_match_)
		goto reset;

	if ((pressed && !*pressed) ||
			(!pressed && !is_pressed(binding->key.key)))
		goto reset;

	if (binding->pressed ||
		(no_primary && !binding->primary_action_release))
		return;

	obs_hotkey_t *hotkey = binding->hotkey;

	binding->pressed = true;
	if (!hotkey->pressed)
		hotkey->func(hotkey->id, hotkey, true, hotkey->data);
	hotkey->pressed += 1;

	return;

reset:
	binding->modifiers_match = modifiers_match_;
	if (!binding->pressed ||
		(no_primary && binding->primary_action_release))
		return;

	release_pressed_binding(binding);
}

struct obs_hotkey_internal_inject {
	obs_key_combination_t hotkey;
	bool                  pressed;
};

static inline bool inject_hotkey(size_t idx, obs_hotkey_binding_t *binding,
		void *data)
{
	UNUSED_PARAMETER(idx);
	struct obs_hotkey_internal_inject *event = data;

	if (modifiers_match(binding, event->hotkey.modifiers)) {
		bool pressed = binding->key.key == event->hotkey.key &&
			event->pressed;
		handle_binding(binding, event->hotkey.modifiers, false,
				&pressed);
	}

	return true;
}

void obs_hotkey_inject_event(obs_key_combination_t hotkey, bool pressed)
{
	if (!lock())
		return;

	struct obs_hotkey_internal_inject event = {
		{hotkey.modifiers, hotkey.key},
		pressed
	};
	enum_bindings(inject_hotkey, &event);
	unlock();
}

void obs_hotkey_enable_background_primary(bool enable)
{
	if (!lock())
		return;

	blog(LOG_INFO, "disabled: %s", !enable ? "true" : "false");
	obs->hotkeys.thread_disable_primary = !enable;
	unlock();
}

static inline bool query_hotkey(size_t idx, obs_hotkey_binding_t *binding,
		void *data)
{
	UNUSED_PARAMETER(idx);

	uint32_t modifiers = *(uint32_t*)data;
	bool no_primary    = obs->hotkeys.thread_disable_primary;
	handle_binding(binding, modifiers, no_primary, NULL);

	return true;
}

static inline void query_hotkeys()
{
	uint32_t modifiers = 0;
	if (is_pressed(OBS_KEY_SHIFT))
		modifiers |= INTERACT_SHIFT_KEY;
	if (is_pressed(OBS_KEY_CONTROL))
		modifiers |= INTERACT_CONTROL_KEY;
	if (is_pressed(OBS_KEY_ALT))
		modifiers |= INTERACT_ALT_KEY;
	if (is_pressed(OBS_KEY_META))
		modifiers |= INTERACT_COMMAND_KEY;

	enum_bindings(query_hotkey, &modifiers);
}

void *obs_hotkey_thread(void *arg)
{
	UNUSED_PARAMETER(arg);
	while (os_event_timedwait(obs->hotkeys.stop_event, 25) == ETIMEDOUT) {
		if (!lock())
			continue;

		query_hotkeys();
		unlock();
	}
	return NULL;
}

