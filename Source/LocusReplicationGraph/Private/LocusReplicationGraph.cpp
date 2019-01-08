// Fill out your copyright notice in the Description page of Project Settings.

#include "LocusReplicationGraph.h"
#include "Engine/LevelScriptActor.h"
#include "Runtime/Engine/Classes/Kismet/KismetSystemLibrary.h"

#if WITH_GAMEPLAY_DEBUGGER
#include "GameplayDebuggerCategoryReplicator.h"
#endif

DEFINE_LOG_CATEGORY(LogLocusReplicationGraph);


ULocusReplicationGraph::ULocusReplicationGraph()
{
	ReplicationConnectionManagerClass = ULocusReplicationConnectionGraph::StaticClass();

	FClassReplicationInfoBP PawnClassRepInfo;
	PawnClassRepInfo.Class = APawn::StaticClass();
	PawnClassRepInfo.DistancePriorityScale = 1.f;
	PawnClassRepInfo.StarvationPriorityScale = 1.f;
	PawnClassRepInfo.ActorChannelFrameTimeout = 4;
	//small size of cull distance squard leads inconsistant cull becuase of distance between actual character and viewposition
	//keep it bigger than distance between actual pawn and inviewer
	PawnClassRepInfo.CullDistanceSquared = 15000.f * 15000.f;
	ReplicationInfoSettings.Add(PawnClassRepInfo);
}

void InitClassReplicationInfo(FClassReplicationInfo& Info, UClass* Class, bool bSpatialize, float ServerMaxTickRate)
{
	AActor* CDO = Class->GetDefaultObject<AActor>();
	if (bSpatialize)
	{
		Info.CullDistanceSquared = CDO->NetCullDistanceSquared;
		UE_LOG(LogLocusReplicationGraph, Log, TEXT("Setting cull distance for %s to %f (%f)"), *Class->GetName(), Info.CullDistanceSquared, FMath::Sqrt(Info.CullDistanceSquared));
	}

	Info.ReplicationPeriodFrame = FMath::Max<uint32>((uint32)FMath::RoundToFloat(ServerMaxTickRate / CDO->NetUpdateFrequency), 1);

	UClass* NativeClass = Class;
	while (!NativeClass->IsNative() && NativeClass->GetSuperClass() && NativeClass->GetSuperClass() != AActor::StaticClass())
	{
		NativeClass = NativeClass->GetSuperClass();
	}

	UE_LOG(LogLocusReplicationGraph, Log, TEXT("Setting replication period for %s (%s) to %d frames (%.2f)"), *Class->GetName(), *NativeClass->GetName(), Info.ReplicationPeriodFrame, CDO->NetUpdateFrequency);
}

const UClass* GetParentNativeClass(const UClass* Class)
{
	while (Class && !Class->IsNative())
	{
		Class = Class->GetSuperClass();
	}

	return Class;
}

