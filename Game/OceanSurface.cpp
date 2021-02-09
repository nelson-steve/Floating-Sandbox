﻿/***************************************************************************************
* Original Author:		Gabriele Giuseppini
* Created:				2018-04-14
* Copyright:			Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
***************************************************************************************/
#include "Physics.h"

#include <GameCore/GameRandomEngine.h>
#include <GameCore/GameWallClock.h>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Physics {

// The number of slices we want to render the water surface as;
// this is the graphical resolution
template<typename T>
T constexpr RenderSlices = 500;

OceanSurface::OceanSurface(
    World & parentWorld,
    std::shared_ptr<GameEventDispatcher> gameEventDispatcher)
    : mParentWorld(parentWorld)
    , mGameEventHandler(std::move(gameEventDispatcher))
    , mSamples(new Sample[SamplesCount + 1]) // One extra sample for the rightmost X
    ////////
    , mBasalWaveAmplitude1(0.0f)
    , mBasalWaveAmplitude2(0.0f)
    , mBasalWaveNumber1(0.0f)
    , mBasalWaveNumber2(0.0f)
    , mBasalWaveAngularVelocity1(0.0f)
    , mBasalWaveAngularVelocity2(0.0f)
    , mBasalWaveSin1()
    , mNextTsunamiTimestamp(GameWallClock::duration::max())
    , mNextRogueWaveTimestamp(GameWallClock::duration::max())
    ////////
    , mWindBaseAndStormSpeedMagnitude(std::numeric_limits<float>::max())
    , mBasalWaveHeightAdjustment(std::numeric_limits<float>::max())
    , mBasalWaveLengthAdjustment(std::numeric_limits<float>::max())
    , mBasalWaveSpeedAdjustment(std::numeric_limits<float>::max())
    , mTsunamiRate(std::chrono::minutes::max())
    , mRogueWaveRate(std::chrono::minutes::max())
    ////////
    , mHeightField(new float[SWETotalSamples + 1]) // One extra cell just to ease interpolations
    , mVelocityField(new float[SWETotalSamples + 1]) // One extra cell just to ease interpolations
    , mDeltaHeightBuffer()
    ////////
    , mSWEInteractiveWaveStateMachine()
    , mSWETsunamiWaveStateMachine()
    , mSWERogueWaveWaveStateMachine()
    , mLastTsunamiTimestamp(GameWallClock::GetInstance().Now())
    , mLastRogueWaveTimestamp(GameWallClock::GetInstance().Now())
{
    //
    // Initialize SWE layer
    // - Initialize *all* values - including extra unused sample
    //

    for (size_t i = 0; i <= SWETotalSamples; ++i)
    {
        mHeightField[i] = SWEHeightFieldOffset;
        mVelocityField[i] = 0.0f;
    }

    mDeltaHeightBuffer.fill(0.0f);

    //
    // Initialize constant sample values
    //

    mSamples[SamplesCount].SampleValuePlusOneMinusSampleValue = 0.0f;
}

void OceanSurface::Update(
    float currentSimulationTime,
    Wind const & wind,
    GameParameters const & gameParameters)
{
    auto const now = GameWallClock::GetInstance().Now();

    //
    // Check whether parameters have changed
    //

    if (mWindBaseAndStormSpeedMagnitude != wind.GetBaseAndStormSpeedMagnitude()
        || mBasalWaveHeightAdjustment != gameParameters.BasalWaveHeightAdjustment
        || mBasalWaveLengthAdjustment != gameParameters.BasalWaveLengthAdjustment
        || mBasalWaveSpeedAdjustment != gameParameters.BasalWaveSpeedAdjustment)
    {
        RecalculateWaveCoefficients(wind, gameParameters);
    }

    if (mTsunamiRate != gameParameters.TsunamiRate
        || mRogueWaveRate != gameParameters.RogueWaveRate)
    {
        RecalculateAbnormalWaveTimestamps(gameParameters);
    }


    //
    // 1. Advance SWE Wave State Machines
    //

    // Interactive
    if (mSWEInteractiveWaveStateMachine.has_value())
    {
        auto heightValue = mSWEInteractiveWaveStateMachine->Update(currentSimulationTime);
        if (!heightValue)
        {
            // Done
            mSWEInteractiveWaveStateMachine.reset();
        }
        else
        {
            // Apply
            SetSWEWaveHeight(
                mSWEInteractiveWaveStateMachine->GetCenterIndex(),
                *heightValue);
        }
    }

    // Tsunami
    if (mSWETsunamiWaveStateMachine.has_value())
    {
        auto heightValue = mSWETsunamiWaveStateMachine->Update(currentSimulationTime);
        if (!heightValue)
        {
            // Done
            mSWETsunamiWaveStateMachine.reset();
        }
        else
        {
            // Apply
            SetSWEWaveHeight(
                mSWETsunamiWaveStateMachine->GetCenterIndex(),
                *heightValue);
        }
    }
    else
    {
        //
        // See if it's time to generate a tsunami
        //

        if (now > mNextTsunamiTimestamp)
        {
            // Tsunami!
            TriggerTsunami(currentSimulationTime);

            mLastTsunamiTimestamp = now;

            // Reset automatically-generated tsunamis
            mNextTsunamiTimestamp = CalculateNextAbnormalWaveTimestamp(
                now,
                gameParameters.TsunamiRate);

            // Tell world
            mParentWorld.DisturbOcean(std::chrono::milliseconds(0));
        }
    }

    // Rogue Wave
    if (mSWERogueWaveWaveStateMachine.has_value())
    {
        auto heightValue = mSWERogueWaveWaveStateMachine->Update(currentSimulationTime);
        if (!heightValue)
        {
            // Done
            mSWERogueWaveWaveStateMachine.reset();
        }
        else
        {
            // Apply
            SetSWEWaveHeight(
                mSWERogueWaveWaveStateMachine->GetCenterIndex(),
                *heightValue);
        }
    }
    else
    {
        //
        // See if it's time to generate a rogue wave
        //

        if (now > mNextRogueWaveTimestamp)
        {
            // RogueWave!
            TriggerRogueWave(currentSimulationTime, wind);

            mLastRogueWaveTimestamp = now;

            // Reset automatically-generated rogue waves
            mNextRogueWaveTimestamp = CalculateNextAbnormalWaveTimestamp(
                now,
                gameParameters.RogueWaveRate);
        }
    }


    //
    // 2. SWE Update
    //

    ApplyDampingBoundaryConditions();

    UpdateFields();

    ////// Calc avg height among all samples
    ////float avgHeight = 0.0f;
    ////for (size_t i = SWEOuterLayerSamples; i < SWEOuterLayerSamples + SamplesCount; ++i)
    ////{
    ////    avgHeight += mHeightField[i];
    ////}
    ////avgHeight /= static_cast<float>(SamplesCount);
    ////LogMessage("AVG:", avgHeight);


    //
    // 3. Generate samples
    //

    GenerateSamples(
        currentSimulationTime,
        wind,
        gameParameters);
}

