/**
 * metro_loop_acceptance
 *
 * Acceptance test requested by the user: a closed loop, half of it on the
 * surface and half underground, with a station and PBS signals on both
 * halves, served by TWO trains (each with a wagon). Every train must
 * complete TWO full circuits.
 *
 * Layout (clockwise traffic, origin = found area, ys = surface row,
 * yu = underground row):
 *
 *   (x0,ys) ─ depot ─ [surface station] ── signals ──→ (xe,ys)
 *      │                                                  │
 *   west side (surface, northbound)            east side (surface, southbound)
 *      │                                                  │
 *   (x0,yu) ←─ P2 ══ UG station ══ signals ══ P1 ──── (xe,yu)
 *               (underground westbound between portals)
 *
 * Pass criteria: each train registers >= 2 arrivals at the surface station
 * AND >= 2 arrivals at the underground station within budget.
 */
class MetroLoopAcceptance extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_loop_acceptance", null, 510555);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();

		try {
			local area = build.find_buildable_rectangle(22, 10);
			report.check_true("build_area_found", area != null, "Expected a clear 22x10 area for the loop.");

			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);
			local engine = company_mode == null ? null : trains.find_rail_engine();
			report.check_true("rail_engine_found", engine != null, "Expected at least one buildable rail engine.");

			if (area != null && engine != null && company != GSCompany.COMPANY_INVALID) {
				local x0 = build.tile_x(area) + 1;
				local ys = build.tile_y(area) + 2;
				local yu = ys + 2;
				local xe = x0 + 15;
				local depth = -1;

				local T = function (tx, ty) { return GSMap.GetTileIndex(tx, ty); };

				/* --- North side: surface line with depot and station --- */
				local st_surf_tile = T(x0 + 6, ys);
				report.check_true("surface_line_built", build.build_surface_x(x0 + 1, xe - 1, ys), "North surface line should be built.");
				report.check_true("surf_station_built", GSRail.BuildRailStation(st_surf_tile, GSRail.RAILTRACK_NE_SW, 1, 2, GSStation.STATION_NEW), "Drive-through surface station should be built.");

				local depot_tile = T(x0 + 2, ys - 1);
				report.check_true("depot_built",
					GSRail.BuildRail(T(x0 + 1, ys), T(x0 + 2, ys), depot_tile) &&
					GSRail.BuildRail(depot_tile, T(x0 + 2, ys), T(x0 + 3, ys)) &&
					GSRail.BuildRailDepot(depot_tile, T(x0 + 2, ys)),
					"Depot north of the line should be built and connected.");

				/* --- East side: surface curve down --- */
				report.check_true("east_side_built",
					GSRail.BuildRail(T(xe - 1, ys), T(xe, ys), T(xe, ys + 1)) &&
					GSRail.BuildRail(T(xe, ys), T(xe, ys + 1), T(xe, yu)) &&
					GSRail.BuildRail(T(xe, ys + 1), T(xe, yu), T(xe - 1, yu)),
					"East surface curve should be built.");

				/* --- West side: surface curve up --- */
				report.check_true("west_side_built",
					GSRail.BuildRail(T(x0 + 1, yu), T(x0, yu), T(x0, ys + 1)) &&
					GSRail.BuildRail(T(x0, yu), T(x0, ys + 1), T(x0, ys)) &&
					GSRail.BuildRail(T(x0, ys + 1), T(x0, ys), T(x0 + 1, ys)),
					"West surface curve should be built.");

				/* --- South side: portals and the underground half --- */
				/* P1 (east): trains enter here travelling west (towards -x). */
				report.check_true("portal_e_built", metro.build_portal(T(xe - 1, yu), GSMetro.DIAGDIR_NE, depth), "East portal (underground entry) should be built.");
				/* P2 (west): underground trains pop out here continuing west. */
				report.check_true("portal_w_built", metro.build_portal(T(x0 + 1, yu), GSMetro.DIAGDIR_SW, depth), "West portal (underground exit) should be built.");

				report.check_true("ug_line_built", metro.build_underground_x(x0 + 2, xe - 2, yu, depth), "Underground line between the portals should be built.");

				local st_ug_tile = T(x0 + 8, yu);
				report.check_true("ug_station_built", metro.build_station_x(st_ug_tile, depth), "Underground station should be built.");

				local sid_ug = GSMetro.GetUndergroundStationID(st_ug_tile, depth);
				report.check_true("ug_station_valid", GSStation.IsValidStation(sid_ug), "Underground station id should be valid.");
				local sid_surf = GSStation.GetStationID(st_surf_tile);
				report.check_true("surf_station_valid", GSStation.IsValidStation(sid_surf), "Surface station id should be valid.");

				/* --- Signals: PBS one-way, enforcing the clockwise direction ---
				 * BuildSignal(tile, front): the signal FACES 'front', i.e. it admits
				 * trains travelling front->tile and blocks tile->front. To allow
				 * eastbound (+x) traffic the front must be the WEST neighbour. */
				report.check_true("surf_signal_1", GSRail.BuildSignal(T(x0 + 9, ys), T(x0 + 8, ys), GSRail.SIGNALTYPE_PBS_ONEWAY), "Surface signal east of the station should be built.");
				report.check_true("surf_signal_2", GSRail.BuildSignal(T(x0 + 4, ys), T(x0 + 3, ys), GSRail.SIGNALTYPE_PBS_ONEWAY), "Surface signal west of the station should be built.");
				report.check_true("ug_signal_1", GSMetro.BuildUndergroundSignal(T(xe - 4, yu), GSRail.RAILTRACK_NE_SW, depth), "Underground signal east of the UG station should be built.");
				report.check_true("ug_signal_2", GSMetro.BuildUndergroundSignal(T(x0 + 5, yu), GSRail.RAILTRACK_NE_SW, depth), "Underground signal west of the UG station should be built.");

				/* --- Two trains, each with a wagon --- */
				local wagon_engine = trains.find_rail_wagon();
				local t1 = trains.build_train(depot_tile, engine);
				local t2 = trains.build_train(depot_tile, engine);
				report.check_true("trains_built", t1 != GSVehicle.VEHICLE_INVALID && t2 != GSVehicle.VEHICLE_INVALID, "Both trains should be built.");

				local wagons_attached = 0;
				if (wagon_engine != null) {
					if (t1 != GSVehicle.VEHICLE_INVALID && trains.attach_wagon(t1, depot_tile, wagon_engine).attached) wagons_attached++;
					if (t2 != GSVehicle.VEHICLE_INVALID && trains.attach_wagon(t2, depot_tile, wagon_engine).attached) wagons_attached++;
				}
				report.check_eq("wagons_attached", 2, wagons_attached, "Each train should have one wagon (crash regression: wagons through portals).");

				if (t1 != GSVehicle.VEHICLE_INVALID && t2 != GSVehicle.VEHICLE_INVALID) {
					foreach (t in [t1, t2]) {
						report.check_true("orders_" + t, trains.append_surface_order(t, st_surf_tile) && trains.append_underground_order(t, sid_ug), "Train " + t + " orders should be added.");
					}

					/* Stagger the starts so the second train queues at signals. */
					GSVehicle.StartStopVehicle(t1);
					local dep1 = trains.wait_for_departure(t1, 6000);
					report.check_true("t1_departed", dep1 != null, "Train 1 must leave the depot.");
					GSVehicle.StartStopVehicle(t2);

					local first_stop = trains.wait_for_surface_station(t1, st_surf_tile, 12000);
					report.check_true("t1_first_surface_stop", first_stop != null, "Train 1 must reach the surface station first.");
					report.metric("t1_pos_x", GSMap.GetTileX(GSVehicle.GetLocation(t1)));
					report.metric("t1_pos_y", GSMap.GetTileY(GSVehicle.GetLocation(t1)));
					report.metric("t1_state", GSVehicle.GetState(t1));
					report.metric("t1_speed", GSVehicle.GetCurrentSpeed(t1));

					/* --- Lap counting: 2 full circuits each = 2 surface + 2 UG stops --- */
					local stats = {};
					stats[t1] <- { surf = 0, ug = 0, was_surf = false, was_ug = false };
					stats[t2] <- { surf = 0, ug = 0, was_surf = false, was_ug = false };

					local budget = 70000;
					local elapsed = 0;
					while (elapsed < budget) {
						local sample = (elapsed < 12000) ? 500 : 5000;
						if (elapsed % sample == 0) {
							foreach (t, s in stats) {
								local loc = GSVehicle.GetLocation(t);
								local dest = GSOrder.GetOrderDestination(t, GSOrder.ORDER_CURRENT);
								GSLog.Info("tick+" + elapsed + " t" + t +
									" pos=(" + GSMap.GetTileX(loc) + "," + GSMap.GetTileY(loc) + ")" +
									" state=" + GSVehicle.GetState(t) +
									" speed=" + GSVehicle.GetCurrentSpeed(t) +
									" ug=" + (GSMetro.IsVehicleUnderground(t) ? 1 : 0) +
									" dest=(" + GSMap.GetTileX(dest) + "," + GSMap.GetTileY(dest) + ")" +
									" depot_order=" + (GSOrder.IsGotoDepotOrder(t, GSOrder.ORDER_CURRENT) ? 1 : 0) +
									" stops=" + s.surf + "/" + s.ug);
							}
						}
						local done = true;
						foreach (t, s in stats) {
							local at_station = GSVehicle.GetState(t) == GSVehicle.VS_AT_STATION;
							local under = GSMetro.IsVehicleUnderground(t);
							local at_surf = at_station && !under && GSStation.GetStationID(GSVehicle.GetLocation(t)) == sid_surf;
							local at_ug = at_station && under;
							if (at_surf && !s.was_surf) s.surf++;
							if (at_ug && !s.was_ug) s.ug++;
							s.was_surf = at_surf;
							s.was_ug = at_ug;
							if (s.surf < 2 || s.ug < 2) done = false;
						}
						if (done) break;
						GSController.Sleep(5);
						elapsed += 5;
					}

					report.check_true("t1_two_laps", stats[t1].surf >= 2 && stats[t1].ug >= 2, "Train 1 must complete two full circuits (2 surface + 2 underground stops).");
					report.check_true("t2_two_laps", stats[t2].surf >= 2 && stats[t2].ug >= 2, "Train 2 must complete two full circuits (2 surface + 2 underground stops).");

					report.metric("t1_surface_stops", stats[t1].surf);
					report.metric("t1_underground_stops", stats[t1].ug);
					report.metric("t2_surface_stops", stats[t2].surf);
					report.metric("t2_underground_stops", stats[t2].ug);
					report.metric("ticks_used", elapsed);
				}
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.check_true("final_save_written", GSTest.SaveGame("metro_loop_acceptance_final.sav"), "Scenario should save final state for manual inspection.");
		report.finish();
	}
}
