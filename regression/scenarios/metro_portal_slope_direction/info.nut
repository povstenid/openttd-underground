class MetroPortalSlopeDirection extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_portal_slope_direction"; }
	function GetShortName()   { return "MPSD"; }
	function GetDescription() { return "GS metro regression: a portal across an incline is refused instead of silently turned to face the incline."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-25"; }
	function CreateInstance() { return "MetroPortalSlopeDirection"; }
}
RegisterGS(MetroPortalSlopeDirection());
