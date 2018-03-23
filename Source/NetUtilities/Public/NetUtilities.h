#pragma once

#include "ModuleManager.h"

class FNetUtilitiesModule 
	: public IModuleInterface
{
public:
	void StartupModule() override;
	void ShutdownModule() override;
};