/*
 * metro_house_legacy_tunnel
 *
 * A savegame of metro version 1 can have a tunnel where version 2 refuses one:
 * one level below tall houses. Such a tunnel stays and keeps working; only
 * new construction under the houses is refused.
 *
 * The fixture (see fixture_generator.nut):
 *   depot -- surface -- P -- tunnel x+1..x+5 -- [U] (P = the portal, level h-1)
 *   tall houses over x+1, x+3, x+5 and a medium house over x+2
 */
class MetroHouseLegacyTunnel extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);
		local report = MetroTest("metro_house_legacy_tunnel", "fixture.sav", null);
		local trains = MetroTest.MetroTrainLib();
		try {
			local portal = null;
			for (local i = 0; i < GSMap.GetMapSize() && portal == null; i++) {
				if (GSMetro.IsPortalTile(i)) portal = i;
			}
			report.check_true("portal_found", portal != null, "the fixture has a portal");
			local px = GSMap.GetTileX(portal), py = GSMap.GetTileY(portal);
			local T = function(dx) : (px, py) { return GSMap.GetTileIndex(px + dx, py); };
			local d = GSMetro.GetMetroDepth(portal);
			report.metric("level", d);

			/* The tunnel lies inside the foundations: that is what an old game can have. */
			local legacy = 0, tall = 0;
			for (local dx = 1; dx <= 5; dx++) {
				local t = T(dx);
				local f = GSMetro.GetFoundationDepth(t);
				if (f == 2) tall++;
				if (f > 0 && GSTile.GetMinHeight(t) - d <= f && GSMetro.IsUndergroundTile(t, d)) legacy++;
			}
			report.check_eq("tall_houses", 3, tall, "three tall houses over the tunnel");

			/* A game of an earlier metro version keeps every cargo through its station exits;
			 * industry foundations get the default, for new construction only. */
			report.check_eq("legacy_exit_cargo_all", 1, GSGameSettings.GetValue("underground.exit_cargo"), "underground.exit_cargo of an older game");
			report.check_eq("legacy_industry_foundation", 2, GSGameSettings.GetValue("underground.industry_foundation_depth"), "underground.industry_foundation_depth of an older game");
			report.check_eq("tunnel_in_foundations", 4, legacy, "four houses with the tunnel inside their foundation");

			local company = GSCompany.ResolveCompanyID(GSCompany.COMPANY_FIRST);
			local cm = GSCompanyMode(company);
			GSRail.SetCurrentRailType(GSMetro.GetUndergroundRailType(T(1), d));
			/* Laying the track that is there already is no new construction... */
			report.check_true("existing_track_ok", GSMetro.BuildUndergroundRail(T(1), GSRail.RAILTRACK_NE_SW, d), "the old track under the house " + GSError.GetLastErrorString());
			/* ...but more track under the house is. */
			report.check_true("new_track_refused", !GSMetro.BuildUndergroundRail(T(1), GSRail.RAILTRACK_NW_SE, d), "a new track under the house");
			report.check_eq("new_track_error", GSMetro.ERR_UNDERGROUND_HOUSE_FOUNDATION, GSError.GetLastError(), "foundation error (got " + GSError.GetLastErrorString() + ")");

			/* The train still drives under the houses to its platform. */
			local vehicles = GSVehicleList();
			vehicles.Valuate(GSVehicle.GetVehicleType);
			vehicles.KeepValue(GSVehicle.VT_RAIL);
			report.check_eq("train_found", 1, vehicles.Count(), "one train");
			local v = vehicles.Begin();
			local sid = GSMetro.GetUndergroundStationID(T(6), d);
			report.check_true("station_found", GSStation.IsValidStation(sid), "the platform after the tunnel");
			if (GSVehicle.GetState(v) == GSVehicle.VS_IN_DEPOT || GSVehicle.GetState(v) == GSVehicle.VS_STOPPED) GSVehicle.StartStopVehicle(v);
			local ticks = trains.wait_for_underground_station(v, sid, 8000);
			report.metric("ticks_to_platform", ticks == null ? -1 : ticks);
			report.check_true("train_reached_platform", ticks != null, "the train reached the platform through the old tunnel");
			report.check_true("train_ok", GSVehicle.GetState(v) != GSVehicle.VS_CRASHED, "no crash");

			/* Nothing was removed: houses and tunnel are both still there. */
			local still = 0;
			for (local dx = 1; dx <= 5; dx++) if (GSMetro.IsUndergroundTile(T(dx), d)) still++;
			report.check_eq("tunnel_kept", 5, still, "the tunnel is complete");
			report.check_true("houses_kept", GSMetro.GetFoundationDepth(T(1)) == 2 && GSMetro.GetFoundationDepth(T(3)) == 2 && GSMetro.GetFoundationDepth(T(5)) == 2, "the houses stand");
			cm = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}
		report.finish();
	}
}
