// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

/**
 * The public interface to this module
 */
class ILocusReplicationGraphPlugin : public IModuleInterface
{

public:

	static inline ILocusReplicationGraphPlugin& Get()
	{
		return FModuleManager::LoadModuleChecked< ILocusReplicationGraphPlugin >("LocusReplicationGraph");
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("LocusReplicationGraph");
	}
};
