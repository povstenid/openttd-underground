class MetroSurfaceStationBuild extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_surface_station_build"; }
	function GetShortName()   { return "MSSB"; }
	function GetDescription() { return "GS smoke test for surface metro station construction."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-03-19"; }
	function CreateInstance() { return "MetroSurfaceStationBuild"; }
}

RegisterGS(MetroSurfaceStationBuild());