void OceanSurface::Upload(Render::RenderContext & renderContext) const
{
    switch (renderContext.GetOceanRenderDetail())
    {
        case OceanRenderDetailType::Basic:
        {
            InternalUpload<OceanRenderDetailType::Basic>(renderContext);

            break;
        }

        case OceanRenderDetailType::Detailed:
        {
            InternalUpload<OceanRenderDetailType::Detailed>(renderContext);

            break;
        }
    }
}

void OceanSurface::AdjustTo(
    std::optional<vec2f> const & worldCoordinates,
    float currentSimulationTime)
{
    if (worldCoordinates.has_value())
    {
        // Calculate target height
        float constexpr MaxRelativeHeight = 4.0f; // Carefully selected; 4.5 makes waves unstable (velocities oscillating around 0.5 and diverging) after a while
        float constexpr MinRelativeHeight = -2.0f;
        float targetHeight =
            Clamp(worldCoordinates->y / SWEHeightFieldAmplification, MinRelativeHeight, MaxRelativeHeight)
            + SWEHeightFieldOffset;

        // Check whether we are already advancing an interactive wave, or whether
        // we may smother the almost-complete existing one
        if (!mSWEInteractiveWaveStateMachine
            || mSWEInteractiveWaveStateMachine->MayBeOverridden())
        {
            //
            // Start advancing a new interactive wave
            //

            auto const sampleIndex = ToSampleIndex(worldCoordinates->x);

            size_t const centerIndex = SWEOuterLayerSamples + static_cast<size_t>(sampleIndex);

            // Start wave
            mSWEInteractiveWaveStateMachine.emplace(
                centerIndex,
                mHeightField[centerIndex],  // LowHeight == current height
                targetHeight,               // HighHeight == target
                currentSimulationTime);
        }
        else
        {
            //
            // Restart currently-advancing interactive wave
            //

            mSWEInteractiveWaveStateMachine->Restart(
                targetHeight,
                currentSimulationTime);
        }
    }
    else
    {
        //
        // Start release of currently-advancing interactive wave
        //

        assert(!!mSWEInteractiveWaveStateMachine);
        mSWEInteractiveWaveStateMachine->Release(currentSimulationTime);
    }
}

void OceanSurface::ApplyThanosSnap(
    float leftFrontX,
    float rightFrontX)
{
    auto const sampleIndexStart = SWEOuterLayerSamples + ToSampleIndex(std::max(leftFrontX, -GameParameters::HalfMaxWorldWidth));
    auto const sampleIndexEnd = SWEOuterLayerSamples + ToSampleIndex(std::min(rightFrontX, GameParameters::HalfMaxWorldWidth));

    assert(sampleIndexStart >= 0 && sampleIndexStart < SWETotalSamples);

    float constexpr WaterDepression = 1.0f / SWEHeightFieldAmplification;

    for (auto idx = sampleIndexStart; idx <= sampleIndexEnd; ++idx)
        mHeightField[idx] -= WaterDepression;
}

void OceanSurface::TriggerTsunami(float currentSimulationTime)
{
    // Choose X
    float const tsunamiWorldX = GameRandomEngine::GetInstance().GenerateUniformReal(
        -GameParameters::HalfMaxWorldWidth,
        GameParameters::HalfMaxWorldWidth);

    // Choose height (good: 5 at 50-50)
    float constexpr AverageTsunamiHeight = 250.0f / SWEHeightFieldAmplification;
    float const tsunamiHeight = GameRandomEngine::GetInstance().GenerateUniformReal(
        AverageTsunamiHeight * 0.96f,
        AverageTsunamiHeight * 1.04f)
        + SWEHeightFieldOffset;

    // Make it a sample index
    auto const sampleIndex = ToSampleIndex(tsunamiWorldX);

    // (Re-)start state machine
    size_t const centerIndex = SWEOuterLayerSamples + static_cast<size_t>(sampleIndex);
    mSWETsunamiWaveStateMachine.emplace(
        centerIndex,
        mHeightField[centerIndex],  // LowHeight == current height
        tsunamiHeight,              // HighHeight == tsunami height
        7.0f,
        5.0f,
        currentSimulationTime);

    // Fire tsunami event
    assert(!!mGameEventHandler);
    mGameEventHandler->OnTsunami(tsunamiWorldX);
}

