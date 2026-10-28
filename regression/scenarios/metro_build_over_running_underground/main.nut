/**
 * metro_build_over_running_underground
 *
 * A metro loop (surface north side, underground south side between two
 * portals). The metro train is stopped while it is underground, under a
 * plain grass tile T. Surface construction on T must not be blocked by the
 * underground train:
 *   - GSRail.BuildRailTrack / RemoveRailTrack on T (same axis as the tunnel),
 *   - GSRoad.BuildRoad across T (turning it into a level crossing),
 *   - GSTile.DemolishTile on T.
 * Afterwards the metro train is restarted and must keep running.
 */
function BuildOverIsCrashed(v)
{
	return !GSVehicle.IsValidVehicle(v) || GSVehicle.GetState(v) == GSVehicle.VS_CRASHED;
}

class MetroBuildOverRunningUnderground extends GSController {
	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_build_over_running_underground", null, 610203);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local trains = MetroTest.MetroTrainLib();
		local T = function (tx, ty) { return GSMap.GetTileIndex(tx, ty); };

		try {
			local area = build.find_buildable_rectangle(24, 10);
			report.check_true("build_area_found", area != null, "Expected a clear 24x10 area.");

			local company = companies.ensure_company();
			local company_mode = company == GSCompany.COMPANY_INVALID ? null : GSCompanyMode(company);
			local engine = company_mode == null ? null : trains.find_rail_engine();
			local wagon_engine = company_mode == null ? null : trains.find_rail_wagon();
			report.check_true("rail_engine_found", engine != null, "Expected at least one buildable rail engine.");

			local roadtypes = GSRoadTypeList(GSRoad.ROADTRAMTYPES_ROAD);
			local roadtype = roadtypes.Begin();
			report.check_true("roadtype_available", !roadtypes.IsEnd(), "Expected a road type.");
			if (!roadtypes.IsEnd()) GSRoad.SetCurrentRoadType(roadtype);

			if (area != null && engine != null && company != GSCompany.COMPANY_INVALID) {
				local x0 = build.tile_x(area) + 1;
				local ys = build.tile_y(area) + 2;
				local yu = ys + 2;
				local xe = x0 + 19;
				local depth = -1;

				local stb_tile = T(x0 + 6, ys);
				report.check_true("surface_line", build.build_surface_x(x0 + 1, xe - 1, ys), "North surface line should be built.");
				report.check_true("surface_station", GSRail.BuildRailStation(stb_tile, GSRail.RAILTRACK_NE_SW, 1, 2, GSStation.STATION_NEW), "Surface station should be built.");
				local depot = T(x0 + 2, ys - 1);
				report.check_true("depot",
					GSRail.BuildRail(T(x0 + 1, ys), T(x0 + 2, ys), depot) &&
					GSRail.BuildRail(depot, T(x0 + 2, ys), T(x0 + 3, ys)) &&
					GSRail.BuildRailDepot(depot, T(x0 + 2, ys)),
					"Depot should be built.");
				report.check_true("east_side",
					GSRail.BuildRail(T(xe - 1, ys), T(xe, ys), T(xe, ys + 1)) &&
					GSRail.BuildRail(T(xe, ys), T(xe, ys + 1), T(xe, yu)) &&
					GSRail.BuildRail(T(xe, ys + 1), T(xe, yu), T(xe - 1, yu)),
					"East curve should be built.");
				report.check_true("west_side",
					GSRail.BuildRail(T(x0 + 1, yu), T(x0, yu), T(x0, ys + 1)) &&
					GSRail.BuildRail(T(x0, yu), T(x0, ys + 1), T(x0, ys)) &&
					GSRail.BuildRail(T(x0, ys + 1), T(x0, ys), T(x0 + 1, ys)),
					"West curve should be built.");
				report.check_true("portal_e", metro.build_portal(T(xe - 1, yu), GSMetro.DIAGDIR_NE, depth), "East portal should be built.");
				report.check_true("portal_w", metro.build_portal(T(x0 + 1, yu), GSMetro.DIAGDIR_SW, depth), "West portal should be built.");
				report.check_true("ug_line", metro.build_underground_x(x0 + 2, xe - 2, yu, depth), "Underground line should be built.");
				report.check_true("signals",
					GSRail.BuildSignal(T(x0 + 9, ys), T(x0 + 8, ys), GSRail.SIGNALTYPE_PBS_ONEWAY) &&
					GSRail.BuildSignal(T(x0 + 4, ys), T(x0 + 3, ys), GSRail.SIGNALTYPE_PBS_ONEWAY),
					"Surface signals should be built.");

				local mt = trains.build_train(depot, engine);
				report.check_true("train_built", mt != GSVehicle.VEHICLE_INVALID, "Metro train should be built.");
				if (mt != GSVehicle.VEHICLE_INVALID) {
					if (wagon_engine != null) trains.attach_wagon(mt, depot, wagon_engine);
					report.check_true("orders", trains.append_surface_order(mt, stb_tile), "Order should be added.");

					GSVehicle.StartStopVehicle(mt);

					/* Wait until the train is underground in the middle of the tunnel, then stop it. */
					local ticks = 0;
					local stop_requested = false;
					while (ticks < 20000) {
						if (GSMetro.IsVehicleUnderground(mt)) {
							local lx = GSMap.GetTileX(GSVehicle.GetLocation(mt));
							if (lx <= x0 + 14 && lx >= x0 + 8) {
								GSVehicle.StartStopVehicle(mt);
								stop_requested = true;
								break;
							}
						}
						GSController.Sleep(1);
						ticks++;
					}
					report.check_true("stop_requested_underground", stop_requested, "Metro train must be stopped while it is in the tunnel.");

					/* Let it come to a standstill. */
					local still = 0;
					for (local i = 0; i < 2000 && still < 20; i++) {
						if (GSVehicle.GetCurrentSpeed(mt) == 0) still++; else still = 0;
						GSController.Sleep(1);
					}

					local t = GSVehicle.GetLocation(mt);
					local tx = GSMap.GetTileX(t);
					local ty = GSMap.GetTileY(t);
					report.metric("stopped_x", tx);
					report.metric("stopped_y", ty);
					report.metric("tunnel_x_range", (x0 + 2) + ".." + (xe - 2));

					local valid_spot = still >= 20 && GSMetro.IsVehicleUnderground(mt) && !GSMetro.IsPortalTile(t) &&
							ty == yu && tx >= x0 + 3 && tx <= xe - 3 && GSTile.IsBuildable(t);
					report.check_true("stopped_under_grass", valid_spot, "Metro train must stand still under a plain buildable surface tile.");

					if (valid_spot) {
						local tn = T(tx, ty - 1);
						local tsouth = T(tx, ty + 1);

						report.check_true("build_rail_over_train", GSRail.BuildRailTrack(t, GSRail.RAILTRACK_NE_SW),
							"Building surface rail above an underground train must succeed.");
						report.check_true("remove_rail_over_train", GSRail.RemoveRailTrack(t, GSRail.RAILTRACK_NE_SW),
							"Removing surface rail above an underground train must succeed (the train is not on the surface track).");
						report.check_true("rebuild_rail_over_train", GSRail.BuildRailTrack(t, GSRail.RAILTRACK_NE_SW),
							"Rebuilding surface rail above an underground train must succeed.");
						report.check_true("build_road_crossing_over_train", GSRoad.BuildRoad(tn, tsouth),
							"Building a road (level crossing) above an underground train must succeed.");
						report.check_true("crossing_built", GSRail.IsLevelCrossingTile(t), "The tile should now be a level crossing.");
						report.check_true("demolish_crossing_over_train", GSTile.DemolishTile(t),
							"Demolishing a level crossing above an underground train must succeed.");
						report.check_true("build_road_over_train", GSRoad.BuildRoad(tn, tsouth),
							"Building plain road above an underground train must succeed.");
						report.check_true("demolish_road_over_train", GSTile.DemolishTile(t),
							"Demolishing road above an underground train must succeed.");
						report.check_true("tile_clear_again", GSTile.IsBuildable(t), "The tile should be clear again.");
						report.check_true("tunnel_intact", GSMetro.HasUndergroundRail(t, depth), "The underground track under the tile must be untouched.");
					}

					report.check_true("train_not_crashed", !BuildOverIsCrashed(mt), "Construction above must not hurt the underground train.");

					/* Restart: the train must keep running its loop. */
					GSVehicle.StartStopVehicle(mt);
					local arrived = trains.wait_for_surface_station(mt, stb_tile, 15000);
					report.check_true("train_resumes", arrived != null && !BuildOverIsCrashed(mt), "Metro train must resume and reach its surface station.");
				}
			}
			company_mode = null;
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.check_true("final_save_written", GSTest.SaveGame("metro_build_over_running_underground_final.sav"), "Scenario should save final state for manual inspection.");
		report.finish();
	}
}
