class MetroPortalLinkCurve extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_portal_link_curve"; }
	function GetShortName()   { return "MPLC"; }
	function GetDescription() { return "GS metro regression: the portal registry links portals through curved underground track."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-20"; }
	function CreateInstance() { return "MetroPortalLinkCurve"; }
}
RegisterGS(MetroPortalLinkCurve());
