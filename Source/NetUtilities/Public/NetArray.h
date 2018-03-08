#pragma once

#include "CoreMinimal.h"
#include "BitWriter.h"
#include "BitReader.h"
#include "Engine/NetSerialization.h"

#include "NetArray.generated.h"

UENUM(meta = (Bitflags))
enum class ENetArrayFlags : uint8
{
	NAF_SyncOrder			UMETA(DisplayName = "Synchronize Order"),
	NAF_Pagination			UMETA(DisplayName = "Pagination")
};
ENUM_CLASS_FLAGS(ENetArrayFlags);

USTRUCT()
struct NETUTILITIES_API FNetArrayItem
{
	GENERATED_BODY()
	
public:
	FNetArrayItem()
		: ReplicationID(INDEX_NONE),
		ReplicationKey(INDEX_NONE),
		MostRecentArrayReplicationKey(INDEX_NONE) { }
		
	FNetArrayItem(const FNetArrayItem& InItem)
		: ReplicationID(INDEX_NONE),
		ReplicationKey(INDEX_NONE),
		MostRecentArrayReplicationKey(INDEX_NONE) { }
		
	FNetArrayItem& operator=(const FNetArrayItem& InItem)
	{
		if(&InItem != this)
		{
			ReplicationID = INDEX_NONE;
			ReplicationKey = INDEX_NONE;
			MostRecentArrayReplicationKey = INDEX_NONE;
		}
		
		return *this;
	}
	
	UPROPERTY(NotReplicated)
	int32 ReplicationID;
	
	UPROPERTY(NotReplicated)
	int32 ReplicationKey;
	
	UPROPERTY(NotReplicated)
	int32 MostRecentArrayReplicationKey;

	/* Call upon adding or changing item in array */
	void MarkDirty(struct FNetArray& InNetArray);
};

/* Create a USTRUCT and inherit from this. Your items MUST inherit from FNetArrayItem. */
/* This differs to FFastArraySerializer in that it supports maintaining item order and pagination. */
USTRUCT()
struct NETUTILITIES_API FNetArray
{
	GENERATED_BODY()
	
public:
	FNetArray()
		: Flags(0), 
		IDCounter(),
		ArrayReplicationKey(0),
		CachedItemCount(INDEX_NONE),
		CachedItemCountToConsiderForWriting(INDEX_NONE) { }
		
	UPROPERTY()
	uint8 Flags;
	
	// ReplicationID, Index in array
	TMap<int32, int32> ItemMap;
	int32 IDCounter;
	int32 ArrayReplicationKey;
	TMap<int32, FFastArraySerializerGuidReferences> GuidReferencesMap;
	
	/* Called after an item is added */
	FORCEINLINE void OnAdded(const FNetArrayItem& InItem, int32 InIndex =-1) { }
	
	/* Called before an item is removed */
	FORCEINLINE void OnRemoved(const FNetArrayItem& InItem, int32 InIndex = -1) { }
	
	/* Called when updating an existing item */
	FORCEINLINE void OnChanged(const FNetArrayItem& InItem, int32 InIndex = -1) { }
	
	/* Called when an existing item has moved */
	FORCEINLINE void OnMoved(const FNetArrayItem& InItem, int32 InSourceIndex = -1, int32 InDestinationIndex = -1) { }
	
	/* Call if you remove an item from the array */
	void MarkDirty()
	{
		ItemMap.Reset();
		IncrementArrayRepicationKey();
		
		CachedItemCount = INDEX_NONE;
		CachedItemCountToConsiderForWriting = INDEX_NONE;
	}
	
	template <typename T, typename TNetArray>
	static bool NetArrayDeltaSerialize(
		TArray<T>& InItems, 
		FNetDeltaSerializeInfo& InParams,
		TNetArray& InNetArray);
	
protected:
	int32 CachedItemCount;
	int32 CachedItemCountToConsiderForWriting;

	void IncrementArrayRepicationKey()
	{
		ArrayReplicationKey++;
		if(ArrayReplicationKey == INDEX_NONE)
			ArrayReplicationKey++;
	}
	
