class MetroRegressionPortalLinkPersists extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_regression_portal_link_persists"; }
	function GetShortName()   { return "MRPP"; }
	function GetDescription() { return "GS metro regression: portal connectivity must persist after signal placement and removal."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-03-19"; }
	function CreateInstance() { return "MetroRegressionPortalLinkPersists"; }
}

RegisterGS(MetroRegressionPortalLinkPersists());
