/******************************************************************************
    Copyright (C) 2014-2015 by Ruwen Hahn <palana@stunned.de>

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

#include "hotkey-edit.hpp"

#include <util/dstr.hpp>
#include <QPointer>

#include "obs-app.hpp"
#include "qt-wrappers.hpp"

static inline bool key_combo_empty(obs_key_combination_t combo)
{
	return !combo.modifiers &&
		(combo.key == OBS_KEY_NONE || combo.key == OBS_KEY_UNKNOWN);
}

static inline bool operator!=(const obs_key_combination_t &c1,
		const obs_key_combination_t &c2)
{
	return c1.modifiers != c2.modifiers || c1.key != c2.key;
}

static inline bool operator==(const obs_key_combination_t &c1,
		const obs_key_combination_t &c2)
{
	return !(c1 != c2);
}

void OBSHotkeyEdit::keyPressEvent(QKeyEvent *event)
{
	if (event->isAutoRepeat())
		return;

	obs_key_combination_t new_key;

	switch (event->key()) {
	case Qt::Key_Shift:
	case Qt::Key_Control:
	case Qt::Key_Alt:
	case Qt::Key_Meta:
		new_key.key = OBS_KEY_NONE;
		break;

#ifdef __APPLE__
	case Qt::Key_CapsLock:
		// kVK_CapsLock == 57
		new_key.key = obs_key_from_virtual_key(57);
		break;
#endif

	default:
		new_key.key =
			obs_key_from_virtual_key(event->nativeVirtualKey());
	}

	key.modifiers = TranslateQtKeyboardEventModifiers(event->modifiers());

	if (new_key == key)
		return;

	key = new_key;

	changed = true;
	emit KeyChanged(key);

	RenderKey();
}

#ifdef __APPLE__
void OBSHotkeyEdit::keyReleaseEvent(QKeyEvent *event)
{
	if (event->isAutoRepeat())
		return;

	if (event->key() != Qt::Key_CapsLock)
		return;

	obs_key_combination_t new_key;

	// kVK_CapsLock == 57
	new_key.key = obs_key_from_virtual_key(57);
	new_key.modifiers =
		TranslateQtKeyboardEventModifiers(event->modifiers());

	if (new_key == key)
		return;

	key = new_key;

	changed = true;
	emit KeyChanged(key);

	RenderKey();
}
#endif

void OBSHotkeyEdit::RenderKey()
{
	DStr str;
	obs_key_to_str(key.key, str);

	int mods = 0;
	auto setMod = [&](uint32_t flag, int code)
	{
		if ((key.modifiers & flag) == flag)
			mods += code;
	};
#ifdef __APPLE__
	bool macFlip = true;
#else
	bool macFlip = false;
#endif
	setMod(INTERACT_SHIFT_KEY,   Qt::SHIFT);
	setMod(INTERACT_CONTROL_KEY, macFlip ? Qt::META : Qt::CTRL);
	setMod(INTERACT_ALT_KEY,     Qt::ALT);
	setMod(INTERACT_COMMAND_KEY, macFlip ? Qt::CTRL : Qt::META);

	setText(QKeySequence(mods).toString(QKeySequence::NativeText) + str);
}

void OBSHotkeyEdit::ResetKey()
{
	key = original;

	changed = false;
	emit KeyChanged(key);

	RenderKey();
}

void OBSHotkeyEdit::ClearKey()
{
	key = {0, OBS_KEY_NONE};

	changed = true;
	emit KeyChanged(key);

	RenderKey();
}

void OBSHotkeyEdit::InitSignalHandler()
{
	layoutChanged = {obs_get_signal_handler(),
			"hotkey_layout_change",
			[](void *this_, calldata_t*)
	{
		auto edit = static_cast<OBSHotkeyEdit*>(this_);
		QMetaObject::invokeMethod(edit, "ReloadKeyLayout");
	}, this};
}

void OBSHotkeyEdit::ReloadKeyLayout()
{
	RenderKey();
}

void OBSHotkeyWidget::SetKeyCombinations(
		const std::vector<obs_key_combination_t> &combos)
{
	QPointer<QVBoxLayout> layout = new QVBoxLayout;
	layout->setSpacing(0);
	layout->setMargin(0);
	setLayout(layout);

	if (combos.empty())
		AddEdit({0, OBS_KEY_NONE});

	for (auto combo : combos)
		AddEdit(combo);
}

bool OBSHotkeyWidget::Changed() const
{
	return changed ||
		std::any_of(begin(edits), end(edits), [](OBSHotkeyEdit *edit)
	{
		return edit->changed;
	});
}

void OBSHotkeyWidget::AddEdit(obs_key_combination combo, int idx)
{
	OBSHotkeyEdit *edit = new OBSHotkeyEdit(combo);

	QPushButton *reset = new QPushButton;
	reset->setText(QTStr("Reset"));
	reset->setEnabled(false);

	QPushButton *clear = new QPushButton;
	clear->setText(QTStr("Clear"));
	clear->setEnabled(!key_combo_empty(combo));

	QObject::connect(edit, &OBSHotkeyEdit::KeyChanged,
			[=](obs_key_combination_t new_combo)
	{
		clear->setEnabled(!key_combo_empty(new_combo));
		reset->setEnabled(combo != new_combo);
	});

	//TODO: localize + and -
	auto add = new QPushButton;
	add->setText("+");

	auto remove = new QPushButton;
	remove->setText("-");
	remove->setEnabled(removeButtons.size() > 0);

	auto CurrentIndex = [&, remove]
	{
		auto res = std::find(begin(removeButtons),
					end(removeButtons),
					remove);
		return std::distance(begin(removeButtons), res);
	};

	QObject::connect(add, &QPushButton::clicked,
			[&, CurrentIndex]
	{
		AddEdit({0, OBS_KEY_NONE}, CurrentIndex() + 1);
	});

	QObject::connect(remove, &QPushButton::clicked,
			[&, CurrentIndex]
	{
		auto idx = CurrentIndex();

		auto &edit = *(begin(edits) + idx);
		if (!key_combo_empty(edit->original)) {
			changed = true;
			emit KeyChanged();
		}

		removeButtons.erase(begin(removeButtons) + idx);
		edits.erase(begin(edits) + idx);

		auto item = layout()->takeAt(idx);
		QLayoutItem *child = nullptr;
		while ((child = item->layout()->takeAt(0))) {
			delete child->widget();
			delete child;
		}
		delete item;

		if (removeButtons.size() == 1)
			removeButtons.front()->setEnabled(false);
	});

	QHBoxLayout *subLayout = new QHBoxLayout;
	subLayout->addWidget(edit);
	subLayout->addWidget(reset);
	subLayout->addWidget(clear);
	subLayout->addWidget(add);
	subLayout->addWidget(remove);

	if (removeButtons.size() == 1)
		removeButtons.front()->setEnabled(true);

	if (idx != -1) {
		removeButtons.insert(begin(removeButtons) + idx, remove);
		edits.insert(begin(edits) + idx, edit);
	} else {
		removeButtons.emplace_back(remove);
		edits.emplace_back(edit);
	}

	layout()->insertLayout(idx, subLayout);

	QObject::connect(reset, &QPushButton::clicked,
			edit, &OBSHotkeyEdit::ResetKey);
	QObject::connect(clear, &QPushButton::clicked,
			edit, &OBSHotkeyEdit::ClearKey);

	QObject::connect(edit, &OBSHotkeyEdit::KeyChanged,
			[&](obs_key_combination)
	{
		emit KeyChanged();
	});
}

