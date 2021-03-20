/***************************************************************************************
 * Original Author:     Gabriele Giuseppini
 * Created:             2018-01-21
 * Copyright:           Gabriele Giuseppini  (https://github.com/GabrieleGiuseppini)
 ***************************************************************************************/
#include "SettingsDialog.h"

#include "WxHelpers.h"

#include <GameCore/ExponentialSliderCore.h>
#include <GameCore/FixedTickSliderCore.h>
#include <GameCore/IntegralLinearSliderCore.h>
#include <GameCore/LinearSliderCore.h>
#include <GameCore/Log.h>

#include <wx/gbsizer.h>
#include <wx/intl.h>
#include <wx/notebook.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/string.h>

#include <algorithm>
#include <stdexcept>

#ifdef _MSC_VER
 // Nothing to do here - we use RC files
#else
#include "Resources/ShipBBB.xpm"
#endif

static int constexpr SliderWidth = 60;
static int constexpr SliderHeight = 140;

static int constexpr StaticBoxTopMargin = 7;
static int constexpr StaticBoxInsetMargin = 10;
static int constexpr CellBorder = 8;

namespace /* anonymous */ {

/*
 * Comparer for sorting persistent settings.
 */
struct PersistedSettingsComparer
{
    bool operator()(PersistedSettingsMetadata const & m1, PersistedSettingsMetadata const & m2)
    {
        // m1 < m2
        // Rules:
        // - All user first, system next
        // - Among user, LastModified is last

        if (m1.Key.StorageType != m2.Key.StorageType)
            return m2.Key.StorageType == PersistedSettingsStorageTypes::System;

        assert(m1.Key.StorageType == m2.Key.StorageType);

        if (m1.Key == PersistedSettingsKey::MakeLastModifiedSettingsKey() || m2.Key == PersistedSettingsKey::MakeLastModifiedSettingsKey())
            return m2.Key == PersistedSettingsKey::MakeLastModifiedSettingsKey();

        return m1.Key.Name < m2.Key.Name;
    }
};

}

SettingsDialog::SettingsDialog(
    wxWindow * parent,
    std::shared_ptr<SettingsManager> settingsManager,
    std::shared_ptr<IGameControllerSettingsOptions> gameControllerSettingsOptions,
    ResourceLocator const & resourceLocator)
    : mParent(parent)
    , mSettingsManager(std::move(settingsManager))
    , mGameControllerSettingsOptions(std::move(gameControllerSettingsOptions))
    // State
    , mLiveSettings(mSettingsManager->MakeSettings())
    , mCheckpointSettings(mSettingsManager->MakeSettings())
    , mPersistedSettings()
{
    Create(
        mParent,
        wxID_ANY,
        _("Simulation Settings"),
        wxDefaultPosition,
        wxSize(400, 200),
        wxCAPTION | wxCLOSE_BOX | wxMINIMIZE_BOX | wxFRAME_NO_TASKBAR
        | /* wxFRAME_FLOAT_ON_PARENT */ wxSTAY_ON_TOP, // See https://trac.wxwidgets.org/ticket/18535
        wxS("Settings Window"));

    this->Bind(wxEVT_CLOSE_WINDOW, &SettingsDialog::OnCloseButton, this);

    // Set font
    {
        auto font = parent->GetFont();
        font.SetPointSize(8);
        SetFont(font);
    }

    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));

    SetIcon(wxICON(BBB_SHIP_ICON));


    //
    // Populate and sort persisted settings
    //

    mPersistedSettings = mSettingsManager->ListPersistedSettings();
    PersistedSettingsComparer cmp;
    std::sort(
        mPersistedSettings.begin(),
        mPersistedSettings.end(),
        cmp);


    //
    // Load icons
    //

    mWarningIcon = std::make_unique<wxBitmap>(
        resourceLocator.GetIconFilePath("warning_icon").string(),
        wxBITMAP_TYPE_PNG);



    //
    // Lay the dialog out
    //

    wxBoxSizer * dialogVSizer = new wxBoxSizer(wxVERTICAL);


    wxNotebook * notebook = new wxNotebook(
        this,
        wxID_ANY,
        wxPoint(-1, -1),
        wxSize(-1, -1),
        wxNB_TOP);


    //
    // Mechanics and Thermodynamics
    //

    {
        wxPanel * panel = new wxPanel(notebook);

        PopulateMechanicsAndThermodynamicsPanel(panel);

        notebook->AddPage(panel, _("Mechanics and Thermodynamics"));
    }

    /* TODOOLD
    //
    // Heat
    //

    wxPanel * heatPanel = new wxPanel(notebook);

    PopulateHeatPanel(heatPanel);

    notebook->AddPage(heatPanel, _("Heat and Combustion"));


    //
    // Ocean, Smoke, Sky
    //

    wxPanel * oceanSmokeSkyPanel = new wxPanel(notebook);

    PopulateOceanSmokeSkyPanel(oceanSmokeSkyPanel);

    notebook->AddPage(oceanSmokeSkyPanel, _("Ocean, Smoke, and Sky"));


    //
    // Wind, Waves, Fishes, Lights
    //

    wxPanel * windWavesFishesLightsPanel = new wxPanel(notebook);

    PopulateWindWavesFishesLightsPanel(windWavesFishesLightsPanel);

    notebook->AddPage(windWavesFishesLightsPanel, _("Wind, Waves, Fishes, and Lights"));


    //
    // Interactions
    //

    wxPanel * interactionsPanel = new wxPanel(notebook);

    PopulateInteractionsPanel(interactionsPanel);

    notebook->AddPage(interactionsPanel, _("Interactions"));


    //
    // Rendering
    //

    wxPanel * renderingPanel = new wxPanel(notebook);

    PopulateRenderingPanel(renderingPanel);

    notebook->AddPage(renderingPanel, _("Rendering"));


    //
    // Sound and Advanced
    //

    wxPanel * soundAndAdvancedPanel = new wxPanel(notebook);

    PopulateSoundAndAdvancedPanel(soundAndAdvancedPanel);

    notebook->AddPage(soundAndAdvancedPanel, _("Sound and Advanced Settings"));


    //
    // Settings Management
    //

    wxPanel * settingsManagementPanel = new wxPanel(notebook);

    PopulateSettingsManagementPanel(settingsManagementPanel);

    notebook->AddPage(settingsManagementPanel, _("Settings Management"));
    */


    dialogVSizer->Add(notebook, 0, wxEXPAND);

    dialogVSizer->AddSpacer(20);


    // Buttons

    {
        wxBoxSizer * buttonsSizer = new wxBoxSizer(wxHORIZONTAL);

        buttonsSizer->AddSpacer(20);

        mRevertToDefaultsButton = new wxButton(this, wxID_ANY, _("Revert to Defaults"));
        mRevertToDefaultsButton->SetToolTip(_("Resets all settings to their default values."));
        mRevertToDefaultsButton->Bind(wxEVT_BUTTON, &SettingsDialog::OnRevertToDefaultsButton, this);
        buttonsSizer->Add(mRevertToDefaultsButton, 0, 0, 0);

        buttonsSizer->AddStretchSpacer(1);

        mOkButton = new wxButton(this, wxID_ANY, _("OK"));
        mOkButton->SetToolTip(_("Closes the window keeping all changes."));
        mOkButton->Bind(wxEVT_BUTTON, &SettingsDialog::OnOkButton, this);
        buttonsSizer->Add(mOkButton, 0, 0, 0);

        buttonsSizer->AddSpacer(20);

        mCancelButton = new wxButton(this, wxID_ANY, _("Cancel"));
        mCancelButton->SetToolTip(_("Reverts all changes effected since the window was last opened, and closes the window."));
        mCancelButton->Bind(wxEVT_BUTTON, &SettingsDialog::OnCancelButton, this);
        buttonsSizer->Add(mCancelButton, 0, 0, 0);

        buttonsSizer->AddSpacer(20);

        mUndoButton = new wxButton(this, wxID_ANY, _("Undo"));
        mUndoButton->SetToolTip(_("Reverts all changes effected since the window was last opened."));
        mUndoButton->Bind(wxEVT_BUTTON, &SettingsDialog::OnUndoButton, this);
        buttonsSizer->Add(mUndoButton, 0, 0, 0);

        buttonsSizer->AddSpacer(20);

        dialogVSizer->Add(buttonsSizer, 0, wxEXPAND, 0);
    }

    dialogVSizer->AddSpacer(20);


    //
    // Finalize dialog
    //

    SetSizerAndFit(dialogVSizer);

    Centre(wxCENTER_ON_SCREEN | wxBOTH);
}

