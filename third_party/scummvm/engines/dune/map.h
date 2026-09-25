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

#ifndef ENGINES_DUNE_MAP_H
#define ENGINES_DUNE_MAP_H

#include "common/array.h"
#include "common/rect.h"
#include "common/scummsys.h"
#include "common/str.h"

#include "graphics/managed_surface.h"

class OSystem;

namespace Dune {

class Panel;
class Resource;
class SentenceBank;
class Sprite;
class StartupLog;
class World;
struct Location;

/**
 * The flat map of Arrakis (MAP.HSQ, 50681 bytes of 4-bit terrain in rows of
 * varying length described by TABLAT.BIN) drawn the way the original does
 * it, band by band with horizontal and vertical interpolation: a port of
 * madmoose's dune-rust map_renderer.rs. The view is 312x144 at (4,4) in the
 * 320x152 picture, 36 latitude rows of four pixels each.
 */
class MapRenderer {
public:
	enum {
		kBands = 196,
		kBandBegin = 5,
		kBandEnd = 191,
		kViewX = 4,
		kViewY = 4,
		kViewWidth = 312,
		kViewRows = 36,
		kMapSize = 50681,
		kTablatSize = 792,
		kMapBase = 0x62fc
	};

	MapRenderer(const Common::Array<byte> &map, const Common::Array<byte> &tablat);

	/** Draw the map around (latitude, longitude): latitude -75..75, longitude 0..65535. */
	void draw(Graphics::Surface &view, int16 latitude, uint16 longitude);
	/** Where a place appears in the view for the same window, or false when off-screen. */
	bool project(int16 latitude, uint16 longitude, int16 placeLatitude, uint16 placeLongitude, int &x, int &y) const;
	/** The inverse of project(): the map position under a view pixel. */
	bool unproject(int16 latitude, uint16 longitude, int x, int y, int16 &placeLatitude, uint16 &placeLongitude) const;

	uint16 rowLength(uint16 row) const;
	uint16 rowOffset(uint16 row) const;

private:
	void drawBand(Graphics::Surface &view, uint16 band, uint16 row, uint16 longitude);
	byte mapPixel(uint offset) const;
	void interpolateLine(uint16 row, uint16 longitude, uint16 output);
	void interpolateVertically(uint16 row);
	void postProcess(uint16 row);

	const Common::Array<byte> &_map;
	struct TablatEntry {
		uint16 offset, length;
	};
	TablatEntry _tablat[99];
	byte _buffer[4096];
};

/**
 * The map screens: the flat map with the location icons of ONMAP.HSQ
 * (frames 58 + place kind, +5 for sietches out of reach, as the executable's
 * sub_15DCE draws them) and the globe of FRESK.HSQ with the same icons in
 * their large versions (122 + kind). Both keep an index map so a tap finds
 * the place under it.
 */
class MapScreen {
public:
	enum Mode {
		kFlat,
		kGlobe
	};

	MapScreen(OSystem *system, Resource &resources, StartupLog &log, World &world);
	~MapScreen();

	bool open(Mode mode, bool selectDestination);
	Mode mode() const { return _mode; }
	bool selecting() const { return _selecting; }
	int destination() const { return _destination; }

	void centreOn(uint locationIndex);
	void scroll(int dx, int dy);
	void rotate(int deltaRotation, int deltaTilt);
	/** Place under a view position (0-151 rows), or -1. */
	int hitLocation(int x, int y) const;
	void select(int locationIndex) { _destination = locationIndex; }
	/** Pick a desert point (arm_pending_travel's desert case): destination -2, its position kept. */
	bool selectPoint(int x, int y);
	uint16 pointLongitude() const { return _pointLongitude; }
	int16 pointLatitude() const { return _pointLatitude; }

