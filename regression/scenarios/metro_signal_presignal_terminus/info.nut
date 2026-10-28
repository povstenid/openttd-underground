class MetroSignalPresignalTerminus extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_signal_presignal_terminus"; }
	function GetShortName()   { return "MSPT"; }
	function GetDescription() { return "GS metro regression: an entry/exit pre-signal pair sends trains to the free terminus platform."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-20"; }
	function CreateInstance() { return "MetroSignalPresignalTerminus"; }
}
RegisterGS(MetroSignalPresignalTerminus());
