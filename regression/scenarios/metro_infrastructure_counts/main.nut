class MetroInfrastructureCounts extends GSController {
	_rt = null;

	function Rail(company) { return GSInfrastructure.GetRailPieceCount(company, this._rt); }
	function Signals(company) { return GSInfrastructure.GetInfrastructurePieceCount(company, GSInfrastructure.INFRASTRUCTURE_SIGNALS); }
	function Stations(company) { return GSInfrastructure.GetInfrastructurePieceCount(company, GSInfrastructure.INFRASTRUCTURE_STATION); }

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_infrastructure_counts", null, 510020);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);

		try {
			local area = build.find_buildable_rectangle(32, 8);
			report.check_true("build_area_found", area != null, "Expected a clear area for the infrastructure count scenario.");

			local company = companies.ensure_company();
			if (area != null && company != GSCompany.COMPANY_INVALID) {
				local x = build.tile_x(area);
				local y = build.tile_y(area) + 3;
				local depth = -1;

				local t8 = build.make_tile(x + 8, y);
				local t9 = build.make_tile(x + 9, y);
				local t10 = build.make_tile(x + 10, y);
				local t12 = build.make_tile(x + 12, y);
				local t13 = build.make_tile(x + 13, y);
				local ramp_tile = build.make_tile(x + 16, y);
				local portal_tile = build.make_tile(x + 22, y);

				local company_mode = GSCompanyMode(company);
				this._rt = GSRail.GetCurrentRailType();

				local base_rail = this.Rail(company);
				local base_sig = this.Signals(company);
				local base_stn = this.Stations(company);
				report.metric("base_rail", base_rail);

				/* --- Track: one piece per track bit. --- */
				report.check_true("chain_built", metro.build_underground_x(x + 8, x + 10, y, depth), "Underground chain should be built.");
				report.check_eq("rail_after_chain", base_rail + 3, this.Rail(company), "Three underground track pieces should be counted.");

				report.check_true("second_track_built", GSMetro.BuildUndergroundRail(t9, GSRail.RAILTRACK_NW_SE, depth), "A crossing track bit should be added.");
				report.check_eq("rail_after_second_bit", base_rail + 4, this.Rail(company), "The extra track bit should count as one piece.");
				report.check_true("second_track_removed", GSMetro.RemoveUndergroundRail(t9, GSRail.RAILTRACK_NW_SE, depth), "The crossing track bit should be removed.");
				report.check_eq("rail_after_second_bit_removed", base_rail + 3, this.Rail(company), "Removing a track bit should uncount it.");

				/* --- Signals. --- */
				report.check_true("signal_built", GSMetro.BuildUndergroundSignal(t9, GSRail.RAILTRACK_NE_SW, depth), "Underground signal should be built.");
				report.check_eq("signals_after_build", base_sig + 1, this.Signals(company), "Underground signal should be counted.");
				report.check_true("signal_removed", GSMetro.RemoveUndergroundSignal(t9, GSRail.RAILTRACK_NE_SW, depth), "Underground signal should be removed.");
				report.check_eq("signals_after_remove", base_sig, this.Signals(company), "Removed underground signal should be uncounted.");

				report.check_true("signal_on_t10_built", GSMetro.BuildUndergroundSignal(t10, GSRail.RAILTRACK_NE_SW, depth), "Second underground signal should be built.");
				report.check_eq("signals_after_second_build", base_sig + 1, this.Signals(company), "Second underground signal should be counted.");
				report.check_true("track_under_signal_removed", GSMetro.RemoveUndergroundRail(t10, GSRail.RAILTRACK_NE_SW, depth), "Track carrying a signal should be removable.");
				report.check_eq("rail_after_signal_track_removed", base_rail + 2, this.Rail(company), "Removed track should be uncounted.");
				report.check_eq("signals_after_signal_track_removed", base_sig, this.Signals(company), "Signal should go away with its track.");

				/* --- Stations: one rail piece and one station tile per platform. --- */
				report.check_true("station_built", metro.build_station_x(t12, depth), "Underground station should be built.");
				local station_id = GSMetro.GetUndergroundStationID(t12, depth);
				report.check_true("station_valid", GSStation.IsValidStation(station_id), "Underground station should resolve to a station.");
				report.check_eq("rail_after_station", base_rail + 3, this.Rail(company), "Platform should count as one rail piece.");
				report.check_eq("stations_after_station", base_stn + 1, this.Stations(company), "Platform should count as one station tile.");

				report.check_true("track_before_platform_built", GSMetro.BuildUndergroundRail(t13, GSRail.RAILTRACK_NE_SW, depth), "Track to be converted should be built.");
				report.check_eq("rail_before_conversion", base_rail + 4, this.Rail(company), "Track to be converted should be counted.");
				report.check_true("platform_over_track_built", metro.build_station_x(t13, depth, station_id), "Platform should replace the track.");
				report.check_eq("rail_after_conversion", base_rail + 4, this.Rail(company), "Converting track into a platform should not change the rail count.");
				report.check_eq("stations_after_conversion", base_stn + 2, this.Stations(company), "Second platform should count as a station tile.");

				/* --- Ramp: one piece. --- */
				report.check_true("ramp_built", GSMetro.BuildRamp(ramp_tile, GSMetro.DIAGDIR_SW, depth, depth - 1), "Ramp should be built.");
				report.check_eq("rail_after_ramp", base_rail + 5, this.Rail(company), "Ramp should count as one rail piece.");

				/* --- Portal: surface rail (vanilla) plus the portal slice. --- */
				report.check_true("portal_built", metro.build_portal(portal_tile, GSMetro.DIAGDIR_SW, depth), "Portal should be built.");
				report.check_true("portal_surface_rail", GSRail.IsRailTile(portal_tile), "Portal should have surface rail.");
				report.check_eq("rail_after_portal", base_rail + 7, this.Rail(company), "Portal should count its surface rail and the portal slice.");

				/* --- Removal back towards the baseline. --- */
				report.check_true("station_t12_removed", GSMetro.RemoveUndergroundStation(t12, depth), "Platform should be removed.");
				report.check_true("station_t13_removed", GSMetro.RemoveUndergroundStation(t13, depth), "Second platform should be removed.");
				report.check_eq("stations_after_station_removal", base_stn, this.Stations(company), "Station tile count should return to the baseline.");
				report.check_true("track_t8_removed", GSMetro.RemoveUndergroundRail(t8, GSRail.RAILTRACK_NE_SW, depth), "Track should be removed.");
				report.check_true("track_t9_removed", GSMetro.RemoveUndergroundRail(t9, GSRail.RAILTRACK_NE_SW, depth), "Track should be removed.");
				report.check_eq("rail_after_track_removal", base_rail + 3, this.Rail(company), "Only ramp and portal pieces should remain counted.");
				report.check_true("ramp_removed", GSMetro.RemoveRamp(ramp_tile, depth), "Ramp should be removed.");
				report.check_eq("rail_after_ramp_removal", base_rail + 2, this.Rail(company), "Removed ramp should be uncounted.");
				report.check_true("portal_removed", GSMetro.RemovePortal(portal_tile), "Portal should be removed.");
				report.check_eq("rail_after_removal", base_rail, this.Rail(company), "Rail count should be back at the baseline after removing the portal and its surface rail.");
				report.check_eq("signals_after_removal", base_sig, this.Signals(company), "Signal count should be back at the baseline.");
				report.check_eq("stations_after_removal", base_stn, this.Stations(company), "Station tile count should be back at the baseline.");

				/* --- Rebuild a little for the ownership checks. --- */
				report.check_true("rebuild_chain", metro.build_underground_x(x + 8, x + 9, y, depth), "Chain should be rebuilt.");
				report.check_true("rebuild_signal", GSMetro.BuildUndergroundSignal(t9, GSRail.RAILTRACK_NE_SW, depth), "Signal should be rebuilt.");
				report.check_true("cycle_signal", GSMetro.BuildUndergroundSignal(t9, GSRail.RAILTRACK_NE_SW, depth), "Building again should flip the signal direction.");
				report.check_eq("signals_after_cycle", base_sig + 1, this.Signals(company), "Flipping a signal's direction should not change the count.");
				report.check_true("rebuild_station", metro.build_station_x(t12, depth), "Station should be rebuilt.");
				station_id = GSMetro.GetUndergroundStationID(t12, depth);
				report.check_true("rebuild_ramp", GSMetro.BuildRamp(ramp_tile, GSMetro.DIAGDIR_SW, depth, depth - 1), "Ramp should be rebuilt.");
				report.check_true("rebuild_portal", metro.build_portal(portal_tile, GSMetro.DIAGDIR_SW, depth), "Portal should be rebuilt.");
				report.check_eq("rail_before_merge", base_rail + 6, this.Rail(company), "Rebuilt pieces should be counted.");

				company_mode = null;

				/* --- Merger: everything moves to the acquiring company, counts included. --- */
				local rail_a = this.Rail(company);
				local sig_a = this.Signals(company);
				local stn_a = this.Stations(company);

				local buyer = GSMetro.CreateCompany(GSCompany.COMPANY_INVALID);
				report.check_true("buyer_created", buyer != GSCompany.COMPANY_INVALID, "A second company should be created.");
				if (buyer != GSCompany.COMPANY_INVALID) {
					local rail_b = this.Rail(buyer);
					local sig_b = this.Signals(buyer);
					local stn_b = this.Stations(buyer);

					report.check_true("merged", GSMetro.MergeCompany(company, buyer), "Company merger should succeed.");
					report.check_true("old_company_gone", GSCompany.ResolveCompanyID(company) == GSCompany.COMPANY_INVALID, "Acquired company should no longer exist.");
					report.check_eq("buyer_rail_after_merge", rail_b + rail_a, this.Rail(buyer), "Rail pieces (surface and underground) should move to the buyer.");
					report.check_eq("buyer_signals_after_merge", sig_b + sig_a, this.Signals(buyer), "Signals should move to the buyer.");
					report.check_eq("buyer_stations_after_merge", stn_b + stn_a, this.Stations(buyer), "Station tiles should move to the buyer.");
					report.check_true("underground_kept_after_merge", GSMetro.HasUndergroundRail(t8, depth) && GSMetro.IsPortalTile(portal_tile), "Underground property should survive a merger.");
					report.check_true("station_kept_after_merge", GSStation.IsValidStation(GSMetro.GetUndergroundStationID(t12, depth)), "Underground station should survive a merger.");

					/* The buyer now owns the slices, so it may remove them. */
					{
						local buyer_mode = GSCompanyMode(buyer);
						report.check_true("buyer_can_remove_track", GSMetro.RemoveUndergroundRail(t8, GSRail.RAILTRACK_NE_SW, depth), "Buyer should own the inherited underground track.");
						report.check_eq("buyer_rail_after_remove", rail_b + rail_a - 1, this.Rail(buyer), "Removing inherited track should uncount it from the buyer.");
						buyer_mode = null;
					}

					/* --- Bankruptcy: all underground property of the company disappears. --- */
					local other = GSMetro.CreateCompany(GSCompany.COMPANY_INVALID);
					report.check_true("third_company_created", other != GSCompany.COMPANY_INVALID, "A third company should be created so the buyer can be removed.");

					report.check_true("buyer_removed", GSMetro.RemoveCompany(buyer), "Company removal should succeed.");
					report.check_true("buyer_gone", GSCompany.ResolveCompanyID(buyer) == GSCompany.COMPANY_INVALID, "Removed company should no longer exist.");
					report.check_eq("track_gone", GSMetro.TK_NONE, GSMetro.GetUndergroundTileKind(t9, depth), "Underground track should be removed with the company.");
					report.check_eq("ramp_gone", GSMetro.TK_NONE, GSMetro.GetUndergroundTileKind(ramp_tile, depth), "Ramp should be removed with the company.");
					report.check_true("portal_gone", !GSMetro.IsPortalTile(portal_tile), "Portal slice should be removed with the company.");
					report.check_true("portal_surface_rail_gone", !GSRail.IsRailTile(portal_tile), "Portal surface rail should be removed with the company.");
					report.check_eq("platform_gone", GSMetro.TK_NONE, GSMetro.GetUndergroundTileKind(t12, depth), "Underground platform should be removed with the company.");
					report.check_eq("complex_platforms_gone", 0, GSMetro.GetStationComplexPlatformCount(station_id), "Station complex should have no platforms left.");
					report.check_true("underground_column_empty", !GSMetro.HasUnderground(t12) && !GSMetro.HasUnderground(portal_tile), "No underground slices should remain.");

					/* The emptied station is deleted by the regular unused-station timeout. */
					local waited = 0;
					while (waited < 6000 && GSStation.IsValidStation(station_id)) {
						GSController.Sleep(100);
						waited += 100;
					}
					report.check_true("station_deleted", !GSStation.IsValidStation(station_id), "Emptied underground station should be deleted like an emptied surface station.");
					report.metric("ticks_until_station_deleted", waited);
				}
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.finish();
	}
}
