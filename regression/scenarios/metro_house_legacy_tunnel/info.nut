class MetroHouseLegacyTunnel extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_house_legacy_tunnel"; }
	function GetShortName()   { return "MHLT"; }
	function GetDescription() { return "GS metro regression: a tunnel below house foundations from an old savegame stays and keeps working."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-21"; }
	function CreateInstance() { return "MetroHouseLegacyTunnel"; }
}
RegisterGS(MetroHouseLegacyTunnel());
