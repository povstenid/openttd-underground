class MetroRegressionSignalRoundtrip extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_regression_signal_roundtrip"; }
	function GetShortName()   { return "MRSR"; }
	function GetDescription() { return "GS metro regression: placing and removing an underground signal must preserve traversable track."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-03-19"; }
	function CreateInstance() { return "MetroRegressionSignalRoundtrip"; }
}

RegisterGS(MetroRegressionSignalRoundtrip());
