/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This file is part of the Dune engine.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dune/book.h"

#include "common/system.h"
#include "common/util.h"

#include "graphics/paletteman.h"
#include "graphics/surface.h"

#include "dune/debug.h"
#include "dune/dialogue.h"
#include "dune/panel.h"
#include "dune/resource.h"
#include "dune/sprite.h"
#include "dune/text.h"

namespace Dune {

enum {
	kEncyclopediaCharacter = 16,
	kEncyclopediaList = 7,
	kCoverFrames = 3,
	kTileFrame = 3,
	kFirstLetterFrame = 5,
	kInkColour = 94,
	kPageWidth = 320,
	kPageHeight = 152,
	kTileWidth = 33,
	kTileHeight = 29,
	kTextLeft = 12,
	kTextRight = 308,
	kTextTop = 6,
	kLineHeight = 10,
	kLinesPerPage = 14,
	kDropCapLines = 3,
	kDropCapWidth = 34
};

static const char kDropCapLetters[] = "ADELOPSTU"; // BOOK.HSQ frames 5-13

Book::Book(OSystem *system, Resource &resources, StartupLog &log, Panel &panel, SentenceBank &sentences,
		Dialogue &dialogue, Conditions &conditions, GameState &state) :
		_system(system), _resources(resources), _log(log), _panel(panel), _sentences(sentences), _dialogue(dialogue),
		_conditions(conditions), _state(state), _sheet(nullptr), _cover(true), _topic(kTopicAll), _page(0) {
}

Book::~Book() {
	delete _sheet;
}

bool Book::open() {
	if (!_sheet) {
		Common::Array<byte> data;
		if (!_resources.load("BOOK.HSQ", data)) {
			_log.line("Book: BOOK.HSQ missing");
			return false;
		}
		_sheet = new Sprite(_system, data);
	}
	_cover = true;
	_topic = kTopicAll;
	_page = 0;
	_pages.clear();
	return true;
}

void Book::setTopic(uint topic) {
	_topic = topic < kTopicCount ? topic : kTopicAll;
	_cover = false;
	_page = 0;
	paginate();
	_log.line(Common::String::format("Book: topic %u, %u page(s)", _topic, _pages.size()));
}

void Book::nextPage() {
	if (_cover) {
		setTopic(_topic);
		return;
	}
	if (_page + 1 < _pages.size())
		++_page;
}

void Book::previousPage() {
	if (!_cover && _page)
		--_page;
}

void Book::collect(Common::Array<Paragraph> &paragraphs) const {
	Dialogue::Entry entry;
	uint offset = _dialogue.listOffset(kEncyclopediaCharacter, kEncyclopediaList);
	while (_dialogue.entryAt(offset, entry)) {
		const uint topic = (entry.flags2 >> 2) & 0x0f;
		if ((_topic == kTopicAll || topic == _topic) && _conditions.evaluate(entry.condition, _state)) {
			Paragraph paragraph;
			paragraph.text = _sentences.text((uint16)((entry.sentence + 1) | SentenceBank::kPhraseFlag),
					_dialogue.secondPhraseFile(offset), _state);
			paragraphs.push_back(paragraph);
		}
		offset += 4;
	}
	for (uint i = 0; i < _state.notebook.size(); ++i) {
		const uint character = _state.notebook[i] >> 11;
		const uint entryOffset = (_state.notebook[i] & 0x7ff) * 4;
		if (!_dialogue.entryAt(entryOffset, entry))
			continue;
		const uint topic = (entry.flags2 >> 2) & 0x0f;
		if (_topic != kTopicAll && topic != _topic)
			continue;
		Paragraph paragraph;
		paragraph.header = _sentences.command(kJournalHeaderBase + character);
		paragraph.text = _sentences.text((uint16)((entry.sentence + 1) | SentenceBank::kPhraseFlag),
				_dialogue.secondPhraseFile(entryOffset), _state);
		paragraphs.push_back(paragraph);
	}
}

void Book::wrap(const Common::String &text, int narrowLines, Common::Array<Common::String> &lines) const {
	Common::String line, word;
	for (uint i = 0; i <= text.size(); ++i) {
		const char c = i < text.size() ? text[i] : ' ';
		if (c != ' ' && c != '\r' && (byte)c != SentenceBank::kPageBreak) {
			word += c;
			continue;
		}
		if (!word.empty()) {
			const int width = (int)lines.size() < narrowLines ? kTextRight - kTextLeft - kDropCapWidth
															   : kTextRight - kTextLeft;
			const Common::String candidate = line.empty() ? word : line + " " + word;
			if (!line.empty() && _panel.textWidth(candidate.c_str(), false) > width) {
				lines.push_back(line);
				line = word;
			} else {
				line = candidate;
			}
			word.clear();
		}
		if (c == '\r') {
			lines.push_back(line);
			line.clear();
		}
	}
	if (!line.empty())
		lines.push_back(line);
}

void Book::paginate() {
	_pages.clear();
	Common::Array<Paragraph> paragraphs;
	collect(paragraphs);

	Common::Array<Row> rows;
	for (uint i = 0; i < paragraphs.size(); ++i) {
		Common::String text = paragraphs[i].text;
		int dropCap = -1;
		if (paragraphs[i].header.empty() && !text.empty()) {
			const char *letter = strchr(kDropCapLetters, toupper(text[0]));
			if (letter && *letter) {
				dropCap = kFirstLetterFrame + (int)(letter - kDropCapLetters);
				text.deleteChar(0);
			}
		}
		// Paragraphs are separated by a blank row, and one with a drop
		// capital starts on a fresh page when fewer than three rows remain.
		if (!rows.empty()) {
			Row blank = { "", -1, false };
			rows.push_back(blank);
			const uint used = rows.size() % kLinesPerPage;
			if (dropCap >= 0 && used && used > kLinesPerPage - kDropCapLines)
				while (rows.size() % kLinesPerPage)
					rows.push_back(blank);
		}
		if (!paragraphs[i].header.empty()) {
			Row header = { paragraphs[i].header, -1, false };
			rows.push_back(header);
		}
		Common::Array<Common::String> lines;
		wrap(text, dropCap >= 0 ? kDropCapLines : 0, lines);
		for (uint j = 0; j < lines.size(); ++j) {
			Row row = { lines[j], j == 0 ? dropCap : -1, dropCap >= 0 && j < kDropCapLines };
			rows.push_back(row);
		}
		// A drop capital spans three rows: keep the space beside it.
		for (uint j = lines.size(); dropCap >= 0 && j < kDropCapLines; ++j) {
			Row row = { "", -1, true };
			rows.push_back(row);
		}
	}
	for (uint i = 0; i < rows.size(); i += kLinesPerPage) {
		Common::Array<Row> page;
		for (uint j = i; j < rows.size() && j < i + kLinesPerPage; ++j)
			page.push_back(rows[j]);
		_pages.push_back(page);
	}
	if (_pages.empty())
		_pages.push_back(Common::Array<Row>());
}

void Book::draw(Graphics::ManagedSurface &surface) {
	surface.fillRect(Common::Rect(0, 0, kPageWidth, kPageHeight), 0);
	if (!_sheet)
		return;
	_sheet->setPalette();
	const byte black[3] = { 0, 0, 0 };
	_system->getPaletteManager()->setPalette(black, 0, 1);

	// The page is composed in a view-sized surface so the tiles cannot
	// spill into the panel rows.
	Graphics::Surface view;
	view.create(kPageWidth, kPageHeight, Graphics::PixelFormat::createFormatCLUT8());
	if (_cover) {
		for (uint frame = 0; frame < kCoverFrames; ++frame)
			_sheet->drawFrame(frame, &view, 0, 0);
	} else {
		for (int y = 0; y < kPageHeight; y += kTileHeight)
			for (int x = 0; x < kPageWidth; x += kTileWidth)
				_sheet->drawFrame(kTileFrame, &view, x, y);
		view.hLine(0, 0, kPageWidth - 1, kInkColour);
		view.hLine(0, kPageHeight - 1, kPageWidth - 1, kInkColour);
		view.vLine(0, 0, kPageHeight - 1, kInkColour);
		view.vLine(kPageWidth - 1, 0, kPageHeight - 1, kInkColour);
	}
	surface.copyRectToSurface(view, 0, 0, Common::Rect(0, 0, kPageWidth, kPageHeight));
	view.free();

	if (_cover || _page >= _pages.size())
		return;
	const Common::Array<Row> &rows = _pages[_page];
	for (uint i = 0; i < rows.size(); ++i) {
		const int y = kTextTop + (int)i * kLineHeight;
		if (rows[i].dropCap >= 0)
			_sheet->drawFrame(rows[i].dropCap, surface.surfacePtr(), kTextLeft, y);
		if (!rows[i].text.empty())
			_panel.drawText(surface, rows[i].text.c_str(), kTextLeft + (rows[i].indented ? kDropCapWidth : 0), y,
					kInkColour, false);
	}
}

} // namespace Dune
