/**
 * metro_portal_collision_control (negative control)
 *
 * A single unsignalled line through a short tunnel:
 *
 *   [depot A]─[stW]──P1~~~~~~~~~~~~P2──[stE]─[depot B]
 *
 * Train 2 leaves depot B westbound for station W and dives through P2; train 1
 * is started in depot A eastbound for station E once train 2 is close to P1.
 *
 * Formerly a negative control (both trains had to crash, proving the layer
 * separation hides no real meeting at the portal). A train below ground on its
 * way out of a portal now occupies the surface block in front of the mouth,
 * like a train in a vanilla tunnel: train 1 must stay in its depot while
 * train 2 comes out, and nobody crashes.
 */
function PortalIsCrashed(v)
{
	return !GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED;
}

class MetroPortalCollisionControl extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_portal_collision_control", null, 610204);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();
		local T = function (tx, ty) { return GSMap.GetTileIndex(tx, ty); };

		try {
			local area = build.find_buildable_rectangle(24, 6);
			report.check_true("build_area_found", area != null, "Expected a clear 24x6 area.");

			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);
			local engine = company_mode == null ? null : trains.find_rail_engine();
			report.check_true("rail_engine_found", engine != null, "Expected at least one buildable rail engine.");

			if (area != null && engine != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area) + 1;
				local y = build.tile_y(area) + 2;
				local depth = -1;

				local depot_a = T(x, y);
				local stw = T(x + 2, y);
				local p1 = T(x + 5, y);
				local p2 = T(x + 14, y);
				local ste = T(x + 16, y);
				local depot_b = T(x + 19, y);

				report.check_true("stations_built",
					GSRail.BuildRailStation(stw, GSRail.RAILTRACK_NE_SW, 1, 2, GSStation.STATION_NEW) &&
					GSRail.BuildRailStation(ste, GSRail.RAILTRACK_NE_SW, 1, 2, GSStation.STATION_NEW),
					"Both surface stations should be built.");
				report.check_true("surface_built",
					build.build_surface_x(x + 1, x + 1, y) && build.build_surface_x(x + 4, x + 4, y) &&
					build.build_surface_x(x + 15, x + 15, y) && build.build_surface_x(x + 18, x + 18, y),
					"Surface track pieces should be built.");
				report.check_true("depots_built",
					GSRail.BuildRailDepot(depot_a, T(x + 1, y)) && GSRail.BuildRailDepot(depot_b, T(x + 18, y)),
					"Both depots should be built.");
				report.check_true("portal_1_built", metro.build_portal(p1, GSMetro.DIAGDIR_SW, depth), "Portal P1 should be built.");
				report.check_true("portal_2_built", metro.build_portal(p2, GSMetro.DIAGDIR_NE, depth), "Portal P2 should be built.");
				report.check_true("tunnel_built", metro.build_underground_x(x + 6, x + 13, y, depth), "Tunnel should be built.");

				local t1 = trains.build_train(depot_a, engine);
				local t2 = trains.build_train(depot_b, engine);
				report.check_true("trains_built", t1 != GSVehicle.VEHICLE_INVALID && t2 != GSVehicle.VEHICLE_INVALID, "Both trains should be built.");

				if (t1 != GSVehicle.VEHICLE_INVALID && t2 != GSVehicle.VEHICLE_INVALID) {
					report.check_true("orders",
						trains.append_surface_order(t1, ste, GSOrder.OF_NON_STOP_INTERMEDIATE) &&
						trains.append_surface_order(t2, stw, GSOrder.OF_NON_STOP_INTERMEDIATE),
						"Orders should be added.");

					/* Train 2 first, westbound into the tunnel. */
					GSVehicle.StartStopVehicle(t2);
					local ticks = 0;
					local released = false;
					while (ticks < 8000) {
						local lx = GSMap.GetTileX(GSVehicle.GetLocation(t2));
						if (GSMetro.IsVehicleUnderground(t2) && lx <= x + 13) {
							GSVehicle.StartStopVehicle(t1);
							released = true;
							break;
						}
						GSController.Sleep(1);
						ticks++;
					}
					report.check_true("train1_released", released, "Train 1 must be released once train 2 is in the tunnel.");
					report.metric("release_tick", ticks);

					/* Watch the meeting point. */
					local last1 = null;
					local last2 = null;
					local crashed1 = false;
					local crashed2 = false;
					local t1_left_while_t2_below = false;
					local t1_has_left = false;
					for (local i = 0; i < 6000 && !(crashed1 && crashed2); i++) {
						if (!crashed1 && PortalIsCrashed(t1)) crashed1 = true;
						if (!crashed2 && PortalIsCrashed(t2)) crashed2 = true;
						if (!crashed1 && !crashed2 && GSVehicle.IsValidVehicle(t1) && GSVehicle.IsValidVehicle(t2)) {
							last1 = { x = GSMap.GetTileX(GSVehicle.GetLocation(t1)), ug = GSMetro.IsVehicleUnderground(t1) };
							last2 = { x = GSMap.GetTileX(GSVehicle.GetLocation(t2)), ug = GSMetro.IsVehicleUnderground(t2) };
							/* The moment train 1 first comes out of its depot, train 2 must be out of the tunnel. */
							if (!t1_has_left && !GSVehicle.IsInDepot(t1)) {
								t1_has_left = true;
								if (GSMetro.IsVehicleUnderground(t2)) t1_left_while_t2_below = true;
							}
						}
						GSController.Sleep(1);
					}

					if (last1 != null) {
						report.metric("t1_last_dx_from_p1", last1.x - (x + 5));
						report.metric("t1_last_underground", last1.ug ? 1 : 0);
					}
					if (last2 != null) {
						report.metric("t2_last_dx_from_p1", last2.x - (x + 5));
						report.metric("t2_last_underground", last2.ug ? 1 : 0);
					}
					report.metric("t1_reached_ste", GSVehicle.IsValidVehicle(t1) && GSStation.GetStationID(GSVehicle.GetLocation(t1)) == GSStation.GetStationID(ste) ? 1 : 0);

					report.check_true("train1_intact", !crashed1, "Train 1 must not crash: it waits in its depot while train 2 comes out.");
					report.check_true("train2_intact", !crashed2, "Train 2 must not crash.");
					report.check_true("train1_held_in_depot", !t1_left_while_t2_below, "Train 1 must not leave its depot into the block train 2 is coming out into.");
					report.check_true("train1_left_later", t1_has_left, "Train 1 leaves its depot once train 2 is out.");
				}
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.check_true("final_save_written", GSTest.SaveGame("metro_portal_collision_control_final.sav"), "Scenario should save final state for manual inspection.");
		report.finish();
	}
}
