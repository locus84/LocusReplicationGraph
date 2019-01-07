// Fill out your copyright notice in the Description page of Project Settings.

#include "LocusReplicationGraphModule.h"

class FLocusReplicationGraphModule : public ILocusReplicationGraphPlugin
{
	/** IModuleInterface implementation */
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
	virtual bool IsGameModule() const override { return true; }
};

IMPLEMENT_GAME_MODULE(FLocusReplicationGraphModule, LocusReplicationGraph)
