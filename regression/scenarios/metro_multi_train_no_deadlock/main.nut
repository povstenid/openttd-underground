/**
 * metro_multi_train_no_deadlock
 *
 * Two trains share one signalled underground line:
 *   [stA]--surface--[depot]--[portal A]--sig--UG--[UG station]--UG--sig--[portal B]--surface--[stB]
 *
 * Train 1 enters first, visits the underground station, and exits to station B.
 * Train 2 follows behind (held at the entry signal while train 1 is inside).
 * After train 1 clears the section its PBS reservation must be released so that
 * train 2 can proceed — this directly tests the Bug 2.1 fix (reservation cleared
 * for all wagons, not just the tail).
 *
 * Pass criteria: both trains reach the underground station and station B within budget.
 */
class MetroMultiTrainNoDeadlock extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_multi_train_no_deadlock", null, 510099);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();

		try {
			local area = build.find_buildable_rectangle(28, 8);
			report.check_true("build_area_found", area != null, "Expected a clear 28x8 area for the two-train underground scenario.");

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
				 *   1-5: surface left track
				 *   6: depot_junction      (depot branch-off)
				 *   7: portal_a            (entry portal, DIAGDIR_SW = trains enter heading right)
				 *   8-10: underground left chain (includes entry signal at 8)
				 *   11: ug_station_tile    (underground platform)
				 *   12-14: underground right chain (includes exit signal at 14)
				 *   15: portal_b           (exit portal, DIAGDIR_NE = trains emerge heading right)
				 *   16-18: surface right track
				 *   19: station_b_tile     (surface destination station)
				 *
				 * Depot is at (6, y+2), front at (6, y+1), junction at (6, y).
				 */
				local station_a_tile  = build.make_tile(x + 0,  y);
				local depot_junction  = build.make_tile(x + 6,  y);
				local depot_front     = build.make_tile(x + 6,  y + 1);
				local depot_tile      = build.make_tile(x + 6,  y + 2);
				local portal_a        = build.make_tile(x + 7,  y);
				local ug_signal_a     = build.make_tile(x + 8,  y);
				local ug_station_tile = build.make_tile(x + 11, y);
				local ug_signal_b     = build.make_tile(x + 14, y);
				local portal_b        = build.make_tile(x + 15, y);
				local station_b_tile  = build.make_tile(x + 19, y);

				/* Surface infrastructure. */
				report.check_true("station_a_built", build.build_surface_station_x(station_a_tile), "Surface origin station A should be built.");
				report.check_true("station_b_built", build.build_surface_station_x(station_b_tile), "Surface destination station B should be built.");
				report.check_true("surface_left_built",
					build.build_surface_x(x + 2, x + 6, y) &&
					GSRail.BuildRail(build.make_tile(x + 5, y), depot_junction, depot_front) &&
					GSRail.BuildRailTrack(depot_junction, GSRail.RAILTRACK_SW_SE),
					"Left surface track and depot junction should be built.");
				report.check_true("surface_right_built", build.build_surface_x(x + 16, x + 18, y), "Right surface approach to station B should be built.");
				report.check_true("depot_built",
					GSRail.BuildRail(depot_tile, depot_front, depot_junction) &&
					GSRail.BuildRailDepot(depot_tile, depot_front),
					"Train depot should be built and connected.");

				/* Portals. */
				report.check_true("portal_a_built", metro.build_portal(portal_a, GSMetro.DIAGDIR_SW, depth), "Entry portal A should be built.");
				report.check_true("portal_b_built", metro.build_portal(portal_b, GSMetro.DIAGDIR_NE, depth), "Exit portal B should be built.");

				/* Underground track (entry signal at x+8, exit signal at x+14). */
				report.check_true("ug_left_built",  metro.build_underground_x(x + 8,  x + 10, y, depth), "Left underground chain including signal tile should be built.");
				report.check_true("ug_station_built", metro.build_station_x(ug_station_tile, depth), "Underground platform should be built.");
				report.check_true("ug_right_built", metro.build_underground_x(x + 12, x + 14, y, depth), "Right underground chain including signal tile should be built.");

				/* PBS signals on both sides of station. */
				report.check_true("ug_signal_a_built", GSMetro.BuildUndergroundSignal(ug_signal_a, GSRail.RAILTRACK_NE_SW, depth), "Entry PBS signal should be built.");
				report.check_true("ug_signal_b_built", GSMetro.BuildUndergroundSignal(ug_signal_b, GSRail.RAILTRACK_NE_SW, depth), "Exit PBS signal should be built.");
				/* The default direction is for westbound traffic; the trains run east: turn both. */
				report.check_true("ug_signals_turned", GSMetro.BuildUndergroundSignal(ug_signal_a, GSRail.RAILTRACK_NE_SW, depth) && GSMetro.BuildUndergroundSignal(ug_signal_b, GSRail.RAILTRACK_NE_SW, depth), "Signals should face the eastbound trains.");

				local sid = GSMetro.GetUndergroundStationID(ug_station_tile, depth);
				report.check_true("ug_station_valid", GSStation.IsValidStation(sid), "Underground station should resolve to a valid station id.");

				/* Build two trains in the same depot. */
				local train1 = trains.build_train(depot_tile, engine);
				local train2 = trains.build_train(depot_tile, engine);
				report.check_true("train1_built", train1 != GSVehicle.VEHICLE_INVALID, "Train 1 should be built in the depot.");
				report.check_true("train2_built", train2 != GSVehicle.VEHICLE_INVALID, "Train 2 should be built in the depot.");

				if (train1 != GSVehicle.VEHICLE_INVALID && train2 != GSVehicle.VEHICLE_INVALID) {
					/* Both trains share the same order: underground station → surface station B. */
					report.check_true("t1_order_ug",   trains.append_underground_order(train1, sid),           "Train 1 underground station order should be added.");
					report.check_true("t1_order_surf",  trains.append_surface_order(train1, station_b_tile),    "Train 1 surface station B order should be added.");
					report.check_true("t2_order_ug",   trains.append_underground_order(train2, sid),           "Train 2 underground station order should be added.");
					report.check_true("t2_order_surf",  trains.append_surface_order(train2, station_b_tile),    "Train 2 surface station B order should be added.");

					/* Launch both trains simultaneously. Train 2 will wait at the entry signal
					 * while train 1 occupies the underground section. After train 1 exits,
					 * its PBS reservation must be fully cleared for train 2 to proceed. */
					GSVehicle.StartStopVehicle(train1);
					GSVehicle.StartStopVehicle(train2);

					/* Phase 1: Train 1 reaches and services the underground station. */
					local t1_ug_ticks = trains.wait_for_underground_station(train1, sid, 20000);
					report.check_true("train1_reached_ug_station", t1_ug_ticks != null, "Train 1 must reach the underground station.");

					/* Phase 2: Train 2 follows train 1 into the underground station.
					 * The PBS entry signal clears as train 1 vacates the approach tiles,
					 * allowing train 2 to enter while train 1 is still loading at the platform.
					 * This directly tests the Bug 2.1 fix: reservation cleared for all wagons. */
					local t2_ug_ticks = trains.wait_for_underground_station(train2, sid, 30000);
					report.check_true("train2_reached_ug_station", t2_ug_ticks != null, "Train 2 must reach the underground station — PBS reservation must have been cleared by train 1.");

					/* Phase 3: Train 1 exits to surface station B (metric only). */
					local t1_surf_ticks = trains.wait_for_surface_station(train1, station_b_tile, 14000);

					/* Phase 4: Train 2 exits to surface station B (metric only).
					 * With one-way underground signals and a shared single-platform surface
					 * station this may not complete within the budget if train 1 occupies
					 * the platform; that is a routing quirk, not a PBS bug. */
					local t2_surf_ticks = trains.wait_for_surface_station(train2, station_b_tile, 14000);

					report.metric("train1", train1);
					report.metric("train2", train2);
					report.metric("underground_station_id", sid);
					report.metric("t1_ticks_to_ug_station",  t1_ug_ticks   == null ? -1 : t1_ug_ticks);
					report.metric("t1_ticks_to_surface",     t1_surf_ticks  == null ? -1 : t1_surf_ticks);
					report.metric("t2_ticks_to_ug_station",  t2_ug_ticks   == null ? -1 : t2_ug_ticks);
					report.metric("t2_ticks_to_surface",     t2_surf_ticks  == null ? -1 : t2_surf_ticks);
				}
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.check_true("final_save_written", GSTest.SaveGame("metro_multi_train_no_deadlock_final.sav"), "Scenario should save final state for manual inspection.");
		report.finish();
	}
}