void ULocusReplicationGraph::InitGlobalActorClassSettings()
{
	Super::InitGlobalActorClassSettings();

	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	// Programatically build the rules.
	// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------

	auto AddInfo = [&](UClass* Class, EClassRepNodeMapping Mapping) { ClassRepNodePolicies.Set(Class, Mapping); };

	AddInfo(AReplicationGraphDebugActor::StaticClass(),			EClassRepNodeMapping::NotRouted);				// Not needed. Replicated special case inside RepGraph
	AddInfo(AInfo::StaticClass(),								EClassRepNodeMapping::RelevantAllConnections);	// Non spatialized, relevant to all
	AddInfo(ALevelScriptActor::StaticClass(),					EClassRepNodeMapping::NotRouted);				// Not needed
#if WITH_GAMEPLAY_DEBUGGER
	AddInfo(AGameplayDebuggerCategoryReplicator::StaticClass(), EClassRepNodeMapping::RelevantOwnerConnection);	// Only owner connection viable
#endif

	for (FClassReplicationPolicyBP PolicyBP : ReplicationPolicySettings)
	{
		if (PolicyBP.Class)
		{
			AddInfo(PolicyBP.Class, PolicyBP.Policy);
		}
	}

	//this does contains all replicated class except GetIsReplicated is false actor
	//if someone need to make replication work, mark it as replicated and control it over replication graph
	TArray<UClass*> AllReplicatedClasses;

	//Iterate all class
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		AActor* ActorCDO = Cast<AActor>(Class->GetDefaultObject());

		if (!ActorCDO || !ActorCDO->GetIsReplicated())
		{
			continue;
		}

		// Skip SKEL and REINST classes.
		if (Class->GetName().StartsWith(TEXT("SKEL_")) || Class->GetName().StartsWith(TEXT("REINST_")))
		{
			continue;
		}

		// --------------------------------------------------------------------
		// This is a replicated class. Save this off for the second pass below
		// --------------------------------------------------------------------

		AllReplicatedClasses.Add(Class);

		// Skip if already in the map (added explicitly)
		if (ClassRepNodePolicies.Contains(Class, false))
		{
			continue;
		}

		auto ShouldSpatialize = [](const AActor* CDO)
		{
			return CDO->GetIsReplicated() && (!(CDO->bAlwaysRelevant || CDO->bOnlyRelevantToOwner || CDO->bNetUseOwnerRelevancy));
		};

		auto GetLegacyDebugStr = [](const AActor* CDO)
		{
			return FString::Printf(TEXT("%s [%d/%d/%d]"), *CDO->GetClass()->GetName(), CDO->bAlwaysRelevant, CDO->bOnlyRelevantToOwner, CDO->bNetUseOwnerRelevancy);
		};

		// Only handle this class if it differs from its super. There is no need to put every child class explicitly in the graph class mapping
		UClass* SuperClass = Class->GetSuperClass();
		if (AActor* SuperCDO = Cast<AActor>(SuperClass->GetDefaultObject()))
		{
			if (SuperCDO->GetIsReplicated() == ActorCDO->GetIsReplicated()
				&& SuperCDO->bAlwaysRelevant == ActorCDO->bAlwaysRelevant
				&&	SuperCDO->bOnlyRelevantToOwner == ActorCDO->bOnlyRelevantToOwner
				&&	SuperCDO->bNetUseOwnerRelevancy == ActorCDO->bNetUseOwnerRelevancy
				)
			{
				//same settings with superclass, ignore this class
				continue;
			}
		}

		if (ShouldSpatialize(ActorCDO))
		{
			AddInfo(Class, EClassRepNodeMapping::Spatialize_Dynamic);
		}
		else if (ActorCDO->bAlwaysRelevant && !ActorCDO->bOnlyRelevantToOwner)
		{
			AddInfo(Class, EClassRepNodeMapping::RelevantAllConnections);
		}
		else if (ActorCDO->bOnlyRelevantToOwner)
		{
			AddInfo(Class, EClassRepNodeMapping::RelevantOwnerConnection);
		}

		//TODO:: currently missing feature, !bAlwaysRelevant && bOnlyRelevantToOwner -> only owner see this but is spatialized
	}

	TArray<FClassReplicationInfoBP> ValidClassReplicationInfoPreset;
	//custom setting
	for (FClassReplicationInfoBP& ReplicationInfoBP : ReplicationInfoSettings)
	{
		if (ReplicationInfoBP.Class)
		{
			GlobalActorReplicationInfoMap.SetClassInfo(ReplicationInfoBP.Class, ReplicationInfoBP.CreateClassReplicationInfo());
			ValidClassReplicationInfoPreset.Add(ReplicationInfoBP);
		}
	}

	UReplicationGraphNode_ActorListFrequencyBuckets::DefaultSettings.ListSize = 12;

	// Set FClassReplicationInfo based on legacy settings from all replicated classes
	for (UClass* ReplicatedClass : AllReplicatedClasses)
	{
		if (FClassReplicationInfoBP* Preset = ValidClassReplicationInfoPreset.FindByPredicate([&](const FClassReplicationInfoBP& Info) { return ReplicatedClass->IsChildOf(Info.Class.Get());  }))
		{
			//duplicated or set included child will be ignored
			if (Preset->Class.Get() == ReplicatedClass || Preset->IncludeChildClass)
			{
				continue;
			}
		}

		const bool bClassIsSpatialized = IsSpatialized(ClassRepNodePolicies.GetChecked(ReplicatedClass));

		FClassReplicationInfo ClassInfo;
		InitClassReplicationInfo(ClassInfo, ReplicatedClass, bClassIsSpatialized, NetDriver->NetServerMaxTickRate);
		GlobalActorReplicationInfoMap.SetClassInfo(ReplicatedClass, ClassInfo);
	}


	// Print out what we came up with
	UE_LOG(LogLocusReplicationGraph, Log, TEXT(""));
	UE_LOG(LogLocusReplicationGraph, Log, TEXT("Class Routing Map: "));
	UEnum* Enum = FindObject<UEnum>(ANY_PACKAGE, TEXT("EClassRepNodeMapping"));
	for (auto ClassMapIt = ClassRepNodePolicies.CreateIterator(); ClassMapIt; ++ClassMapIt)
	{
		const UClass* Class = CastChecked<UClass>(ClassMapIt.Key().ResolveObjectPtr());
		const EClassRepNodeMapping Mapping = ClassMapIt.Value();

		// Only print if different than native class
		const UClass* ParentNativeClass = GetParentNativeClass(Class);
		const EClassRepNodeMapping* ParentMapping = ClassRepNodePolicies.Get(ParentNativeClass);
		if (ParentMapping && Class != ParentNativeClass && Mapping == *ParentMapping)
		{
			continue;
		}

		UE_LOG(LogLocusReplicationGraph, Log, TEXT("  %s (%s) -> %s"), *Class->GetName(), *GetNameSafe(ParentNativeClass), *Enum->GetNameStringByValue(static_cast<uint32>(Mapping)));
	}

	UE_LOG(LogLocusReplicationGraph, Log, TEXT(""));
	UE_LOG(LogLocusReplicationGraph, Log, TEXT("Class Settings Map: "));
	FClassReplicationInfo LocusValues;
	for (auto ClassRepInfoIt = GlobalActorReplicationInfoMap.CreateClassMapIterator(); ClassRepInfoIt; ++ClassRepInfoIt)
	{
		const UClass* Class = CastChecked<UClass>(ClassRepInfoIt.Key().ResolveObjectPtr());
		const FClassReplicationInfo& ClassInfo = ClassRepInfoIt.Value();
		UE_LOG(LogLocusReplicationGraph, Log, TEXT("  %s (%s) -> %s"), *Class->GetName(), *GetNameSafe(GetParentNativeClass(Class)), *ClassInfo.BuildDebugStringDelta());
	}


	// Rep destruct infos based on CVar value
	DestructInfoMaxDistanceSquared = DestructionInfoMaxDistance * DestructionInfoMaxDistance;

