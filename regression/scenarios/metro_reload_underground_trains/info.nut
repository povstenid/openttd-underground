class MetroReloadUndergroundTrains extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_reload_underground_trains"; }
	function GetShortName()   { return "MRUT"; }
	function GetDescription() { return "GS metro regression: load a savegame whose trains are mid-route on an underground loop (stale slice IDs must be re-resolved) and verify both trains keep serving surface and underground stations."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-06-10"; }
	function CreateInstance() { return "MetroReloadUndergroundTrains"; }
}

RegisterGS(MetroReloadUndergroundTrains());