void OceanSurface::TriggerRogueWave(
    float currentSimulationTime,
    Wind const & wind)
{
    // Choose locus
    size_t centerIndex;
    if (wind.GetBaseAndStormSpeedMagnitude() >= 0.0f)
    {
        // Left locus
        centerIndex = SWEBoundaryConditionsSamples;
    }
    else
    {
        // Right locus
        centerIndex = SWEOuterLayerSamples + OceanSurface::SamplesCount;
    }

    // Choose height
    float constexpr MaxRogueWaveHeight = 50.0f / SWEHeightFieldAmplification;
    float const rogueWaveHeight = GameRandomEngine::GetInstance().GenerateUniformReal(
        MaxRogueWaveHeight * 0.35f,
        MaxRogueWaveHeight)
        + SWEHeightFieldOffset;

    // Choose rate
    float const rogueWaveDelay = GameRandomEngine::GetInstance().GenerateUniformReal(
        0.7f,
        2.0f);

    // (Re-)start state machine
    mSWERogueWaveWaveStateMachine.emplace(
        centerIndex,
        mHeightField[centerIndex],  // LowHeight == current height
        rogueWaveHeight,            // HighHeight == rogue wave height
        rogueWaveDelay, // Rise delay
        rogueWaveDelay, // Fall delay
        currentSimulationTime);
}

///////////////////////////////////////////////////////////////////////////////////////////////

template<OceanRenderDetailType DetailType>
void OceanSurface::InternalUpload(Render::RenderContext & renderContext) const
{
    static_assert(DetailType == OceanRenderDetailType::Basic || DetailType == OceanRenderDetailType::Detailed);

    int64_t constexpr DetailXOffsetSamples = 2; // # of (whole) samples that the detailed planes are offset by

    float constexpr MidPlaneDamp = 0.8f;
    float constexpr BackPlaneDamp = 0.45f;

    //
    // We want to upload at most RenderSlices slices
    //

    // Find index of leftmost sample, and its corresponding world X
    auto const leftmostSampleIndex = FastTruncateToArchInt((renderContext.GetVisibleWorld().TopLeft.x + GameParameters::HalfMaxWorldWidth) / Dx);
    float sampleIndexX = -GameParameters::HalfMaxWorldWidth + (Dx * leftmostSampleIndex);

    // Calculate number of samples required to cover screen from leftmost sample
    // up to the visible world right (included)
    float const coverageWidth = renderContext.GetVisibleWorld().BottomRight.x - sampleIndexX;
    auto const numberOfSamplesToRender = static_cast<size_t>(ceil(coverageWidth / Dx));

    if (numberOfSamplesToRender >= RenderSlices<size_t>)
    {
        //
        // Zoom out from afar: each slice encompasses more than 1 sample;
        // we upload then RenderSlices slices, interpolating Y at each slice boundary
        //

        if constexpr(DetailType == OceanRenderDetailType::Basic)
            renderContext.UploadOceanBasicStart(RenderSlices<int>);
        else
            renderContext.UploadOceanDetailedStart(RenderSlices<int>);

        // Calculate dx between each pair of slices with want to upload
        float const sliceDx = coverageWidth / RenderSlices<float>;

        // We do one extra iteration as the number of slices is the number of quads, and the last vertical
        // quad side must be at the end of the width
        for (size_t s = 0; s <= RenderSlices<size_t>; ++s, sampleIndexX += sliceDx)
        {
            //
            // Split sample index X into index in sample array and fractional part
            // between that sample and the next
            //

            assert(sampleIndexX >= -GameParameters::HalfMaxWorldWidth
                && sampleIndexX <= GameParameters::HalfMaxWorldWidth);

            // Fractional index in the sample array
            float const sampleIndexF = (sampleIndexX + GameParameters::HalfMaxWorldWidth) / Dx;

            // Integral part
            auto const sampleIndexI = FastTruncateToArchInt(sampleIndexF);

            // Fractional part within sample index and the next sample index
            float const sampleIndexDx = sampleIndexF - sampleIndexI;

            assert(sampleIndexI >= 0 && sampleIndexI <= SamplesCount);
            assert(sampleIndexDx >= 0.0f && sampleIndexDx <= 1.0f);

            //
            // Interpolate sample at sampleIndexX
            //

            float const sample =
                mSamples[sampleIndexI].SampleValue
                + mSamples[sampleIndexI].SampleValuePlusOneMinusSampleValue * sampleIndexDx;

            //
            // Upload slice
            //

            if constexpr (DetailType == OceanRenderDetailType::Basic)
            {
                renderContext.UploadOceanBasic(
                    sampleIndexX,
                    sample);
            }
            else
            {
                //
                // Interpolate samples at sampleIndeX minus offsets,
                // re-using the fractional part that we've already calculated for sampleIndexX
                //

                auto const indexBack = std::max(sampleIndexI - DetailXOffsetSamples * 2, int64_t(0));
                float const sampleBack =
                    mSamples[indexBack].SampleValue
                    + mSamples[indexBack].SampleValuePlusOneMinusSampleValue * sampleIndexDx;

                auto const indexMid = std::max(sampleIndexI - DetailXOffsetSamples, int64_t(0));
                float const sampleMid =
                    mSamples[indexMid].SampleValue
                    + mSamples[indexMid].SampleValuePlusOneMinusSampleValue * sampleIndexDx;

                renderContext.UploadOceanDetailed(
                    sampleIndexX,
                    sampleBack * BackPlaneDamp,
                    sampleMid * MidPlaneDamp,
                    sample);
            }
        }
    }
    else
    {
        //
        // Zoom in: each sample encompasses multiple slices;
        // we upload then just the required number of samples, which is less than
        // the max number of slices we're prepared to upload, and we let OpenGL
        // interpolate on our behalf
        //

        if constexpr (DetailType == OceanRenderDetailType::Basic)
            renderContext.UploadOceanBasicStart(numberOfSamplesToRender);
        else
            renderContext.UploadOceanDetailedStart(numberOfSamplesToRender);

        // We do one extra iteration as the number of slices is the number of quads, and the last vertical
        // quad side must be at the end of the width
        for (size_t s = 0; s <= numberOfSamplesToRender; ++s, sampleIndexX += Dx)
        {
            if constexpr (DetailType == OceanRenderDetailType::Basic)
            {
                renderContext.UploadOceanBasic(
                    sampleIndexX,
                    mSamples[leftmostSampleIndex + static_cast<int64_t>(s)].SampleValue);
            }
            else
            {
                renderContext.UploadOceanDetailed(
                    sampleIndexX,
                    mSamples[std::max(leftmostSampleIndex + static_cast<int64_t>(s) - DetailXOffsetSamples * 2, int64_t(0))].SampleValue * BackPlaneDamp,
                    mSamples[std::max(leftmostSampleIndex + static_cast<int64_t>(s) - DetailXOffsetSamples, int64_t(0))].SampleValue * MidPlaneDamp,
                    mSamples[leftmostSampleIndex + static_cast<int64_t>(s)].SampleValue);
            }
        }
    }

    if constexpr (DetailType == OceanRenderDetailType::Basic)
        renderContext.UploadOceanBasicEnd();
    else
        renderContext.UploadOceanDetailedEnd();
}

