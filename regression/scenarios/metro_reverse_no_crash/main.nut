/**
 * metro_reverse_no_crash
 *
 * One train on a dead-ended underground line:
 *   [stA]--surface--[depot]--[portal A]--UG--[UG station]--UG--(dead end)
 *
 * Phase 1: the train enters the portal and services the underground station.
 * Phase 2: its next order (surface station A) lies BEHIND it; driving forward
 *          it hits the dead end, must reverse in place underground, drive back,
 *          exit through the entry portal and reach station A on the surface.
 * Phase 3: on the second trip underground the script issues an explicit
 *          ReverseVehicle while the train is below ground, forcing the
 *          underground branch of ReverseTrainDirection; the train must still
 *          reach the underground station afterwards.
 *
 * Pass criteria: all phases complete within budget and the game never crashes.
 */
class MetroReverseNoCrash extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_reverse_no_crash", null, 510123);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();

		try {
			local area = build.find_buildable_rectangle(20, 8);
			report.check_true("build_area_found", area != null, "Expected a clear 20x8 area for the reversal scenario.");

			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);
			local engine = company_mode == null ? null : trains.find_rail_engine();
			report.check_true("rail_engine_found", engine != null, "Expected at least one buildable rail engine.");

			if (area != null && engine != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area);
				local y = build.tile_y(area) + 3;
				local depth = -1;

				/* Layout (offsets from area origin, all on row y):
				 *   0: station_a_tile      (surface origin station)
				 *   1-5: surface track
				 *   6: depot_junction      (depot branch-off)
				 *   7: portal_a            (entry portal, DIAGDIR_SW = trains enter heading right)
				 *   8-10: underground chain
				 *   11: ug_station_tile    (underground platform)
				 *   12-14: underground chain, DEAD END after 14 (no exit portal)
				 */
				local station_a_tile  = build.make_tile(x + 0,  y);
				local depot_junction  = build.make_tile(x + 6,  y);
				local depot_front     = build.make_tile(x + 6,  y + 1);
				local depot_tile      = build.make_tile(x + 6,  y + 2);
				local portal_a        = build.make_tile(x + 7,  y);
				local ug_station_tile = build.make_tile(x + 11, y);

				/* Surface infrastructure. */
				report.check_true("station_a_built", build.build_surface_station_x(station_a_tile), "Surface origin station A should be built.");
				report.check_true("surface_built",
					build.build_surface_x(x + 2, x + 6, y) &&
					GSRail.BuildRail(build.make_tile(x + 5, y), depot_junction, depot_front) &&
					GSRail.BuildRailTrack(depot_junction, GSRail.RAILTRACK_SW_SE),
					"Surface track and depot junction should be built.");
				report.check_true("depot_built",
					GSRail.BuildRail(depot_tile, depot_front, depot_junction) &&
					GSRail.BuildRailDepot(depot_tile, depot_front),
					"Train depot should be built and connected.");

				/* Portal and dead-ended underground line. */
				report.check_true("portal_a_built", metro.build_portal(portal_a, GSMetro.DIAGDIR_SW, depth), "Entry portal A should be built.");
				report.check_true("ug_left_built",  metro.build_underground_x(x + 8,  x + 10, y, depth), "Underground approach chain should be built.");
				report.check_true("ug_station_built", metro.build_station_x(ug_station_tile, depth), "Underground platform should be built.");
				report.check_true("ug_right_built", metro.build_underground_x(x + 12, x + 14, y, depth), "Dead-ended underground tail should be built.");

				local sid = GSMetro.GetUndergroundStationID(ug_station_tile, depth);
				report.check_true("ug_station_valid", GSStation.IsValidStation(sid), "Underground station should resolve to a valid station id.");

				local train = trains.build_train(depot_tile, engine);
				report.check_true("train_built", train != GSVehicle.VEHICLE_INVALID, "Train should be built in the depot.");

				if (train != GSVehicle.VEHICLE_INVALID) {
					/* Orders: underground station, then surface station A (behind the train). */
					report.check_true("order_ug",   trains.append_underground_order(train, sid),        "Underground station order should be added.");
					report.check_true("order_surf", trains.append_surface_order(train, station_a_tile), "Surface station A order should be added.");

					GSVehicle.StartStopVehicle(train);

					/* Phase 1: enter the portal and service the underground station. */
					local entry = trains.wait_for_underground_entry(train, 10000);
					report.check_true("entered_underground", entry != null, "Train must enter the underground through portal A.");

					local ug_ticks = trains.wait_for_underground_station(train, sid, 15000);
					report.check_true("reached_ug_station", ug_ticks != null, "Train must reach the underground station.");

					/* Phase 2: next order is BEHIND the train. Driving forward it hits the
					 * dead end at x+14, must reverse in place underground, drive back and
					 * exit to the surface through the entry portal. */
					local back_ticks = trains.wait_for_surface_station(train, station_a_tile, 25000);
					report.check_true("returned_after_dead_end_reverse", back_ticks != null, "Train must reverse at the underground dead end and return to surface station A.");

					/* Phase 3: second trip — force an explicit reversal while underground. */
					local entry2 = trains.wait_for_underground_entry(train, 15000);
					report.check_true("entered_underground_again", entry2 != null, "Train must enter the underground a second time.");

					local reversed = false;
					if (entry2 != null) {
						reversed = GSVehicle.ReverseVehicle(train);
						report.check_true("reverse_command_accepted", reversed, "Explicit ReverseVehicle on an underground train should be accepted.");
					}

					/* After the forced reversal the train must still settle on its
					 * underground station order eventually (exit, re-route, re-enter). */
					local ug_ticks2 = trains.wait_for_underground_station(train, sid, 25000);
					report.check_true("reached_ug_station_after_reverse", ug_ticks2 != null, "Train must recover from the forced reversal and reach the underground station again.");

					/* Diagnostics: where did the train end up? */
					local loc = GSVehicle.GetLocation(train);
					report.metric("area_origin_x", build.tile_x(area));
					report.metric("area_origin_y", build.tile_y(area));
					report.metric("final_tile_x", GSMap.GetTileX(loc));
					report.metric("final_tile_y", GSMap.GetTileY(loc));
					report.metric("final_is_underground", GSMetro.IsVehicleUnderground(train) ? 1 : 0);
					report.metric("final_state", GSVehicle.GetState(train));
					report.metric("final_speed", GSVehicle.GetCurrentSpeed(train));

					report.metric("train", train);
					report.metric("underground_station_id", sid);
					report.metric("ticks_to_first_ug_station", ug_ticks == null ? -1 : ug_ticks);
					report.metric("ticks_back_to_station_a",   back_ticks == null ? -1 : back_ticks);
					report.metric("ticks_to_ug_station_after_reverse", ug_ticks2 == null ? -1 : ug_ticks2);
				}
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.check_true("final_save_written", GSTest.SaveGame("metro_reverse_no_crash_final.sav"), "Scenario should save final state for manual inspection.");
		report.finish();
	}
}
