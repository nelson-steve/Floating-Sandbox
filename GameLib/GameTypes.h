/***************************************************************************************
* Original Author:		Gabriele Giuseppini
* Created:				2018-03-25
* Copyright:			Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
***************************************************************************************/
#pragma once

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

/*
 * These types define the cardinality of elements in the ElementContainer.
 *
 * Indices are equivalent to pointers in OO terms. Given that we don't believe
 * we'll ever have more than 4 billion elements, a 32-bit integer suffices.
 *
 * This also implies that where we used to store one pointer, we can now store two indices,
 * resulting in even better data locality.
 */
using ElementCount = std::uint32_t;
using ElementIndex = std::uint32_t;
static constexpr ElementIndex NoneElementIndex = std::numeric_limits<ElementIndex>::max();

/*
 * Connected component identifiers. 
 *
 * Comparable and ordered. Start from 1.
 */
using ConnectedComponentId = std::uint32_t; 

/*
 * Object ID's, as generated by the ObjectID generator.
 *
 * Not comparable, not ordered.
 */
using ObjectId = std::uint32_t;

/*
 * Graph visit sequence numbers. 
 *
 * Equatable. Never zero.
 */
using VisitSequenceNumber = std::uint32_t; 
static constexpr VisitSequenceNumber NoneVisitSequenceNumber = 0;

/*
 * Types of bombs (duh).
 */
enum class BombType
{
    TimerBomb,
    RCBomb
};

/*
 * Generic duration enum - short and long.
 */
enum class DurationShortLongType
{
    Short,
    Long
};

DurationShortLongType StrToDurationShortLongType(std::string const & str);

////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering
////////////////////////////////////////////////////////////////////////////////////////////////

/*
 * The different ways in which ships may be rendered.
 */
enum class ShipRenderMode
{
    Points,
    Springs,
    Structure,
    Texture
};

/*
 * The different vector fields that may be rendered.
 */
enum class VectorFieldRenderMode
{
    None,
    PointVelocity,
    PointWaterVelocity,
    PointWaterMomentum
};

/*
 * The texture groups we support.
 */
enum class TextureGroupType : uint16_t
{
    Cloud = 0,
    Land = 1,
    PinnedPoint = 2,
    RcBomb = 3,
    RcBombExplosion = 4,
    RcBombPing = 5,
    TimerBomb = 6,
    TimerBombDefuse = 7,
    TimerBombExplosion = 8,
    TimerBombFuse = 9,
    Water = 10,

    _Count = 11
};

/*
 * The type of an index in a group of textures.
 */
using TextureFrameIndex = std::uint16_t;

/*
 * Describes the global identifier of a texture frame.
 */
struct TextureFrameId
{
    TextureGroupType Group;
    TextureFrameIndex FrameIndex;

    TextureFrameId(
        TextureGroupType group,
        TextureFrameIndex frameIndex)
        : Group(group)
        , FrameIndex(frameIndex)
    {}


    TextureFrameId & operator=(TextureFrameId const & other) = default;

    inline bool operator==(TextureFrameId const & other) const
    {
        return this->Group == other.Group
            && this->FrameIndex == other.FrameIndex;
    }

    inline bool operator<(TextureFrameId const & other) const
    {
        return this->Group < other.Group
            || (this->Group == other.Group && this->FrameIndex < other.FrameIndex);
    }

    std::string ToString() const
    {
        std::stringstream ss;

        ss << static_cast<int>(Group) << ":" << static_cast<int>(FrameIndex);

        return ss.str();
    }
};

/*
 * The different fonts available.
 */
enum class FontType
{
    // Indices must match suffix of filename
    StatusText = 0
};

/*
 * The positions at which text may be rendered.
 */
enum class TextPositionType
{
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

/*
 * The handle to "sticky" rendered text.
 */
using RenderedTextHandle = uint32_t;
static constexpr RenderedTextHandle NoneRenderedTextHandle = std::numeric_limits<RenderedTextHandle>::max();
