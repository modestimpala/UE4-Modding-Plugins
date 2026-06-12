#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FModPackagerModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Registers the Content Browser folder right-click "Package Mod" entry. */
	void RegisterContentBrowserMenu();
	void UnregisterContentBrowserMenu();

	/** Delegate handle for the path-view context menu extender we install. */
	FDelegateHandle PathViewExtenderHandle;
};
