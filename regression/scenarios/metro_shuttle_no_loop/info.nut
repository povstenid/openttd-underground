class MetroShuttleNoLoop extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_shuttle_no_loop"; }
	function GetShortName()   { return "MSNL"; }
	function GetDescription() { return "GS metro regression: a train on a line that is no loop shuttles between a through platform and a terminus."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-20"; }
	function CreateInstance() { return "MetroShuttleNoLoop"; }
}
RegisterGS(MetroShuttleNoLoop());
