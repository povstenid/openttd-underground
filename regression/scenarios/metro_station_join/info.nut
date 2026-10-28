class MetroStationJoin extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_station_join"; }
	function GetShortName()   { return "MSJN"; }
	function GetDescription() { return "GS metro regression: underground platforms join adjacent, distant and other-level stations like vanilla stations."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-19"; }
	function CreateInstance() { return "MetroStationJoin"; }
}

RegisterGS(MetroStationJoin());