	/** SEE SPICE DENSITY: circles sized by each known sietch's density, and the box's title. */
	void setDensity(bool on) { _density = on; }
	/**
	 * The "DUNE MAP" title popup (map_show_rallied_troops_popup, seg000:5bb0)
	 * greets the map when it opens from the room. A click with either button
	 * dismisses it (seg000:5c76, 5ce4), and the idle handler takes it away
	 * 1000 timer ticks after the view opened (seg000:5c03).
	 */
	void setCaption(bool on) { _caption = on; }
	bool caption() const { return _caption; }
	/** 1000 ticks of the 200.3 Hz timer, in milliseconds. */
	static const uint32 kCaptionMillis = 4993;
	uint32 captionStart() const { return _captionStart; }
	bool density() const { return _density; }
	/**
	 * The flight (seg000:4f0c): the view follows Paul's position, drawn as
	 * ICONES 0x30 over the trail of past steps (0x2f) with the destination
	 * marked by 0x2e (seg000:49a0, 4a1a). @p active false clears it.
	 */
	void setFlight(bool active, uint16 longitude, int16 latitude, int destination);
	bool loadFlatMap() { return loadFlat(); }
	void addFlightTrail(uint16 longitude, int16 latitude);
	/**
	 * The flight's minimap (travel_minimap_redraw, seg000:49a0): the flat
	 * map around the position in a small framed window, drawn with whatever
	 * palette the flight view has installed (which is why it looks brown in
	 * the original), with the trail and the position.
	 */
	void drawMinimap(Graphics::ManagedSurface &surface, const Common::Rect &box, const Panel &panel);
	/**
	 * SEE RESULTS on the globe: the Harkonnen and Atreides panels of FRESK
	 * slide over the planet by @p pixels (0-100, swift-dune Fresk.swift
	 * renderHousesPanels); the scene writes the figures on them.
	 */
	void setResults(uint pixels) { _results = MIN<uint>(pixels, 100); }
	uint results() const { return _results; }

	/** Draw the view (rows 0-151) into @p surface and install the palette. */
	void draw(Graphics::ManagedSurface &surface, const Panel &panel, const SentenceBank *sentences, uint rallied);
	/** The navigation arrows over the panel (dune-rust wasm_map/wasm_globe), drawn with @p panel's icons. */
	void drawPanelExtras(Graphics::ManagedSurface &surface, const Panel &panel) const;
	/** Navigation arrow under a panel position: 0 up, 1 right, 2 down, 3 left, 4 centre, -1 none. */
	int hitArrow(int x, int y) const;

private:
	bool loadFlat();
	bool loadGlobe();
	void drawIcons(Graphics::ManagedSurface &surface);
	bool placeOnScreen(const Location &l, int &x, int &y) const;
	void drawFlight(Graphics::ManagedSurface &surface, const Panel &panel);
	void drawVegetation(Graphics::ManagedSurface &surface, const Panel &panel);
	void drawLocationPopup(Graphics::ManagedSurface &surface, const Panel &panel, const SentenceBank *sentences, uint index);
public:
	/** Draw ONMAP sprite @p sprite at (x, y) (the equipment icons, ds:192f). */
	void drawOnmap(Graphics::ManagedSurface &surface, uint sprite, int x, int y) const;
	uint onmapWidth(uint sprite) const;
	/** ONMAP's palette (the map colours, for the flight's minimap). */
	void applyPalette() const;
private:
	void drawInfoBox(Graphics::ManagedSurface &surface, const Panel &panel, const SentenceBank *sentences, uint rallied);
	static void patchRalliedCount(Common::String &text, uint rallied);

	OSystem *_system;
	Resource &_resources;
	StartupLog &_log;
	World &_world;
	Mode _mode;
	bool _selecting;
	int _destination;

	Common::Array<byte> _mapData, _tablatData;
	MapRenderer *_renderer;
	Sprite *_icons;   ///< ONMAP.HSQ
	Sprite *_fresk;   ///< FRESK.HSQ
	Sprite *_font;    ///< unused placeholder for the box text sheet
	int16 _latitude;
	uint16 _longitude;
	uint16 _rotation;
	int _tilt;
	bool _density;
	bool _caption = true;
	uint32 _captionStart = 0;
	uint16 _pointLongitude = 0;
	int16 _pointLatitude = 0;
	bool _flying;
	uint16 _flightLongitude;
	int16 _flightLatitude;
	int _flightDestination;
	Common::Array<uint32> _trail; ///< (longitude << 16) | (uint16)latitude
	uint _results;
	byte _index[320 * 152];
};

} // namespace Dune

#endif // ENGINES_DUNE_MAP_H