#if WITH_GAMEPLAY_DEBUGGER
	AGameplayDebuggerCategoryReplicator::NotifyDebuggerOwnerChange.AddUObject(this, &ULocusReplicationGraph::OnGameplayDebuggerOwnerChange);
#endif
}

void ULocusReplicationGraph::InitGlobalGraphNodes()
{
	// Preallocate some replication lists.
	PreAllocateRepList(3, 12);
	PreAllocateRepList(6, 12);
	PreAllocateRepList(128, 64);
	PreAllocateRepList(512, 16);

	// -----------------------------------------------
	//	Spatial Actors
	// -----------------------------------------------

	GridNode = CreateNewNode<UReplicationGraphNode_GridSpatialization2D>();
	GridNode->CellSize = SpacialCellSize;
	GridNode->SpatialBias = SpatialBias;

	if (!EnableSpatialRebuilds)
	{
		GridNode->AddSpatialRebuildBlacklistClass(AActor::StaticClass()); // Disable All spatial rebuilding
	}

	AddGlobalGraphNode(GridNode);

	// -----------------------------------------------
	//	Always Relevant (to everyone) Actors
	// -----------------------------------------------
	AlwaysRelevantNode = CreateNewNode<UReplicationGraphNode_AlwaysRelevant_WithPending>();
	AddGlobalGraphNode(AlwaysRelevantNode);
}

