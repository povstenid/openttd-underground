class MetroRampSignalHold extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_ramp_signal_hold"; }
	function GetShortName()   { return "MRSH"; }
	function GetDescription() { return "GS metro regression: a block signal on a ramp between levels holds a following train while a train stands beyond it."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-20"; }
	function CreateInstance() { return "MetroRampSignalHold"; }
}
RegisterGS(MetroRampSignalHold());
