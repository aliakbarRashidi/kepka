/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "info/media/info_media_inner_widget.h"

#include <rpl/flatten_latest.h>
#include "boxes/abstract_box.h"
#include "info/media/info_media_list_widget.h"
#include "info/media/info_media_buttons.h"
#include "info/profile/info_profile_button.h"
#include "info/profile/info_profile_icon.h"
#include "info/info_controller.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/search_field_controller.h"
#include "styles/style_info.h"
#include "lang/lang_keys.h"

namespace Info {
namespace Media {
namespace {

using Type = InnerWidget::Type;

base::optional<int> TypeToTabIndex(Type type) {
	switch (type) {
	case Type::Photo: return 0;
	case Type::Video: return 1;
	case Type::File: return 2;
	}
	return base::none;
}

Type TabIndexToType(int index) {
	switch (index) {
	case 0: return Type::Photo;
	case 1: return Type::Video;
	case 2: return Type::File;
	}
	Unexpected("Index in Info::Media::TabIndexToType()");
}

} // namespace

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller)
: RpWidget(parent)
, _controller(controller) {
	_list = setupList();
	setupOtherTypes();
}

void InnerWidget::setupOtherTypes() {
	_controller->wrapValue()
		| rpl::start_with_next([this](Wrap value) {
			if (value == Wrap::Side
				&& TypeToTabIndex(type())) {
				createOtherTypes();
			} else {
				_otherTabs = nullptr;
				_otherTypes.destroy();
				refreshHeight();
			}
			refreshSearchField();
		}, lifetime());
}

void InnerWidget::createOtherTypes() {
	_otherTabsShadow.create(this);
	_otherTabsShadow->show();

	_otherTabs = nullptr;
	_otherTypes.create(this);
	_otherTypes->show();

	createTypeButtons();
	_otherTypes->add(object_ptr<BoxContentDivider>(_otherTypes));
	createTabs();

	_otherTypes->heightValue()
		| rpl::start_with_next(
			[this] { refreshHeight(); },
			_otherTypes->lifetime());
}

void InnerWidget::createTypeButtons() {
	auto wrap = _otherTypes->add(object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
		_otherTypes,
		object_ptr<Ui::VerticalLayout>(_otherTypes)));
	auto content = wrap->entity();
	content->add(object_ptr<Ui::FixedHeightWidget>(
		content,
		st::infoProfileSkip));

	auto tracker = Ui::MultiSlideTracker();
	auto addMediaButton = [&](
			Type type,
			const style::icon &icon) {
		auto result = AddButton(
			content,
			_controller->window(),
			_controller->peer(),
			_controller->migrated(),
			type,
			tracker);
		object_ptr<Profile::FloatingIcon>(
			result,
			icon,
			st::infoSharedMediaButtonIconPosition);
	};
	auto addCommonGroupsButton = [&](
			not_null<UserData*> user,
			const style::icon &icon) {
		auto result = AddCommonGroupsButton(
			content,
			_controller->window(),
			user,
			tracker);
		object_ptr<Profile::FloatingIcon>(
			result,
			icon,
			st::infoSharedMediaButtonIconPosition);
	};

	addMediaButton(Type::MusicFile, st::infoIconMediaAudio);
	addMediaButton(Type::Link, st::infoIconMediaLink);
	if (auto user = _controller->peer()->asUser()) {
		addCommonGroupsButton(user, st::infoIconMediaGroup);
	}
	addMediaButton(Type::VoiceFile, st::infoIconMediaVoice);
//	addMediaButton(Type::RoundFile, st::infoIconMediaRound);

	content->add(object_ptr<Ui::FixedHeightWidget>(
		content,
		st::infoProfileSkip));
	wrap->toggleOn(tracker.atLeastOneShownValue());
	wrap->finishAnimating();
}