SettingsDialog::~SettingsDialog()
{
}

void SettingsDialog::Open()
{
    if (IsShown())
        return; // Handle Ctrl^S while minimized

    assert(!!mSettingsManager);

    //
    // Initialize state
    //

    // Pull currently-enforced settings
    mSettingsManager->Pull(mLiveSettings);
    mLiveSettings.ClearAllDirty();

    // Save checkpoint for undo
    mCheckpointSettings = mLiveSettings;

    // Populate controls with live settings
    SyncControlsWithSettings(mLiveSettings);

    // Remember that the user hasn't changed anything yet in this session
    mHasBeenDirtyInCurrentSession = false;

    // Enable Revert to Defaults button only if settings are different than defaults
    mAreSettingsDirtyWrtDefaults = (mLiveSettings != mSettingsManager->GetDefaults());

    // Reconcile controls wrt dirty state
    ReconcileDirtyState();


    //
    // Open dialog
    //

    this->Raise();
    this->Show();
}

////TODOOLD
////void SettingsDialog::OnRestoreDefaultTerrainButton(wxCommandEvent & /*event*/)
////{
////    mLiveSettings.ClearAllDirty();
////
////    mLiveSettings.SetValue<OceanFloorTerrain>(
////        GameSettings::OceanFloorTerrain,
////        mSettingsManager->GetDefaults().GetValue<OceanFloorTerrain>(GameSettings::OceanFloorTerrain));
////
////    OnLiveSettingsChanged();
////}
////
////void SettingsDialog::OnOceanRenderModeRadioButtonClick(wxCommandEvent & /*event*/)
////{
////    if (mTextureOceanRenderModeRadioButton->GetValue())
////    {
////        mLiveSettings.SetValue(GameSettings::OceanRenderMode, OceanRenderModeType::Texture);
////    }
////    else if (mDepthOceanRenderModeRadioButton->GetValue())
////    {
////        mLiveSettings.SetValue(GameSettings::OceanRenderMode, OceanRenderModeType::Depth);
////    }
////    else
////    {
////        assert(mFlatOceanRenderModeRadioButton->GetValue());
////        mLiveSettings.SetValue(GameSettings::OceanRenderMode, OceanRenderModeType::Flat);
////    }
////
////    OnLiveSettingsChanged();
////
////    ReconciliateOceanRenderModeSettings();
////}
////
////void SettingsDialog::OnDepthOceanColorStartChanged(wxColourPickerEvent & event)
////{
////    auto color = event.GetColour();
////
////    mLiveSettings.SetValue(
////        GameSettings::DepthOceanColorStart,
////        rgbColor(color.Red(), color.Green(), color.Blue()));
////
////    OnLiveSettingsChanged();
////}
////
////void SettingsDialog::OnDepthOceanColorEndChanged(wxColourPickerEvent & event)
////{
////    auto color = event.GetColour();
////
////    mLiveSettings.SetValue(
////        GameSettings::DepthOceanColorEnd,
////        rgbColor(color.Red(), color.Green(), color.Blue()));
////
////    OnLiveSettingsChanged();
////}
////
////void SettingsDialog::OnFlatOceanColorChanged(wxColourPickerEvent & event)
////{
////    auto color = event.GetColour();
////
////    mLiveSettings.SetValue(
////        GameSettings::FlatOceanColor,
////        rgbColor(color.Red(), color.Green(), color.Blue()));
////
////    OnLiveSettingsChanged();
////}
////
////void SettingsDialog::OnLandRenderModeRadioButtonClick(wxCommandEvent & /*event*/)
////{
////    if (mTextureLandRenderModeRadioButton->GetValue())
////    {
////        mLiveSettings.SetValue(GameSettings::LandRenderMode, LandRenderModeType::Texture);
////    }
////    else
////    {
////        assert(mFlatLandRenderModeRadioButton->GetValue());
////        mLiveSettings.SetValue(GameSettings::LandRenderMode, LandRenderModeType::Flat);
////    }
////
////    ReconciliateLandRenderModeSettings();
////
////    OnLiveSettingsChanged();
////}
////
////void SettingsDialog::OnFlatLandColorChanged(wxColourPickerEvent & event)
////{
////    auto color = event.GetColour();
////
////    mLiveSettings.SetValue(
////        GameSettings::FlatLandColor,
////        rgbColor(color.Red(), color.Green(), color.Blue()));
////
////    OnLiveSettingsChanged();
////}
////
////void SettingsDialog::OnFlatSkyColorChanged(wxColourPickerEvent & event)
////{
////    auto color = event.GetColour();
////
////    mLiveSettings.SetValue(
////        GameSettings::FlatSkyColor,
////        rgbColor(color.Red(), color.Green(), color.Blue()));
////
////    OnLiveSettingsChanged();
////}
////
////void SettingsDialog::OnFlatLampLightColorChanged(wxColourPickerEvent & event)
////{
////    auto color = event.GetColour();
////
////    mLiveSettings.SetValue(
////        GameSettings::FlatLampLightColor,
////        rgbColor(color.Red(), color.Green(), color.Blue()));
////
////    OnLiveSettingsChanged();
////}
////
////void SettingsDialog::OnHeatRenderModeRadioBox(wxCommandEvent & /*event*/)
////{
////    auto selectedHeatRenderMode = mHeatRenderModeRadioBox->GetSelection();
////    if (0 == selectedHeatRenderMode)
////    {
////        mLiveSettings.SetValue(GameSettings::HeatRenderMode, HeatRenderModeType::Incandescence);
////    }
////    else if (1 == selectedHeatRenderMode)
////    {
////        mLiveSettings.SetValue(GameSettings::HeatRenderMode, HeatRenderModeType::HeatOverlay);
////    }
////    else
////    {
////        assert(2 == selectedHeatRenderMode);
////        mLiveSettings.SetValue(GameSettings::HeatRenderMode, HeatRenderModeType::None);
////    }
////
////    mHeatSensitivitySlider->Enable(selectedHeatRenderMode != 2);
////
////    OnLiveSettingsChanged();
////}
////
////void SettingsDialog::OnDefaultWaterColorChanged(wxColourPickerEvent & event)
////{
////    auto color = event.GetColour();
////
////    mLiveSettings.SetValue(
////        GameSettings::DefaultWaterColor,
////        rgbColor(color.Red(), color.Green(), color.Blue()));
////
////    OnLiveSettingsChanged();
////}
////
////void SettingsDialog::OnDebugShipRenderModeRadioBox(wxCommandEvent & /*event*/)
////{
////    auto selectedDebugShipRenderMode = mDebugShipRenderModeRadioBox->GetSelection();
////    if (0 == selectedDebugShipRenderMode)
////    {
////        mLiveSettings.SetValue(GameSettings::DebugShipRenderMode, DebugShipRenderModeType::None);
////    }
////    else if (1 == selectedDebugShipRenderMode)
////    {
////        mLiveSettings.SetValue(GameSettings::DebugShipRenderMode, DebugShipRenderModeType::Wireframe);
////    }
////    else if (2 == selectedDebugShipRenderMode)
////    {
////        mLiveSettings.SetValue(GameSettings::DebugShipRenderMode, DebugShipRenderModeType::Points);
////    }
////    else if (3 == selectedDebugShipRenderMode)
////    {
////        mLiveSettings.SetValue(GameSettings::DebugShipRenderMode, DebugShipRenderModeType::Springs);
////    }
////    else if (4 == selectedDebugShipRenderMode)
////    {
////        mLiveSettings.SetValue(GameSettings::DebugShipRenderMode, DebugShipRenderModeType::EdgeSprings);
////    }
////    else if (5 == selectedDebugShipRenderMode)
////    {
////        mLiveSettings.SetValue(GameSettings::DebugShipRenderMode, DebugShipRenderModeType::Decay);
////    }
////    else
////    {
////        assert(6 == selectedDebugShipRenderMode);
////        mLiveSettings.SetValue(GameSettings::DebugShipRenderMode, DebugShipRenderModeType::Structure);
////    }
////
////    OnLiveSettingsChanged();
////}
////
////void SettingsDialog::OnVectorFieldRenderModeRadioBox(wxCommandEvent & /*event*/)
////{
////    auto selectedVectorFieldRenderMode = mVectorFieldRenderModeRadioBox->GetSelection();
////    switch (selectedVectorFieldRenderMode)
////    {
////    case 0:
////    {
////        mLiveSettings.SetValue(GameSettings::VectorFieldRenderMode, VectorFieldRenderModeType::None);
////        break;
////    }
////
////    case 1:
////    {
////        mLiveSettings.SetValue(GameSettings::VectorFieldRenderMode, VectorFieldRenderModeType::PointVelocity);
////        break;
////    }
////
////    case 2:
////    {
////        mLiveSettings.SetValue(GameSettings::VectorFieldRenderMode, VectorFieldRenderModeType::PointForce);
////        break;
////    }
////
////    case 3:
////    {
////        mLiveSettings.SetValue(GameSettings::VectorFieldRenderMode, VectorFieldRenderModeType::PointWaterVelocity);
////        break;
////    }
////
////    default:
////    {
////        assert(4 == selectedVectorFieldRenderMode);
////        mLiveSettings.SetValue(GameSettings::VectorFieldRenderMode, VectorFieldRenderModeType::PointWaterMomentum);
////        break;
////    }
////    }
////
////    OnLiveSettingsChanged();
////}
////
////void SettingsDialog::OnPersistedSettingsListCtrlSelected(wxListEvent & /*event*/)
////{
////    ReconciliateLoadPersistedSettings();
////}
////
////void SettingsDialog::OnPersistedSettingsListCtrlActivated(wxListEvent & event)
////{
////    assert(event.GetIndex() != wxNOT_FOUND);
////
////    LoadPersistedSettings(static_cast<size_t>(event.GetIndex()), true);
////}
////
////void SettingsDialog::OnApplyPersistedSettingsButton(wxCommandEvent & /*event*/)
////{
////    auto selectedIndex = GetSelectedPersistedSettingIndexFromCtrl();
////
////    assert(selectedIndex != wxNOT_FOUND); // Enforced by UI
////    assert(static_cast<size_t>(selectedIndex) < mPersistedSettings.size());
////
////    if (selectedIndex != wxNOT_FOUND)
////    {
////        LoadPersistedSettings(static_cast<size_t>(selectedIndex), false);
////    }
////}
////
////void SettingsDialog::OnRevertToPersistedSettingsButton(wxCommandEvent & /*event*/)
////{
////    auto selectedIndex = GetSelectedPersistedSettingIndexFromCtrl();
////
////    assert(selectedIndex != wxNOT_FOUND); // Enforced by UI
////    assert(static_cast<size_t>(selectedIndex) < mPersistedSettings.size());
////
////    if (selectedIndex != wxNOT_FOUND)
////    {
////        LoadPersistedSettings(static_cast<size_t>(selectedIndex), true);
////    }
////}
////
////void SettingsDialog::OnReplacePersistedSettingsButton(wxCommandEvent & /*event*/)
////{
////    auto selectedIndex = GetSelectedPersistedSettingIndexFromCtrl();
////
////    assert(selectedIndex != wxNOT_FOUND); // Enforced by UI
////    assert(static_cast<size_t>(selectedIndex) < mPersistedSettings.size());
////    assert(mPersistedSettings[selectedIndex].Key.StorageType == PersistedSettingsStorageTypes::User); // Enforced by UI
////
////    if (selectedIndex != wxNOT_FOUND)
////    {
////        auto const & metadata = mPersistedSettings[selectedIndex];
////
////        wxString message;
////        message.Printf(_("Are you sure you want to replace settings \"%s\" with the current settings?"), metadata.Key.Name.c_str());
////        auto result = wxMessageBox(
////            message,
////            _("Warning"),
////            wxCANCEL | wxOK);
////
////        if (result == wxOK)
////        {
////            // Save
////            SavePersistedSettings(metadata);
////
////            // Reconciliate load UI
////            ReconciliateLoadPersistedSettings();
////        }
////    }
////}
////
////void SettingsDialog::OnDeletePersistedSettingsButton(wxCommandEvent & /*event*/)
////{
////    auto selectedIndex = GetSelectedPersistedSettingIndexFromCtrl();
////
////    assert(selectedIndex != wxNOT_FOUND); // Enforced by UI
////    assert(static_cast<size_t>(selectedIndex) < mPersistedSettings.size());
////    assert(mPersistedSettings[selectedIndex].Key.StorageType == PersistedSettingsStorageTypes::User); // Enforced by UI
////
////    if (selectedIndex != wxNOT_FOUND)
////    {
////        auto const & metadata = mPersistedSettings[selectedIndex];
////
////        // Ask user whether they're sure
////        wxString message;
////        message.Printf(_("Are you sure you want to delete settings \"%s\"?"), metadata.Key.Name.c_str());
////        auto result = wxMessageBox(
////            message,
////            _("Warning"),
////            wxCANCEL | wxOK);
////
////        if (result == wxOK)
////        {
////            try
////            {
////                // Delete
////                mSettingsManager->DeletePersistedSettings(metadata.Key);
////            }
////            catch (std::runtime_error const & ex)
////            {
////                OnPersistenceError(std::string("Error deleting settings: ") + ex.what());
////                return;
////            }
////
////            // Remove from list box
////            mPersistedSettingsListCtrl->DeleteItem(selectedIndex);
////
////            // Remove from mPersistedSettings
////            mPersistedSettings.erase(mPersistedSettings.cbegin() + selectedIndex);
////
////            // Reconciliate with UI
////            ReconciliateLoadPersistedSettings();
////        }
////    }
////}
////
////void SettingsDialog::OnSaveSettingsTextEdited(wxCommandEvent & /*event*/)
////{
////    ReconciliateSavePersistedSettings();
////}
////
////void SettingsDialog::OnSaveSettingsButton(wxCommandEvent & /*event*/)
////{
////    assert(!mSaveSettingsNameTextCtrl->IsEmpty()); // Guaranteed by UI
////
////    if (mSaveSettingsNameTextCtrl->IsEmpty())
////        return;
////
////    auto settingsMetadata = PersistedSettingsMetadata(
////        PersistedSettingsKey(
////            mSaveSettingsNameTextCtrl->GetValue().ToStdString(),
////            PersistedSettingsStorageTypes::User),
////        mSaveSettingsDescriptionTextCtrl->GetValue().ToStdString());
////
////
////    //
////    // Check if settings with this name already exist
////    //
////
////    {
////        auto it = std::find_if(
////            mPersistedSettings.cbegin(),
////            mPersistedSettings.cend(),
////            [&settingsMetadata](auto const & sm)
////            {
////                return sm.Key == settingsMetadata.Key;
////            });
////
////        if (it != mPersistedSettings.cend())
////        {
////            // Ask user if sure
////            wxString message;
////            message.Printf(_("Settings \"%s\" already exist; do you want to replace them with the current settings?"), settingsMetadata.Key.Name.c_str());
////            auto result = wxMessageBox(
////                message,
////                _("Warning"),
////                wxCANCEL | wxOK);
////
////            if (result == wxCANCEL)
////            {
////                // Abort
////                return;
////            }
////        }
////    }
////
////    //
////    // Save settings
////    //
////
////    // Save
////    SavePersistedSettings(settingsMetadata);
////
////    // Find index for insertion
////    PersistedSettingsComparer cmp;
////    auto const it = std::lower_bound(
////        mPersistedSettings.begin(),
////        mPersistedSettings.end(),
////        settingsMetadata,
////        cmp);
////
////    if (it != mPersistedSettings.end()
////        && it->Key == settingsMetadata.Key)
////    {
////        // It's a replace
////
////        // Replace in persisted settings
////        it->Description = settingsMetadata.Description;
////    }
////    else
////    {
////        // It's an insert
////
////        auto const insertIdx = std::distance(mPersistedSettings.begin(), it);
////
////        // Insert into persisted settings
////        mPersistedSettings.insert(it, settingsMetadata);
////
////        // Insert in list control
////        InsertPersistedSettingInCtrl(insertIdx, settingsMetadata.Key);
////    }
////
////    // Reconciliate load UI
////    ReconciliateLoadPersistedSettings();
////
////    // Clear name and description
////    mSaveSettingsNameTextCtrl->Clear();
////    mSaveSettingsDescriptionTextCtrl->Clear();
////
////    // Reconciliate save UI
////    ReconciliateSavePersistedSettings();
////}

