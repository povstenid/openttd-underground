/*
 * metro_lost_train_other_rail
 *
 *   surface -- P(x+7) -- x+8..x+9 -- [m](x+10) -- x+11..x+12 -- [U](x+13)
 *
 * m = one tile of monorail in a railway line (in a player's game: a junction
 * built with another rail type). The train has no power on it, so it finds no
 * way to platform U and is lost. A lost train used to reserve its way on
 * without looking at the rail type, drove onto the tile and was stranded
 * there without power, blocking the line for every train behind it. It must
 * not go onto that tile.
 */
class MetroLostTrainOtherRail extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local report = MetroTest("metro_lost_train_other_rail", null, 510097);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();
		try {
			local area = build.find_buildable_rectangle(24, 8);
			report.check_true("build_area_found", area != null, "area");
			local company = companies.ensure_company();
			local cm = GSCompanyMode(company);
			local rail = null, mono = null;
			foreach (rt, _ in GSRailTypeList()) {
				local name = GSRail.GetName(rt);
				if (name == "Railway construction") rail = rt;
				if (name == "Monorail construction") mono = rt;
			}
			report.check_true("railtypes", rail != null && mono != null, "railway and monorail");
			local engine = null;
			foreach (e, _ in GSEngineList(GSVehicle.VT_RAIL)) {
				if (!GSEngine.IsBuildable(e) || GSEngine.IsWagon(e)) continue;
				if (!GSEngine.HasPowerOnRail(e, rail) || GSEngine.HasPowerOnRail(e, mono)) continue;
				engine = e;
				break;
			}
			report.check_true("engine", engine != null, "railway engine");
			GSRail.SetCurrentRailType(rail);
			local x = build.tile_x(area), y = build.tile_y(area) + 3, d = -1;
			local T = function(ax, ay) : (build) { return build.make_tile(ax, ay); };
			local depot = T(x + 4, y + 2), front = T(x + 4, y + 1), junction = T(x + 4, y);
			report.check_true("surface", build.build_surface_x(x + 2, x + 6, y) && GSRail.BuildRail(T(x + 3, y), junction, front) && GSRail.BuildRailTrack(junction, GSRail.RAILTRACK_SW_SE), "surface");
			report.check_true("depot", GSRail.BuildRail(depot, front, junction) && GSRail.BuildRailDepot(depot, front), "depot");
			report.check_true("portal", metro.build_portal(T(x + 7, y), GSMetro.DIAGDIR_SW, d), "portal");
			report.check_true("ug", metro.build_underground_x(x + 8, x + 9, y, d) && metro.build_underground_x(x + 11, x + 12, y, d), "ug");
			report.check_true("platform", GSMetro.BuildUndergroundStation(T(x + 13, y), GSRail.RAILTRACK_NE_SW, d, GSStation.STATION_NEW), "platform");
			local sid = GSMetro.GetUndergroundStationID(T(x + 13, y), d);
			GSRail.SetCurrentRailType(mono);
			report.check_true("other_rail", GSMetro.BuildUndergroundRail(T(x + 10, y), GSRail.RAILTRACK_NE_SW, d), "monorail tile " + GSError.GetLastErrorString());
			GSRail.SetCurrentRailType(rail);

			local v = trains.build_train(depot, engine);
			report.check_true("train", v != GSVehicle.VEHICLE_INVALID, "train");
			trains.append_underground_order(v, sid, GSOrder.OF_NON_STOP_INTERMEDIATE);
			GSVehicle.StartStopVehicle(v);
			local max_x = -99, crashed = 0, entered = false;
			for (local i = 0; i < 8000; i += 10) {
				if (!GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED) { crashed = 1; break; }
				if (GSMetro.IsVehicleUnderground(v)) {
					entered = true;
					local tx = GSMap.GetTileX(GSMetro.GetVehicleUndergroundTile(v)) - x;
					if (tx > max_x) max_x = tx;
				}
				GSController.Sleep(10);
			}
			report.metric("max_x", max_x);
			report.metric("entered_tunnel", entered ? 1 : 0);
			report.check_eq("no_crash", 0, crashed, "No crash.");
			report.check_true("never_on_other_rail", max_x < 10, "The lost train must not drive onto the monorail tile (max x offset " + max_x + ").");
			cm = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
