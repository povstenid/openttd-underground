class MetroConvertRail extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_convert_rail"; }
	function GetShortName()   { return "MCRT"; }
	function GetDescription() { return "GS metro regression: converting the rail type of underground track, platforms, ramps and portals in an area."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-19"; }
	function CreateInstance() { return "MetroConvertRail"; }
}

RegisterGS(MetroConvertRail());
