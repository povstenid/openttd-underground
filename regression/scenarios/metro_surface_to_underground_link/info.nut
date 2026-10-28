class MetroSurfaceToUndergroundLink extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_surface_to_underground_link"; }
	function GetShortName()   { return "MSUL"; }
	function GetDescription() { return "GS smoke test for linking surface and underground metro lines."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-03-19"; }
	function CreateInstance() { return "MetroSurfaceToUndergroundLink"; }
}

RegisterGS(MetroSurfaceToUndergroundLink());