void OceanSurface::SetSWEWaveHeight(
    size_t centerIndex,
    float height)
{
    int const firstSampleIndex = static_cast<int>(centerIndex) - static_cast<int>(SWEWaveStateMachinePerturbedSamplesCount / 2);

    for (int i = 0; i < SWEWaveStateMachinePerturbedSamplesCount; ++i)
    {
        int idx = firstSampleIndex + i;
        if (idx >= SWEBoundaryConditionsSamples
            && idx < SWEOuterLayerSamples + SamplesCount + SWEWaveGenerationSamples)
        {
            mHeightField[idx] = height;
        }
    }
}

void OceanSurface::RecalculateWaveCoefficients(
    Wind const & wind,
    GameParameters const & gameParameters)
{
    //
    // Basal waves
    //

    float baseWindSpeedMagnitude = std::abs(wind.GetBaseAndStormSpeedMagnitude()); // km/h
    if (baseWindSpeedMagnitude < 60)
        // y = 63.09401 - 63.09401*e^(-0.05025263*x)
        baseWindSpeedMagnitude = 63.09401f - 63.09401f * std::exp(-0.05025263f * baseWindSpeedMagnitude); // Dramatize

    float const baseWindSpeedSign = wind.GetBaseAndStormSpeedMagnitude() >= 0.0f ? 1.0f : -1.0f;

    // Amplitude
    // - Amplitude = f(WindSpeed, km/h), with f fitted over points from Full Developed Waves
    //   (H. V. Thurman, Introductory Oceanography, 1988)
    // y = 1.039702 - 0.08155357*x + 0.002481548*x^2

    float const basalWaveHeightBase = (baseWindSpeedMagnitude != 0.0f)
        ? 0.002481548f * (baseWindSpeedMagnitude * baseWindSpeedMagnitude)
          - 0.08155357f * baseWindSpeedMagnitude
          + 1.039702f
        : 0.0f;

    mBasalWaveAmplitude1 = basalWaveHeightBase / 2.0f * gameParameters.BasalWaveHeightAdjustment;
    mBasalWaveAmplitude2 = 0.75f * mBasalWaveAmplitude1;

    // Wavelength
    // - Wavelength = f(WaveHeight (adjusted), m), with f fitted over points from same table
    // y = -738512.1 + 738525.2*e^(+0.00001895026*x)

    float const basalWaveLengthBase =
        -738512.1f
        + 738525.2f * exp(0.00001895026f * (2.0f * mBasalWaveAmplitude1));

    float const basalWaveLength = basalWaveLengthBase * gameParameters.BasalWaveLengthAdjustment;

    assert(basalWaveLength != 0.0f);
    mBasalWaveNumber1 = baseWindSpeedSign * 2.0f * Pi<float> / basalWaveLength;
    mBasalWaveNumber2 = 0.66f * mBasalWaveNumber1;

    // Period
    // - Technically, period = sqrt(2 * Pi * L / g), however this doesn't fit the table, so:
    // - Period = f(WaveLength (adjusted), m), with f fitted over points from same table
    // y = 17.91851 - 15.52928*e^(-0.006572834*x)

    float const basalWavePeriodBase =
        17.91851f
        - 15.52928f * exp(-0.006572834f * basalWaveLength);

    assert(gameParameters.BasalWaveSpeedAdjustment != 0.0f);
    float const basalWavePeriod = basalWavePeriodBase / gameParameters.BasalWaveSpeedAdjustment;

    assert(basalWavePeriod != 0.0f);
    mBasalWaveAngularVelocity1 = 2.0f * Pi<float> / basalWavePeriod;
    mBasalWaveAngularVelocity2 = 0.75f * mBasalWaveAngularVelocity1;

    //
    // Pre-calculate basal wave sinusoid
    //
    // By pre-multiplying with the first basal wave's amplitude we may save
    // one multiplication
    //

    mBasalWaveSin1.Recalculate(
        [a = mBasalWaveAmplitude1](float x)
        {
            return a * sin(2.0f * Pi<float> * x);
        });


    //
    // Store new parameter values that we are now current with
    //

    mWindBaseAndStormSpeedMagnitude = wind.GetBaseAndStormSpeedMagnitude();
    mBasalWaveHeightAdjustment = gameParameters.BasalWaveHeightAdjustment;
    mBasalWaveLengthAdjustment = gameParameters.BasalWaveLengthAdjustment;
    mBasalWaveSpeedAdjustment = gameParameters.BasalWaveSpeedAdjustment;
}

