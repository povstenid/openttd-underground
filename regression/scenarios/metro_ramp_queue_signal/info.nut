class MetroRampQueueSignal extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_ramp_queue_signal"; }
	function GetShortName()   { return "MRQS"; }
	function GetDescription() { return "GS metro regression: a train standing on ramps between levels holds them; the next train waits at the signal in front of them."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-10-20"; }
	function CreateInstance() { return "MetroRampQueueSignal"; }
}
RegisterGS(MetroRampQueueSignal());
