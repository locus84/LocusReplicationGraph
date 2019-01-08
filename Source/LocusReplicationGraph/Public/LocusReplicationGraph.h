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
	// Does not route to any node. Special case when you want to control manual way.
	NotRouted,						
	// Routes to an AlwaysRelevantNode
	RelevantAllConnections,			
	// Routes to an AlwaysRelevantNode_ForConnection node
	RelevantOwnerConnection,			
	// Routes to an AlwaysRelevantNode_ForTeam node
	RelevantTeamConnection,			

	// ONLY SPATIALIZED Enums below here! See UReplicationGraphBase::IsSpatialized

	// Routes to GridNode: these actors don't move and don't need to be updated every frame.
	Spatialize_Static,				
	// Routes to GridNode: these actors mode frequently and are updated once per frame.
	Spatialize_Dynamic,				
	// Routes to GridNode: While dormant we treat as static. When flushed/not dormant dynamic. Note this is for things that "move while not dormant".
	Spatialize_Dormancy,
};


struct FTeamRequest
{
	FName TeamName;
	APlayerController* Requestor;
	FTeamRequest(FName InTeamName, APlayerController* PC):TeamName(InTeamName), Requestor(PC) {}
};


USTRUCT(BlueprintType)
struct LOCUSREPLICATIONGRAPH_API FClassReplicationPolicyPreset
{
	GENERATED_BODY()
public:
	// Class to set replication policy.
	UPROPERTY(EditAnywhere)
	TSubclassOf<AActor> Class;
	// Policy to set.
	UPROPERTY(EditAnywhere)
	EClassRepNodeMapping Policy;
};


USTRUCT(BlueprintType)
struct LOCUSREPLICATIONGRAPH_API FClassReplicationInfoPreset
{
	GENERATED_BODY()
public:

	// Class of this Replication info is related
	UPROPERTY(EditAnywhere)
	TSubclassOf<AActor> Class;
	// How much will distance affect to priority
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float DistancePriorityScale = 1.f;
	// How much will stavation affect to priority
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float StarvationPriorityScale = 1.f;
	// Cull distance that overrides NetCullDistance (Warning : IsNetRelevantFor will not be called in this system)
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", UIMin = "0.0"))
	float CullDistanceSquared = 0.f;
	//Server frame count per actual replication
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", UIMin = "0.0"))
	uint8 ReplicationPeriodFrame = 1;
	// How long will this actor channel stay alive even after it's being out of relevancy
	UPROPERTY(EditAnywhere, meta = (ClampMin = "0.0", UIMin = "0.0"))
	uint8 ActorChannelFrameTimeout = 4;
	// Whether this setting overrides all child classes or not
	UPROPERTY(EditAnywhere)
	bool IncludeChildClasses = true;

	FClassReplicationInfo CreateClassReplicationInfo()
	{
		FClassReplicationInfo Info;
		Info.DistancePriorityScale = DistancePriorityScale;
		Info.StarvationPriorityScale = StarvationPriorityScale;
		Info.CullDistanceSquared = CullDistanceSquared;
		Info.ReplicationPeriodFrame = ReplicationPeriodFrame;
		Info.ActorChannelFrameTimeout = ActorChannelFrameTimeout;
		return Info;
	}
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
	UReplicationGraphNode_AlwaysRelevant_WithPending();
	virtual void PrepareForReplication() override;
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
	
	// How far destruction infos will be sent. When server destroyed an actor that is NetLoadOnClient tick is on
	UPROPERTY(EditDefaultsOnly, meta = (ClampMin = "1000.0", ClampMax = "100000.0", UIMin = "1000.0", UIMax = "100000.0"))
	float DestructionInfoMaxDistance = 30000.f;

	// Cell size of spatial gird.
	UPROPERTY(EditDefaultsOnly, meta = (ClampMin = "1000.0", ClampMax = "100000.0", UIMin = "1000.0", UIMax = "100000.0"))
	float SpacialCellSize = 10000.f;

	// Spatial grid out of bound bias. Better not change this.
	UPROPERTY(EditDefaultsOnly)
	FVector2D SpatialBias = FVector2D(-150000.f, -200000.f);

	// Should spatial grid rebuilt upon detecting an actor that is out of bias?
	UPROPERTY(EditDefaultsOnly)
	bool EnableSpatialRebuilds = false;

	UPROPERTY(EditDefaultsOnly)
	TArray<FClassReplicationPolicyPreset> ReplicationPolicySettings;

	UPROPERTY(EditDefaultsOnly)
	TArray<FClassReplicationInfoPreset> ReplicationInfoSettings;
	
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

	//to handle actors that has no connection at addnofity execution
	void RouteAddNetworkActorToConnectionNodes(EClassRepNodeMapping Policy, const FNewReplicatedActorInfo& ActorInfo, FGlobalActorReplicationInfo& GlobalInfo);
	void RouteRemoveNetworkActorToConnectionNodes(EClassRepNodeMapping Policy, const FNewReplicatedActorInfo& ActorInfo);

	//handle pending team requests and notifies
	void HandlePendingActorsAndTeamRequests();

	ULocusReplicationConnectionGraph* FindLocusConnectionGraph(const AActor* Actor);

	//Just copy-pasted from ShooterGame
#if WITH_GAMEPLAY_DEBUGGER
	void OnGameplayDebuggerOwnerChange(AGameplayDebuggerCategoryReplicator* Debugger, APlayerController* OldOwner);
#endif

	void PrintRepNodePolicies();

private:

	EClassRepNodeMapping GetMappingPolicy(const UClass* Class);

	bool IsSpatialized(EClassRepNodeMapping Mapping) const { return Mapping >= EClassRepNodeMapping::Spatialize_Static; }

	TClassMap<EClassRepNodeMapping> ClassRepNodePolicies;

	friend UReplicationGraphNode_AlwaysRelevant_ForTeam;
	FTeamConnectionListMap TeamConnectionListMap;

	TArray<AActor*> PendingConnectionActors;
	TArray<FTeamRequest> PendingTeamRequests;

};