void ULocusReplicationGraph::InitConnectionGraphNodes(UNetReplicationGraphConnection* RepGraphConnection)
{
	Super::InitConnectionGraphNodes(RepGraphConnection);

	//UE_LOG(LogLocusReplicationGraph, Warning, TEXT("InitConnection : %s"), *RepGraphConnection->NetConnection->PlayerController->GetName());
	ULocusReplicationConnectionGraph* LocusConnManager = Cast<ULocusReplicationConnectionGraph>(RepGraphConnection);
	if (!LocusConnManager)
	{
		UE_LOG(LogLocusReplicationGraph, Warning, TEXT("Unrecognized ConnectionDriver class, Expected ULocusReplicationConnectionGraph"));
	}

	LocusConnManager->AlwaysRelevantForConnectionNode = CreateNewNode<UReplicationGraphNode_AlwaysRelevant_ForConnection>();
	AddConnectionGraphNode(LocusConnManager->AlwaysRelevantForConnectionNode, RepGraphConnection);

	LocusConnManager->TeamConnectionNode = CreateNewNode<UReplicationGraphNode_AlwaysRelevant_ForTeam>();
	AddConnectionGraphNode(LocusConnManager->TeamConnectionNode, RepGraphConnection);

	//don't care about team names as it's initial value is always  NAME_None
}

void ULocusReplicationGraph::OnRemoveConnectionGraphNodes(UNetReplicationGraphConnection* RepGraphConnection)
{
	ULocusReplicationConnectionGraph* LocusConnManager = Cast<ULocusReplicationConnectionGraph>(RepGraphConnection);
	if (LocusConnManager)
	{
		if (LocusConnManager->TeamName != NAME_None)
		{
			TeamConnectionListMap.RemoveConnectionFromTeam(LocusConnManager->TeamName, LocusConnManager);
		}
	}
}

void ULocusReplicationGraph::RemoveClientConnection(UNetConnection* NetConnection)
{
	//we completely override super function

	int32 ConnectionId = 0;
	bool bFound = false;

	// Remove the RepGraphConnection associated with this NetConnection. Also update ConnectionIds to stay compact.
	auto UpdateList = [&](TArray<UNetReplicationGraphConnection*> List)
	{
		for (int32 idx = 0; idx < Connections.Num(); ++idx)
		{
			UNetReplicationGraphConnection* ConnectionManager = Connections[idx];
			repCheck(ConnectionManager);

			if (ConnectionManager->NetConnection == NetConnection)
			{
				ensure(!bFound);
				//Nofity this to handle something - remove from team list
				OnRemoveConnectionGraphNodes(ConnectionManager);
				Connections.RemoveAtSwap(idx, 1, false);
				bFound = true;
			}
			else
			{
				ConnectionManager->ConnectionId = ConnectionId++;
			}
		}
	};

	UpdateList(Connections);
	UpdateList(PendingConnections);

	if (!bFound)
	{
		// At least one list should have found the connection
		UE_LOG(LogLocusReplicationGraph, Warning, TEXT("UReplicationGraph::RemoveClientConnection could not find connection in Connection (%d) or PendingConnections (%d) lists"), *GetNameSafe(NetConnection), Connections.Num(), PendingConnections.Num());
	}
}

