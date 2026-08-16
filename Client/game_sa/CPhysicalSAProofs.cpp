#include "StdInc.h"
#include "CPhysicalSA.h"

SPhysicalProofs CPhysicalSA::GetPhysicalProofs() const
{
    const auto* physical = reinterpret_cast<const CPhysicalSAInterface*>(GetInterface());
    if (!physical)
        return {};
    return {physical->bBulletProof != 0, physical->bFireProof != 0, physical->bExplosionProof != 0,
            physical->bCollisionProof != 0, physical->bMeeleProof != 0};
}

void CPhysicalSA::SetPhysicalProofs(const SPhysicalProofs& proofs)
{
    auto* physical = reinterpret_cast<CPhysicalSAInterface*>(GetInterface());
    if (!physical)
        return;
    physical->bBulletProof = proofs.bullet;
    physical->bFireProof = proofs.fire;
    physical->bExplosionProof = proofs.explosion;
    physical->bCollisionProof = proofs.collision;
    physical->bMeeleProof = proofs.melee;
}
