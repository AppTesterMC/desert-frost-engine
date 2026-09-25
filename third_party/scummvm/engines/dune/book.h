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

#ifndef ENGINES_DUNE_BOOK_H
#define ENGINES_DUNE_BOOK_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

#include "graphics/managed_surface.h"

class OSystem;

namespace Dune {

class Conditions;
class Dialogue;
struct GameState;
class Panel;
class Resource;
class SentenceBank;
class Sprite;
class StartupLog;

/**
 * The book behind the panel's book icon: Paul's journal.
 *
 * BOOK.HSQ holds the cover (frames 0-2), the parchment tile (3, 33x29),
 * the drop capitals A D E L O P S T U (5-13) and two ornaments. The text
 * comes from two places, both filtered by the topic chosen in the command
 * box (COMMAND 214-218: all, politics, Paul on Dune, spice, the Fremen):
 *
 *  - the encyclopedia paragraphs, DIALOGUE.HSQ character 16 list 7, one per
 *    entry whose condition holds (most open with a story phase), the topic
 *    in bits 2-5 of the entry's third byte;
 *  - the journal: lines characters said that carry the same topic bits,
 *    recorded by the conversation code (GameState::notebook) and shown
 *    under the speaker's header (COMMAND 230 + character).
 *
 * The layout (page tile, ink colour 94, drop capitals) follows swift-dune's
 * Book.swift; how the original paginates is not recovered, so paragraphs
 * are set in the 9-row font, 14 lines a page, a tap on the right half of
 * the page turning forward and on the left half back.
 */
class Book {
public:
	enum {
		kTopicAll = 0,
		kTopicCount = 5,
		kCommandAllTopics = 214, ///< COMMANDx sentence of the first command row
		kJournalHeaderBase = 230 ///< COMMANDx sentence of "And the Duke said to Paul:"
	};

	Book(OSystem *system, Resource &resources, StartupLog &log, Panel &panel, SentenceBank &sentences,
		 Dialogue &dialogue, Conditions &conditions, GameState &state);
	~Book();

	/** Load the sheet and show the cover. */
	bool open();
	bool onCover() const { return _cover; }
	uint topic() const { return _topic; }
	uint page() const { return _page; }
	uint pageCount() const { return _pages.size(); }

	void setTopic(uint topic);
	/** From the cover: the first page; otherwise the next page when there is one. */
	void nextPage();
	void previousPage();

	/** Draw the cover or the current page into rows 0-151 of @p surface. */
	void draw(Graphics::ManagedSurface &surface);

private:
	struct Paragraph {
		Common::String header; ///< The speaker, for journal lines.
		Common::String text;
	};
	struct Row {
		Common::String text;
		int dropCap;   ///< BOOK.HSQ frame drawn in front of this row, or -1.
		bool indented; ///< Set beside a drop capital.
	};

	void collect(Common::Array<Paragraph> &paragraphs) const;
	void wrap(const Common::String &text, int narrowLines, Common::Array<Common::String> &lines) const;
	void paginate();

	OSystem *_system;
	Resource &_resources;
	StartupLog &_log;
	Panel &_panel;
	SentenceBank &_sentences;
	Dialogue &_dialogue;
	Conditions &_conditions;
	GameState &_state;

	Sprite *_sheet;
	bool _cover;
	uint _topic;
	uint _page;
	Common::Array<Common::Array<Row> > _pages;
};

} // namespace Dune

#endif // ENGINES_DUNE_BOOK_H