void SettingsDialog::OnRevertToDefaultsButton(wxCommandEvent & /*event*/)
{
    //
    // Enforce default settings
    //

    mLiveSettings = mSettingsManager->GetDefaults();

    // Do not update checkpoint, allow user to revert to it

    // Enforce everything as a safety net, immediately
    mLiveSettings.MarkAllAsDirty();
    mSettingsManager->EnforceDirtySettingsImmediate(mLiveSettings);

    // We are back in sync
    mLiveSettings.ClearAllDirty();

    assert(mSettingsManager->Pull() == mLiveSettings);

    // Re-populate controls with new values
    SyncControlsWithSettings(mLiveSettings);

    // Remember user has made changes wrt checkpoint
    mHasBeenDirtyInCurrentSession = true;

    // Remember we are clean now wrt defaults
    mAreSettingsDirtyWrtDefaults = false;

    ReconcileDirtyState();
}

void SettingsDialog::OnOkButton(wxCommandEvent & /*event*/)
{
    // Just close the dialog
    DoClose();
}

void SettingsDialog::OnCancelButton(wxCommandEvent & /*event*/)
{
    DoCancel();
}

void SettingsDialog::OnUndoButton(wxCommandEvent & /*event*/)
{
    assert(!!mSettingsManager);

    //
    // Undo changes done since last open, including eventual loads
    //

    mLiveSettings = mCheckpointSettings;

    // Just enforce anything in the checkpoint that is different than the current settings,
    // immediately
    mLiveSettings.SetDirtyWithDiff(mSettingsManager->Pull());
    mSettingsManager->EnforceDirtySettingsImmediate(mLiveSettings);

    mLiveSettings.ClearAllDirty();

    assert(mSettingsManager->Pull() == mCheckpointSettings);

    // Re-populate controls with new values
    SyncControlsWithSettings(mLiveSettings);

    // Remember we are clean now
    mHasBeenDirtyInCurrentSession = false;
    ReconcileDirtyState();
}

