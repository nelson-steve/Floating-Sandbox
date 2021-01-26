/***************************************************************************************
* Original Author:      Gabriele Giuseppini
* Created:              2018-05-23
* Copyright:            Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
***************************************************************************************/
#include "Physics.h"

namespace Physics {

void Gadgets::Update(
    GameWallClock::time_point currentWallClockTime,
    float currentSimulationTime,
    Storm::Parameters const & stormParameters,
    GameParameters const & gameParameters)
{
    //
    // Gadgets
    //

    // Run through all gadgets and invoke Update() on each;
    // remove those gadgets that have expired
    for (auto it = mCurrentGadgets.begin(); it != mCurrentGadgets.end(); /* incremented in loop */)
    {
        bool const isActive = (*it)->Update(
            currentWallClockTime,
            currentSimulationTime,
            stormParameters,
            gameParameters);

        if (!isActive)
        {
            //
            // Gadget has expired
            //

            // Gadget has detached itself already
            // (our rule, to allow gadgets' state machines to detach themselves at will)
            assert(mShipPoints.IsGadgetAttached((*it)->GetPointIndex()));

            // Notify (soundless) removal
            mGameEventHandler->OnGadgetRemoved(
                (*it)->GetId(),
                (*it)->GetType(),
                std::nullopt);

            // Remove it from the container
            it = mCurrentGadgets.erase(it);
        }
        else
        {
            ++it;
        }
    }

    //
    // Physics probe gadget
    //

    if (!!mCurrentPhysicsProbeGadget)
    {
        bool const isActive = mCurrentPhysicsProbeGadget->Update(
            currentWallClockTime,
            currentSimulationTime,
            stormParameters,
            gameParameters);

        assert(isActive); // Guy never expires
        (void)isActive;
    }
}

void Gadgets::OnPointDetached(ElementIndex pointElementIndex)
{
    auto squareNeighborhoodRadius = NeighborhoodRadius * NeighborhoodRadius;

    auto neighborhoodCenter = mShipPoints.GetPosition(pointElementIndex);

    //
    // Gadgets
    //

    for (auto & gadget : mCurrentGadgets)
    {
        // Check if the gadget is within the neighborhood of the disturbed point
        float squareGadgetDistance = (gadget->GetPosition() - neighborhoodCenter).squareLength();
        if (squareGadgetDistance < squareNeighborhoodRadius)
        {
            // Tel the gadget that its neighborhood has been disturbed
            gadget->OnNeighborhoodDisturbed();
        }
    }

    // No need to check Physics probe gadget
}

void Gadgets::OnSpringDestroyed(ElementIndex springElementIndex)
{
    auto squareNeighborhoodRadius = NeighborhoodRadius * NeighborhoodRadius;

    auto neighborhoodCenter = mShipSprings.GetMidpointPosition(springElementIndex, mShipPoints);

    //
    // Gadgets
    //

    for (auto & gadget : mCurrentGadgets)
    {
        // Check if the gadget is tracking this spring
        auto trackedSpring = gadget->GetTrackedSpringIndex();
        if (trackedSpring.has_value() && *trackedSpring == springElementIndex)
        {
            // Tell gadget
            gadget->OnTrackedSpringDestroyed();
        }

        // Check if the gadget is within the neighborhood of the disturbed center
        float squareGadgetDistance = (gadget->GetPosition() - neighborhoodCenter).squareLength();
        if (squareGadgetDistance < squareNeighborhoodRadius)
        {
            // Tel the gadget that its neighborhood has been disturbed
            gadget->OnNeighborhoodDisturbed();
        }
    }

    //
    // Physics probe gadget
    //

    if (!!mCurrentPhysicsProbeGadget)
    {
        // Check if the gadget is tracking this spring
        auto trackedSpring = mCurrentPhysicsProbeGadget->GetTrackedSpringIndex();
        if (trackedSpring.has_value() && *trackedSpring == springElementIndex)
        {
            // Tell gadget
            mCurrentPhysicsProbeGadget->OnTrackedSpringDestroyed();
        }
    }
}

std::optional<bool> Gadgets::TogglePhysicsProbeAt(
    vec2f const & targetPos,
    GameParameters const & gameParameters)
{
    float const squareSearchRadius = gameParameters.ToolSearchRadius * gameParameters.ToolSearchRadius;

    if (!!mCurrentPhysicsProbeGadget)
    {
        //
        // We already have a physics probe...
        // ...see if it's in radius, and if so, remove it
        //

        if ((mCurrentPhysicsProbeGadget->GetPosition() - targetPos).squareLength() < squareSearchRadius)
        {
            //
            // Remove physics probe gadget
            //

            assert(mCurrentPhysicsProbeGadget->MayBeRemoved()); // Physics probes may always be removed

            // Tell it we're removing it
            mCurrentPhysicsProbeGadget->OnExternallyRemoved();

            // Detach gadget from its particle
            assert(mShipPoints.IsGadgetAttached(mCurrentPhysicsProbeGadget->GetPointIndex()));
            mShipPoints.DetachGadget(
                mCurrentPhysicsProbeGadget->GetPointIndex(),
                mShipSprings);

            // Notify removal
            mGameEventHandler->OnGadgetRemoved(
                mCurrentPhysicsProbeGadget->GetId(),
                mCurrentPhysicsProbeGadget->GetType(),
                mParentWorld.IsUnderwater(mCurrentPhysicsProbeGadget->GetPosition()));

            // Remove it
            mCurrentPhysicsProbeGadget.reset();

            // We've removed a physics probe gadget
            return false;
        }
    }

    //
    // No physics probe in ship...
    // ...find closest particle with at least one spring and with no gadgets attached within the search radius, and
    // if found, attach probe to it
    //

    ElementIndex nearestCandidatePointIndex = NoneElementIndex;
    float nearestCandidatePointDistance = std::numeric_limits<float>::max();

    for (auto pointIndex : mShipPoints.RawShipPoints())
    {
        if (!mShipPoints.GetConnectedSprings(pointIndex).ConnectedSprings.empty()
            && !mShipPoints.IsGadgetAttached(pointIndex))
        {
            float const squareDistance = (mShipPoints.GetPosition(pointIndex) - targetPos).squareLength();
            if (squareDistance < squareSearchRadius)
            {
                // This particle is within the search radius

                // Keep the nearest
                if (squareDistance < squareSearchRadius && squareDistance < nearestCandidatePointDistance)
                {
                    nearestCandidatePointIndex = pointIndex;
                    nearestCandidatePointDistance = squareDistance;
                }
            }
        }
    }

    // TODOHERE

    if (NoneElementIndex != nearestCandidatePointIndex)
    {
        //
        // We have a nearest candidate particle...
        // ...attach probe it it
        //

        // ...before attaching the probe, however, remove the already existing one
        bool isMovingProbe = false;
        if (!!mCurrentPhysicsProbeGadget)
        {
            assert(mCurrentPhysicsProbeGadget->MayBeRemoved()); // Physics probes may always be removed

            // Tell it we're removing it
            mCurrentPhysicsProbeGadget->OnExternallyRemoved();

            // Detach gadget from its particle
            assert(mShipPoints.IsGadgetAttached(mCurrentPhysicsProbeGadget->GetPointIndex()));
            mShipPoints.DetachGadget(
                mCurrentPhysicsProbeGadget->GetPointIndex(),
                mShipSprings);

            // We don't want to notify for the removal, as we're
            // simply moving the gadget, not removing it

            // Remove it
            mCurrentPhysicsProbeGadget.reset();

            // Remember that we're not simply adding a probe,
            // but in reality we're moving the existing one
            isMovingProbe = true;
        }

        // Create gadget
        assert(!mCurrentPhysicsProbeGadget);
        mCurrentPhysicsProbeGadget = std::make_unique<PhysicsProbeGadget>(
            GadgetId(mShipId, mNextLocalGadgetId++),
            nearestCandidatePointIndex,
            mParentWorld,
            mGameEventHandler,
            mShipPhysicsHandler,
            mShipPoints,
            mShipSprings);

        // Attach gadget to the particle
        assert(!mShipPoints.IsGadgetAttached(nearestCandidatePointIndex));
        mShipPoints.AttachGadget(
            nearestCandidatePointIndex,
            mCurrentPhysicsProbeGadget->GetMass(),
            mShipSprings);

        // Notify - but only if we're not simply moving it
        if (!isMovingProbe)
        {
            mGameEventHandler->OnGadgetPlaced(
                mCurrentPhysicsProbeGadget->GetId(),
                mCurrentPhysicsProbeGadget->GetType(),
                mParentWorld.IsUnderwater(
                    mCurrentPhysicsProbeGadget->GetPosition()));

            // Tell caller that we've placed a physic probe gadget
            return true;
        }
        else
        {
            // Just moved, hence in the eyes of the caller, nothing has happened
            return std::nullopt;
        }
    }

    // Can't do anything
    return std::nullopt;
}

void Gadgets::RemovePhysicsProbe()
{
    if (!!mCurrentPhysicsProbeGadget)
    {
        assert(mCurrentPhysicsProbeGadget->MayBeRemoved());

        // Tell it we're removing it
        mCurrentPhysicsProbeGadget->OnExternallyRemoved();

        // Detach gadget from its particle
        assert(mShipPoints.IsGadgetAttached(mCurrentPhysicsProbeGadget->GetPointIndex()));
        mShipPoints.DetachGadget(
            mCurrentPhysicsProbeGadget->GetPointIndex(),
            mShipSprings);

        // Notify removal
        mGameEventHandler->OnGadgetRemoved(
            mCurrentPhysicsProbeGadget->GetId(),
            mCurrentPhysicsProbeGadget->GetType(),
            mParentWorld.IsUnderwater(mCurrentPhysicsProbeGadget->GetPosition()));

        // Remove it
        mCurrentPhysicsProbeGadget.reset();
    }
}

void Gadgets::DetonateRCBombs()
{
    for (auto & gadget : mCurrentGadgets)
    {
        if (GadgetType::RCBomb == gadget->GetType())
        {
            RCBombGadget * rcb = dynamic_cast<RCBombGadget *>(gadget.get());
            rcb->Detonate();
        }
    }
}

void Gadgets::DetonateAntiMatterBombs()
{
    for (auto & gadget : mCurrentGadgets)
    {
        if (GadgetType::AntiMatterBomb == gadget->GetType())
        {
            AntiMatterBombGadget * amb = dynamic_cast<AntiMatterBombGadget *>(gadget.get());
            amb->Detonate();
        }
    }
}

void Gadgets::Upload(
    ShipId shipId,
    Render::RenderContext & renderContext) const
{
    for (auto & gadget : mCurrentGadgets)
    {
        gadget->Upload(shipId, renderContext);
    }

    if (mCurrentPhysicsProbeGadget)
    {
        mCurrentPhysicsProbeGadget->Upload(shipId, renderContext);
    }
}

}