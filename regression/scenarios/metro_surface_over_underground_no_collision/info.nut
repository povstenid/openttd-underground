class MetroSurfaceOverUndergroundNoCollision extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_surface_over_underground_no_collision"; }
	function GetShortName()   { return "MSUC"; }
	function GetDescription() { return "GS metro regression: a surface loop crosses straight over an underground loop of the same company; the trains must never collide across layers."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-18"; }
	function CreateInstance() { return "MetroSurfaceOverUndergroundNoCollision"; }
}

RegisterGS(MetroSurfaceOverUndergroundNoCollision());