void SettingsDialog::OnCloseButton(wxCloseEvent & /*event*/)
{
    DoCancel();
}

/////////////////////////////////////////////////////////////////////////////

void SettingsDialog::DoCancel()
{
    assert(!!mSettingsManager);

    if (mHasBeenDirtyInCurrentSession)
    {
        //
        // Undo changes done since last open, including eventual loads
        //

        mLiveSettings = mCheckpointSettings;

        // Just enforce anything in the checkpoint that is different than the current settings,
        // immediately
        mLiveSettings.SetDirtyWithDiff(mSettingsManager->Pull());
        mSettingsManager->EnforceDirtySettingsImmediate(mLiveSettings);
    }

    //
    // Close the dialog
    //

    DoClose();
}

void SettingsDialog::DoClose()
{
    this->Hide();
}

void SettingsDialog::PopulateMechanicsAndThermodynamicsPanel(wxPanel * panel)
{
    wxGridBagSizer * gridSizer = new wxGridBagSizer(0, 0);

    //
    // Mechanics
    //

    {
        wxStaticBoxSizer * mechanicsBoxSizer = new wxStaticBoxSizer(wxVERTICAL, panel, _("Mechanics"));

        {
            wxGridBagSizer * mechanicsSizer = new wxGridBagSizer(0, 0);

            // Simulation Quality
            {
                mMechanicalQualitySlider = new SliderControl<float>(
                    mechanicsBoxSizer->GetStaticBox(),
                    SliderWidth,
                    SliderHeight,
                    _("Simulation Quality"),
                    _("Higher values improve the rigidity of simulated structures, at the expense of longer computation times."),
                    [this](float value)
                    {
                        this->mLiveSettings.SetValue(GameSettings::NumMechanicalDynamicsIterationsAdjustment, value);
                        this->OnLiveSettingsChanged();
                    },
                    std::make_unique<FixedTickSliderCore>(
                        0.5f,
                        mGameControllerSettingsOptions->GetMinNumMechanicalDynamicsIterationsAdjustment(),
                        mGameControllerSettingsOptions->GetMaxNumMechanicalDynamicsIterationsAdjustment()),
                    mWarningIcon.get());

                mechanicsSizer->Add(
                    mMechanicalQualitySlider,
                    wxGBPosition(0, 0),
                    wxGBSpan(1, 1),
                    wxEXPAND | wxALL,
                    CellBorder);
            }

            // Strength Adjust
            {
                mStrengthSlider = new SliderControl<float>(
                    mechanicsBoxSizer->GetStaticBox(),
                    SliderWidth,
                    SliderHeight,
                    _("Strength Adjust"),
                    _("Adjusts the breaking point of springs under stress. Has no effect on the rigidity of a ship."),
                    [this](float value)
                    {
                        this->mLiveSettings.SetValue(GameSettings::SpringStrengthAdjustment, value);
                        this->OnLiveSettingsChanged();
                    },
                    std::make_unique<ExponentialSliderCore>(
                        mGameControllerSettingsOptions->GetMinSpringStrengthAdjustment(),
                        1.0f,
                        mGameControllerSettingsOptions->GetMaxSpringStrengthAdjustment()));

                mechanicsSizer->Add(
                    mStrengthSlider,
                    wxGBPosition(0, 1),
                    wxGBSpan(1, 1),
                    wxEXPAND | wxALL,
                    CellBorder);
            }

            // Global Damping Adjust
            {
                mGlobalDampingAdjustmentSlider = new SliderControl<float>(
                    mechanicsBoxSizer->GetStaticBox(),
                    SliderWidth,
                    SliderHeight,
                    _("Global Damping Adjust"),
                    _("Adjusts the global damping of velocities."),
                    [this](float value)
                    {
                        this->mLiveSettings.SetValue(GameSettings::GlobalDampingAdjustment, value);
                        this->OnLiveSettingsChanged();
                    },
                    std::make_unique<ExponentialSliderCore>(
                        mGameControllerSettingsOptions->GetMinGlobalDampingAdjustment(),
                        1.0f,
                        mGameControllerSettingsOptions->GetMaxGlobalDampingAdjustment()));

                mechanicsSizer->Add(
                    mGlobalDampingAdjustmentSlider,
                    wxGBPosition(0, 2),
                    wxGBSpan(1, 1),
                    wxEXPAND | wxALL,
                    CellBorder);
            }

            mechanicsBoxSizer->Add(mechanicsSizer);
        }

        gridSizer->Add(
            mechanicsBoxSizer,
            wxGBPosition(0, 0),
            wxGBSpan(1, 3),
            wxEXPAND | wxALL,
            CellBorder);
    }

    //
    // Thermodynamics
    //

    {
        wxStaticBoxSizer * thermodynamicsBoxSizer = new wxStaticBoxSizer(wxVERTICAL, panel, _("Thermodynamics"));

        {
            wxGridBagSizer * thermodynamicsSizer = new wxGridBagSizer(0, 0);

            // Thermal Conductivity Adjustment
            {
                mThermalConductivityAdjustmentSlider = new SliderControl<float>(
                    thermodynamicsBoxSizer->GetStaticBox(),
                    SliderWidth,
                    SliderHeight,
                    _("Thermal Conductivity Adjust"),
                    _("Adjusts the speed with which heat propagates along materials."),
                    [this](float value)
                    {
                        this->mLiveSettings.SetValue(GameSettings::ThermalConductivityAdjustment, value);
                        this->OnLiveSettingsChanged();
                    },
                    std::make_unique<ExponentialSliderCore>(
                        mGameControllerSettingsOptions->GetMinThermalConductivityAdjustment(),
                        1.0f,
                        mGameControllerSettingsOptions->GetMaxThermalConductivityAdjustment()));

                thermodynamicsSizer->Add(
                    mThermalConductivityAdjustmentSlider,
                    wxGBPosition(0, 0),
                    wxGBSpan(1, 1),
                    wxEXPAND | wxALL,
                    CellBorder);
            }

            // Heat Dissipation Adjustment
            {
                mHeatDissipationAdjustmentSlider = new SliderControl<float>(
                    thermodynamicsBoxSizer->GetStaticBox(),
                    SliderWidth,
                    SliderHeight,
                    _("Heat Dissipation Adjust"),
                    _("Adjusts the speed with which materials dissipate or accumulate heat to or from air and water."),
                    [this](float value)
                    {
                        this->mLiveSettings.SetValue(GameSettings::HeatDissipationAdjustment, value);
                        this->OnLiveSettingsChanged();
                    },
                    std::make_unique<ExponentialSliderCore>(
                        mGameControllerSettingsOptions->GetMinHeatDissipationAdjustment(),
                        1.0f,
                        mGameControllerSettingsOptions->GetMaxHeatDissipationAdjustment()));

                thermodynamicsSizer->Add(
                    mHeatDissipationAdjustmentSlider,
                    wxGBPosition(0, 1),
                    wxGBSpan(1, 1),
                    wxEXPAND | wxALL,
                    CellBorder);
            }

            thermodynamicsBoxSizer->Add(thermodynamicsSizer, 0, wxALL, StaticBoxInsetMargin);
        }

        gridSizer->Add(
            thermodynamicsBoxSizer,
            wxGBPosition(0, 3),
            wxGBSpan(1, 2),
            wxEXPAND | wxALL,
            CellBorder);
    }

    //
    // Combustion
    //

    {
        wxStaticBoxSizer * combustionBoxSizer = new wxStaticBoxSizer(wxVERTICAL, panel, _("Combustion"));

        {
            wxGridBagSizer * combustionSizer = new wxGridBagSizer(0, 0);

            // Ignition Temperature Adjustment
            {
                mIgnitionTemperatureAdjustmentSlider = new SliderControl<float>(
                    combustionBoxSizer->GetStaticBox(),
                    SliderWidth,
                    SliderHeight,
                    _("Burning Point Adjust"),
                    _("Adjusts the temperature at which materials ignite."),
                    [this](float value)
                    {
                        this->mLiveSettings.SetValue(GameSettings::IgnitionTemperatureAdjustment, value);
                        this->OnLiveSettingsChanged();
                    },
                    std::make_unique<ExponentialSliderCore>(
                        mGameControllerSettingsOptions->GetMinIgnitionTemperatureAdjustment(),
                        1.0f,
                        mGameControllerSettingsOptions->GetMaxIgnitionTemperatureAdjustment()));

                combustionSizer->Add(
                    mIgnitionTemperatureAdjustmentSlider,
                    wxGBPosition(0, 0),
                    wxGBSpan(1, 1),
                    wxEXPAND | wxALL,
                    CellBorder);
            }

            // Melting Temperature Adjustment
            {
                mMeltingTemperatureAdjustmentSlider = new SliderControl<float>(
                    combustionBoxSizer->GetStaticBox(),
                    SliderWidth,
                    SliderHeight,
                    _("Melting Point Adjust"),
                    _("Adjusts the temperature at which materials melt."),
                    [this](float value)
                    {
                        this->mLiveSettings.SetValue(GameSettings::MeltingTemperatureAdjustment, value);
                        this->OnLiveSettingsChanged();
                    },
                    std::make_unique<ExponentialSliderCore>(
                        mGameControllerSettingsOptions->GetMinMeltingTemperatureAdjustment(),
                        1.0f,
                        mGameControllerSettingsOptions->GetMaxMeltingTemperatureAdjustment()));

                combustionSizer->Add(
                    mMeltingTemperatureAdjustmentSlider,
                    wxGBPosition(0, 1),
                    wxGBSpan(1, 1),
                    wxEXPAND | wxALL,
                    CellBorder);
            }

            // Combustion Speed Adjustment
            {
                mCombustionSpeedAdjustmentSlider = new SliderControl<float>(
                    combustionBoxSizer->GetStaticBox(),
                    SliderWidth,
                    SliderHeight,
                    _("Combustion Speed Adjust"),
                    _("Adjusts the rate with which materials consume when burning."),
                    [this](float value)
                    {
                        this->mLiveSettings.SetValue(GameSettings::CombustionSpeedAdjustment, value);
                        this->OnLiveSettingsChanged();
                    },
                    std::make_unique<ExponentialSliderCore>(
                        mGameControllerSettingsOptions->GetMinCombustionSpeedAdjustment(),
                        1.0f,
                        mGameControllerSettingsOptions->GetMaxCombustionSpeedAdjustment()));

                combustionSizer->Add(
                    mCombustionSpeedAdjustmentSlider,
                    wxGBPosition(0, 2),
                    wxGBSpan(1, 1),
                    wxEXPAND | wxALL,
                    CellBorder);
            }

            // Combustion Heat Adjustment
            {
                mCombustionHeatAdjustmentSlider = new SliderControl<float>(
                    combustionBoxSizer->GetStaticBox(),
                    SliderWidth,
                    SliderHeight,
                    _("Combustion Heat Adjust"),
                    _("Adjusts the heat generated by fire; together with the maximum number of burning particles, determines the speed with which fire spreads to adjacent particles."),
                    [this](float value)
                    {
                        this->mLiveSettings.SetValue(GameSettings::CombustionHeatAdjustment, value);
                        this->OnLiveSettingsChanged();
                    },
                    std::make_unique<ExponentialSliderCore>(
                        mGameControllerSettingsOptions->GetMinCombustionHeatAdjustment(),
                        1.0f,
                        mGameControllerSettingsOptions->GetMaxCombustionHeatAdjustment()));

                combustionSizer->Add(
                    mCombustionHeatAdjustmentSlider,
                    wxGBPosition(0, 3),
                    wxGBSpan(1, 1),
                    wxEXPAND | wxALL,
                    CellBorder);
            }

            // Max Particles
            {
                mMaxBurningParticlesSlider = new SliderControl<unsigned int>(
                    combustionBoxSizer->GetStaticBox(),
                    SliderWidth,
                    SliderHeight,
                    _("Max Burning Particles"),
                    _("The maximum number of particles that may burn at any given moment in time; together with the combustion heat adjustment, determines the speed with which fire spreads to adjacent particles. Warning: higher values require more computing resources, with the risk of slowing the simulation down!"),
                    [this](unsigned int value)
                    {
                        this->mLiveSettings.SetValue(GameSettings::MaxBurningParticles, value);
                        this->OnLiveSettingsChanged();
                    },
                    std::make_unique<IntegralLinearSliderCore<unsigned int>>(
                        mGameControllerSettingsOptions->GetMinMaxBurningParticles(),
                        mGameControllerSettingsOptions->GetMaxMaxBurningParticles()),
                    mWarningIcon.get());

                combustionSizer->Add(
                    mMaxBurningParticlesSlider,
                    wxGBPosition(0, 4),
                    wxGBSpan(1, 1),
                    wxEXPAND | wxALL,
                    CellBorder);
            }

            combustionBoxSizer->Add(combustionSizer, 0, wxALL, StaticBoxInsetMargin);
        }

        gridSizer->Add(
            combustionBoxSizer,
            wxGBPosition(1, 0),
            wxGBSpan(1, 5),
            wxEXPAND | wxALL,
            CellBorder);
    }

    // Finalize panel

    panel->SetSizerAndFit(gridSizer);
}