void OceanSurface::RecalculateAbnormalWaveTimestamps(GameParameters const & gameParameters)
{
    if (gameParameters.TsunamiRate.count() > 0)
    {
        mNextTsunamiTimestamp = CalculateNextAbnormalWaveTimestamp(
            mLastTsunamiTimestamp,
            gameParameters.TsunamiRate);
    }
    else
    {
        mNextTsunamiTimestamp = GameWallClock::time_point::max();
    }

    if (gameParameters.RogueWaveRate.count() > 0)
    {
        mNextRogueWaveTimestamp = CalculateNextAbnormalWaveTimestamp(
            mLastRogueWaveTimestamp,
            gameParameters.RogueWaveRate);
    }
    else
    {
        mNextRogueWaveTimestamp = GameWallClock::time_point::max();
    }


    //
    // Store new parameter values that we are now current with
    //

    mTsunamiRate = gameParameters.TsunamiRate;
    mRogueWaveRate = gameParameters.RogueWaveRate;
}

template<typename TDuration>
GameWallClock::time_point OceanSurface::CalculateNextAbnormalWaveTimestamp(
    GameWallClock::time_point lastTimestamp,
    TDuration rate)
{
    float const rateSeconds = static_cast<float>(std::chrono::duration_cast<std::chrono::seconds>(rate).count());

    return lastTimestamp
        + std::chrono::duration_cast<GameWallClock::duration>(
            std::chrono::duration<float>(
                120.0f // Grace period between tsunami waves
                + GameRandomEngine::GetInstance().GenerateExponentialReal(1.0f / rateSeconds)));
}

/* Note: in this implementation we let go of the field advections,
   as they dont's seem to improve the simulation in any visible way.

void OceanSurface::AdvectHeightField()
{
    //
    // Semi-Lagrangian method
    //

    // Process all height samples, except for boundary condition samples
    for (size_t i = SWEBoundaryConditionsSamples; i < SWETotalSamples - SWEBoundaryConditionsSamples; ++i)
    {
        // The height field values are at the center of the cell,
        // while velocities are at the edges - hence we need to take
        // the two neighboring velocities
        float const v = (mCurrentVelocityField[i] + mCurrentVelocityField[i + 1]) / 2.0f;

        // Calculate the (fractional) index that this height sample had one time step ago
        float const prevCellIndex =
            static_cast<float>(i)
            - v * GameParameters::SimulationStepTimeDuration<float> / Dx;

        // Transform index to ease interpolations, constraining the cell
        // to our grid at the same time
        float const prevCellIndex2 = std::min(
            std::max(0.0f, prevCellIndex),
            static_cast<float>(SWETotalSamples - 1));

        // Calculate integral and fractional parts of the index
        auto const prevCellIndexI = FastTruncateToArchInt(prevCellIndex2);
        float const prevCellIndexF = prevCellIndex2 - prevCellIndexI;
        assert(prevCellIndexF >= 0.0f && prevCellIndexF < 1.0f);

        // Set this height field sample as the previous (in time) sample,
        // interpolated between its two neighbors
        mNextHeightField[i] =
            (1.0f - prevCellIndexF) * mCurrentHeightField[prevCellIndexI]
            + prevCellIndexF * mCurrentHeightField[prevCellIndexI + 1];
    }
}

void OceanSurface::AdvectVelocityField()
{
    //
    // Semi-Lagrangian method
    //

    // Process all velocity samples, except for boundary condition samples
    //
    // Note: the last velocity sample is the one after the last height field sample
    for (size_t i = SWEBoundaryConditionsSamples; i <= SWETotalSamples - SWEBoundaryConditionsSamples; ++i)
    {
        // Velocity values are at the edges of the cell
        float const v = mCurrentVelocityField[i];

        // Calculate the (fractional) index that this velocity sample had one time step ago
        float const prevCellIndex =
            static_cast<float>(i)
            - v * GameParameters::SimulationStepTimeDuration<float> / Dx;

        // Transform index to ease interpolations, constraining the cell
        // to our grid at the same time
        float const prevCellIndex2 = std::min(
            std::max(0.0f, prevCellIndex),
            static_cast<float>(SWETotalSamples - 1));

        // Calculate integral and fractional parts of the index
        auto const prevCellIndexI = FastTruncateToArchInt(prevCellIndex2);
        float const prevCellIndexF = prevCellIndex2 - prevCellIndexI;
        assert(prevCellIndexF >= 0.0f && prevCellIndexF < 1.0f);

        // Set this velocity field sample as the previous (in time) sample,
        // interpolated between its two neighbors
        mNextVelocityField[i] =
            (1.0f - prevCellIndexF) * mCurrentVelocityField[prevCellIndexI]
            + prevCellIndexF * mCurrentVelocityField[prevCellIndexI + 1];
    }
}
*/

