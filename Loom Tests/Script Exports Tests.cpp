#include "doctest.h"

// Scripts link against the executable's exports; the web build has none.
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)

#include <windows.h>


TEST_CASE("Script exports: the Utilities a script can use are exported from the executable")
{
	// One out-of-line member of each, by its decorated name: what a script's
	// import library asks the executable for.
	const char* const names[] =
	{
		"??0DialogueManager@Loom@@QEAA@XZ",
		"??0SpriteManager@Loom@@QEAA@XZ",
		"?Split@DialogueArgs@Loom@@SA?AV?$vector@V?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@V?$allocator@V?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@@2@@std@@AEBV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@4@@Z",
		"?Continued@DialogueInput@Loom@@SA_NXZ",
		"?Exists@StaticStateMachine@Loom@@SA_NXZ",
		"?Measure@SpriteSheetScanner@Loom@@SA?AV?$vector@HV?$allocator@H@std@@@std@@AEBUPixelSheet@2@AEBUInt2@2@E@Z",
		"?DragFloatN@AxisGui@Loom@@SA_NPEBDPEAMHM0@Z",
		"?Draw@AttributeGui@Loom@@SAXAEAUAttributeOwner@2@@Z",
		"?CopyBases@AttributeFields@Loom@@SAXAEBUAttributeOwner@2@AEAU32@@Z",
		"?Tick@Utilities@Loom@@SAXXZ",
	};

	const HMODULE executable = GetModuleHandleA(nullptr);

	for (const char* name : names)
	{
		CAPTURE(name);
		CHECK(GetProcAddress(executable, name) != nullptr);
	};
};

#endif
