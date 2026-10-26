/* ScummVM - Graphic Adventure Engine
 *
 * This file is part of the Dune engine.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dune/live_state.h"

#include "common/config-manager.h"
#include "common/fs.h"
#include "common/stream.h"
#include "common/system.h"

#include "dune/build_id.h"
#include "dune/debug.h"
#include "dune/resource.h"
#include "dune/text.h"
#include "dune/world.h"

namespace Dune {

namespace {

LiveStateExporter *g_liveState = nullptr;

void quoted(Common::String &json, const Common::String &text) {
	json += '"';
	for (uint i = 0; i < text.size(); ++i) {
		const byte c = (byte)text[i];
		if (c == '"' || c == '\\') {
			json += '\\';
			json += (char)c;
		} else if (c < 0x20 || c >= 0x7f) {
			json += Common::String::format("\\u%04x", (uint)c);
		} else {
			json += (char)c;
		}
	}
	json += '"';
}

void hexByte(Common::String &json, byte value) {
	static const char digits[] = "0123456789abcdef";
	json += digits[value >> 4];
	json += digits[value & 15];
}

const char *const kEquipment[] = { "Harvester", "Ornithopter", "Crysknives", "Laser guns", "Weirding modules", "Atomics", "Bulbs" };

const char *troopTask(const Troop &troop) {
	if (!troop.hired())
		return "Not rallied";
	if (troop.occupation & 0x10)
		return "Stopped";
	switch (troop.occupation & 0x0f) {
	case 0: return "Spice mining";
	case 1: return "Prospecting";
	case 2: return "Waiting for orders";
	case 4: return "Military training";
	case 5: return "Espionage";
	case 6: return "Attacking";
	case 8: return "Irrigation";
	case 9: return "Building wind trap";
	case 10: return "Growing bulbs";
	default: return "Working";
	}
}

} // namespace

LiveStateExporter::LiveStateExporter(World &world, Resource &resources, StartupLog &log) :
		_world(world), _resources(resources), _log(log) {
	if (!ConfMan.hasKey("dune_live_state"))
		return;
	_path = ConfMan.get("dune_live_state");
	if (_path.empty())
		return;
	// Accept POSIX and Windows absolute paths, never a filename relative to
	// the game data directory. Only the local publisher handles credentials.
	if (_path[0] != '/' && !(_path.size() > 2 && _path[1] == ':' && (_path[2] == '/' || _path[2] == '\\'))) {
		_log.line("Live results: dune_live_state must be an absolute filename; export disabled");
		_path.clear();
		return;
	}
	g_liveState = this;
}

LiveStateExporter::~LiveStateExporter() {
	if (g_liveState == this) {
		tick(g_system, true);
		g_liveState = nullptr;
	}
	delete _sentences;
}

void LiveStateExporter::setGlobeView(uint phase, int tilt, bool visible) {
	_globePhase = phase % 398;
	_globeTilt = CLIP<int>(tilt, -98, 98);
	_globeVisible = visible;
	_globeViewKnown = true;
}

void LiveStateExporter::tick(OSystem *system, bool ending) {
	if (_path.empty() || !system || !_world.ready())
		return;
	const uint32 now = system->getMillis();
	if (_started && !ending && now - _lastWrite < (_failed ? 30000u : 2000u))
		return;
	if (!_started) {
		_started = true;
		_start = now;
		_sentences = new SentenceBank(_resources, _log);
		_sentences->load();
		_resources.load("MAP2.HSQ", _spiceFields);
	}
	_lastWrite = now;
	const Common::String json = snapshot(now - _start, !ending);
	Common::SeekableWriteStream *file = Common::FSNode(Common::Path(_path)).createWriteStream(true);
	bool ok = false;
	if (file) {
		const bool written = file->write(json.c_str(), json.size()) == json.size();
		file->finalize();
		ok = written && !file->err();
		delete file;
	}
	if (!ok && !_failed)
		_log.line("Live results: snapshot write failed; retrying in 30 seconds while gameplay continues");
	else if (ok && _failed)
		_log.line("Live results: snapshot writes recovered");
	else if (ok && !_sequence)
		_log.line("Live results: player-known JSON feed enabled (two-second updates)");
	_failed = !ok;
	++_sequence;
}

Common::String LiveStateExporter::snapshot(uint32 elapsed, bool running) {
	// Loading the map cache is safe and ensures results and map use the
	// same live cells, including vegetation and changes of territory.
	_world.map();
	const ResultsSummary r = _world.resultsSummary();
	const GameState &state = _world.state();
	Common::String json = Common::String::format("{\"schemaVersion\":1,\"elapsedMillis\":%u,\"sequence\":%u,\"running\":%s,\"stream\":{\"release\":",
			elapsed, _sequence, running ? "true" : "false");
	quoted(json, _world.segaCd() ? "segacd" : _world.amiga() ? "amiga" : _world.floppy() ? "floppy" : "cd");
	json += ",\"route\":";
	quoted(json, ConfMan.hasKey("dune_speedrun") ? ConfMan.get("dune_speedrun") :
			ConfMan.hasKey("dune_segacd_showcase") ? ConfMan.get("dune_segacd_showcase") : Common::String("manual"));
	json += ",\"build\":";
	quoted(json, DUNE_BUILD_ID);
	json += Common::String::format("},\"game\":{\"day\":%u,\"charisma\":%u,\"phase\":%u",
			_world.displayDay(), state.b(World::kCharisma) / 2u, state.b(GameState::kPhase));
	const uint here = _world.currentLocation();
	if (here < _world.locationCount() && !_world.location(here).hidden())
		json += Common::String::format(",\"locationId\":%u", here);
	json += '}';
	if (_globeViewKnown)
		json += Common::String::format(",\"globe\":{\"available\":true,\"visible\":%s,\"phase\":%u,\"tilt\":%d}",
				_globeVisible ? "true" : "false", _globePhase, _globeTilt);
	json += Common::String::format(",\"results\":{\"atreides\":{\"areaPercent\":%u,\"spiceProductionKg\":%u,\"men\":%u},"
			"\"harkonnen\":{\"areaPercent\":%u,\"spiceProductionKg\":%u,\"men\":%u}},",
			r.atreidesArea, r.atreidesSpice, r.atreidesMen, r.harkonnenArea, r.harkonnenSpice, r.harkonnenMen);
	appendMap(json);
	json += ',';
	appendLocations(json);
	json += "}\n";
	return json;
}

void LiveStateExporter::appendMap(Common::String &json) {
	const Common::Array<byte> &map = _world.map();
	const Common::Array<byte> vegetation = _world.vegetationMap();
	byte densities[256] = {};
	for (uint i = 0; i < _world.locationCount(); ++i) {
		const Location l = _world.location(i);
		if (!l.hidden() && l.spiceField)
			densities[l.spiceField] = (l.status & 0x40) ? (byte)(2 + (l.spiceDensity >> 4)) : 1;
	}
	json += "\"map\":{\"encoding\":\"hex-rows-v1\",\"latitudeStep\":0.9090909091,\"longitudeOrigin\":0,\"vegetationAvailable\":true,\"rows\":[";
	for (uint layer = 0; layer < 3; ++layer) {
		bool comma = false;
		if (layer == 1)
			json += "],\"spiceRows\":[";
		else if (layer == 2)
			json += "],\"vegetationRows\":[";
		for (int latitude = -98; latitude <= 98; ++latitude) {
			const int start = _world.mapCell(0, (int16)latitude);
			const uint cells = _world.rowCells(latitude);
			if (start < 0 || !cells || (uint)start + cells > map.size())
				continue;
			if (comma)
				json += ',';
			comma = true;
			json += Common::String::format("{\"latitude\":%d,\"cells\":\"", latitude);
			for (uint x = 0; x < cells; ++x) {
				const uint at = (uint)start + x;
				// Bit 6 marks *all* places, including undiscovered ones. It
				// must never leave the engine. Stage bits are the public
				// RESULTS globe colours, so they can be shown worldwide.
				const byte cell = layer == 0 ? (map[at] & 0x3f) : layer == 1 ?
						(at < _spiceFields.size() ? densities[_spiceFields[at]] : 0) : vegetation[at];
				hexByte(json, cell);
			}
			json += "\"}";
		}
	}
	json += "]}";
}

void LiveStateExporter::appendLocations(Common::String &json) {
	json += "\"locations\":[";
	bool comma = false;
	for (uint i = 0; i < _world.locationCount(); ++i) {
		const Location l = _world.location(i);
		if (l.hidden())
			continue;
		Common::Array<uint> ids;
		_world.troopsAt(i, ids);
		bool ownTroops = false;
		for (uint n = 0; n < ids.size(); ++n) {
			const Troop t = _world.troop(ids[n]);
			if (t.id && !t.harkonnen() && t.hired() && !(t.occupation & 0x60))
				ownTroops = true;
		}
		const bool visited = (l.status & 0x10) != 0 || i == _world.currentLocation();
		const bool friendly = _world.friendlyPlace(i);
		const bool details = friendly && (visited || ownTroops);
		const char *type = l.isSietch() ? "sietch" : l.isVillage() ? "village" : l.isFortress() ? "fortress" : "palace";
		const char *owner = !friendly ? "harkonnen" : (ownTroops || (l.status & 8) || l.type == Location::kPalace) ? "atreides" : "fremen";
		if (comma)
			json += ',';
		comma = true;
		json += Common::String::format("{\"id\":%u,\"name\":", i);
		quoted(json, _world.locationName(i, *_sentences));
		json += ",\"type\":";
		quoted(json, type);
		json += Common::String::format(",\"longitude\":%.6f,\"latitude\":%.6f,\"visited\":%s,\"owner\":",
				(double)l.longitude * 360.0 / 65536.0, -(double)l.latitude * 90.0 / 99.0, visited ? "true" : "false");
		quoted(json, owner);
		json += Common::String::format(",\"spiceProspected\":%s", (l.status & 0x40) ? "true" : "false");
		if (l.status & 0x40)
			json += Common::String::format(",\"spiceDensity\":%u", (uint)l.spiceDensity);
		json += ",\"equipment\":[";
		if (details) {
			byte counts[7] = {};
			_world.placeFreeEquipment(i, counts);
			bool itemComma = false;
			for (uint n = 0; n < 7; ++n) {
				if (!counts[n])
					continue;
				if (itemComma)
					json += ',';
				itemComma = true;
				json += "{\"name\":";
				quoted(json, kEquipment[n]);
				json += Common::String::format(",\"count\":%u}", (uint)counts[n]);
			}
		}
		json += "],\"troops\":[";
		bool troopComma = false;
		for (uint n = 0; n < ids.size(); ++n) {
			const Troop t = _world.troop(ids[n]);
			// An icon revealed by espionage does not disclose the enemy's
			// full record. Keep individual enemy stats private. Moving or
			// captured troops likewise cannot expose a secret destination.
			if (!t.id || t.harkonnen() || (t.occupation & 0x60) || (!t.hired() && !visited))
				continue;
			if (troopComma)
				json += ',';
			troopComma = true;
			json += Common::String::format("{\"id\":%u,\"name\":\"Fremen troop %u\",\"men\":%u,\"task\":", t.id, t.id, t.population);
			quoted(json, troopTask(t));
			json += ",\"side\":";
			quoted(json, t.hired() ? "atreides" : "fremen");
			json += ",\"equipment\":[";
			bool itemComma = false;
			for (uint item = 0; item < 7; ++item) {
				if (!(t.equipment & (0x80 >> item)))
					continue;
				if (itemComma)
					json += ',';
				itemComma = true;
				quoted(json, kEquipment[item]);
			}
			json += "]}";
		}
		json += "],\"notes\":[";
		if (!details)
			quoted(json, friendly ? "Discovered on the map; local equipment has not been inspected." : "Enemy location discovered. Individual enemy troop and equipment details are not disclosed.");
		json += "]}";
	}
	json += ']';
}

void liveStateTick(OSystem *system) {
	if (g_liveState)
		g_liveState->tick(system);
}

void liveStateGlobeView(uint phase, int tilt, bool visible) {
	if (g_liveState)
		g_liveState->setGlobeView(phase, tilt, visible);
}

} // namespace Dune