void OceanSurface::ApplyDampingBoundaryConditions()
{
    for (size_t i = 0; i < SWEBoundaryConditionsSamples; ++i)
    {
        float const damping = static_cast<float>(i) / static_cast<float>(SWEBoundaryConditionsSamples);

        mHeightField[i] =
            (mHeightField[i] - SWEHeightFieldOffset) * damping
            + SWEHeightFieldOffset;

        mVelocityField[i] *= damping;

        mHeightField[SWEOuterLayerSamples + SamplesCount + SWEOuterLayerSamples - 1 - i] =
            (mHeightField[SWEOuterLayerSamples + SamplesCount + SWEOuterLayerSamples - 1 - i] - SWEHeightFieldOffset) * damping
            + SWEHeightFieldOffset;

        // For symmetry we actually damp the v-sample after this height field sample
        mVelocityField[SWEOuterLayerSamples + SamplesCount + SWEOuterLayerSamples - 1 - i + 1] *= damping;
    }

}

void OceanSurface::UpdateFields()
{
    //
    // 1. Incorporate delta-height into height field, after smoothing
    //
    // We use a two-pass average on a window of width DeltaHeightSmoothing,
    // centered on the sample
    //

    // TODOTEST: we got a field clear when using i <= SamplesCount
    for (size_t i = 0; i < SamplesCount; ++i)
    {
        // Central samples
        float accumulatedHeight = mDeltaHeightBuffer[i] * static_cast<float>((DeltaHeightSmoothing / 2) + 1);

        // Lateral samples - l is offset from central
        for (size_t l = 1; l <= DeltaHeightSmoothing / 2; ++l)
        {
            float const lateralWeight = static_cast<float>((DeltaHeightSmoothing / 2) + 1 - l);

            accumulatedHeight +=
                mDeltaHeightBuffer[i - l] * lateralWeight
                + mDeltaHeightBuffer[i + l] * lateralWeight;
        }

        // Update height field
        mHeightField[SWEOuterLayerSamples + i] +=
            (1.0f / static_cast<float>(DeltaHeightSmoothing))
            * (1.0f / static_cast<float>(DeltaHeightSmoothing))
            * accumulatedHeight;

        // TODOTEST : sets height field equal to delta-height
        //mHeightField[SWEOuterLayerSamples + i] = SWEHeightFieldOffset + mDeltaHeightBuffer[i];

        // TODOTEST : sets height field equal to smoothed delta-height
        ////mHeightField[SWEOuterLayerSamples + i] = SWEHeightFieldOffset +
        ////    (1.0f / static_cast<float>(DeltaHeightSmoothing))
        ////    * (1.0f / static_cast<float>(DeltaHeightSmoothing))
        ////    * accumulatedHeight;
    }

    //
    // 2. SWE Update
    //
    // Height field  : from 0 to SWETotalSamples
    // Velocity field: from 1 to SWETotalSamples
    //

    // We will divide deltaField by Dx (spatial derivatives) and
    // then multiply by dt (because we are integrating over time)
    float constexpr FactorH = GameParameters::SimulationStepTimeDuration<float> / Dx;
    float constexpr FactorV = FactorH * GameParameters::GravityMagnitude;

    mHeightField[0] -=
        mHeightField[0]
        * (mVelocityField[0 + 1] - mVelocityField[0])
        * FactorH;

    for (size_t i = 1; i < SWETotalSamples; ++i)
    {
        mHeightField[i] -=
            mHeightField[i]
            * (mVelocityField[i + 1] - mVelocityField[i])
            * FactorH;

        mVelocityField[i] +=
            (mHeightField[i - 1] - mHeightField[i])
            * FactorV;
    }

    //
    // 3. Clear delta-height buffer
    //

    mDeltaHeightBuffer.fill(0.0f);
}

