/**
 * metro_blueprint_paste
 *
 * A line with a depot, a portal, a station on each of two levels joined by a
 * ramp, block signals and a station exit is copied into a blueprint together
 * with its surface:
 *
 *        exit
 *   ---- P(SW, D1) -- [M M] - A B R(D1->D2) - [T T T](D2)
 *     depot
 *
 * It is pasted as it is, and turned and mirrored; a train runs on each copy.
 * Pasting over the copy again changes nothing, pasting it shifted by a tile
 * builds nothing, unless pieces that do not fit may be left out. A station too
 * big for one command is pasted in several parts as one station. A one-way
 * signal keeps its direction through a turn.
 */
class MetroBlueprintPaste extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local report = MetroTest("metro_blueprint_paste", null, 510132);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();
		try {
			local area = build.find_buildable_rectangle(40, 40);
			report.check_true("build_area_found", area != null, "area");
			local company = companies.ensure_company();
			local cm = GSCompanyMode(company);
			local engine = trains.find_rail_engine();
			report.check_true("engine", engine != null, "engine");
			local x0 = build.tile_x(area), y0 = build.tile_y(area);
			local x = x0 + 2, y = y0 + 3;
			local d1 = -1, d2 = -2;
			local track = GSRail.RAILTRACK_NE_SW;
			local T = function(ax, ay) : (build) { return build.make_tile(ax, ay); };
			local kind = function(ax, ay, d) : (T) { return GSMetro.GetUndergroundTileKind(T(ax, ay), d); };

			/* The line, as in metro_signal_remove. */
			local depot_junction = T(x + 4, y), depot_front = T(x + 4, y + 1), depot_tile = T(x + 4, y + 2);
			report.check_true("surface", build.build_surface_x(x, x + 4, y) &&
				GSRail.BuildRail(T(x + 3, y), depot_junction, depot_front) &&
				GSRail.BuildRailTrack(depot_junction, GSRail.RAILTRACK_SW_SE), "surface");
			report.check_true("depot", GSRail.BuildRail(depot_tile, depot_front, depot_junction) && GSRail.BuildRailDepot(depot_tile, depot_front), "depot");
			report.check_true("portal", metro.build_portal(T(x + 5, y), GSMetro.DIAGDIR_SW, d1), "portal " + GSError.GetLastErrorString());
			report.check_true("ug1", metro.build_underground_x(x + 6, x + 7, y, d1), "ug1 " + GSError.GetLastErrorString());
			report.check_true("plat_m", metro.build_station_x(T(x + 8, y), d1), "plat_m " + GSError.GetLastErrorString());
			local sid_m = GSMetro.GetUndergroundStationID(T(x + 8, y), d1);
			report.check_true("plat_m2", metro.build_station_x(T(x + 9, y), d1, sid_m), "plat_m2 " + GSError.GetLastErrorString());
			report.check_true("ug2", metro.build_underground_x(x + 10, x + 11, y, d1), "ug2 " + GSError.GetLastErrorString());
			report.check_true("ramp", GSMetro.BuildRamp(T(x + 12, y), GSMetro.DIAGDIR_SW, d1, d2), "ramp " + GSError.GetLastErrorString());
			report.check_true("ug3", GSMetro.BuildUndergroundRail(T(x + 13, y), track, d2), "ug3 " + GSError.GetLastErrorString());
			report.check_true("plat_t", metro.build_station_x(T(x + 14, y), d2), "plat_t " + GSError.GetLastErrorString());
			local sid_t = GSMetro.GetUndergroundStationID(T(x + 14, y), d2);
			report.check_true("plat_t23", metro.build_station_x(T(x + 15, y), d2, sid_t) && metro.build_station_x(T(x + 16, y), d2, sid_t), "plat_t23");
			report.check_true("sig_a", GSMetro.BuildUndergroundSignal(T(x + 10, y), track, d1, GSRail.SIGNALTYPE_NORMAL), "A " + GSError.GetLastErrorString());
			report.check_true("sig_b", GSMetro.BuildUndergroundSignal(T(x + 11, y), track, d1, GSRail.SIGNALTYPE_NORMAL), "B " + GSError.GetLastErrorString());
			report.check_true("sig_ramp", GSMetro.BuildUndergroundSignal(T(x + 12, y), track, d2, GSRail.SIGNALTYPE_NORMAL), "R " + GSError.GetLastErrorString());
			report.check_true("exit", GSMetro.BuildExitSurface(T(x + 8, y - 1), sid_m, GSMetro.EXIT_PAVILION), "exit " + GSError.GetLastErrorString());
			/* A surface signal on the stub west of the depot junction, off the trains' way. */
			report.check_true("surface_signal", GSRail.BuildSignal(T(x + 1, y), T(x, y), GSRail.SIGNALTYPE_NORMAL), "surface signal " + GSError.GetLastErrorString());

			/* Copy it with its surface: rows y-1 .. y+2, columns x .. x+16. */
			local bp = GSMetro.CopyBlueprint(T(x, y - 1), T(x + 16, y + 2), d1, d2, true);
			report.check_true("copied", bp != null, "The line is copied.");
			report.metric("blueprint_length", bp.len());

			local paste = function(text, tile, depth, rotation, mirror, options) {
				local n = GSMetro.GetBlueprintPasteChunkCount(text, rotation, mirror, options);
				if (n <= 0) return false;
				local ok = true;
				for (local i = 0; i < n; i++) ok = GSMetro.PasteBlueprintChunk(text, tile, depth, rotation, mirror, options, i) && ok;
				return ok;
			};

			/* 1. As it is, eight rows further. */
			local money_before = GSCompany.GetBankBalance(company);
			report.check_true("paste_plain", paste(bp, T(x, y + 7), d1, 0, false, 0), "paste " + GSError.GetLastErrorString());
			local cost = money_before - GSCompany.GetBankBalance(company);
			report.metric("paste_cost", cost);
			report.check_true("paste_costs", cost > 0, "Pasting costs money.");
			local py = y + 8;
			report.check_true("pasted_depot", GSRail.IsRailDepotTile(T(x + 4, py + 2)), "The depot is pasted.");
			report.check_true("pasted_portal", GSMetro.IsPortalTile(T(x + 5, py)), "The portal is pasted.");
			report.check_eq("pasted_ramp", GSMetro.TK_RAMP, kind(x + 12, py, d1), "The ramp is pasted.");
			report.check_eq("pasted_t", GSMetro.TK_STATION, kind(x + 16, py, d2), "The lower platforms are pasted.");
			report.check_true("pasted_signal", GSMetro.HasUndergroundSignal(T(x + 11, py), track, d1) && GSMetro.IsUndergroundSignalTwoWay(T(x + 11, py), track, d1), "The signals are pasted.");
			report.check_true("pasted_ramp_signal", GSMetro.HasUndergroundSignal(T(x + 12, py), track, d2), "The ramp signal is pasted.");
			local sid_m1 = GSMetro.GetUndergroundStationID(T(x + 8, py), d1);
			local sid_t1 = GSMetro.GetUndergroundStationID(T(x + 16, py), d2);
			report.check_true("new_stations", GSStation.IsValidStation(sid_m1) && sid_m1 != sid_m && GSStation.IsValidStation(sid_t1) && sid_t1 != sid_t && sid_m1 != sid_t1, "The copy gets stations of its own.");
			report.check_eq("one_station_m", sid_m1, GSMetro.GetUndergroundStationID(T(x + 9, py), d1), "The platforms of M stay one station.");
			report.check_eq("platforms_t", 3, GSMetro.GetStationComplexPlatformCount(sid_t1), "T has its three platforms.");
			report.check_eq("exit_pasted", 1, GSMetro.GetStationComplexExitCount(sid_m1), "The exit of M is pasted with it.");
			report.check_eq("surface_signal_pasted", GSRail.SIGNALTYPE_NORMAL, GSRail.GetSignalType(T(x + 1, py), T(x, py)), "The surface signal is pasted.");

			/* Pasting over a copy changes nothing; shifted by a tile it builds nothing. */
			money_before = GSCompany.GetBankBalance(company);
			report.check_true("paste_again", paste(bp, T(x, y + 7), d1, 0, false, 0), "paste again " + GSError.GetLastErrorString());
			report.check_eq("paste_again_free", money_before, GSCompany.GetBankBalance(company), "Pasting over an identical copy costs nothing.");
			report.check_true("paste_third", paste(bp, T(x, y + 14), d1, 0, false, 0), "third copy " + GSError.GetLastErrorString());
			report.check_true("paste_shifted_fails", !paste(bp, T(x + 1, y + 14), d1, 0, false, 0), "A shifted paste meets the copy.");
			report.check_eq("shifted_nothing", GSMetro.TK_NONE, kind(x + 17, y + 15, d2), "Nothing of the shifted paste is built.");
			report.check_true("paste_shifted_best_effort", paste(bp, T(x + 1, y + 14), d1, 0, false, GSMetro.BLUEPRINT_BEST_EFFORT), "best effort " + GSError.GetLastErrorString());
			report.check_eq("shifted_some", GSMetro.TK_STATION, kind(x + 17, y + 15, d2), "Building what fits builds the free tiles.");

			/* 2. Turned a quarter and mirrored, to the east. */
			local ox = x + 22, oy = y0 + 2;
			report.check_true("paste_turned", paste(bp, T(ox, oy), d1, 1, true, 0), "paste turned " + GSError.GetLastErrorString());
			local depot2 = null, sid_m2 = null, sid_t2 = null;
			for (local dx = 0; dx < 4; dx++) {
				for (local dy = 0; dy < 17; dy++) {
					local t = T(ox + dx, oy + dy);
					if (GSRail.IsRailDepotTile(t)) depot2 = t;
					if (GSMetro.GetUndergroundTileKind(t, d1) == GSMetro.TK_STATION) sid_m2 = GSMetro.GetUndergroundStationID(t, d1);
					if (GSMetro.GetUndergroundTileKind(t, d2) == GSMetro.TK_STATION) sid_t2 = GSMetro.GetUndergroundStationID(t, d2);
				}
			}
			report.check_true("turned_found", depot2 != null && sid_m2 != null && sid_t2 != null, "The turned copy has its depot and stations.");
			report.check_eq("turned_depot_at", T(ox, oy + 12), depot2, "The depot lies where the turn and the mirror take it.");

			/* A station too big for one command. */
			local bx = x0 + 2, by = y0 + 24;
			local sid_big = null;
			local big_ok = true;
			for (local row = 0; row < 12 && big_ok; row++) {
				for (local col = 0; col < 12 && big_ok; col++) {
					big_ok = metro.build_station_x(T(bx + col, by + row), d1, sid_big == null ? GSStation.STATION_NEW : sid_big);
					if (sid_big == null) sid_big = GSMetro.GetUndergroundStationID(T(bx, by), d1);
				}
			}
			report.check_true("big_station", big_ok, "big station " + GSError.GetLastErrorString());
			local big = GSMetro.CopyBlueprint(T(bx, by), T(bx + 11, by + 11), d1, d1, false);
			local big_chunks = GSMetro.GetBlueprintPasteChunkCount(big, 0, false, 0);
			report.metric("big_chunks", big_chunks);
			report.check_true("big_split", big_chunks >= 2, "144 platforms need several commands.");
			report.check_true("paste_big", paste(big, T(bx + 14, by), d1, 0, false, 0), "paste big " + GSError.GetLastErrorString());
			local sid_big2 = GSMetro.GetUndergroundStationID(T(bx + 14, by), d1);
			report.check_true("big_new_station", GSStation.IsValidStation(sid_big2) && sid_big2 != sid_big, "The copy of the big station is a station of its own.");
			report.check_eq("big_one_station", sid_big2, GSMetro.GetUndergroundStationID(T(bx + 25, by + 11), d1), "All its parts make one station.");
			report.check_eq("big_platforms", 144, GSMetro.GetStationComplexPlatformCount(sid_big2), "All its platforms are pasted.");

			/* A one-way signal keeps its direction through a turn: north-east becomes north-west. */
			local sx = x0 + 30, sy = y0 + 26;
			report.check_true("oneway_track", GSMetro.BuildUndergroundRail(T(sx, sy), track, d1), "track " + GSError.GetLastErrorString());
			report.check_true("oneway_signal", GSMetro.BuildUndergroundSignal(T(sx, sy), track, d1, GSRail.SIGNALTYPE_PBS_ONEWAY), "signal " + GSError.GetLastErrorString());
			local one = GSMetro.CopyBlueprint(T(sx, sy), T(sx, sy), d1, d1, false);
			report.check_true("paste_one_turned", paste(one, T(sx + 4, sy), d1, 1, false, 0), "paste one " + GSError.GetLastErrorString());
			local one_turned = GSMetro.CopyBlueprint(T(sx + 4, sy), T(sx + 4, sy), d1, d1, false);
			local turn = { ne = "nw", nw = "sw", sw = "se", se = "ne" };
			local towards = function(text) {
				local at = text.find("\"towards\":[\"");
				return at == null ? null : text.slice(at + 12, at + 14);
			};
			local before_dir = towards(one), after_dir = towards(one_turned);
			report.metric("oneway_before", before_dir);
			report.metric("oneway_after", after_dir);
			report.check_true("oneway_turned", before_dir != null && after_dir == turn[before_dir], "The one-way signal turns with the blueprint.");
			report.check_true("oneway_track_turned", one_turned.find("\"tracks\":[\"y\"]") != null, "Its track turns from x to y.");

			/* Every turn, mirrored or not: mirroring swaps north-east and south-west first. */
			local mirror = { ne = "sw", sw = "ne", nw = "nw", se = "se" };
			for (local t = 0; t < 8; t++) {
				local rotation = t % 4, mirrored = t >= 4;
				local expected = mirrored ? mirror[before_dir] : before_dir;
				for (local r = 0; r < rotation; r++) expected = turn[expected];
				local at = T(sx + 6, sy + 2 + t);
				report.check_true("oneway_paste_" + t, paste(one, at, d1, rotation, mirrored, 0), "paste " + t + " " + GSError.GetLastErrorString());
				local got = GSMetro.CopyBlueprint(at, at, d1, d1, false);
				report.check_eq("oneway_dir_" + t, expected, got == null ? null : towards(got), "rotation " + rotation + (mirrored ? " mirrored" : ""));
			}

			/* What the test of a paste must see coming, so nothing is built half. */
			local ry = y0 + 37;
			local json = function(w, levels, items) {
				return "{\"openttd_metro_blueprint\":1,\"name\":\"t\",\"size\":[" + w + ",1],\"stations\":1,\"railtypes\":[\"RAIL\"],\"items\":[" + items + "]}";
			};
			local platform = function(px, pz) { return "{\"kind\":\"platform\",\"pos\":[" + px + ",0," + pz + "],\"rail\":0,\"axis\":\"x\",\"station\":0}"; };

			/* A signalled track across own track: the signal could not stand on the crossing. */
			local cx = x0 + 30;
			report.check_true("cross_y", GSMetro.BuildUndergroundRail(T(cx, ry), GSRail.RAILTRACK_NW_SE, d1), "cross " + GSError.GetLastErrorString());
			report.check_true("cross_x_src", GSMetro.BuildUndergroundRail(T(cx + 3, ry), track, d1) && GSMetro.BuildUndergroundSignal(T(cx + 3, ry), track, d1, GSRail.SIGNALTYPE_NORMAL), "cross source");
			local signalled = GSMetro.CopyBlueprint(T(cx + 3, ry), T(cx + 3, ry), d1, d1, false);
			money_before = GSCompany.GetBankBalance(company);
			report.check_true("cross_fails", !paste(signalled, T(cx, ry), d1, 0, false, 0), "A signal cannot go on a crossing.");
			report.check_true("cross_fails_best_effort", !paste(signalled, T(cx, ry), d1, 0, false, GSMetro.BLUEPRINT_BEST_EFFORT), "Nor when building what fits.");
			report.check_eq("cross_untouched", 2, GSMetro.GetUndergroundTrackBits(T(cx, ry), d1), "The crossing track is not built half.");
			report.check_eq("cross_free", money_before, GSCompany.GetBankBalance(company), "Nothing is charged.");

			/* A station wider than the station spread. */
			local spread = GSGameSettings.GetValue("station.station_spread");
			local items = [];
			for (local i = 0; i <= spread; i++) items.append(platform(i, 0));
			local wide_items = "";
			foreach (i, it in items) wide_items += (i > 0 ? "," : "") + it;
			local wide = json(spread + 1, 1, wide_items);
			report.check_true("wide_parses", GSMetro.GetBlueprintPasteChunkCount(wide, 0, false, 0) > 0, "The wide blueprint is valid.");
			report.check_true("wide_fails", !paste(wide, T(x0 + 2, ry), d1, 0, false, 0), "A station wider than the spread is refused.");
			report.check_eq("wide_nothing", GSMetro.TK_NONE, kind(x0 + 2, ry, d1), "Nothing of it is built.");

			/* An exit further from the platforms than allowed. */
			local reach = GSGameSettings.GetValue("construction.max_station_exit_distance");
			local far = json(reach + 2, 1, platform(0, 0) + ",{\"kind\":\"exit\",\"pos\":[" + (reach + 1) + ",0],\"type\":\"pavilion\",\"station\":0}");
			report.check_true("far_exit_fails", !paste(far, T(x0 + 2, ry + 1), d1, 0, false, 0), "An exit too far from the platforms is refused.");
			report.check_eq("far_exit_nothing", GSMetro.TK_NONE, kind(x0 + 2, ry + 1, d1), "Its platform is not built either.");

			/* A station on two levels needs distant joining. */
			local two = json(1, 2, platform(0, 0) + "," + platform(0, -1));
			report.check_true("distant_off", GSGameSettings.SetValue("station.distant_join_stations", 0), "setting");
			report.check_true("two_levels_fails", !paste(two, T(x0 + 2, ry + 2), d1, 0, false, 0), "Without distant joining a station on two levels is refused.");
			report.check_eq("two_levels_nothing", GSMetro.TK_NONE, kind(x0 + 2, ry + 2, d1), "Nothing of it is built.");
			GSGameSettings.SetValue("station.distant_join_stations", 1);
			report.check_true("two_levels_ok", paste(two, T(x0 + 2, ry + 2), d1, 0, false, 0), "With distant joining it is pasted " + GSError.GetLastErrorString());
			report.check_eq("two_levels_one_station", GSMetro.GetUndergroundStationID(T(x0 + 2, ry + 2), d1), GSMetro.GetUndergroundStationID(T(x0 + 2, ry + 2), d2), "Both levels are one station.");

			/* Trains on both copies. */
			local run_train = function(depot, sid_a, sid_b, label) : (trains, engine, report) {
				local v = trains.build_train(depot, engine);
				report.check_true(label + "_train", v != GSVehicle.VEHICLE_INVALID, label + " train");
				trains.append_underground_order(v, sid_a, GSOrder.OF_NON_STOP_INTERMEDIATE);
				trains.append_underground_order(v, sid_b, GSOrder.OF_NON_STOP_INTERMEDIATE);
				GSVehicle.StartStopVehicle(v);
				return v;
			};
			local v1 = run_train(T(x + 4, py + 2), sid_m1, sid_t1, "plain");
			local v2 = run_train(depot2, sid_m2, sid_t2, "turned");
			local arrivals = [0, 0], prev = [-1, -1], vs = [v1, v2], crashed = false;
			for (local t = 0; t < 16000 && (arrivals[0] < 2 || arrivals[1] < 2); t += 10) {
				foreach (i, v in vs) {
					if (!GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED) { crashed = true; continue; }
					local o = GSOrder.ResolveOrderPosition(v, GSOrder.ORDER_CURRENT);
					if (prev[i] == 1 && o == 0) arrivals[i]++;
					prev[i] = o;
				}
				if (crashed) break;
				GSController.Sleep(10);
			}
			report.metric("plain_arrivals", arrivals[0]);
			report.metric("turned_arrivals", arrivals[1]);
			report.check_true("not_crashed", !crashed, "No train crashes.");
			report.check_true("plain_runs", arrivals[0] >= 2, "A train runs between the stations of the plain copy.");
			report.check_true("turned_runs", arrivals[1] >= 2, "A train runs between the stations of the turned and mirrored copy.");

			/* Pasting over a copy a train runs on still changes nothing. */
			money_before = GSCompany.GetBankBalance(company);
			report.check_true("paste_over_running", paste(bp, T(x, y + 7), d1, 0, false, 0), "paste over running " + GSError.GetLastErrorString());
			report.check_eq("paste_over_running_free", money_before, GSCompany.GetBankBalance(company), "Pasting over a copy in use costs nothing.");
			cm = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
