/**
 * metro_loop_two_underground_stations
 *
 * Closed clockwise loop, half surface / half underground. In the tunnel the
 * line FORKS into two parallel underground tracks, each carrying its own
 * station (A on the main row, B on the south row), then merges back before
 * the exit portal. Train 1 serves the surface station + station A, train 2
 * serves the surface station + station B; each must complete two rounds and
 * must never call at the other train's station.
 *
 * Underground layout at depth -1 (westbound traffic P1 -> P2):
 *
 *   P2 == J2 ==== [st A] ==== J1 == P1        (row yu, main track)
 *          \\                  //
 *           B2 == [st B] === B1               (row yb = yu+1, second track)
 */
class MetroLoopTwoUndergroundStations extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_loop_two_underground_stations", null, 510888);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();

		try {
			local area = build.find_buildable_rectangle(22, 11);
			report.check_true("build_area_found", area != null, "Expected a clear 22x11 area for the loop.");

			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);
			local engine = company_mode == null ? null : trains.find_rail_engine();
			report.check_true("rail_engine_found", engine != null, "Expected at least one buildable rail engine.");

			if (area != null && engine != null && company != GSCompany.COMPANY_INVALID) {
				local x0 = build.tile_x(area) + 1;
				local ys = build.tile_y(area) + 2;
				local yu = ys + 2;
				local yb = yu + 1;
				local xe = x0 + 15;
				local depth = -1;

				local T = function (tx, ty) { return GSMap.GetTileIndex(tx, ty); };

				/* --- Surface: north row, depot, drive-through station --- */
				local st_surf_tile = T(x0 + 6, ys);
				report.check_true("surface_line_built", build.build_surface_x(x0 + 1, xe - 1, ys), "North surface line should be built.");
				report.check_true("surf_station_built", GSRail.BuildRailStation(st_surf_tile, GSRail.RAILTRACK_NE_SW, 1, 2, GSStation.STATION_NEW), "Drive-through surface station should be built.");

				local depot_tile = T(x0 + 2, ys - 1);
				report.check_true("depot_built",
					GSRail.BuildRail(T(x0 + 1, ys), T(x0 + 2, ys), depot_tile) &&
					GSRail.BuildRail(depot_tile, T(x0 + 2, ys), T(x0 + 3, ys)) &&
					GSRail.BuildRailDepot(depot_tile, T(x0 + 2, ys)),
					"Depot north of the line should be built and connected.");

				report.check_true("east_side_built",
					GSRail.BuildRail(T(xe - 1, ys), T(xe, ys), T(xe, ys + 1)) &&
					GSRail.BuildRail(T(xe, ys), T(xe, ys + 1), T(xe, yu)) &&
					GSRail.BuildRail(T(xe, ys + 1), T(xe, yu), T(xe - 1, yu)),
					"East surface curve should be built.");

				report.check_true("west_side_built",
					GSRail.BuildRail(T(x0 + 1, yu), T(x0, yu), T(x0, ys + 1)) &&
					GSRail.BuildRail(T(x0, yu), T(x0, ys + 1), T(x0, ys)) &&
					GSRail.BuildRail(T(x0, ys + 1), T(x0, ys), T(x0 + 1, ys)),
					"West surface curve should be built.");

				/* One-way PBS signals enforcing the clockwise direction.
				 * BuildSignal(tile, front): the signal FACES front and admits
				 * trains travelling front->tile, so front = west neighbour. */
				report.check_true("surf_signal_1", GSRail.BuildSignal(T(x0 + 9, ys), T(x0 + 8, ys), GSRail.SIGNALTYPE_PBS_ONEWAY), "Surface signal east of the station should be built.");
				report.check_true("surf_signal_2", GSRail.BuildSignal(T(x0 + 4, ys), T(x0 + 3, ys), GSRail.SIGNALTYPE_PBS_ONEWAY), "Surface signal west of the station should be built.");

				/* --- Portals --- */
				report.check_true("portal_e_built", metro.build_portal(T(xe - 1, yu), GSMetro.DIAGDIR_NE, depth), "East portal (underground entry) should be built.");
				report.check_true("portal_w_built", metro.build_portal(T(x0 + 1, yu), GSMetro.DIAGDIR_SW, depth), "West portal (underground exit) should be built.");

				/* --- Underground: main track + fork to a second parallel track --- */
				report.check_true("ug_main_built", metro.build_underground_x(x0 + 2, xe - 2, yu, depth), "Main underground track should be built.");

				local j1 = T(x0 + 11, yu); /* fork:  X + LOWER (SW+SE)  */
				local b1 = T(x0 + 11, yb); /* turn:  UPPER (NW+NE)      */
				local b2 = T(x0 + 4, yb);  /* turn:  LEFT (NW+SW)       */
				local j2 = T(x0 + 4, yu);  /* merge: X + RIGHT (NE+SE)  */
				report.check_true("ug_fork_built", GSMetro.BuildUndergroundRail(j1, GSRail.RAILTRACK_SW_SE, depth), "Fork corner piece at J1 should be built.");
				report.check_true("ug_b1_built", GSMetro.BuildUndergroundRail(b1, GSRail.RAILTRACK_NW_NE, depth), "Second-track corner B1 should be built.");
				report.check_true("ug_second_built", metro.build_underground_x(x0 + 5, x0 + 10, yb, depth), "Second underground track should be built.");
				report.check_true("ug_b2_built", GSMetro.BuildUndergroundRail(b2, GSRail.RAILTRACK_NW_SW, depth), "Second-track corner B2 should be built.");
				report.check_true("ug_merge_built", GSMetro.BuildUndergroundRail(j2, GSRail.RAILTRACK_NE_SE, depth), "Merge corner piece at J2 should be built.");

				/* --- Stations on DIFFERENT underground tracks --- */
				local st_a_tile = T(x0 + 7, yu);
				local st_b_tile = T(x0 + 7, yb);
				report.check_true("ug_station_a_built", metro.build_station_x(st_a_tile, depth), "Underground station A (main track) should be built.");
				report.check_true("ug_station_b_built", metro.build_station_x(st_b_tile, depth), "Underground station B (second track) should be built.");

				local sid_a = GSMetro.GetUndergroundStationID(st_a_tile, depth);
				local sid_b = GSMetro.GetUndergroundStationID(st_b_tile, depth);
				report.check_true("ug_station_a_valid", GSStation.IsValidStation(sid_a), "Underground station A id should be valid.");
				report.check_true("ug_station_b_valid", GSStation.IsValidStation(sid_b), "Underground station B id should be valid.");
				report.check_true("ug_station_ids_differ", sid_a != sid_b, "Stations A and B must be distinct stations.");

				/* --- Underground signals: entry, one per track, exit --- */
				report.check_true("ug_signal_entry", GSMetro.BuildUndergroundSignal(T(x0 + 13, yu), GSRail.RAILTRACK_NE_SW, depth), "Underground signal before the fork should be built.");
				report.check_true("ug_signal_a", GSMetro.BuildUndergroundSignal(T(x0 + 9, yu), GSRail.RAILTRACK_NE_SW, depth), "Underground signal on track A should be built.");
				report.check_true("ug_signal_b", GSMetro.BuildUndergroundSignal(T(x0 + 9, yb), GSRail.RAILTRACK_NE_SW, depth), "Underground signal on track B should be built.");
				report.check_true("ug_signal_exit", GSMetro.BuildUndergroundSignal(T(x0 + 2, yu), GSRail.RAILTRACK_NE_SW, depth), "Underground signal after the merge should be built.");

				/* --- Two trains with wagons, each bound to its own UG station --- */
				local wagon_engine = trains.find_rail_wagon();
				local t1 = trains.build_train(depot_tile, engine);
				local t2 = trains.build_train(depot_tile, engine);
				report.check_true("trains_built", t1 != GSVehicle.VEHICLE_INVALID && t2 != GSVehicle.VEHICLE_INVALID, "Both trains should be built.");

				local wagons_attached = 0;
				if (wagon_engine != null) {
					if (t1 != GSVehicle.VEHICLE_INVALID && trains.attach_wagon(t1, depot_tile, wagon_engine).attached) wagons_attached++;
					if (t2 != GSVehicle.VEHICLE_INVALID && trains.attach_wagon(t2, depot_tile, wagon_engine).attached) wagons_attached++;
				}
				report.check_eq("wagons_attached", 2, wagons_attached, "Each train should have one wagon.");

				if (t1 != GSVehicle.VEHICLE_INVALID && t2 != GSVehicle.VEHICLE_INVALID) {
					/* Non-stop underground orders: each train calls only at its own station. */
					report.check_true("orders_t1", trains.append_surface_order(t1, st_surf_tile) && trains.append_underground_order(t1, sid_a, GSOrder.OF_NON_STOP_INTERMEDIATE), "Train 1 orders (surface + A) should be added.");
					report.check_true("orders_t2", trains.append_surface_order(t2, st_surf_tile) && trains.append_underground_order(t2, sid_b, GSOrder.OF_NON_STOP_INTERMEDIATE), "Train 2 orders (surface + B) should be added.");

					GSVehicle.StartStopVehicle(t1);
					local dep1 = trains.wait_for_departure(t1, 6000);
					report.check_true("t1_departed", dep1 != null, "Train 1 must leave the depot.");
					GSVehicle.StartStopVehicle(t2);

					/* --- Round counting + wrong-station watch --- */
					local stats = {};
					stats[t1] <- { surf = 0, own = 0, wrong = 0, was_surf = false, was_ug = false, own_sid = sid_a };
					stats[t2] <- { surf = 0, own = 0, wrong = 0, was_surf = false, was_ug = false, own_sid = sid_b };

					local sid_surf = GSStation.GetStationID(st_surf_tile);
					local budget = 70000;
					local elapsed = 0;
					while (elapsed < budget) {
						local sample = (elapsed < 12000) ? 500 : 5000;
						if (elapsed % sample == 0) {
							foreach (t, s in stats) {
								local loc = GSVehicle.GetLocation(t);
								GSLog.Info("tick+" + elapsed + " t" + t +
									" pos=(" + GSMap.GetTileX(loc) + "," + GSMap.GetTileY(loc) + ")" +
									" state=" + GSVehicle.GetState(t) +
									" speed=" + GSVehicle.GetCurrentSpeed(t) +
									" ug=" + (GSMetro.IsVehicleUnderground(t) ? 1 : 0) +
									" stops=" + s.surf + "/" + s.own + "/" + s.wrong);
							}
						}
						local done = true;
						foreach (t, s in stats) {
							local at_station = GSVehicle.GetState(t) == GSVehicle.VS_AT_STATION;
							local under = GSMetro.IsVehicleUnderground(t);
							local at_surf = at_station && !under && GSStation.GetStationID(GSVehicle.GetLocation(t)) == sid_surf;
							local at_ug = at_station && under;
							if (at_surf && !s.was_surf) s.surf++;
							if (at_ug && !s.was_ug) {
								local here = GSMetro.GetUndergroundStationID(GSMetro.GetVehicleUndergroundTile(t), GSMetro.GetVehicleUndergroundDepth(t));
								if (here == s.own_sid) {
									s.own++;
								} else {
									s.wrong++;
									GSLog.Info("WRONG STOP: t" + t + " at UG station " + here + " (own " + s.own_sid + ")");
								}
							}
							s.was_surf = at_surf;
							s.was_ug = at_ug;
							if (s.surf < 2 || s.own < 2) done = false;
						}
						if (done) break;
						GSController.Sleep(5);
						elapsed += 5;
					}

					report.check_true("t1_two_rounds", stats[t1].surf >= 2 && stats[t1].own >= 2, "Train 1 must serve the surface station and station A twice each.");
					report.check_true("t2_two_rounds", stats[t2].surf >= 2 && stats[t2].own >= 2, "Train 2 must serve the surface station and station B twice each.");
					report.check_eq("t1_no_wrong_stops", 0, stats[t1].wrong, "Train 1 must never stop at station B.");
					report.check_eq("t2_no_wrong_stops", 0, stats[t2].wrong, "Train 2 must never stop at station A.");

					report.metric("t1_stops_surf", stats[t1].surf);
					report.metric("t1_stops_own", stats[t1].own);
					report.metric("t2_stops_surf", stats[t2].surf);
					report.metric("t2_stops_own", stats[t2].own);
					report.metric("ticks_used", elapsed);
				}
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.check_true("final_save_written", GSTest.SaveGame("metro_loop_two_underground_stations_final.sav"), "Scenario should save final state for manual inspection.");
		report.finish();
	}
}
