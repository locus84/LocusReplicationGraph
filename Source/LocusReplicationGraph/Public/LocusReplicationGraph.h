// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "ReplicationGraph.h"
#include "LocusReplicationGraph.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogLocusReplicationGraph, Display, All);

//class UReplicationGraphNode_GridSpatialization2D;
class AGameplayDebuggerCategoryReplicator;
class ULocusReplicationConnectionGraph;

// This is the main enum we use to route actors to the right replication node. Each class maps to one enum.
UENUM(BlueprintType)
enum class EClassRepNodeMapping : uint8
{
	NotRouted,						// Doesn't map to any node. Used for special case actors that handled by special case nodes (UShooterReplicationGraphNode_PlayerStateFrequencyLimiter)
	RelevantAllConnections,			// Routes to an AlwaysRelevantNode or AlwaysRelevantStreamingLevelNode node
	RelevantOwnerConnection,			// Routes to an AlwaysRelevantNode or AlwaysRelevantStreamingLevelNode node
	RelevantTeamConnection,			// Routes to an TeamRelevantNode

	// ONLY SPATIALIZED Enums below here! See UReplicationGraphBase::IsSpatialized

	Spatialize_Static,				// Routes to GridNode: these actors don't move and don't need to be updated every frame.
	Spatialize_Dynamic,				// Routes to GridNode: these actors mode frequently and are updated once per frame.
	Spatialize_Dormancy,			// Routes to GridNode: While dormant we treat as static. When flushed/not dormant dynamic. Note this is for things that "move while not dormant".
};



struct LOCUSREPLICATIONGRAPH_API FTeamConnectionListMap : public TMap<FName, TArray<ULocusReplicationConnectionGraph*>>
{
public:
	//Get array of connection managers for gathering actor list
	TArray<ULocusReplicationConnectionGraph*>* GetConnectionArrayForTeam(FName TeamName);

	//Add Connection to team, if there's no array, add one.
	void AddConnectionToTeam(FName TeamName, ULocusReplicationConnectionGraph* ConnManager);

	//Remove Connection from team, if there's no member of the team after removal, remove array from the map
	void RemoveConnectionFromTeam(FName TeamName, ULocusReplicationConnectionGraph* ConnManager);
};


UCLASS()
class LOCUSREPLICATIONGRAPH_API UReplicationGraphNode_AlwaysRelevant_WithPending : public UReplicationGraphNode_ActorList
{
	GENERATED_BODY()

	public:
	//Gather up other team member's list 
	virtual void GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params) override;
};

UCLASS()
class LOCUSREPLICATIONGRAPH_API UReplicationGraphNode_AlwaysRelevant_ForTeam : public UReplicationGraphNode_ActorList
{
	GENERATED_BODY()

public:
	//Gather up other team member's list 
	virtual void GatherActorListsForConnection(const FConnectionGatherActorListParameters& Params) override;

	//Function that calls parent ActorList's GatherActorList...
	virtual void GatherActorListsForConnectionDefault(const FConnectionGatherActorListParameters& Params);
};

//ReplicationConnectionGraph that holds team information and connection specific nodes.
UCLASS()
class LOCUSREPLICATIONGRAPH_API ULocusReplicationConnectionGraph : public UNetReplicationGraphConnection
{
	GENERATED_BODY()

public:
	UPROPERTY()
	UReplicationGraphNode_AlwaysRelevant_ForConnection* AlwaysRelevantForConnectionNode;

	UPROPERTY()
	class UReplicationGraphNode_AlwaysRelevant_ForTeam* TeamConnectionNode;

	FName TeamName = NAME_None;
};
/**
 * 
 */
UCLASS(Blueprintable)
class LOCUSREPLICATIONGRAPH_API ULocusReplicationGraph : public UReplicationGraph
{
	GENERATED_BODY()
	
public:

	ULocusReplicationGraph();

	//Set up Enums for each uclass of entire actor
	virtual void InitGlobalActorClassSettings() override;

	//initialize global node, like gridnode
	virtual void InitGlobalGraphNodes() override;

	//initialize per connection node, like always relevant node
	virtual void InitConnectionGraphNodes(UNetReplicationGraphConnection* RepGraphConnection) override;
	//deinitialize per connection node
	virtual void OnRemoveConnectionGraphNodes(UNetReplicationGraphConnection* RepGraphConnection);

	//override to make notification when a connection manager is removed
	virtual void RemoveClientConnection(UNetConnection* NetConnection) override;

	//routng actor add/removal
	virtual void RouteAddNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo, FGlobalActorReplicationInfo& GlobalInfo) override;
	virtual void RouteRemoveNetworkActorToNodes(const FNewReplicatedActorInfo& ActorInfo) override;

	virtual void ResetGameWorldState() override;

	//gridnode for spatialization handling
	UPROPERTY()
	UReplicationGraphNode_GridSpatialization2D* GridNode;

	//always relevant for all connection
	UPROPERTY()
	UReplicationGraphNode_AlwaysRelevant_WithPending* AlwaysRelevantNode;

	//always relevant for all connection but in streaming level, so always relevant to connection who loaded key level
	//TMap<FName, FActorRepListRefView> AlwaysRelevantStreamingLevelActors; //but this is not needed as AlwaysRelevantNode already handle streaming level

	//Add Dependent Actor to ReplicationActor's Dep List, DependentActor will relevant according to Replicator's relevancy
	void AddDependentActor(AActor* ReplicatorActor, AActor* DependentActor);
	void RemoveDependentActor(AActor* ReplicatorActor, AActor* DependentActor);
	
	//Change Owner of an actor that is relevant to connection specific
	void ChangeOwnerOfAnActor(AActor* ActorToChange, AActor* NewOwner);

	//SetTeam via Name
	void SetTeamForPlayerController(APlayerController* PlayerController, FName TeamName);

	//virtual function for native implementation
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Replication")
	void OnCustomClassNodeMapping();
	virtual void OnCustomClassNodeMapping_Implementation();

	UFUNCTION(BlueprintCallable, Category="Replication")
	void AddCustomClassMapping(UClass* Class, EClassRepNodeMapping ClassRepNodeMapping);

	//to handle actors that has no connection at addnofity execution
	void RouteAddNetworkActorToConnectionNodes(EClassRepNodeMapping Policy, const FNewReplicatedActorInfo& ActorInfo, FGlobalActorReplicationInfo& GlobalInfo);
	void RouteRemoveNetworkActorToConnectionNodes(EClassRepNodeMapping Policy, const FNewReplicatedActorInfo& ActorInfo);
	void HandlePendingActorsForConnection(UNetReplicationGraphConnection& ConnectionManager);

	ULocusReplicationConnectionGraph* FindLocusConnectionGraph(const AActor* Actor);

	//Just copy-pasted from ShooterGame
#if WITH_GAMEPLAY_DEBUGGER
	void OnGameplayDebuggerOwnerChange(AGameplayDebuggerCategoryReplicator* Debugger, APlayerController* OldOwner);
#endif

	void PrintRepNodePolicies();

private:

	bool IsSettingCustomMapping = false;

	EClassRepNodeMapping GetMappingPolicy(const UClass* Class);

	bool IsSpatialized(EClassRepNodeMapping Mapping) const { return Mapping >= EClassRepNodeMapping::Spatialize_Static; }

	TClassMap<EClassRepNodeMapping> ClassRepNodePolicies;

	friend UReplicationGraphNode_AlwaysRelevant_ForTeam;
	FTeamConnectionListMap TeamConnectionListMap;

	TArray<AActor*> PendingConnectionActors;
	TMap<FName, FName> PendingTeamRequests;
};
