# LocusReplicationGraph

This is an extension of ReplicationGraph plugin introduced in UE 4.20.
It contains a few blueprint library functions that controls replication graph.

As Epic provides a good example of ReplicationGraph in **ShooterGame** project, most of concepts are came from there.

## What it does

It expose basic control of ReplicationGraph to Blueprints.  
It supports actors that only relevant to owner connection.  
It supports actors that only relevant to team connections.  
It provides api that add/remove dependent actors(c++/blueprint).  

## Limitations

It has same limitations that original replication graph has.
For performance reasons, only initial setup is exposed to blueprints.
Exceptions are setting team, owner, dependent actor.  

This plugin does not support complex owner chain. It only look for it's NetOwner at spawn time.
If you want to change owner after spawn, use provided function.  
If A owns B, and B owns C. changing owner should be from bottom to top. C->B->A.  
This is for performance reason. I don't want to find and look up all possible actors per a frame.

## What the hell is ReplicationGraph

In Unreal Engine Networking, Listen/Dedicated Server replicates networked actors to connections. To reduce network bandwidth and performance, those actors expected to have relevancy to that connection. In default implementation, only actors which have less distance (from ViewPosition) than NetCullDistance value will be replicated. Of course, some classes such as APlayerController do have their own replication rules.

Let's say we have 10 replicated(networked) actors, to know it's relevancy to a connection, we have to calculate distance 10 times per connection. Looks straightforward and simple. But what if we need more and more replicated actors and player connections? Let's say we're making a PUBG like game that has 1000+ replicated actors and 100+ connections. In that case, we have to run 1000*100 calculations to get actor relevancies, and that is huge.

But most of MMO server implementation, there usually is a system AKA **Interest Management**. Which divides entire world to small divisions and populate relevancy only in interested divisions. It is very common optimization in MMOGs. Back to UnrealEngine, Epic also introduced this kind of relevancy optimization in ReplicationGraph which is provided by Engine Plugin(Take a look at UReplicationGraphNode_GridSpatialization2D class). 

A ReplicationGraph is collection of ReplicationGraphNodes. A ReplicationGraphNode is a main building block that calculates actor relevancy for a connection. There are many kind of ReplicationGraphNode that completes ReplicationGraph functionality. AlwaysRelevant Node(AGameState), GridSpatialization2D Node(Actors culled by NetCullDistance), AlwaysRelevantForConnection Node(APlayerController, APlayerState) are one of these.