void ULocusReplicationGraph::RouteAddNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo, FGlobalActorReplicationInfo& GlobalInfo)
{
	EClassRepNodeMapping Policy = GetMappingPolicy(ActorInfo.Class);
	switch (Policy)
	{
	case EClassRepNodeMapping::NotRouted:
	{
		break;
	}

	case EClassRepNodeMapping::RelevantAllConnections:
	{
		AlwaysRelevantNode->NotifyAddNetworkActor(ActorInfo);
		break;
	}

	case EClassRepNodeMapping::RelevantOwnerConnection:
	case EClassRepNodeMapping::RelevantTeamConnection:
	{
		RouteAddNetworkActorToConnectionNodes(Policy, ActorInfo, GlobalInfo);
		break;
	}

	case EClassRepNodeMapping::Spatialize_Static:
	{
		GridNode->AddActor_Static(ActorInfo, GlobalInfo);
		break;
	}

	case EClassRepNodeMapping::Spatialize_Dynamic:
	{
		GridNode->AddActor_Dynamic(ActorInfo, GlobalInfo);
		break;
	}

	case EClassRepNodeMapping::Spatialize_Dormancy:
	{
		GridNode->AddActor_Dormancy(ActorInfo, GlobalInfo);
		break;
	}
	};
}

void ULocusReplicationGraph::RouteRemoveNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo)
{
	EClassRepNodeMapping Policy = GetMappingPolicy(ActorInfo.Class);

	switch (Policy)
	{
	case EClassRepNodeMapping::NotRouted:
	{
		break;
	}

	case EClassRepNodeMapping::RelevantAllConnections:
	{
		AlwaysRelevantNode->NotifyRemoveNetworkActor(ActorInfo);
		break;
	}

	case EClassRepNodeMapping::RelevantOwnerConnection:
	case EClassRepNodeMapping::RelevantTeamConnection:
	{
		RouteRemoveNetworkActorToConnectionNodes(Policy, ActorInfo);
		break;
	}

	case EClassRepNodeMapping::Spatialize_Static:
	{
		GridNode->RemoveActor_Static(ActorInfo);
		break;
	}

	case EClassRepNodeMapping::Spatialize_Dynamic:
	{
		GridNode->RemoveActor_Dynamic(ActorInfo);
		break;
	}

	case EClassRepNodeMapping::Spatialize_Dormancy:
	{
		GridNode->RemoveActor_Dormancy(ActorInfo);
		break;
	}
	};
}

//this function will be called seamless map transition
//as all actors will be removed in silly order, we have to deal with it
void ULocusReplicationGraph::ResetGameWorldState()
{
	Super::ResetGameWorldState();

	//all actor will be destroyed. just reset it.
	PendingConnectionActors.Reset();
	PendingTeamRequests.Reset();

	auto EmptyConnectionNode = [](TArray<UNetReplicationGraphConnection*>& Connections)
	{
		for (UNetReplicationGraphConnection* ConnManager : Connections)
		{
			if (ULocusReplicationConnectionGraph* LocusConnManager = Cast<ULocusReplicationConnectionGraph>(ConnManager))
			{
				LocusConnManager->AlwaysRelevantForConnectionNode->NotifyResetAllNetworkActors();
			}
		}
	};

	EmptyConnectionNode(PendingConnections);
	EmptyConnectionNode(Connections);

	//as connection does not destroyed, we keep it
	//TeamConnectionListMap.Reset();
}

// Since we listen to global (static) events, we need to watch out for cross world broadcasts (PIE)
#if WITH_EDITOR
#define CHECK_WORLDS(X) if(X->GetWorld() != GetWorld()) return;
#else
#define CHECK_WORLDS(X)
#endif

void ULocusReplicationGraph::AddDependentActor(AActor* ReplicatorActor, AActor* DependentActor)
{
	if (ReplicatorActor && DependentActor)
	{
		CHECK_WORLDS(ReplicatorActor);

		FGlobalActorReplicationInfo& ActorInfo = GlobalActorReplicationInfoMap.Get(ReplicatorActor);
		ActorInfo.DependentActorList.PrepareForWrite();

		if (!ActorInfo.DependentActorList.Contains(DependentActor))
		{
			ActorInfo.DependentActorList.Add(DependentActor);
		}
	}
}

