class MetroReverseNoCrash extends GSInfo {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "metro_reverse_no_crash"; }
	function GetShortName()   { return "MRNC"; }
	function GetDescription() { return "GS metro scenario: dead-end reversal underground and explicit ReverseVehicle while underground must not crash or strand the train."; }
	function GetVersion()     { return 1; }
	function GetAPIVersion()  { return "16"; }
	function GetDate()        { return "2026-06-09"; }
	function CreateInstance() { return "MetroReverseNoCrash"; }
}

RegisterGS(MetroReverseNoCrash());
