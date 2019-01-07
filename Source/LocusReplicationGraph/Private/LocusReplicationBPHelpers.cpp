// Fill out your copyright notice in the Description page of Project Settings.

#include "LocusReplicationBPHelpers.h"
#include "LocusReplicationGraph.h"

void ULocusReplicationBPHelpers::SetTeamForPlayerController(APlayerController* Player, FName TeamName)
{
	if (ULocusReplicationGraph* LocusGraph = FindLocusReplicationGraph(Player))
	{
		LocusGraph->SetTeamForPlayerController(Player, TeamName);
	}

	UE_LOG(LogLocusReplicationGraph, Warning, TEXT("LocusReplicationGraph not found"));
}

void ULocusReplicationBPHelpers::AddDependentActor(AActor* ReplicatorActor, AActor* DependentActor)
{
	if (ULocusReplicationGraph* LocusGraph = FindLocusReplicationGraph(ReplicatorActor))
	{
		LocusGraph->AddDependentActor(ReplicatorActor, DependentActor);
	}

	UE_LOG(LogLocusReplicationGraph, Warning, TEXT("LocusReplicationGraph not found"));
}

void ULocusReplicationBPHelpers::RemoveDependentActor(AActor* ReplicatorActor, AActor* DependentActor)
{
	if (ULocusReplicationGraph* LocusGraph = FindLocusReplicationGraph(ReplicatorActor))
	{
		LocusGraph->RemoveDependentActor(ReplicatorActor, DependentActor);
	}

	UE_LOG(LogLocusReplicationGraph, Warning, TEXT("LocusReplicationGraph not found"));
}

void ULocusReplicationBPHelpers::ChangeOwnerAndRefreshReplication(AActor* ActorToChange, AActor* NewOwner)
{
	if (ULocusReplicationGraph* LocusGraph = FindLocusReplicationGraph(ActorToChange))
	{
		LocusGraph->ChangeOwnerOfAnActor(ActorToChange, NewOwner);
	}

	UE_LOG(LogLocusReplicationGraph, Warning, TEXT("LocusReplicationGraph not found"));
}

ULocusReplicationGraph* ULocusReplicationBPHelpers::FindLocusReplicationGraph(const UObject* WorldContextObject)
{
	if (WorldContextObject)
	{
		if (UWorld* World = WorldContextObject->GetWorld())
		{
			if (UNetDriver* NetworkDriver = World->GetNetDriver())
			{
				if (ULocusReplicationGraph* LocusGraph = NetworkDriver->GetReplicationDriver<ULocusReplicationGraph>())
				{
					return LocusGraph;
				}
			}
		}
	}

	return nullptr;
}
