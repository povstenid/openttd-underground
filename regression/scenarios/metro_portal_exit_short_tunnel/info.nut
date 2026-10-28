class MetroPortalExitShortTunnel extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_portal_exit_short_tunnel"; }
	function GetShortName()   { return "MPST"; }
	function GetDescription() { return "GS metro regression: a train longer than a short tunnel does not come out into a train standing in front of the exit mouth (block and path signals)."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-25"; }
	function CreateInstance() { return "MetroPortalExitShortTunnel"; }
}
RegisterGS(MetroPortalExitShortTunnel());