void SettingsDialog::SyncControlsWithSettings(Settings<GameSettings> const & settings)
{
    //
    // Mechanics and Thermodynamics
    //

    mMechanicalQualitySlider->SetValue(settings.GetValue<float>(GameSettings::NumMechanicalDynamicsIterationsAdjustment));

    mStrengthSlider->SetValue(settings.GetValue<float>(GameSettings::SpringStrengthAdjustment));

    mGlobalDampingAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::GlobalDampingAdjustment));

    mThermalConductivityAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::ThermalConductivityAdjustment));

    mHeatDissipationAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::HeatDissipationAdjustment));

    mIgnitionTemperatureAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::IgnitionTemperatureAdjustment));

    mMeltingTemperatureAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::MeltingTemperatureAdjustment));

    mCombustionSpeedAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::CombustionSpeedAdjustment));

    mCombustionHeatAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::CombustionHeatAdjustment));

    mMaxBurningParticlesSlider->SetValue(settings.GetValue<unsigned int>(GameSettings::MaxBurningParticles));

    /* TODOOLD
    mRotAcceler8rSlider->SetValue(settings.GetValue<float>(GameSettings::RotAcceler8r));

    mAirFrictionDragSlider->SetValue(settings.GetValue<float>(GameSettings::AirFrictionDragAdjustment));

    mAirPressureDragSlider->SetValue(settings.GetValue<float>(GameSettings::AirPressureDragAdjustment));

    mWaterDensitySlider->SetValue(settings.GetValue<float>(GameSettings::WaterDensityAdjustment));

    mWaterFrictionDragSlider->SetValue(settings.GetValue<float>(GameSettings::WaterFrictionDragAdjustment));

    mWaterPressureDragSlider->SetValue(settings.GetValue<float>(GameSettings::WaterPressureDragAdjustment));

    mWaterIntakeSlider->SetValue(settings.GetValue<float>(GameSettings::WaterIntakeAdjustment));

    mWaterCrazynessSlider->SetValue(settings.GetValue<float>(GameSettings::WaterCrazyness));

    mWaterDiffusionSpeedSlider->SetValue(settings.GetValue<float>(GameSettings::WaterDiffusionSpeedAdjustment));

    // Heat


    mAirTemperatureSlider->SetValue(settings.GetValue<float>(GameSettings::AirTemperature));

    mWaterTemperatureSlider->SetValue(settings.GetValue<float>(GameSettings::WaterTemperature));

    mElectricalElementHeatProducedAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::ElectricalElementHeatProducedAdjustment));

    mHeatBlasterRadiusSlider->SetValue(settings.GetValue<float>(GameSettings::HeatBlasterRadius));

    mHeatBlasterHeatFlowSlider->SetValue(settings.GetValue<float>(GameSettings::HeatBlasterHeatFlow));



    // Ocean, Smoke, and Sky

    mOceanDepthSlider->SetValue(settings.GetValue<float>(GameSettings::SeaDepth));
    mOceanFloorBumpinessSlider->SetValue(settings.GetValue<float>(GameSettings::OceanFloorBumpiness));
    mOceanFloorDetailAmplificationSlider->SetValue(settings.GetValue<float>(GameSettings::OceanFloorDetailAmplification));
    mOceanFloorElasticitySlider->SetValue(settings.GetValue<float>(GameSettings::OceanFloorElasticity));
    mOceanFloorFrictionSlider->SetValue(settings.GetValue<float>(GameSettings::OceanFloorFriction));

    mSmokeEmissionDensityAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::SmokeEmissionDensityAdjustment));
    mSmokeParticleLifetimeAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::SmokeParticleLifetimeAdjustment));

    mStormStrengthAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::StormStrengthAdjustment));
    mDoRainWithStormCheckBox->SetValue(settings.GetValue<bool>(GameSettings::DoRainWithStorm));
    mRainFloodAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::RainFloodAdjustment));
    mRainFloodAdjustmentSlider->Enable(settings.GetValue<bool>(GameSettings::DoRainWithStorm));
    mStormDurationSlider->SetValue(settings.GetValue<std::chrono::seconds>(GameSettings::StormDuration).count());
    mStormRateSlider->SetValue(settings.GetValue<std::chrono::minutes>(GameSettings::StormRate).count());

    mNumberOfStarsSlider->SetValue(settings.GetValue<unsigned int>(GameSettings::NumberOfStars));
    mNumberOfCloudsSlider->SetValue(settings.GetValue<unsigned int>(GameSettings::NumberOfClouds));
    mDoDayLightCycleCheckBox->SetValue(settings.GetValue<bool>(GameSettings::DoDayLightCycle));
    mDayLightCycleDurationSlider->SetValue(settings.GetValue<std::chrono::minutes>(GameSettings::DayLightCycleDuration).count());
    mDayLightCycleDurationSlider->Enable(settings.GetValue<bool>(GameSettings::DoDayLightCycle));

    // Wind, Waves, Fishes, and Lights

    mWindSpeedBaseSlider->SetValue(settings.GetValue<float>(GameSettings::WindSpeedBase));
    mModulateWindCheckBox->SetValue(settings.GetValue<bool>(GameSettings::DoModulateWind));
    mWindGustAmplitudeSlider->SetValue(settings.GetValue<float>(GameSettings::WindSpeedMaxFactor));
    mWindGustAmplitudeSlider->Enable(settings.GetValue<bool>(GameSettings::DoModulateWind));

    mBasalWaveHeightAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::BasalWaveHeightAdjustment));
    mBasalWaveLengthAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::BasalWaveLengthAdjustment));
    mBasalWaveSpeedAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::BasalWaveSpeedAdjustment));
    mTsunamiRateSlider->SetValue(settings.GetValue<std::chrono::minutes>(GameSettings::TsunamiRate).count());
    mRogueWaveRateSlider->SetValue(settings.GetValue<std::chrono::minutes>(GameSettings::RogueWaveRate).count());

    mDoDisplaceWaterCheckBox->SetValue(settings.GetValue<bool>(GameSettings::DoDisplaceWater));
    mWaterDisplacementWaveHeightAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::WaterDisplacementWaveHeightAdjustment));
    mWaterDisplacementWaveHeightAdjustmentSlider->Enable(settings.GetValue<bool>(GameSettings::DoDisplaceWater));

    mNumberOfFishesSlider->SetValue(settings.GetValue<unsigned int>(GameSettings::NumberOfFishes));
    mFishSizeMultiplierSlider->SetValue(settings.GetValue<float>(GameSettings::FishSizeMultiplier));
    mFishSpeedAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::FishSpeedAdjustment));
    mDoFishShoalingCheckBox->SetValue(settings.GetValue<bool>(GameSettings::DoFishShoaling));
    mFishShoalRadiusAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::FishShoalRadiusAdjustment));
    mFishShoalRadiusAdjustmentSlider->Enable(settings.GetValue<bool>(GameSettings::DoFishShoaling));

    mLuminiscenceSlider->SetValue(settings.GetValue<float>(GameSettings::LuminiscenceAdjustment));
    mLightSpreadSlider->SetValue(settings.GetValue<float>(GameSettings::LightSpreadAdjustment));

    // Interactions

    mDestroyRadiusSlider->SetValue(settings.GetValue<float>(GameSettings::DestroyRadius));

    mBombBlastRadiusSlider->SetValue(settings.GetValue<float>(GameSettings::BombBlastRadius));

    mBombBlastForceAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::BombBlastForceAdjustment));

    mBombBlastHeatSlider->SetValue(settings.GetValue<float>(GameSettings::BombBlastHeat));

    mAntiMatterBombImplosionStrengthSlider->SetValue(settings.GetValue<float>(GameSettings::AntiMatterBombImplosionStrength));

    mScrubRotRadiusSlider->SetValue(settings.GetValue<float>(GameSettings::ScrubRotRadius));

    mFloodRadiusSlider->SetValue(settings.GetValue<float>(GameSettings::FloodRadius));

    mFloodQuantitySlider->SetValue(settings.GetValue<float>(GameSettings::FloodQuantity));

    mRepairRadiusSlider->SetValue(settings.GetValue<float>(GameSettings::RepairRadius));

    mRepairSpeedAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::RepairSpeedAdjustment));

    mGenerateAirBubblesCheckBox->SetValue(settings.GetValue<bool>(GameSettings::DoGenerateAirBubbles));

    mDisplaceOceanFloorSurfaceAtAirBubbleSurfacingCheckBox->SetValue(settings.GetValue<bool>(GameSettings::DoDisplaceOceanSurfaceAtAirBubblesSurfacing));
    mDisplaceOceanFloorSurfaceAtAirBubbleSurfacingCheckBox->Enable(settings.GetValue<bool>(GameSettings::DoGenerateAirBubbles));

    mAirBubbleDensitySlider->SetValue(settings.GetValue<float>(GameSettings::AirBubblesDensity));
    mAirBubbleDensitySlider->Enable(settings.GetValue<bool>(GameSettings::DoGenerateAirBubbles));

    mGenerateDebrisCheckBox->SetValue(settings.GetValue<bool>(GameSettings::DoGenerateDebris));

    mGenerateSparklesForCutsCheckBox->SetValue(settings.GetValue<bool>(GameSettings::DoGenerateSparklesForCuts));

    mGenerateEngineWakeCheckBox->SetValue(settings.GetValue<bool>(GameSettings::DoGenerateEngineWakeParticles));

    mUltraViolentCheckBox->SetValue(settings.GetValue<bool>(GameSettings::UltraViolentMode));

    mEngineThrustAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::EngineThrustAdjustment));

    mWaterPumpPowerAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::WaterPumpPowerAdjustment));

    // Render

    auto oceanRenderMode = settings.GetValue<OceanRenderModeType>(GameSettings::OceanRenderMode);
    switch (oceanRenderMode)
    {
    case OceanRenderModeType::Texture:
    {
        mTextureOceanRenderModeRadioButton->SetValue(true);
        break;
    }

    case OceanRenderModeType::Depth:
    {
        mDepthOceanRenderModeRadioButton->SetValue(true);
        break;
    }

    case OceanRenderModeType::Flat:
    {
        mFlatOceanRenderModeRadioButton->SetValue(true);
        break;
    }
    }

    mTextureOceanComboBox->Select(static_cast<int>(settings.GetValue<size_t>(GameSettings::TextureOceanTextureIndex)));

    auto depthOceanColorStart = settings.GetValue<rgbColor>(GameSettings::DepthOceanColorStart);
    mDepthOceanColorStartPicker->SetColour(wxColor(depthOceanColorStart.r, depthOceanColorStart.g, depthOceanColorStart.b));

    auto depthOceanColorEnd = settings.GetValue<rgbColor>(GameSettings::DepthOceanColorEnd);
    mDepthOceanColorEndPicker->SetColour(wxColor(depthOceanColorEnd.r, depthOceanColorEnd.g, depthOceanColorEnd.b));

    auto flatOceanColor = settings.GetValue<rgbColor>(GameSettings::FlatOceanColor);
    mFlatOceanColorPicker->SetColour(wxColor(flatOceanColor.r, flatOceanColor.g, flatOceanColor.b));

    ReconciliateOceanRenderModeSettings();

    mOceanRenderDetailModeDetailedCheckBox->SetValue(settings.GetValue<OceanRenderDetailType>(GameSettings::OceanRenderDetail) == OceanRenderDetailType::Detailed);

    mSeeShipThroughOceanCheckBox->SetValue(settings.GetValue<bool>(GameSettings::ShowShipThroughOcean));

    mOceanTransparencySlider->SetValue(settings.GetValue<float>(GameSettings::OceanTransparency));

    mOceanDarkeningRateSlider->SetValue(settings.GetValue<float>(GameSettings::OceanDarkeningRate));

    auto landRenderMode = settings.GetValue<LandRenderModeType>(GameSettings::LandRenderMode);
    switch (landRenderMode)
    {
    case LandRenderModeType::Texture:
    {
        mTextureLandRenderModeRadioButton->SetValue(true);
        break;
    }

    case LandRenderModeType::Flat:
    {
        mFlatLandRenderModeRadioButton->SetValue(true);
        break;
    }
    }

    mTextureLandComboBox->Select(static_cast<int>(settings.GetValue<size_t>(GameSettings::TextureLandTextureIndex)));

    auto flatLandColor = settings.GetValue<rgbColor>(GameSettings::FlatLandColor);
    mFlatLandColorPicker->SetColour(wxColor(flatLandColor.r, flatLandColor.g, flatLandColor.b));

    ReconciliateLandRenderModeSettings();

    auto flatSkyColor = settings.GetValue<rgbColor>(GameSettings::FlatSkyColor);
    mFlatSkyColorPicker->SetColour(wxColor(flatSkyColor.r, flatSkyColor.g, flatSkyColor.b));

    mShowStressCheckBox->SetValue(settings.GetValue<bool>(GameSettings::ShowShipStress));

    auto flatLampLightColor = settings.GetValue<rgbColor>(GameSettings::FlatLampLightColor);
    mFlatLampLightColorPicker->SetColour(wxColor(flatLampLightColor.r, flatLampLightColor.g, flatLampLightColor.b));

    auto defaultWaterColor = settings.GetValue<rgbColor>(GameSettings::DefaultWaterColor);
    mDefaultWaterColorPicker->SetColour(wxColor(defaultWaterColor.r, defaultWaterColor.g, defaultWaterColor.b));

    mWaterContrastSlider->SetValue(settings.GetValue<float>(GameSettings::WaterContrast));

    mWaterLevelOfDetailSlider->SetValue(settings.GetValue<float>(GameSettings::WaterLevelOfDetail));

    auto const heatRenderMode = settings.GetValue<HeatRenderModeType>(GameSettings::HeatRenderMode);
    switch (heatRenderMode)
    {
        case HeatRenderModeType::Incandescence:
        {
            mHeatRenderModeRadioBox->SetSelection(0);
            break;
        }

        case HeatRenderModeType::HeatOverlay:
        {
            mHeatRenderModeRadioBox->SetSelection(1);
            break;
        }

        case HeatRenderModeType::None:
        {
            mHeatRenderModeRadioBox->SetSelection(2);
            break;
        }
    }

    mHeatSensitivitySlider->SetValue(settings.GetValue<float>(GameSettings::HeatSensitivity));
    mHeatSensitivitySlider->Enable(heatRenderMode != HeatRenderModeType::None);

    mDrawHeatBlasterFlameCheckBox->SetValue(settings.GetValue<bool>(GameSettings::DrawHeatBlasterFlame));

    mShipFlameSizeAdjustmentSlider->SetValue(settings.GetValue<float>(GameSettings::ShipFlameSizeAdjustment));

    // Sound

    mEffectsVolumeSlider->SetValue(settings.GetValue<float>(GameSettings::MasterEffectsVolume));

    mToolsVolumeSlider->SetValue(settings.GetValue<float>(GameSettings::MasterToolsVolume));

    mPlayBreakSoundsCheckBox->SetValue(settings.GetValue<bool>(GameSettings::PlayBreakSounds));

    mPlayStressSoundsCheckBox->SetValue(settings.GetValue<bool>(GameSettings::PlayStressSounds));

    mPlayWindSoundCheckBox->SetValue(settings.GetValue<bool>(GameSettings::PlayWindSound));

    mPlayAirBubbleSurfaceSoundCheckBox->SetValue(settings.GetValue<bool>(GameSettings::PlayAirBubbleSurfaceSound));

    // Advanced

    mSpringStiffnessSlider->SetValue(settings.GetValue<float>(GameSettings::SpringStiffnessAdjustment));

    mSpringDampingSlider->SetValue(settings.GetValue<float>(GameSettings::SpringDampingAdjustment));

    auto const debugShipRenderMode = settings.GetValue<DebugShipRenderModeType>(GameSettings::DebugShipRenderMode);
    switch (debugShipRenderMode)
    {
        case DebugShipRenderModeType::None:
        {
            mDebugShipRenderModeRadioBox->SetSelection(0);
            break;
        }

        case DebugShipRenderModeType::Wireframe:
        {
            mDebugShipRenderModeRadioBox->SetSelection(1);
            break;
        }

        case DebugShipRenderModeType::Points:
        {
            mDebugShipRenderModeRadioBox->SetSelection(2);
            break;
        }

        case DebugShipRenderModeType::Springs:
        {
            mDebugShipRenderModeRadioBox->SetSelection(3);
            break;
        }

        case DebugShipRenderModeType::EdgeSprings:
        {
            mDebugShipRenderModeRadioBox->SetSelection(4);
            break;
        }

        case DebugShipRenderModeType::Decay:
        {
            mDebugShipRenderModeRadioBox->SetSelection(5);
            break;
        }

        case DebugShipRenderModeType::Structure:
        {
            mDebugShipRenderModeRadioBox->SetSelection(6);
            break;
        }
    }

    mDrawExplosionsCheckBox->SetValue(settings.GetValue<bool>(GameSettings::DrawExplosions));

    mDrawFlamesCheckBox->SetValue(settings.GetValue<bool>(GameSettings::DrawFlames));

    mShowFrontiersCheckBox->SetValue(settings.GetValue<bool>(GameSettings::ShowShipFrontiers));

    mShowAABBsCheckBox->SetValue(settings.GetValue<bool>(GameSettings::ShowAABBs));

    auto vectorFieldRenderMode = settings.GetValue<VectorFieldRenderModeType>(GameSettings::VectorFieldRenderMode);
    switch (vectorFieldRenderMode)
    {
    case VectorFieldRenderModeType::None:
    {
        mVectorFieldRenderModeRadioBox->SetSelection(0);
        break;
    }

    case VectorFieldRenderModeType::PointVelocity:
    {
        mVectorFieldRenderModeRadioBox->SetSelection(1);
        break;
    }

    case VectorFieldRenderModeType::PointForce:
    {
        mVectorFieldRenderModeRadioBox->SetSelection(2);
        break;
    }

    case VectorFieldRenderModeType::PointWaterVelocity:
    {
        mVectorFieldRenderModeRadioBox->SetSelection(3);
        break;
    }

    case VectorFieldRenderModeType::PointWaterMomentum:
    {
        mVectorFieldRenderModeRadioBox->SetSelection(4);
        break;
    }
    }
    */
}

