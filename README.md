# LocusReplicationGraph

This is an extension of ReplicationGraph plugin introduced in UE 4.20.
It contains a few blueprint library functions that controls replication graph.

As Epic provides a good example of ReplicationGraph in **ShooterGame** project, most of concepts are came from there.

## What it does

Basic functionality that ReplicationGraph provies + 
It expose basic control of ReplicationGraph to Blueprints.  
It supports actors that only relevant to owner connection.  
It supports actors that only relevant to team connections.  
It provides api that add/remove dependent actors(c++/blueprint).  

## How to install

1. Download or clone this repo.

2. Create "Plugins" folder in root folder of your project.

3. UnZip/Paste downloaded files into Plugins/LocusReplicationGraph.

4. Open project, under Edit/Plugins window, enable LocusReplicationGraphPlugin in Project/Networking. This step requires restart/compile.
![enableplugin](https://user-images.githubusercontent.com/6591432/50825525-9a78ef00-137c-11e9-973b-07614cc0a91e.PNG)

5. Now, create your blueprint class and inherit from LocusReplicationGraph. Do required settings there.
![createblueprint](https://user-images.githubusercontent.com/6591432/50824559-410fc080-137a-11e9-8927-e6698219c19f.PNG)

6. Open up Config/DefaultEngine.ini then add
```text
[/Script/OnlineSubsystemUtils.IpNetDriver] 
ReplicationDriverClassName="[Your custom blueprint created in step 5]"
```
this usually looks like this
```text
ReplicationDriverClassName="/Game/Blueprints/Online/CustomReplicationGraph.CustomReplicationGraph_C"
```
![defaultengine](https://user-images.githubusercontent.com/6591432/50824553-3d7c3980-137a-11e9-9e9f-de9bf2808a2f.jpg)

7. Play game in network mode and type console command blow to ensure it works
```text
LocusRepGraph.PrintRouting
```

## How to use it.

After installation, open up created blueprint class.
In Class default details, there are some settings that you may customize to fit your own game.
Each of these values have simple description you can check when you hover your mouse.

During play, you can access ReplicationGraph's functionality with given library functions.  

1. **Add/Remove Dependent Actor.**
  * Add/Remove Dependent actor to Replicator's dependent actor list. Whenever Replicator actor replicates, DependentActor will replicate either. 
  * Dependent Actor should not routed to any nodes(but bReplicated=true), as well as Replicator actor should be currently networked.
  
2. **Set Team for Player Controller.**
  * Set Team name for a APlayerController. Name_None does not have team(default)
  * Routed Policy Relevant Team Connection will show your owned Actors to teammates
  
3. **Change Owner and Refresh Replication.**
  * As we don't collect all replicated actor's owner during playtime, you have to tell exactly which actor want to change it's owner.
  * Actor will be out of ReplicationRgaph, and back to it after changing owner(inside function)



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