void OceanSurface::GenerateSamples(
    float currentSimulationTime,
    Wind const & wind,
    GameParameters const & /*gameParameters*/)
{
    //
    // Sample values are a combination of:
    //  - SWE's height field
    //  - Basal waves
    //  - Wind gust ripples
    //

    // Secondary basal component
    float const secondaryBasalComponentPhase = Pi<float> * sin(currentSimulationTime);

    //
    // Wind gust ripples
    //

    float constexpr WindRippleWaveNumber = 2.0f; // # waves per unit of length
    float constexpr WindRippleWaveHeight = 0.125f;

    float const windSpeedAbsoluteMagnitude = wind.GetCurrentWindSpeed().length();
    float const windSpeedGustRelativeAmplitude = wind.GetMaxSpeedMagnitude() - wind.GetBaseAndStormSpeedMagnitude();
    float const rawWindNormalizedIncisiveness = (windSpeedGustRelativeAmplitude == 0.0f)
        ? 0.0f
        : std::max(0.0f, windSpeedAbsoluteMagnitude - std::abs(wind.GetBaseAndStormSpeedMagnitude()))
        / std::abs(windSpeedGustRelativeAmplitude);

    float const windRipplesAngularVelocity = (wind.GetBaseAndStormSpeedMagnitude() >= 0)
        ? 128.0f
        : -128.0f;

    float const smoothedWindNormalizedIncisiveness = mWindIncisivenessRunningAverage.Update(rawWindNormalizedIncisiveness);
    float const windRipplesWaveHeight = WindRippleWaveHeight * smoothedWindNormalizedIncisiveness;


    //
    // Generate samples
    //

    float const x = -GameParameters::HalfMaxWorldWidth;

    float const basalWave2AmplitudeCoeff =
        (mBasalWaveAmplitude1 != 0.0f)
        ? mBasalWaveAmplitude2 / mBasalWaveAmplitude1
        : 0.0f;

    float const rippleWaveAmplitudeCoeff =
        (mBasalWaveAmplitude1 != 0.0f)
        ? windRipplesWaveHeight / mBasalWaveAmplitude1
        : 0.0f;

    float sinArg1 = (mBasalWaveNumber1 * x - mBasalWaveAngularVelocity1 * currentSimulationTime) / (2 * Pi<float>);
    float sinArg2 = (mBasalWaveNumber2 * x - mBasalWaveAngularVelocity2 * currentSimulationTime + secondaryBasalComponentPhase) / (2 * Pi<float>);
    float sinArgRipple = (WindRippleWaveNumber * x - windRipplesAngularVelocity * currentSimulationTime) / (2 * Pi<float>);

    // sample index = 0
    float previousSampleValue;
    {
        float const sweValue =
            (mHeightField[SWEOuterLayerSamples + 0] - SWEHeightFieldOffset)
            * SWEHeightFieldAmplification;

        float const basalValue1 =
            mBasalWaveSin1.GetLinearlyInterpolatedPeriodic(sinArg1);

        float const basalValue2 =
            basalWave2AmplitudeCoeff
            * mBasalWaveSin1.GetLinearlyInterpolatedPeriodic(sinArg2);

        float const rippleValue =
            rippleWaveAmplitudeCoeff
            * mBasalWaveSin1.GetLinearlyInterpolatedPeriodic(sinArgRipple);

        previousSampleValue =
            sweValue
            + basalValue1
            + basalValue2
            + rippleValue;

        mSamples[0].SampleValue = previousSampleValue;
    }

    float const sinArg1Dx = mBasalWaveNumber1 * Dx / (2 * Pi<float>);
    float const sinArg2Dx = mBasalWaveNumber2 * Dx / (2 * Pi<float>);
    float const sinArgRippleDx = WindRippleWaveNumber * Dx / (2 * Pi<float>);

    // sample index = 1...SamplesCount - 1
    for (size_t i = 1; i < SamplesCount; ++i)
    {
        float const sweValue =
            (mHeightField[SWEOuterLayerSamples + i] - SWEHeightFieldOffset)
            * SWEHeightFieldAmplification;

        sinArg1 += sinArg1Dx;
        float const basalValue1 =
            mBasalWaveSin1.GetLinearlyInterpolatedPeriodic(sinArg1);

        sinArg2 += sinArg2Dx;
        float const basalValue2 =
            basalWave2AmplitudeCoeff
            * mBasalWaveSin1.GetLinearlyInterpolatedPeriodic(sinArg2);

        sinArgRipple += sinArgRippleDx;
        float const rippleValue =
            rippleWaveAmplitudeCoeff
            * mBasalWaveSin1.GetLinearlyInterpolatedPeriodic(sinArgRipple);

        float const sampleValue =
            sweValue
            + basalValue1
            + basalValue2
            + rippleValue;

        mSamples[i].SampleValue = sampleValue;
        mSamples[i - 1].SampleValuePlusOneMinusSampleValue = sampleValue - previousSampleValue;

        previousSampleValue = sampleValue;
    }

    // Populate last delta (extra sample will have same value as this sample)
    mSamples[SamplesCount - 1].SampleValuePlusOneMinusSampleValue = 0.0f;

    // Populate extra sample - same value as last sample
    assert(previousSampleValue == mSamples[SamplesCount - 1].SampleValue);
    mSamples[SamplesCount].SampleValue = previousSampleValue;

    assert(mSamples[SamplesCount].SampleValuePlusOneMinusSampleValue == 0.0f);
}

///////////////////////////////////////////////////////////////////////////////////////////

OceanSurface::SWEInteractiveWaveStateMachine::SWEInteractiveWaveStateMachine(
    size_t centerIndex,
    float startHeight,
    float targetHeight,
    float currentSimulationTime)
    : mCenterIndex(centerIndex)
    , mOriginalHeight(startHeight)
    , mCurrentPhaseStartHeight(startHeight)
    , mCurrentPhaseTargetHeight(targetHeight)
    , mCurrentHeight(startHeight)
    , mStartSimulationTime(currentSimulationTime)
    , mCurrentWavePhase(WavePhaseType::Rise)
    , mRisingPhaseDuration(CalculateRisingPhaseDuration(targetHeight - mOriginalHeight))
    , mFallingPhaseDecayCoefficient(0.0f) // Will be calculated when needed
{
}

void OceanSurface::SWEInteractiveWaveStateMachine::Restart(
    float restartHeight,
    float currentSimulationTime)
{
    if (mCurrentWavePhase == WavePhaseType::Rise)
    {
        // Restart during rise...

        // ...extend the current smoothing, keeping the following invariants:
        // - The current value
        // - The current time
        // - The "slope" at the current time

        // Calculate current timestamp as fraction of duration
        //
        // We need to make sure we're not too close to 1.0f, or else
        // values start diverging too much.
        // We may safely clamp down to 0.9 as the value will stay and the slope
        // will only change marginally.
        float const elapsed = currentSimulationTime - mStartSimulationTime;
        float const progressFraction = std::min(
            elapsed / mRisingPhaseDuration,
            0.9f);

        // Calculate new duration which would be required to go
        // from where we started from, up to our new target
        float const newDuration = CalculateRisingPhaseDuration(restartHeight - mOriginalHeight);

        // Calculate fictitious start timestamp so that current elapsed is
        // to old duration like new elapsed would be to new duration
        mStartSimulationTime = currentSimulationTime - newDuration * progressFraction;

        // Our new target is the restart target
        mCurrentPhaseTargetHeight = restartHeight;

        // Calculate fictitious start value so that calculated current value
        // at current timestamp matches current value:
        //  newStartValue = currentValue - f(newEndValue - newStartValue)
        float const valueFraction = SmoothStep(0.0f, 1.0f, progressFraction);
        mCurrentPhaseStartHeight =
            (mCurrentHeight - mCurrentPhaseTargetHeight * valueFraction)
            / (1.0f - valueFraction);

        // Store new duration
        mRisingPhaseDuration = newDuration;
    }
    else
    {
        // Restart during fall...

        // ...start rising from scratch
        mCurrentPhaseStartHeight = mCurrentHeight;
        mCurrentPhaseTargetHeight = restartHeight;
        mStartSimulationTime = currentSimulationTime;
        mCurrentWavePhase = WavePhaseType::Rise;

        mRisingPhaseDuration = CalculateRisingPhaseDuration(restartHeight - mOriginalHeight);
    }
}