void InnerWidget::createTabs() {
	_otherTabs = _otherTypes->add(object_ptr<Ui::SettingsSlider>(
		this,
		st::infoTabs));
	auto sections = QStringList();
	sections.push_back(lang(lng_media_type_photos).toUpper());
	sections.push_back(lang(lng_media_type_videos).toUpper());
	sections.push_back(lang(lng_media_type_files).toUpper());
	_otherTabs->setSections(sections);
	_otherTabs->setActiveSection(*TypeToTabIndex(type()));
	_otherTabs->finishAnimating();

	_otherTabs->sectionActivated()
		| rpl::map([](int index) { return TabIndexToType(index); })
		| rpl::start_with_next(
			[this](Type newType) {
				if (type() != newType) {
					switchToTab(Memento(
						_controller->peerId(),
						_controller->migratedPeerId(),
						newType));
				}
			},
			_otherTabs->lifetime());
}

Type InnerWidget::type() const {
	return _controller->section().mediaType();
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_list, visibleTop, visibleBottom);
}

bool InnerWidget::showInternal(not_null<Memento*> memento) {
	if (!_controller->validateMementoPeer(memento)) {
		return false;
	}
	auto mementoType = memento->section().mediaType();
	if (mementoType == type()) {
		restoreState(memento);
		return true;
	} else if (_otherTypes) {
		if (TypeToTabIndex(mementoType)) {
			switchToTab(std::move(*memento));
			return true;
		}
	}
	return false;
}

void InnerWidget::switchToTab(Memento &&memento) {
	// Save state of the tab before setSection() call.
	_controller->setSection(memento.section());
	_list = setupList();
	restoreState(&memento);
	_list->show();
	_list->resizeToWidth(width());
	refreshHeight();
	if (_otherTypes) {
		_otherTabsShadow->raise();
		_otherTypes->raise();
		_otherTabs->setActiveSection(*TypeToTabIndex(type()));
	}
}

void InnerWidget::refreshSearchField() {
	auto search = _controller->searchFieldController();
	if (search && _otherTabs) {
		_searchField = search->createView(
			this,
			st::infoMediaSearch);
		_searchField->resizeToWidth(width());
		_searchField->show();
	} else {
		_searchField = nullptr;
	}
}

object_ptr<ListWidget> InnerWidget::setupList() {
	refreshSearchField();
	auto result = object_ptr<ListWidget>(
		this,
		_controller);
	result->heightValue()
		| rpl::start_with_next(
			[this] { refreshHeight(); },
			result->lifetime());
	using namespace rpl::mappers;
	result->scrollToRequests()
		| rpl::map([widget = result.data()](int to) {
			return widget->y() + to;
		})
		| rpl::start_to_stream(
			_scrollToRequests,
			result->lifetime());
	_selectedLists.fire(result->selectedListValue());
	return result;
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	_list->saveState(memento);
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	_list->restoreState(memento);
}

rpl::producer<SelectedItems> InnerWidget::selectedListValue() const {
	return _selectedLists.events_starting_with(_list->selectedListValue())
		| rpl::flatten_latest();
}

void InnerWidget::cancelSelection() {
	_list->cancelSelection();
}

InnerWidget::~InnerWidget() = default;

int InnerWidget::resizeGetHeight(int newWidth) {
	_inResize = true;
	auto guard = gsl::finally([this] { _inResize = false; });

	if (_otherTypes) {
		_otherTypes->resizeToWidth(newWidth);
		_otherTabsShadow->resizeToWidth(newWidth);
	}
	if (_searchField) {
		_searchField->resizeToWidth(newWidth);
	}
	_list->resizeToWidth(newWidth);
	return recountHeight();
}

void InnerWidget::refreshHeight() {
	if (_inResize) {
		return;
	}
	resize(width(), recountHeight());
}

int InnerWidget::recountHeight() {
	auto top = 0;
	if (_otherTypes) {
		_otherTypes->moveToLeft(0, top);
		top += _otherTypes->heightNoMargins() - st::lineWidth;
		_otherTabsShadow->moveToLeft(0, top);
	}
	if (_searchField) {
		_searchField->moveToLeft(0, top);
		top += _searchField->heightNoMargins() - st::lineWidth;
	}
	if (_list) {
		_list->moveToLeft(0, top);
		top += _list->heightNoMargins();
	}
	return top;
}

} // namespace Media
} // namespace Info