void ULocusReplicationGraph::RemoveDependentActor(AActor* ReplicatorActor, AActor* DependentActor)
{
	if (ReplicatorActor && DependentActor)
	{
		CHECK_WORLDS(ReplicatorActor);

		FGlobalActorReplicationInfo& ActorInfo = GlobalActorReplicationInfoMap.Get(ReplicatorActor);
		ActorInfo.DependentActorList.PrepareForWrite();
		ActorInfo.DependentActorList.Remove(DependentActor);
	}
}

void ULocusReplicationGraph::ChangeOwnerOfAnActor(AActor* ActorToChange, AActor* NewOwner)
{
	EClassRepNodeMapping Policy = GetMappingPolicy(ActorToChange->GetClass());
	if (!ActorToChange || Policy == EClassRepNodeMapping::NotRouted || IsSpatialized(Policy))
	{
		//Policy doesn't matter for chaning owner
		return;
	}

	//remove from previous connection specific nodes.
	RouteRemoveNetworkActorToConnectionNodes(Policy, FNewReplicatedActorInfo(ActorToChange));

	//change owner safely
	ActorToChange->SetOwner(NewOwner);

	//re-route to connection specific nodes with new owner
	FGlobalActorReplicationInfo& GlobalInfo = GlobalActorReplicationInfoMap.Get(ActorToChange);
	RouteAddNetworkActorToConnectionNodes(Policy, FNewReplicatedActorInfo(ActorToChange), GlobalInfo);
}

void ULocusReplicationGraph::SetTeamForPlayerController(APlayerController* PlayerController, FName NextTeam)
{
	if (PlayerController)
	{
		if (ULocusReplicationConnectionGraph* ConnManager = FindLocusConnectionGraph(PlayerController))
		{
			FName CurrentTeam = ConnManager->TeamName;
			if (CurrentTeam != NextTeam)
			{
				if (CurrentTeam != NAME_None)
				{
					TeamConnectionListMap.RemoveConnectionFromTeam(CurrentTeam, ConnManager);
				}

				if (NextTeam != NAME_None)
				{
					TeamConnectionListMap.AddConnectionToTeam(NextTeam, ConnManager);
				}
				ConnManager->TeamName = NextTeam;
			}
		}
		else
		{
			PendingTeamRequests.Emplace(NextTeam, PlayerController);
		}
	}
}

void ULocusReplicationGraph::RouteAddNetworkActorToConnectionNodes(EClassRepNodeMapping Policy, const FNewReplicatedActorInfo& ActorInfo, FGlobalActorReplicationInfo& GlobalInfo)
{
	if (ULocusReplicationConnectionGraph* ConnManager = FindLocusConnectionGraph(ActorInfo.GetActor()))
	{
		switch (Policy)
		{
		case EClassRepNodeMapping::RelevantOwnerConnection:
		{
			ConnManager->AlwaysRelevantForConnectionNode->NotifyAddNetworkActor(ActorInfo);
			break;
		}
		case EClassRepNodeMapping::RelevantTeamConnection:
		{
			ConnManager->TeamConnectionNode->NotifyAddNetworkActor(ActorInfo);
			break;
		}
		};
	}
	else if(ActorInfo.Actor->GetNetOwner())
	{
		//this actor is not yet ready. add to pending array to handle pending route
		PendingConnectionActors.Add(ActorInfo.GetActor());
	}
}


void ULocusReplicationGraph::RouteRemoveNetworkActorToConnectionNodes(EClassRepNodeMapping Policy, const FNewReplicatedActorInfo& ActorInfo)
{
	if (ULocusReplicationConnectionGraph* ConnManager = FindLocusConnectionGraph(ActorInfo.GetActor()))
	{
		switch (Policy)
		{
		case EClassRepNodeMapping::RelevantOwnerConnection:
		{
			ConnManager->AlwaysRelevantForConnectionNode->NotifyRemoveNetworkActor(ActorInfo);
			break;
		}
		case EClassRepNodeMapping::RelevantTeamConnection:
		{
			ConnManager->TeamConnectionNode->NotifyRemoveNetworkActor(ActorInfo);
			break;
		}
		};
	}
	else if (ActorInfo.Actor->GetNetOwner())
	{
		//this actor is not yet ready. but doesn't matter the pending array contains the actor or not
		PendingConnectionActors.Remove(ActorInfo.GetActor());
	}
}

