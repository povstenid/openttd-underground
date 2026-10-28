class MetroSignalTwowayTerminus extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_signal_twoway_terminus"; }
	function GetShortName()   { return "MSTT"; }
	function GetDescription() { return "GS metro regression: a terminus behind a two-way passable path signal can be entered and left."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-20"; }
	function CreateInstance() { return "MetroSignalTwowayTerminus"; }
}
RegisterGS(MetroSignalTwowayTerminus());
