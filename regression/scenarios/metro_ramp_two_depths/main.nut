/**
 * metro_ramp_two_depths
 *
 * A train must traverse a ramp between two underground levels:
 *   [stA]--surface--[depot]--[portal A, depth -1]--UG(-1)--[ramp -1→-2]--UG(-2)--[UG station, depth -2]
 *
 * The pathfinder must route through the ramp and the train must change depth
 * while crossing it, stop at the deep station, then come back the same way
 * (ascending the ramp) and exit to the surface.
 *
 * Pass criteria: the train services the deep station and returns to station A.
 */
class MetroRampTwoDepths extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_ramp_two_depths", null, 510321);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();

		try {
			local area = build.find_buildable_rectangle(22, 8);
			report.check_true("build_area_found", area != null, "Expected a clear 22x8 area for the ramp scenario.");

			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);
			local engine = company_mode == null ? null : trains.find_rail_engine();
			report.check_true("rail_engine_found", engine != null, "Expected at least one buildable rail engine.");

			if (area != null && engine != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area);
				local y = build.tile_y(area) + 3;

				/* Layout (offsets from area origin, all on row y):
				 *   0: station_a_tile      (surface origin station)
				 *   1-5: surface track
				 *   6: depot_junction      (depot branch-off)
				 *   7: portal_a            (entry portal at depth -1)
				 *   8-9: underground track at depth -1
				 *   10: ramp               (descends -1 → -2 towards +x)
				 *   11-12: underground track at depth -2
				 *   13: ug_station_tile    (underground platform at depth -2)
				 */
				local station_a_tile  = build.make_tile(x + 0,  y);
				local depot_junction  = build.make_tile(x + 6,  y);
				local depot_front     = build.make_tile(x + 6,  y + 1);
				local depot_tile      = build.make_tile(x + 6,  y + 2);
				local portal_a        = build.make_tile(x + 7,  y);
				local ramp_tile       = build.make_tile(x + 10, y);
				local ug_station_tile = build.make_tile(x + 13, y);

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

				/* Portal, shallow track, ramp, deep track, deep station. */
				report.check_true("portal_a_built", metro.build_portal(portal_a, GSMetro.DIAGDIR_SW, -1), "Entry portal A should be built.");
				report.check_true("ug_shallow_built", metro.build_underground_x(x + 8, x + 9, y, -1), "Underground track at depth -1 should be built.");
				report.check_true("ramp_built", GSMetro.BuildRamp(ramp_tile, GSMetro.DIAGDIR_SW, -1, -2), "Ramp from depth -1 down to -2 should be built.");
				report.check_true("ug_deep_built", metro.build_underground_x(x + 11, x + 12, y, -2), "Underground track at depth -2 should be built.");
				report.check_true("ug_station_built", metro.build_station_x(ug_station_tile, -2), "Underground platform at depth -2 should be built.");

				local sid = GSMetro.GetUndergroundStationID(ug_station_tile, -2);
				report.check_true("ug_station_valid", GSStation.IsValidStation(sid), "Deep underground station should resolve to a valid station id.");

				local train = trains.build_train(depot_tile, engine);
				report.check_true("train_built", train != GSVehicle.VEHICLE_INVALID, "Train should be built in the depot.");

				if (train != GSVehicle.VEHICLE_INVALID) {
					report.check_true("order_ug",   trains.append_underground_order(train, sid),        "Deep underground station order should be added.");
					report.check_true("order_surf", trains.append_surface_order(train, station_a_tile), "Surface station A order should be added.");

					GSVehicle.StartStopVehicle(train);

					/* Phase 1: descend through the ramp to the deep station. */
					local entry = trains.wait_for_underground_entry(train, 10000);
					report.check_true("entered_underground", entry != null, "Train must enter the underground through portal A.");

					local ug_ticks = trains.wait_for_underground_station(train, sid, 20000);
					report.check_true("reached_deep_station", ug_ticks != null, "Train must descend the ramp and reach the station at depth -2.");

					local depth_at_station = GSMetro.GetVehicleUndergroundDepth(train);
					report.check_eq("depth_is_minus_two", -2, depth_at_station, "Train must be at depth -2 at the deep station.");

					/* Phase 2: come back up the ramp and out to the surface. */
					local back_ticks = trains.wait_for_surface_station(train, station_a_tile, 25000);
					report.check_true("returned_to_surface", back_ticks != null, "Train must ascend the ramp, exit the portal and reach surface station A.");

					report.metric("train", train);
					report.metric("underground_station_id", sid);
					report.metric("ticks_to_deep_station", ug_ticks == null ? -1 : ug_ticks);
					report.metric("ticks_back_to_surface", back_ticks == null ? -1 : back_ticks);
					report.metric("depth_at_station", depth_at_station);
				}
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.check_true("final_save_written", GSTest.SaveGame("metro_ramp_two_depths_final.sav"), "Scenario should save final state for manual inspection.");
		report.finish();
	}
}