void ULocusReplicationGraph::HandlePendingActorsAndTeamRequests()
{
	if(PendingTeamRequests.Num() > 0)
	{
		TArray<FTeamRequest> TempRequests = MoveTemp(PendingTeamRequests);

		for (FTeamRequest& Request : TempRequests)
		{
			if (Request.Requestor && Request.Requestor->IsValidLowLevel())
			{
				//if failed, it will automatically re-added to pending list
				SetTeamForPlayerController(Request.Requestor, Request.TeamName);
			}
		}
	}

	if (PendingConnectionActors.Num() > 0)
	{
		TArray<AActor*> TempActors = MoveTemp(PendingConnectionActors);

		for (AActor* Actor : TempActors)
		{
			if (Actor && Actor->IsValidLowLevel())
			{
				if (UNetConnection* Connection = Actor->GetNetConnection())
				{
					//if failed, it will automatically re-added to pending list
					EClassRepNodeMapping Policy = GetMappingPolicy(Actor->GetClass());
					FGlobalActorReplicationInfo& GlobalInfo = GlobalActorReplicationInfoMap.Get(Actor);
					RouteAddNetworkActorToConnectionNodes(Policy, FNewReplicatedActorInfo(Actor), GlobalInfo);
				}
			}
		}
	}
}


class ULocusReplicationConnectionGraph* ULocusReplicationGraph::FindLocusConnectionGraph(const AActor* Actor)
{
	if (Actor)
	{
		if (UNetConnection* NetConnection = Actor->GetNetConnection())
		{
			if (ULocusReplicationConnectionGraph* ConnManager = Cast<ULocusReplicationConnectionGraph>(FindOrAddConnectionManager(NetConnection)))
			{
				return ConnManager;
			}
		}
	}
	return nullptr;
}

#if WITH_GAMEPLAY_DEBUGGER
void ULocusReplicationGraph::OnGameplayDebuggerOwnerChange(AGameplayDebuggerCategoryReplicator* Debugger, APlayerController* OldOwner)
{
	if (ULocusReplicationConnectionGraph* ConnManager = FindLocusConnectionGraph(OldOwner))
	{
		FNewReplicatedActorInfo ActorInfo(Debugger);
		ConnManager->AlwaysRelevantForConnectionNode->NotifyRemoveNetworkActor(ActorInfo);
	}

	if (ULocusReplicationConnectionGraph* ConnManager = FindLocusConnectionGraph(Debugger->GetReplicationOwner()))
	{
		FNewReplicatedActorInfo ActorInfo(Debugger);
		ConnManager->AlwaysRelevantForConnectionNode->NotifyAddNetworkActor(ActorInfo);
	}
}
#endif

void ULocusReplicationGraph::PrintRepNodePolicies()
{
	UEnum* Enum = FindObject<UEnum>(ANY_PACKAGE, TEXT("EClassRepNodeMapping"));
	if (!Enum)
	{
		return;
	}

	GLog->Logf(TEXT("===================================="));
	GLog->Logf(TEXT("Shooter Replication Routing Policies"));
	GLog->Logf(TEXT("===================================="));

	for (auto It = ClassRepNodePolicies.CreateIterator(); It; ++It)
	{
		FObjectKey ObjKey = It.Key();

		EClassRepNodeMapping Mapping = It.Value();

		GLog->Logf(TEXT("%-40s --> %s"), *GetNameSafe(ObjKey.ResolveObjectPtr()), *Enum->GetNameStringByValue(static_cast<uint32>(Mapping)));
	}
}

EClassRepNodeMapping ULocusReplicationGraph::GetMappingPolicy(const UClass* Class)
{
	EClassRepNodeMapping* PolicyPtr = ClassRepNodePolicies.Get(Class);
	EClassRepNodeMapping Policy = PolicyPtr ? *PolicyPtr : EClassRepNodeMapping::NotRouted;
	return Policy;
}

