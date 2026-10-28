/**
 * metro_depot_via_portal
 *
 * The clockwise half-underground loop from metro_loop_acceptance, one train.
 * Once the train is UNDERGROUND it is sent to the depot. It must surface
 * through a portal and arrive at the (surface) depot: the depot search for
 * underground trains anchors at the portal they will exit from.
 */
class MetroDepotViaPortal extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_depot_via_portal", null, 510999);
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

				report.check_true("surf_signal_1", GSRail.BuildSignal(T(x0 + 9, ys), T(x0 + 8, ys), GSRail.SIGNALTYPE_PBS_ONEWAY), "Surface signal east of the station should be built.");
				report.check_true("surf_signal_2", GSRail.BuildSignal(T(x0 + 4, ys), T(x0 + 3, ys), GSRail.SIGNALTYPE_PBS_ONEWAY), "Surface signal west of the station should be built.");

				report.check_true("portal_e_built", metro.build_portal(T(xe - 1, yu), GSMetro.DIAGDIR_NE, depth), "East portal should be built.");
				report.check_true("portal_w_built", metro.build_portal(T(x0 + 1, yu), GSMetro.DIAGDIR_SW, depth), "West portal should be built.");
				report.check_true("ug_line_built", metro.build_underground_x(x0 + 2, xe - 2, yu, depth), "Underground line should be built.");

				local st_ug_tile = T(x0 + 8, yu);
				report.check_true("ug_station_built", metro.build_station_x(st_ug_tile, depth), "Underground station should be built.");
				local sid_ug = GSMetro.GetUndergroundStationID(st_ug_tile, depth);
				report.check_true("ug_station_valid", GSStation.IsValidStation(sid_ug), "Underground station id should be valid.");

				local t1 = trains.build_train(depot_tile, engine);
				report.check_true("train_built", t1 != GSVehicle.VEHICLE_INVALID, "Train should be built.");

				if (t1 != GSVehicle.VEHICLE_INVALID) {
					report.check_true("orders_added", trains.append_surface_order(t1, st_surf_tile) && trains.append_underground_order(t1, sid_ug, GSOrder.OF_NON_STOP_INTERMEDIATE), "Orders should be added.");
					GSVehicle.StartStopVehicle(t1);

					/* Wait until the train is underground, then order it home. */
					local entered = trains.wait_for_underground_entry(t1, 20000);
					report.check_true("train_went_underground", entered != null, "Train must enter the tunnel.");

					if (entered != null) {
						report.check_true("send_to_depot_accepted", GSVehicle.SendVehicleToDepot(t1), "Send-to-depot must be accepted while the train is underground.");

						local in_depot = null;
						for (local ticks = 0; ticks <= 30000; ticks += 5) {
							if (GSVehicle.IsInDepot(t1)) { in_depot = ticks; break; }
							GSController.Sleep(5);
						}
						report.check_true("train_reached_depot", in_depot != null, "Train must surface through a portal and reach the depot.");
						if (in_depot != null) report.metric("ticks_to_depot", in_depot);
					}
				}
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
