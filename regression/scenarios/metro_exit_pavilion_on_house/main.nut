class MetroExitPavilionOnHouse extends GSController {
	/* Tiles within distance r (DistanceMax) of a tile, in a fixed order. */
	function Nearby(build, tile, r)
	{
		local result = [];
		local cx = build.tile_x(tile);
		local cy = build.tile_y(tile);
		for (local dy = -r; dy <= r; dy++) {
			for (local dx = -r; dx <= r; dx++) {
				local x = cx + dx;
				local y = cy + dy;
				if (x < 1 || y < 1 || x >= GSMap.GetMapSizeX() - 1 || y >= GSMap.GetMapSizeY() - 1) continue;
				result.append(build.make_tile(x, y));
			}
		}
		return result;
	}

	function Start()
	{
		local MetroTest = import("test.MetroTestLib", "", 1);

		local report = MetroTest("metro_exit_pavilion_on_house", null, 510098);
		local companies = MetroTest.MetroCompanyLib(report);
		local build = MetroTest.MetroBuildLib();
		local metro = MetroTest.MetroLib(build);
		local world = MetroTest.MetroWorldLib(build);

		try {
			local area = build.find_buildable_rectangle(40, 40);
			report.check_true("build_area_found", area != null, "Expected a clear area for the town.");

			local company = companies.ensure_company();
			local other = GSMetro.CreateCompany(GSCompany.COMPANY_INVALID);
			report.check_true("other_company_created", other != GSCompany.COMPANY_INVALID && other != company, "A second company should own rail next to the exit.");

			local town = area == null ? null : world.create_town_near(build.make_tile(build.tile_x(area) + 20, build.tile_y(area) + 20), 6, "Exit Town", 60);
			report.check_true("town_created", town != null && GSTown.IsValidTown(town), "A town with houses should be founded.");

			if (area != null && company != GSCompany.COMPANY_INVALID && other != GSCompany.COMPANY_INVALID && town != null) {
				local depth = -1;
				/* Good standing with the town, so the authority allows demolition. */
				GSTown.ChangeRating(town, company, 1000);

				/* A house with a buildable tile, a road tile and another house within the exit distance. */
				local house = null;
				local free_tile = null;
				local road_tile = null;
				local other_house = null;
				foreach (t in this.Nearby(build, GSTown.GetLocation(town), 8)) {
					if (!GSTile.IsHouseTile(t)) continue;
					local f = null, r = null, h = null;
					foreach (n in this.Nearby(build, t, 3)) {
						if (f == null && GSTile.IsBuildable(n) && GSTile.GetSlope(n) == GSTile.SLOPE_FLAT) f = n;
						if (r == null && GSRoad.IsRoadTile(n) && !GSTile.IsStationTile(n)) r = n;
						if (h == null && GSTile.IsHouseTile(n) && GSTown.GetLocation(town) != n && GSTile.GetDistanceManhattanToTile(n, t) > 2) h = n;
					}
					if (f == null || r == null || h == null) continue;
					house = t;
					free_tile = f;
					road_tile = r;
					other_house = h;
					break;
				}
				report.check_true("house_site_found", house != null, "Expected a house with free land, road and another house nearby.");

				if (house != null) {
					/* Another company's rail next to the station. */
					local other_mode = GSCompanyMode(other);
					GSCompany.ChangeBankBalance(other, 10000000, GSCompany.EXPENSES_OTHER, GSMap.TILE_INVALID);
					local railtypes = GSRailTypeList();
					GSRail.SetCurrentRailType(railtypes.Begin());
					report.check_true("other_rail_built", GSRail.BuildRailTrack(free_tile, GSRail.RAILTRACK_NE_SW), "The other company should build rail on the free tile.");
					other_mode = null;

					local company_mode = GSCompanyMode(company);
					report.check_true("station_built", metro.build_station_x(house, depth), "Underground station should be built under the house.");
					local station_id = GSMetro.GetUndergroundStationID(house, depth);
					report.check_true("station_valid", GSStation.IsValidStation(station_id), "Underground station should resolve to a station id.");

					/* Cost estimate (Shift) must include the demolition and match the real cost. */
					local rating_before = GSTown.GetDetailedRating(town, company);
					local test_mode = GSTestMode();
					local estimate_acc = GSAccounting();
					local estimate_ok = GSMetro.BuildExitSurface(house, station_id, GSMetro.EXIT_PAVILION);
					local estimate = estimate_acc.GetCosts();
					estimate_acc = null;
					test_mode = null;
					report.check_true("estimate_succeeds", estimate_ok, "Estimating a pavilion on a house should succeed.");
					report.check_true("house_kept_by_estimate", GSTile.IsHouseTile(house), "The estimate must not demolish the house.");

					local acc = GSAccounting();
					local built = GSMetro.BuildExitSurface(house, station_id, GSMetro.EXIT_PAVILION);
					local cost = acc.GetCosts();
					acc = null;
					report.check_true("pavilion_on_house_built", built, "A pavilion on a house tile should demolish the house and be built.");
					report.check_true("house_gone", !GSTile.IsHouseTile(house), "The house should be demolished.");
					report.check_true("pavilion_is_station", GSTile.IsStationTile(house) && GSStation.GetStationID(house) == station_id, "The pavilion should be a tile of the underground station.");
					report.check_true("station_has_exit", GSMetro.HasSurfaceExit(station_id), "The station should report its surface exit.");
					report.check_eq("estimate_matches_cost", estimate, cost, "The estimate should equal the real cost.");
					local rating_after = GSTown.GetDetailedRating(town, company);
					report.check_true("rating_lowered", rating_after < rating_before, "Demolishing a house should lower the town rating.");

					/* Plain free tile for comparison: a house costs more than that. */
					local free_cmp = null;
					foreach (n in this.Nearby(build, house, 3)) {
						if (GSTile.IsBuildable(n) && GSTile.GetSlope(n) == GSTile.SLOPE_FLAT && !GSTile.IsHouseTile(n)) { free_cmp = n; break; }
					}
					if (free_cmp != null) {
						local tm = GSTestMode();
						local cmp_acc = GSAccounting();
						GSMetro.BuildExitSurface(free_cmp, station_id, GSMetro.EXIT_PAVILION);
						local plain = cmp_acc.GetCosts();
						cmp_acc = null;
						tm = null;
						report.metric("plain_pavilion_cost", plain);
						report.check_true("house_costs_more", cost > plain, "A pavilion on a house should include the demolition cost.");
					}

					/* Unclearable tiles still refuse the pavilion. */
					report.check_true("other_rail_refused", !GSMetro.BuildExitSurface(free_tile, station_id, GSMetro.EXIT_PAVILION), "A pavilion must not demolish another company's rail.");
					report.check_true("other_rail_kept", GSRail.IsRailTile(free_tile) && GSTile.GetOwner(free_tile) == other, "The other company's rail should stay.");
					report.check_true("road_refused", !GSMetro.BuildExitSurface(road_tile, station_id, GSMetro.EXIT_PAVILION), "A pavilion must not demolish a road.");
					report.check_true("road_kept", GSRoad.IsRoadTile(road_tile), "The road should stay.");

					/* With a bad rating the town authority refuses to demolish. */
					company_mode = null;
					GSTown.ChangeRating(town, company, -2000);
					company_mode = GSCompanyMode(company);
					report.check_true("refused_by_authority", !GSMetro.BuildExitSurface(other_house, station_id, GSMetro.EXIT_PAVILION), "The town authority should refuse demolition at a bad rating.");
					report.check_true("other_house_kept", GSTile.IsHouseTile(other_house), "The refused house should stay.");

					report.metric("house_tile", house);
					report.metric("pavilion_cost", cost);
					report.metric("pavilion_estimate", estimate);
					report.metric("rating_before", rating_before);
					report.metric("rating_after", rating_after);
					company_mode = null;
				}
			}
		} catch (e) {
			report.check_true("unexpected_exception", false, e);
		}

		report.check_true("final_save_written", GSTest.SaveGame("metro_exit_pavilion_on_house_final.sav"), "Scenario should save the final world for manual inspection.");
		report.finish();
	}
}
