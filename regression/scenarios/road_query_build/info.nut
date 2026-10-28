class RoadQueryBuild extends AIInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "road_query_build"; }
	function GetShortName()   { return "RQRY"; }
	function GetDescription() { return "Scenario smoke test for dry-run and real road construction."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-03-18"; }
	function CreateInstance() { return "RoadQueryBuild"; }
	function UseAsRandomAI()  { return false; }
}

RegisterAI(RoadQueryBuild());
