#include "Props/PropInstanceVolumeQuery.h"
#include "Props/InteractablePropManager.h"
#include "Components/BoxComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"

TSet<int32> PropInstanceVolumeQuery::Query(const UBoxComponent* Box, APropManager* Manager)
{
	TSet<int32> IDs;
	if (!IsValid(Box) || !IsValid(Box->GetOwner()) || !Box->GetOwner()->HasAuthority()
		|| Box->GetNetMode()==NM_Client || !Box->GetWorld() || !Box->GetWorld()->GetPhysicsScene()) return IDs;
	if (!Manager) Manager=APropManager::Get(Box);
	if (!IsValid(Manager) || Manager->GetWorld()!=Box->GetWorld()) return IDs;
	const FVector Extent=Box->GetScaledBoxExtent();
	if (Extent.ContainsNaN() || Extent.GetMin()<=0 || Box->GetComponentTransform().ContainsNaN()) return IDs;
	TArray<FOverlapResult> Hits;
	Box->GetWorld()->OverlapMultiByObjectType(Hits,Box->GetComponentLocation(),Box->GetComponentQuat(),
		FCollisionObjectQueryParams::AllObjects,FCollisionShape::MakeBox(Extent),
		FCollisionQueryParams(SCENE_QUERY_STAT(PropInstanceVolume),false,Box->GetOwner()));
	for (const auto& Hit:Hits)
	{
		const int32 ID=Manager->GetISID(Hit.GetComponent(),Hit.GetItemIndex());
		if (ID>0) IDs.Add(ID);
	}
	return IDs;
}