void OceanSurface::SWEInteractiveWaveStateMachine::Release(float currentSimulationTime)
{
    assert(mCurrentWavePhase == WavePhaseType::Rise);

    // Start falling back to original height
    mCurrentPhaseStartHeight = mCurrentHeight;
    mCurrentPhaseTargetHeight = mOriginalHeight;
    mStartSimulationTime = currentSimulationTime;
    mCurrentWavePhase = WavePhaseType::Fall;

    // Calculate decay coefficient based on delta to fall
    mFallingPhaseDecayCoefficient = CalculateFallingPhaseDecayCoefficient(mCurrentHeight - mOriginalHeight);
}

std::optional<float> OceanSurface::SWEInteractiveWaveStateMachine::Update(
    float currentSimulationTime)
{
    if (mCurrentWavePhase == WavePhaseType::Rise)
    {
        float const elapsed = currentSimulationTime - mStartSimulationTime;

        // Calculate height as f(elapsed)

        float const smoothFactor = SmoothStep(
            0.0f,
            mRisingPhaseDuration,
            elapsed);

        mCurrentHeight =
            mCurrentPhaseStartHeight + (mCurrentPhaseTargetHeight - mCurrentPhaseStartHeight) * smoothFactor;

        return mCurrentHeight;
    }
    else
    {
        assert(mCurrentWavePhase == WavePhaseType::Fall);

        // Calculate height with decay process


        mCurrentHeight += (mCurrentPhaseTargetHeight - mCurrentHeight) * mFallingPhaseDecayCoefficient;

        // Check whether it's time to shut down
        if (std::abs(mCurrentPhaseTargetHeight - mCurrentHeight) < 0.001f)
        {
            return std::nullopt;
        }

        return mCurrentHeight;
    }
}

bool OceanSurface::SWEInteractiveWaveStateMachine::MayBeOverridden() const
{
    return mCurrentWavePhase == WavePhaseType::Fall
        && std::abs(mCurrentPhaseTargetHeight - mCurrentHeight) < 0.2f;
}

float OceanSurface::SWEInteractiveWaveStateMachine::CalculateRisingPhaseDuration(float deltaHeight)
{
    // We want very little rises to be quick, so they generate nice ripples on the surface.
    // We want large rises to be small, so that we don't generate height slopes that are
    // too steep.
    //
    // From empirical observations, we want the following fixed points:
    //  deltaH = 0.00:  duration = 0.00
    //  deltaH = 0.01:  duration = 0.13
    //  deltaH =  0.1:  duration ~= 1.5
    //  deltaH =  0.5:  duration = 2.5

    // y = 2.53079 - 2.572298*e^(-9.031207*x)
    return std::max(2.53079f - 2.572298f * std::exp(-9.031207f * std::abs(deltaHeight)), 0.0f);
}

float OceanSurface::SWEInteractiveWaveStateMachine::CalculateFallingPhaseDecayCoefficient(float deltaHeight)
{
    // When delta is very small, we want to converge very fast - but not too much
    // or else spiky ripples occur;
    // when delta is wide enough, we're fine with 0.025.
    return 0.65f - (0.65f - 0.025f) * SmoothStep(0.0f, 0.1f, std::abs(deltaHeight));
}

///////////////////////////////////////////////////////////////////////////////////////////

OceanSurface::SWEAbnormalWaveStateMachine::SWEAbnormalWaveStateMachine(
    size_t centerIndex,
    float lowHeight,
    float highHeight,
    float riseDelay, // sec
    float fallDelay, // sec
    float currentSimulationTime)
    : mCenterIndex(centerIndex)
    , mLowHeight(lowHeight)
    , mHighHeight(highHeight)
    , mFallDelay(fallDelay)
    , mCurrentProgress(0.0f)
    , mCurrentPhaseStartSimulationTime(currentSimulationTime)
    , mCurrentPhaseDelay(riseDelay)
    , mCurrentWavePhase(WavePhaseType::Rise)
{}

std::optional<float> OceanSurface::SWEAbnormalWaveStateMachine::Update(
    float currentSimulationTime)
{
    // Advance
    mCurrentProgress =
        (currentSimulationTime - mCurrentPhaseStartSimulationTime)
        / mCurrentPhaseDelay;

    // Calculate sinusoidal progress
    float const sinProgress = sin(Pi<float> / 2.0f * std::min(mCurrentProgress, 1.0f));

    // Calculate new height value
    float currentHeight =
        (WavePhaseType::Rise == mCurrentWavePhase)
        ? mLowHeight + (mHighHeight - mLowHeight) * sinProgress
        : mHighHeight - (mHighHeight - mLowHeight) * sinProgress;

    // Check whether it's time to switch phase
    if (mCurrentProgress >= 1.0f)
    {
        if (mCurrentWavePhase == WavePhaseType::Rise)
        {
            // Start falling
            mCurrentProgress = 0.0f;
            mCurrentPhaseStartSimulationTime = currentSimulationTime;
            mCurrentPhaseDelay = mFallDelay;
            mCurrentWavePhase = WavePhaseType::Fall;
        }
        else
        {
            // We're done
            return std::nullopt;
        }
    }

    return currentHeight;
}

}