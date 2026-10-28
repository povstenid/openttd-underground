class MetroPortalExitBlock extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_portal_exit_block"; }
	function GetShortName()   { return "MPEB"; }
	function GetDescription() { return "GS metro regression: a train coming out of a portal waits while another train occupies the surface block in front of it."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-20"; }
	function CreateInstance() { return "MetroPortalExitBlock"; }
}
RegisterGS(MetroPortalExitBlock());
