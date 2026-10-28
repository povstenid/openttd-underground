class MetroPortalRules extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_portal_rules"; }
	function GetShortName()   { return "MPRL"; }
	function GetDescription() { return "A metro portal tile only carries the portal's own axis track, on the surface and at the portal's level."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-19"; }
	function CreateInstance() { return "MetroPortalRules"; }
}

RegisterGS(MetroPortalRules());