/* TODOOLD
void SettingsDialog::ReconciliateOceanRenderModeSettings()
{
    mTextureOceanComboBox->Enable(mTextureOceanRenderModeRadioButton->GetValue());
    mDepthOceanColorStartPicker->Enable(mDepthOceanRenderModeRadioButton->GetValue());
    mDepthOceanColorEndPicker->Enable(mDepthOceanRenderModeRadioButton->GetValue());
    mFlatOceanColorPicker->Enable(mFlatOceanRenderModeRadioButton->GetValue());
}

void SettingsDialog::ReconciliateLandRenderModeSettings()
{
    mTextureLandComboBox->Enable(mTextureLandRenderModeRadioButton->GetValue());
    mFlatLandColorPicker->Enable(mFlatLandRenderModeRadioButton->GetValue());
}
*/

void SettingsDialog::OnLiveSettingsChanged()
{
    assert(!!mSettingsManager);

    // Enforce settings that have just changed
    mSettingsManager->EnforceDirtySettings(mLiveSettings);

    // We're back in sync
    mLiveSettings.ClearAllDirty();

    // Remember that we have changed since we were opened
    mHasBeenDirtyInCurrentSession = true;
    mAreSettingsDirtyWrtDefaults = true; // Best effort, assume each change deviates from defaults
    ReconcileDirtyState();
}

