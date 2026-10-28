class MetroTunnelBlockedByUnderground extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_tunnel_blocked_by_underground"; }
	function GetShortName()   { return "MTNB"; }
	function GetDescription() { return "GS metro regression: a vanilla tunnel must not cut through underground rail at its level."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-18"; }
	function CreateInstance() { return "MetroTunnelBlockedByUnderground"; }
}

RegisterGS(MetroTunnelBlockedByUnderground());