	template <typename T, typename TNetArray>
	static bool ShouldWriteItem(const T& InItem, const bool bIsWritingOnClient)
	{
		if(bIsWritingOnClient)
			return InItem.ReplicationID != INDEX_NONE;
		
		return true;
	}
};

template <typename T, typename TNetArray>
bool FNetArray::NetArrayDeltaSerialize(TArray<T>& InItems, FNetDeltaSerializeInfo& InParams, TNetArray& InNetArray)
{
	class UScriptStruct* InnerStruct = T::StaticStruct();

	/* If updating or reading */
	if(InParams.bUpdateUnmappedObjects || InParams.Writer == nullptr)
	{
		// If the incoming ItemMap size doesn't match the actual array size, recreate
		if(InNetArray.ItemMap.Num() != InItems.Num())
		{
			InNetArray.ItemMap.Empty();
			for(auto i = 0; i < InItems.Num(); ++i)
			{
				if(InItems[i].ReplicationID == INDEX_NONE)
					continue;

				InNetArray.ItemMap.Add(InItems[i].ReplicationID, i);
			}
		}
	}
	
	if(InParams.GatherGuidReferences)
	{
		for(const auto& GuidReferencesPair : InNetArray.GuidReferencesMap)
		{
			const FFastArraySerializerGuidReferences& GuidReferences = GuidReferencesPair.Value;
			
			InParams.GatherGuidReferences->Append(GuidReferences.UnmappedGUIDs);
			InParams.GatherGuidReferences->Append(GuidReferences.MappedDynamicGUIDs);
			
			if(InParams.TrackedGuidMemoryBytes)
				*InParams.TrackedGuidMemoryBytes += GuidReferences.Buffer.Num();
		}
		
		return true;
	}
	
	if(InParams.MoveGuidToUnmapped)
	{
		auto bFound = false;
		const auto GUID = *InParams.MoveGuidToUnmapped;
		for(auto& GuidReferencesPair : InNetArray.GuidReferencesMap)
		{
			FFastArraySerializerGuidReferences& GuidReferences = GuidReferencesPair.Value;
			
			if(GuidReferences.MappedDynamicGUIDs.Contains(GUID))
			{
				GuidReferences.MappedDynamicGUIDs.Remove(GUID);
				GuidReferences.UnmappedGUIDs.Add(GUID);
				bFound = true;
			}
		}
	}
	
	if(InParams.bUpdateUnmappedObjects)
	{
		for(auto& GuidReferencesPair : InNetArray.GuidReferencesMap)
		{
			const int32 ReplicationID = GuidReferencesPair.Key();
			
			FFastArraySerializerGuidReferences& GuidReferences = GuidReferencesPair.Value();
			
			if((GuidReferences.UnmappedGUIDs.Num() == 0 && GuidReferences.MappedDynamicGUIDs.Num() == 0) || InNetArray.ItemMap.Find(ReplicationID) == nullptr)
			{
				InNetArray.Remove(ReplicationID);
				continue;
			}

			auto bMappedSomeGUIDs = false;
			for(auto& UnmappedGUID : GuidReferences.UnmappedGUIDs)
			{
				if(InParams.Map->IsGUIDBroken(UnmappedGUID, false))
				{
					GuidReferences.UnmappedGUIDs.Remove(UnmappedGUID);
					continue;
				}

				const auto Object = InParams.Map->GetObjectFromNetGUID(UnmappedGUID, false);
				if(Object != nullptr)
				{
					if(UnmappedGUID.IsDynamic())
						GuidReferences.MappedDynamicGUIDs.Add(UnmappedGUID);

					GuidReferences.UnmappedGUIDs.Remove(UnmappedGUID);
					bMappedSomeGUIDs = true;
				}
			}
			
			if(bMappedSomeGUIDs)
			{
				InParams.bOutSomeObjectsWereMapped = true;
				if(!InParams.bCalledPreNetReceive)
				{
					InParams.Object->PreNetReceive();
					InParams.bCalledPreNetReceive = true;
				}
				
				T* ThisElement = &InItems[InNetArray.ItemMap.FindChecked(ReplicationID)];
				FNetBitReader Reader(InParams.Map, GuidReferences.Buffer.GetData(), GuidReferences.NumBufferBits);
				auto bHasUnmapped = false;
				InParams.NetSerializeCB->NetSerializeStruct(InnerStruct, Reader, InParams.Map, ThisElement, bHasUnmapped);
				
				ThisElement->PostReplicatedChange(InNetArray);
			}
			
			if(GuidReferences.UnmappedGUIDs.Num() == 0 && GuidReferences.MappedDynamicGUIDs.Num() == 0)
				InNetArray.GuidReferencesMap.Remove(GuidReferencesPair);
		}
		
		if(InNetArray.GuidReferencesMap.Num() > 0)
			InParams.bOutHasMoreUnmapped = true;
		
		return true;
	}
	
	// Writing/Saving
	if(InParams.Writer)
	{
		check(InParams.Struct);
		auto& Writer = *InParams.Writer;
		
		// Create a new map
		auto NewState = new FNetFastTArrayBaseState();
		check(InParams.NewState);
		*InParams.NewState = MakeShareable(NewState);
		auto& NewMap = NewState->IDToCLMap;
		NewState->ArrayReplicationKey = InNetArray.ArrayReplicationKey;
		
		// Get the old map (if it exists)
		auto OldMap = InParams.OldState ? &static_cast<FNetFastTArrayBaseState*>(InParams.OldState)->IDToCLMap : nullptr;
		auto OldReplicationKey = InParams.OldState ? static_cast<FNetFastTArrayBaseState*>(InParams.OldState)->ArrayReplicationKey : -1;
		
		/* If we're writing on the client and the Item has a valid ReplicationID, add it. */
		auto CalculateItemCountForConsideration = [&InNetArray, &InItems, &InParams]()
		{
			auto Count = 0;
			for(const T& Item : InItems)
				if(FNetArray::ShouldWriteItem<T, TNetArray>(Item, InParams.bIsWritingOnClient))
					Count++;
				
			return Count;
		};
		
		if(InParams.OldState && (InNetArray.ArrayReplicationKey == OldReplicationKey))
		{
			if(ensureMsgf(OldMap, TEXT("Invalid OldMap")))
			{
				if(InNetArray.CachedItemCount == INDEX_NONE
					|| InNetArray.CachedItemCount != InItems.Num()
					|| InNetArray.CachedItemCountToConsiderForWriting == INDEX_NONE)
				{
					InNetArray.CachedItemCount = InItems.Num();
					InNetArray.CachedItemCountToConsiderForWriting = CalculateItemCountForConsideration();
				}
				
				ensureMsgf((OldMap->Num() == InNetArray.CachedItemCountToConsiderForWriting), TEXT("OldMap size (%d) does not match the item count (%d)"), OldMap->Num(), InNetArray.CachedItemCountToConsiderForWriting);
			}
			
			return false;
		}

		const int32 ConsideredItemCount = CalculateItemCountForConsideration();
		
		TArray<FFastArraySerializer_FastArrayDeltaSerialize_FIdxIDPair, TInlineAllocator<8>> ChangedElements;
		TArray<int32, TInlineAllocator<8>> DeletedElements;
		
		// If the oldmap has more items than the new considered items, something has been deleted.
		auto DeletedCount = (OldMap ? OldMap->Num() : 0) - ConsideredItemCount;
		
		for(auto i = 0; i < InItems.Num(); ++i)
		{
			// If we're not writing on the client skip it, if we are writing on the client and it has an invalid ReplicationID, skip it
			if(!FNetArray::ShouldWriteItem<T, TNetArray>(InItems[i], InParams.bIsWritingOnClient))
				continue;
			
			// If we're not writing on the client and the ReplicationID is invalid, mark it as changed
			// This gives it a ReplicationID
			if(InItems[i].ReplicationID == INDEX_NONE)
				InItems[i].MarkDirty(InNetArray);
			
			// Add the item for replication
			NewMap.Add(InItems[i].ReplicationID, InItems[i].ReplicationKey);
			
			int32* OldValuePtr = OldMap ? OldMap->Find(InItems[i].ReplicationID) : nullptr;
			// The item previously existed in a different index
			if(OldValuePtr)
			{
				if(*OldValuePtr == InItems[i].ReplicationKey)
					continue;
				else
					ChangedElements.Add(FFastArraySerializer_FastArrayDeltaSerialize_FIdxIDPair(i, InItems[i].ReplicationID));
			}
			else
			{
				ChangedElements.Add(FFastArraySerializer_FastArrayDeltaSerialize_FIdxIDPair(i, InItems[i].ReplicationID));
				DeletedCount++;
			}
		}
		
		if(DeletedCount > 0 && OldMap)
		{
			for(auto& KVP : OldMap)
			{
				// If the new map doesn't contain an item in the old map, mark it as deleted
				if(!NewMap.Contains(KVP.Key()))
				{
					DeletedElements.Add(KVP.Key());
					if(--DeletedCount <= 0)
						break;
				}
			}
		}
		
		NewState->ArrayReplicationKey = InNetArray.ArrayReplicationKey;
		
		int32 ArrayReplicationKey = InNetArray.ArrayReplicationKey;
		Writer << ArrayReplicationKey;
		Writer << OldReplicationKey;
		
		// Write the amount of deleted items
		uint32 ElementCount = DeletedElements.Num();
		Writer << ElementCount;
		
		// Write the amount of added or changed items
		ElementCount = ChangedElements.Num();
		Writer << ElementCount;
		
		// Write the actual deleted item ReplicationID
		for(auto& ID : DeletedElements)
			Writer << ID;
		
		// Write the actual added/changed item ReplciationID
		for(auto& Element : ChangedElements)
		{
			void* ThisElement = &InItems[Element.Idx];
			
			uint32 ID = Element.ID;
			Writer << ID;
			
			// Because it's a new item, we need to add it so write the contents of the item here
			auto bHasUnmapped = false;
			InParams.NetSerializeCB->NetSerializeStruct(InnerStruct, Writer, InParams.Map, ThisElement, bHasUnmapped);
		}
	}
	// Reading/Loading
	else
	{
		check(InParams.Reader);
		auto& Reader = *InParams.Reader;
		
		// How is this enforced when writing?
		static const auto MaxCountChanged = 2048;
		static const auto MaxCountDeleted = 2048;
		
		int32 ArrayReplicationKey;
		Reader << ArrayReplicationKey;
		
		int32 OldReplicationKey;
		Reader << OldReplicationKey;
		
		uint32 DeletedCount;
		Reader << DeletedCount;
		
		if(DeletedCount > MaxCountDeleted)
		{
			Reader.SetError();
			return false;
		}
		
		// Changed includes added items
		uint32 ChangedCount;
		Reader << ChangedCount;
		
		if(ChangedCount > MaxCountChanged)
		{
			Reader.SetError();
			return false;
		}
		
		TArray<int32, TInlineAllocator<8>> DeletedIndices;
		TArray<int32, TInlineAllocator<8>> AddedIndices;
		TArray<int32, TInlineAllocator<8>> ChangedIndices;
		
		// Read deleted items
		if(DeletedCount > 0)
		{
			for(auto i = 0; i < DeletedCount; ++i)
			{
				// Get the ReplicationID
				int32 ReplicationID;
				Reader << ReplicationID;
				
				InNetArray.GuidReferencesMap.Remove(ReplicationID);
				
				// Find it in the ItemMap
				int32* ElementIndexPtr = InNetArray.ItemMap.Find(ReplicationID);
				if(ElementIndexPtr)
				{
					// If found, add it as the actual items index
					auto DeleteIndex = *ElementIndexPtr;
					DeletedIndices.Add(DeleteIndex);
				}
			}
		}
		
		// Readed Added/Changed items
		if(ChangedCount > 0)
		{
			for(auto i = 0; i < ChangedCount; ++i)
			{
				// Get the ReplicationID
				int32 ReplicationID;
				Reader << ReplicationID;
				
				// Find the actual item's index
				int32* ElementIndexPtr = InNetArray.ItemMap.Find(ReplicationID);
				auto ElementIndex = 0;
				T* ThisElement = nullptr;
				
				// Element doesn't exist, create it
				if(!ElementIndexPtr)
				{
					ThisElement = new (InItems)T();
					ThisElement->ReplicationID = ReplicationID;
					
					ElementIndex = InItems.Num() - 1;
					InNetArray.ItemMap.Add(ReplicationID, ElementIndex);
					
					AddedIndices.Add(ElementIndex);
				}
				// Element exists, so it's only changed
				else
				{
					ElementIndex = *ElementIndexPtr;
					ThisElement = &InItems[ElementIndex];
					ChangedIndices.Add(ElementIndex);
				}
				
				ThisElement->MostRecentArrayReplicationKey = ArrayReplicationKey;
				++ThisElement->ReplicationKey;
				InParams.Map->ResetTrackedGuids(true);
				
				FBitReaderMark Mark(Reader);
				
				// Deserialize actual struct data
				auto bHasUnmapped = false;
				InParams.NetSerializeCB->NetSerializeStruct(InnerStruct, Reader, InParams.Map, ThisElement, bHasUnmapped);
				
				if(!Reader.IsError())
				{
					const auto& TrackedUnmappedGuids = InParams.Map->GetTrackedUnmappedGuids();
					const TSet<FNetworkGUID>& TrackedMappedDynamicGuids = InParams.Map->GetTrackedDynamicMappedGuids();
					
					if(TrackedUnmappedGuids.Num() || TrackedMappedDynamicGuids.Num())
					{
						FFastArraySerializerGuidReferences& GuidReferences = InNetArray.GuidReferencesMap.FindOrAdd(ReplicationID);
						
						if(!NetworkGuidSetsAreSame(GuidReferences.UnmappedGUIDs, TrackedUnmappedGuids))
						{
							GuidReferences.UnmappedGUIDs = TrackedUnmappedGuids;
							InParams.bGuidListsChanged = true;
						}
						
						if(!NetworkGuidSetsAreSame(GuidReferences.MappedDynamicGUIDs, TrackedMappedDynamicGuids))
						{
							GuidReferences.MappedDynamicGUIDs = TrackedMappedDynamicGuids;
							InParams.bGuidListsChanged = true;
						}
						
						GuidReferences.Buffer.Empty();
						
						GuidReferences.NumBufferBits = Reader.GetPosBits() - Mark.GetPos();
						
						Mark.Copy(Reader, GuidReferences.Buffer);
						
						if(TrackedUnmappedGuids.Num())
							InParams.bOutHasMoreUnmapped = true;
					}
					else
						InNetArray.GuidReferencesMap.Remove(ReplicationID);
				}
				
				InParams.Map->ResetTrackedGuids(false);
				
				if(Reader.IsError())
					return false;
			}
			
			// Look for unacknowledged deleted items
			for(auto i = 0; i < InItems.Num(); ++i)
			{
				T& Item = InItems[i];
				// Check if the item is stale
				if(Item.MostRecentArrayReplicationKey < ArrayReplicationKey && Item.MostRecentArrayReplicationKey > OldReplicationKey)
					DeletedIndices.Add(i);
			}
			
			// If anything was deleted or changed, increment the Replication Key
			if(DeletedIndices.Num() > 0 || ChangedCount > 0)
				InNetArray.IncrementArrayRepicationKey();
			
			// Fire callbacks
			int32 PreRemoveSize = InItems.Num();
			for(auto i : DeletedIndices)
				if(InItems.IsValidIndex(i))
					InNetArray.OnRemoved(InItems[i], i);

			for(auto i : AddedIndices)
				InNetArray.OnAdded(InItems[i], i);
				
			for(auto i : ChangedIndices)
				InNetArray.OnChanged(InItems[i], i);
				
			// Now delete the actual items
			if(DeletedIndices.Num() > 0)
			{
				DeletedIndices.Sort();
				for(auto i = DeletedIndices.Num() - 1; i >= 0; --i)
				{
					auto DeleteIndex = DeletedIndices[i];
					if(InItems.IsValidIndex(DeleteIndex))
						InItems.RemoveAtSwap(DeleteIndex, 1, false);
				}
				
				InNetArray.ItemMap.Empty();
			}
		}
		
		return true;
	}
}

inline void FNetArrayItem::MarkDirty(FNetArray& InNetArray)
{
	if (ReplicationID == INDEX_NONE)
	{
		ReplicationID = ++InNetArray.IDCounter;
		if (InNetArray.IDCounter == INDEX_NONE)
			InNetArray.IDCounter++;
	}

	ReplicationKey++;

	InNetArray.MarkDirty();
}