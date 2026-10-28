class TestLib extends GSLibrary {
	function GetAuthor()      { return "OpenTTD Developers"; }
	function GetName()        { return "Structured scenario test helpers"; }
	function GetShortName()   { return "TSTG"; }
	function GetDescription() { return "Helper library for scripted scenario tests with JSON feedback."; }
	function GetVersion()     { return 1; }
	function GetDate()        { return "2026-03-18"; }
	function GetCategory()    { return "test"; }
	function CreateInstance() { return "TestLib"; }
}

RegisterLibrary(TestLib());
