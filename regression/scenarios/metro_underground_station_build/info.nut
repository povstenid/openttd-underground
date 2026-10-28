class MetroUndergroundStationBuild extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_underground_station_build"; }
	function GetShortName()   { return "MUSB"; }
	function GetDescription() { return "GS smoke test for underground metro station construction."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-03-19"; }
	function CreateInstance() { return "MetroUndergroundStationBuild"; }
}

RegisterGS(MetroUndergroundStationBuild());