void UReplicationGraphNode_AlwaysRelevant_ForTeam::GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params)
{
	ULocusReplicationConnectionGraph* LocusConnManager = Cast<ULocusReplicationConnectionGraph>(&Params.ConnectionManager);
	if (LocusConnManager && LocusConnManager->TeamName != NAME_None)
	{
		ULocusReplicationGraph* ReplicationGraph = Cast<ULocusReplicationGraph>(GetOuter());
		if (TArray<ULocusReplicationConnectionGraph*>* TeamConnections = ReplicationGraph->TeamConnectionListMap.GetConnectionArrayForTeam(LocusConnManager->TeamName))
		{
			for (ULocusReplicationConnectionGraph* TeamMember : *TeamConnections)
			{
				//we call parent 
				TeamMember->TeamConnectionNode->GatherActorListsForConnectionDefault(Params);
			}
		}
	}
	else
	{
		Super::GatherActorListsForConnection(Params);
	}
}

UReplicationGraphNode_AlwaysRelevant_WithPending::UReplicationGraphNode_AlwaysRelevant_WithPending()
{
	bRequiresPrepareForReplicationCall = true;
}

void UReplicationGraphNode_AlwaysRelevant_WithPending::PrepareForReplication()
{
	ULocusReplicationGraph* ReplicationGraph = Cast<ULocusReplicationGraph>(GetOuter());
	ReplicationGraph->HandlePendingActorsAndTeamRequests();
}

void UReplicationGraphNode_AlwaysRelevant_ForTeam::GatherActorListsForConnectionDefault(const FConnectionGatherActorListParameters& Params)
{
	Super::GatherActorListsForConnection(Params);
}

TArray<class ULocusReplicationConnectionGraph*>* FTeamConnectionListMap::GetConnectionArrayForTeam(FName TeamName)
{
	return Find(TeamName);
}

void FTeamConnectionListMap::AddConnectionToTeam(FName TeamName, ULocusReplicationConnectionGraph* ConnManager)
{
	TArray<class ULocusReplicationConnectionGraph*>& TeamList = FindOrAdd(TeamName);
	TeamList.Add(ConnManager);
}

void FTeamConnectionListMap::RemoveConnectionFromTeam(FName TeamName, ULocusReplicationConnectionGraph* ConnManager)
{
	if (TArray<class ULocusReplicationConnectionGraph*>* TeamList = Find(TeamName))
	{
		TeamList->RemoveSwap(ConnManager);
		//remove team if there's noone left
		if (TeamList->Num() == 0)
		{
			Remove(TeamName);
		}
	}
}


//console commands copied from shooter repgraph
// ------------------------------------------------------------------------------

FAutoConsoleCommandWithWorldAndArgs ShooterPrintRepNodePoliciesCmd(TEXT("LocusRepGraph.PrintRouting"), TEXT("Prints how actor classes are routed to RepGraph nodes"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
{
	for (TObjectIterator<ULocusReplicationGraph> It; It; ++It)
	{
		It->PrintRepNodePolicies();
	}
})
);


FAutoConsoleCommandWithWorldAndArgs ChangeFrequencyBucketsCmd(TEXT("LocusRepGraph.FrequencyBuckets"), TEXT("Resets frequency bucket count."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray< FString >& Args, UWorld* World)
{
	int32 Buckets = 1;
	if (Args.Num() > 0)
	{
		LexTryParseString<int32>(Buckets, *Args[0]);
	}

	UE_LOG(LogLocusReplicationGraph, Display, TEXT("Setting Frequency Buckets to %d"), Buckets);
	for (TObjectIterator<UReplicationGraphNode_ActorListFrequencyBuckets> It; It; ++It)
	{
		UReplicationGraphNode_ActorListFrequencyBuckets* Node = *It;
		Node->SetNonStreamingCollectionSize(Buckets);
	}
}));
