class MetroUndergroundBlockedByTunnel extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_underground_blocked_by_tunnel"; }
	function GetShortName()   { return "MUBT"; }
	function GetDescription() { return "GS metro regression: underground rail must not be built inside a vanilla tunnel."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-18"; }
	function CreateInstance() { return "MetroUndergroundBlockedByTunnel"; }
}

RegisterGS(MetroUndergroundBlockedByTunnel());