void SettingsDialog::ReconcileDirtyState()
{
    //
    // Update buttons' state based on dirty state
    //

    mRevertToDefaultsButton->Enable(mAreSettingsDirtyWrtDefaults);
    mUndoButton->Enable(mHasBeenDirtyInCurrentSession);
}

/* TODOOLD
long SettingsDialog::GetSelectedPersistedSettingIndexFromCtrl() const
{
    return mPersistedSettingsListCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
}

void SettingsDialog::InsertPersistedSettingInCtrl(
    int index,
    PersistedSettingsKey const & psKey)
{
    mPersistedSettingsListCtrl->InsertItem(
        index,
        psKey.Name);

    if (psKey.StorageType == PersistedSettingsStorageTypes::System
        || psKey == PersistedSettingsKey::MakeLastModifiedSettingsKey())
    {
        // Make it bold
        auto font = mPersistedSettingsListCtrl->GetItemFont(index);
        font.SetWeight(wxFONTWEIGHT_BOLD);
        mPersistedSettingsListCtrl->SetItemFont(index, font);
    }
}

void SettingsDialog::LoadPersistedSettings(size_t index, bool withDefaults)
{
    assert(index < mPersistedSettings.size());

    if (index < mPersistedSettings.size())
    {
        if (withDefaults)
        {
            //
            // Apply loaded settings to {Defaults}
            //

            mLiveSettings = mSettingsManager->GetDefaults();

            mSettingsManager->LoadPersistedSettings(
                mPersistedSettings[index].Key,
                mLiveSettings);

            // Make sure we enforce everything
            mLiveSettings.MarkAllAsDirty();
        }
        else
        {
            //
            // Apply loaded settings to {Current}
            //

            mSettingsManager->LoadPersistedSettings(
                mPersistedSettings[index].Key,
                mLiveSettings);
        }

        // Enforce, immediate
        mSettingsManager->EnforceDirtySettingsImmediate(mLiveSettings);

        // We're back in sync
        mLiveSettings.ClearAllDirty();

        // Remember that we have changed since we were opened
        mHasBeenDirtyInCurrentSession = true;
        mAreSettingsDirtyWrtDefaults = true; // Best effort, assume each change deviates from defaults
        ReconcileDirtyState();

        // Re-populate controls
        SyncControlsWithSettings(mLiveSettings);
    }
}

void SettingsDialog::ReconciliateLoadPersistedSettings()
{
    auto selectedIndex = GetSelectedPersistedSettingIndexFromCtrl();

    assert(selectedIndex == wxNOT_FOUND || static_cast<size_t>(selectedIndex) < mPersistedSettings.size());

    // Enable as long as there's a selection
    mApplyPersistedSettingsButton->Enable(selectedIndex != wxNOT_FOUND);
    mRevertToPersistedSettingsButton->Enable(selectedIndex != wxNOT_FOUND);

    // Enable as long as there's a selection for a user setting that's not the "last-modified" setting
    mReplacePersistedSettingsButton->Enable(
        selectedIndex != wxNOT_FOUND
        && mPersistedSettings[selectedIndex].Key.StorageType == PersistedSettingsStorageTypes::User
        && mPersistedSettings[selectedIndex].Key != PersistedSettingsKey::MakeLastModifiedSettingsKey());

    // Enable as long as there's a selection for a user setting that's not the "last-modified" setting
    mDeletePersistedSettingsButton->Enable(
        selectedIndex != wxNOT_FOUND
        && mPersistedSettings[selectedIndex].Key.StorageType == PersistedSettingsStorageTypes::User
        && mPersistedSettings[selectedIndex].Key != PersistedSettingsKey::MakeLastModifiedSettingsKey());

    if (selectedIndex != wxNOT_FOUND)
    {
        // Set description content
        mPersistedSettingsDescriptionTextCtrl->SetValue(mPersistedSettings[selectedIndex].Description);
    }
    else
    {
        // Clear description content
        mPersistedSettingsDescriptionTextCtrl->Clear();
    }
}

void SettingsDialog::SavePersistedSettings(PersistedSettingsMetadata const & metadata)
{
    // Only save settings different than default
    mLiveSettings.SetDirtyWithDiff(mSettingsManager->GetDefaults());

    // Save settings
    try
    {
        // Save dirty settings
        mSettingsManager->SaveDirtySettings(
            metadata.Key.Name,
            metadata.Description,
            mLiveSettings);
    }
    catch (std::runtime_error const & ex)
    {
        OnPersistenceError(std::string("Error saving settings: ") + ex.what());
        return;
    }

    // We are in sync (well, we were even before saving)
    mLiveSettings.ClearAllDirty();
}

void SettingsDialog::ReconciliateSavePersistedSettings()
{
    // Enable save button if we have name and description
    mSaveSettingsButton->Enable(
        !mSaveSettingsNameTextCtrl->IsEmpty()
        && !mSaveSettingsDescriptionTextCtrl->IsEmpty());
}

void SettingsDialog::OnPersistenceError(std::string const & errorMessage) const
{
    wxMessageBox(errorMessage, _("Error"), wxICON_ERROR);
}
*/