/*
 * Generator of fixture.sav for metro_house_legacy_tunnel (not run by the suite).
 *
 * It builds what a game of metro version 1 could have: tall houses standing
 * over a tunnel one level below the surface. Version 2 refuses that both
 * ways, so the fixture was made with a build whose house rule was switched
 * off (CheckUndergroundClearance and HasUndergroundWithinDepth ignoring
 * houses): run this file as the main.nut of the scenario with SEED 510201
 * and copy scenario/save/metro_house_legacy_tunnel_fixture.sav to fixture.sav.
 * The savegame format is the same with and without the rule.
 *
 *   depot (x+4, y+2)
 *   surface x+2..x+6 -- P(x+7) -- tunnel x+8..x+12 -- [U](x+13..x+14), level h-1
 *   tall houses over x+8, x+10, x+12; a medium house over x+9
 */
class MetroHouseLegacyTunnel extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local report = MetroTest("metro_house_legacy_tunnel", null, 510201);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local world = MetroTest.MetroWorldLib(build);
		local trains = MetroTest.MetroTrainLib();
		try {
			local area = build.find_buildable_rectangle(64, 24);
			local company = companies.ensure_company();
			local T = function(ax, ay) : (build) { return build.make_tile(ax, ay); };
			report.check_true("levelled", build.level_area_min_height(area, 20, 14, 2), "flat " + GSError.GetLastErrorString());
			local x = build.tile_x(area), y = build.tile_y(area) + 3;
			local d = GSTile.GetMinHeight(T(x + 8, y)) - 1;
			local town = world.create_town(T(x + 48, y + 8), "Altstadt", 0);
			report.check_true("town", town != null, "town");

			local sid = null, v = null;
			{
				local cm = GSCompanyMode(company);
				local depot = T(x + 4, y + 2), front = T(x + 4, y + 1), junction = T(x + 4, y);
				report.check_true("surface", build.build_surface_x(x + 2, x + 6, y) && GSRail.BuildRail(T(x + 3, y), junction, front) && GSRail.BuildRailTrack(junction, GSRail.RAILTRACK_SW_SE), "surface");
				report.check_true("depot", GSRail.BuildRail(depot, front, junction) && GSRail.BuildRailDepot(depot, front), "depot");
				report.check_true("portal", GSMetro.BuildPortal(T(x + 7, y), GSMetro.DIAGDIR_SW, d), "portal " + GSError.GetLastErrorString());
				report.check_true("ug", metro.build_underground_x(x + 8, x + 12, y, d), "tunnel");
				report.check_true("platform", GSMetro.BuildUndergroundStation(T(x + 13, y), GSRail.RAILTRACK_NE_SW, d, GSStation.STATION_NEW), "platform");
				sid = GSMetro.GetUndergroundStationID(T(x + 13, y), d);
				report.check_true("platform2", GSMetro.BuildUndergroundStation(T(x + 14, y), GSRail.RAILTRACK_NE_SW, d, sid), "platform 2");
				v = trains.build_train(depot, trains.find_rail_engine());
				report.check_true("train", GSVehicle.IsValidVehicle(v), "train");
				trains.append_underground_order(v, sid, GSOrder.OF_NON_STOP_INTERMEDIATE);
				cm = null;
			}

			GSGameSettings.SetValue("economy.place_houses", 1);
			local tall = [], medium = [];
			for (local h = 0; h < GSMetro.GetHouseTypeCount(); h++) {
				local f = GSMetro.GetHouseTypeFoundationDepth(h);
				if (f == 2) tall.append(h);
				if (f == 1) medium.append(h);
			}
			local place = function(tile, types) {
				foreach (h in types) if (GSMetro.PlaceHouse(tile, h)) return true;
				return false;
			};
			report.check_true("houses", place(T(x + 9, y), medium) && place(T(x + 8, y), tall) && place(T(x + 10, y), tall) && place(T(x + 12, y), tall), "houses over the tunnel " + GSError.GetLastErrorString());
			report.check_true("saved", GSTest.SaveGame("metro_house_legacy_tunnel_fixture.sav"), "fixture saved");
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
