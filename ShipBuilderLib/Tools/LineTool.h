/***************************************************************************************
* Original Author:		Gabriele Giuseppini
* Created:				2021-12-01
* Copyright:			Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
***************************************************************************************/
#pragma once

#include "Model.h"
#include "Tool.h"

#include <Game/Layers.h>
#include <Game/Materials.h>
#include <Game/ResourceLocator.h>

#include <GameCore/Finalizer.h>
#include <GameCore/GameTypes.h>

#include <memory>
#include <optional>

namespace ShipBuilder {

template<LayerType TLayer>
class LineTool : public Tool
{
public:

    ~LineTool();

    void OnMouseMove(ShipSpaceCoordinates const & mouseCoordinates) override;
    void OnLeftMouseDown() override;
    void OnLeftMouseUp() override;
    void OnRightMouseDown() override;
    void OnRightMouseUp() override;
    void OnShiftKeyDown() override {}
    void OnShiftKeyUp() override {}

protected:

    LineTool(
        ToolType toolType,
        ModelController & modelController,
        UndoStack & undoStack,
        WorkbenchState const & workbenchState,
        IUserInterface & userInterface,
        View & view,
        ResourceLocator const & resourceLocator);

private:

    using LayerMaterialType = typename LayerTypeTraits<TLayer>::material_type;

private:

    void StartEngagement(
        ShipSpaceCoordinates const & mouseCoordinates,
        MaterialPlaneType plane);

    void EndEngagement(ShipSpaceCoordinates const & mouseCoordinates);

    void DoEphemeralVisualization(ShipSpaceCoordinates const & mouseCoordinates);

    std::optional<ShipSpaceRect> CalculateApplicableRect(ShipSpaceCoordinates const & coords) const;

    int GetLineSize() const;

    inline LayerMaterialType const * GetFillMaterial(MaterialPlaneType plane) const;

private:

    // Original layer - taken at cctor and replaced after each edit operation
    typename LayerTypeTraits<TLayer>::layer_data_type mOriginalLayerClone;

    // Ephemeral visualization
    std::optional<Finalizer> mEphemeralVisualization;

    struct EngagementData
    {
        // Dirty state
        Model::DirtyState OriginalDirtyState;

        // Start point
        ShipSpaceCoordinates StartCoords;

        // Plane of the engagement
        MaterialPlaneType Plane;

        EngagementData(
            Model::DirtyState const & dirtyState,
            ShipSpaceCoordinates const & startCoords,
            MaterialPlaneType plane)
            : OriginalDirtyState(dirtyState)
            , StartCoords(startCoords)
            , Plane(plane)
        {}
    };

    // Engagement data - when set, it means we're engaged
    std::optional<EngagementData> mEngagementData;
};

class StructuralLineTool : public LineTool<LayerType::Structural>
{
public:

    StructuralLineTool(
        ModelController & modelController,
        UndoStack & undoStack,
        WorkbenchState const & workbenchState,
        IUserInterface & userInterface,
        View & view,
        ResourceLocator const & resourceLocator);
};

class ElectricalLineTool : public LineTool<LayerType::Electrical>
{
public:

    ElectricalLineTool(
        ModelController & modelController,
        UndoStack & undoStack,
        WorkbenchState const & workbenchState,
        IUserInterface & userInterface,
        View & view,
        ResourceLocator const & resourceLocator);
};

}