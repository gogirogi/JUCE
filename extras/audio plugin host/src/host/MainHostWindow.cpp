/*
  ==============================================================================

   This file is part of the JUCE library - "Jules' Utility Class Extensions"
   Copyright 2004-7 by Raw Material Software ltd.

  ------------------------------------------------------------------------------

   JUCE can be redistributed and/or modified under the terms of the
   GNU General Public License, as published by the Free Software Foundation;
   either version 2 of the License, or (at your option) any later version.

   JUCE is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with JUCE; if not, visit www.gnu.org/licenses or write to the
   Free Software Foundation, Inc., 59 Temple Place, Suite 330,
   Boston, MA 02111-1307 USA

  ------------------------------------------------------------------------------

   If you'd like to release a closed-source product which uses JUCE, commercial
   licenses are also available: visit www.rawmaterialsoftware.com/juce for
   more information.

  ==============================================================================
*/

#include "../../../../juce.h"
#include "MainHostWindow.h"
#include "InternalFilters.h"
#include "../plugins/juce_PluginListComponent.h"

//==============================================================================
class PluginListWindow  : public DocumentWindow
{
public:
    PluginListWindow (KnownPluginList& knownPluginList)
        : DocumentWindow ("Available Plugins", Colours::white,
                          DocumentWindow::minimiseButton | DocumentWindow::closeButton)
    {
        currentPluginListWindow = this;

        const File deadMansPedalFile (ApplicationProperties::getInstance()->getUserSettings()
                                        ->getFile().getSiblingFile ("RecentlyCrashedPluginsList"));

        setContentComponent (new PluginListComponent (knownPluginList,
                                                      deadMansPedalFile,
                                                      ApplicationProperties::getInstance()->getUserSettings()), true, true);

        setResizable (true, false);
        setVisible (true);
        setResizeLimits (300, 400, 800, 1500);
        setTopLeftPosition (60, 60);

        restoreWindowStateFromString (ApplicationProperties::getInstance()->getUserSettings()->getValue ("listWindowPos"));
    }

    ~PluginListWindow()
    {
        ApplicationProperties::getInstance()->getUserSettings()->setValue ("listWindowPos", getWindowStateAsString());

        setContentComponent (0);

        jassert (currentPluginListWindow == this);
        currentPluginListWindow = 0;
    }

    void closeButtonPressed()
    {
        delete this;
    }

    static PluginListWindow* currentPluginListWindow;
};

PluginListWindow* PluginListWindow::currentPluginListWindow = 0;


//==============================================================================
MainHostWindow::MainHostWindow()
    : DocumentWindow (JUCEApplication::getInstance()->getApplicationName(), Colours::lightgrey,
                      DocumentWindow::allButtons)
{
    setResizable (true, false);
    setResizeLimits (500, 400, 10000, 10000);
    centreWithSize (800, 600);

    setContentComponent (new GraphDocumentComponent (&deviceManager));

    restoreWindowStateFromString (ApplicationProperties::getInstance()->getUserSettings()->getValue ("mainWindowPos"));

#if JUCE_MAC
    setMacMainMenu (this);
#else
    setMenuBar (this);
#endif

    setVisible (true);

    InternalPluginFormat internalFormat;
    internalFormat.getAllTypes (internalTypes);

    XmlElement* const savedAudioState = ApplicationProperties::getInstance()->getUserSettings()
                                            ->getXmlValue (T("audioDeviceState"));

    deviceManager.initialise (256, 256, savedAudioState, true);

    delete savedAudioState;

    XmlElement* const savedPluginList = ApplicationProperties::getInstance()
                                          ->getUserSettings()
                                          ->getXmlValue (T("pluginList"));

    if (savedPluginList != 0)
    {
        knownPluginList.recreateFromXml (*savedPluginList);
        delete savedPluginList;
    }

    pluginSortMethod = (KnownPluginList::SortMethod) ApplicationProperties::getInstance()->getUserSettings()
                            ->getIntValue (T("pluginSortMethod"), KnownPluginList::sortByManufacturer);

    knownPluginList.addChangeListener (this);

    addKeyListener (commandManager->getKeyMappings());

    Process::setPriority (Process::HighPriority);
}

MainHostWindow::~MainHostWindow()
{
    delete PluginListWindow::currentPluginListWindow;

#if JUCE_MAC
    setMacMainMenu (0);
#else
    setMenuBar (0);
#endif

    knownPluginList.removeChangeListener (this);

    ApplicationProperties::getInstance()->getUserSettings()->setValue ("mainWindowPos", getWindowStateAsString());
    setContentComponent (0);
}

void MainHostWindow::closeButtonPressed()
{
    tryToQuitApplication();
}

bool MainHostWindow::tryToQuitApplication()
{
    if (getGraphEditor() != 0 
        && getGraphEditor()->graph.saveIfNeededAndUserAgrees() == FileBasedDocument::savedOk)
    {
        JUCEApplication::quit();
        return true;
    }

    return false;
}

void MainHostWindow::changeListenerCallback (void*)
{
    menuItemsChanged();

    // save the plugin list every time it gets chnaged, so that if we're scanning
    // and it crashes, we've still saved the previous ones
    XmlElement* const savedPluginList = knownPluginList.createXml();

    if (savedPluginList != 0)
    {
        ApplicationProperties::getInstance()->getUserSettings()
              ->setValue (T("pluginList"), savedPluginList);

        delete savedPluginList;

        ApplicationProperties::getInstance()->saveIfNeeded();
    }
}

const StringArray MainHostWindow::getMenuBarNames()
{
    const tchar* const names[] = { T("File"), T("Plugins"), T("Options"), 0 };

    return StringArray ((const tchar**) names);
}

const PopupMenu MainHostWindow::getMenuForIndex (int topLevelMenuIndex, const String& /*menuName*/)
{
    PopupMenu menu;

    if (topLevelMenuIndex == 0)
    {
        // "File" menu
        menu.addCommandItem (commandManager, CommandIDs::open);

        RecentlyOpenedFilesList recentFiles;
        recentFiles.restoreFromString (ApplicationProperties::getInstance()->getUserSettings()
                                            ->getValue ("recentFilterGraphFiles"));

        PopupMenu recentFilesMenu;
        recentFiles.createPopupMenuItems (recentFilesMenu, 100, true, true);
        menu.addSubMenu (T("Open recent file"), recentFilesMenu);

        menu.addCommandItem (commandManager, CommandIDs::save);
        menu.addCommandItem (commandManager, CommandIDs::saveAs);
        menu.addSeparator();
        menu.addCommandItem (commandManager, StandardApplicationCommandIDs::quit);
    }
    else if (topLevelMenuIndex == 1)
    {
        // "Plugins" menu
        PopupMenu pluginsMenu;
        addPluginsToMenu (pluginsMenu);
        menu.addSubMenu (T("Create plugin"), pluginsMenu);
        menu.addSeparator();
        menu.addItem (250, T("Delete all plugins"));

    }
    else if (topLevelMenuIndex == 2)
    {
        // "Options" menu

        menu.addCommandItem (commandManager, CommandIDs::showPluginListEditor);

        PopupMenu sortTypeMenu;
        sortTypeMenu.addItem (200, "List plugins in default order", true, pluginSortMethod == KnownPluginList::defaultOrder);
        sortTypeMenu.addItem (201, "List plugins in alphabetical order", true, pluginSortMethod == KnownPluginList::sortAlphabetically);
        sortTypeMenu.addItem (202, "List plugins by category", true, pluginSortMethod == KnownPluginList::sortByCategory);
        sortTypeMenu.addItem (203, "List plugins by manufacturer", true, pluginSortMethod == KnownPluginList::sortByManufacturer);
        menu.addSubMenu ("Plugin menu type", sortTypeMenu);

        menu.addSeparator();
        menu.addCommandItem (commandManager, CommandIDs::showAudioSettings);

        menu.addSeparator();
        menu.addCommandItem (commandManager, CommandIDs::aboutBox);
    }

    return menu;
}

void MainHostWindow::menuItemSelected (int menuItemID, int /*topLevelMenuIndex*/)
{
    GraphDocumentComponent* const graphEditor = getGraphEditor();

    if (menuItemID == 250)
    {
        if (graphEditor != 0)
            graphEditor->graph.clear();
    }
    else if (menuItemID >= 100 && menuItemID < 200)
    {
        RecentlyOpenedFilesList recentFiles;
        recentFiles.restoreFromString (ApplicationProperties::getInstance()->getUserSettings()
                                            ->getValue ("recentFilterGraphFiles"));

        if (graphEditor != 0 && graphEditor->graph.saveIfNeededAndUserAgrees() == FileBasedDocument::savedOk)
            graphEditor->graph.loadFrom (recentFiles.getFile (menuItemID - 100), true);
    }
    else if (menuItemID >= 200 && menuItemID < 210)
    {
        if (menuItemID == 200)
            pluginSortMethod = KnownPluginList::defaultOrder;
        else if (menuItemID == 201)
            pluginSortMethod = KnownPluginList::sortAlphabetically;
        else if (menuItemID == 202)
            pluginSortMethod = KnownPluginList::sortByCategory;
        else if (menuItemID == 203)
            pluginSortMethod = KnownPluginList::sortByManufacturer;

        ApplicationProperties::getInstance()->getUserSettings()
           ->setValue (T("pluginSortMethod"), (int) pluginSortMethod);
    }
    else
    {
        createPlugin (getChosenType (menuItemID),
                      proportionOfWidth (0.3f + Random::getSystemRandom().nextFloat() * 0.6f),
                      proportionOfHeight (0.3f + Random::getSystemRandom().nextFloat() * 0.6f));
    }
}

void MainHostWindow::createPlugin (const PluginDescription* desc, int x, int y)
{
    GraphDocumentComponent* const graphEditor = getGraphEditor();

    if (graphEditor != 0)
        graphEditor->createNewPlugin (desc, x, y);
}

void MainHostWindow::addPluginsToMenu (PopupMenu& m) const
{
    for (int i = 0; i < internalTypes.size(); ++i)
        m.addItem (i + 1, internalTypes.getUnchecked(i)->name);

    m.addSeparator();

    knownPluginList.addToMenu (m, pluginSortMethod);
}

const PluginDescription* MainHostWindow::getChosenType (const int menuID) const
{
    if (menuID >= 1 && menuID < 1 + internalTypes.size())
    {
        return internalTypes [menuID - 1];
    }
    else
    {
        return knownPluginList.getType (knownPluginList.getIndexChosenByMenu (menuID));
    }
}

//==============================================================================
ApplicationCommandTarget* MainHostWindow::getNextCommandTarget()
{
    return findFirstTargetParentComponent();
}

void MainHostWindow::getAllCommands (Array <CommandID>& commands)
{
    // this returns the set of all commands that this target can perform..
    const CommandID ids[] = { CommandIDs::open,
                              CommandIDs::save,
                              CommandIDs::saveAs,
                              CommandIDs::showPluginListEditor,
                              CommandIDs::showAudioSettings,
                              CommandIDs::aboutBox
                            };

    commands.addArray (ids, numElementsInArray (ids));
}

void MainHostWindow::getCommandInfo (const CommandID commandID, ApplicationCommandInfo& result)
{
    const String category ("General");

    switch (commandID)
    {
    case CommandIDs::open:
        result.setInfo (T("Open..."),
                        T("Opens a filter graph file"),
                        category, 0);
        result.defaultKeypresses.add (KeyPress (T('o'), ModifierKeys::commandModifier, 0));
        break;

    case CommandIDs::save:
        result.setInfo (T("Save"),
                        T("Saves the current graph to a file"),
                        category, 0);
        result.defaultKeypresses.add (KeyPress (T('s'), ModifierKeys::commandModifier, 0));
        break;

    case CommandIDs::saveAs:
        result.setInfo (T("Save As..."),
                        T("Saves a copy of the current graph to a file"),
                        category, 0);
        result.defaultKeypresses.add (KeyPress (T('s'), ModifierKeys::shiftModifier | ModifierKeys::commandModifier, 0));
        break;

    case CommandIDs::showPluginListEditor:
        result.setInfo ("Edit the list of available plug-Ins...", String::empty, category, 0);
        result.addDefaultKeypress (T('p'), ModifierKeys::commandModifier);
        break;

    case CommandIDs::showAudioSettings:
        result.setInfo ("Change the audio device settings", String::empty, category, 0);
        result.addDefaultKeypress (T('a'), ModifierKeys::commandModifier);
        break;

    case CommandIDs::aboutBox:
        result.setInfo ("About...", String::empty, category, 0);
        break;

    default:
        break;
    }
}

bool MainHostWindow::perform (const InvocationInfo& info)
{
    GraphDocumentComponent* const graphEditor = getGraphEditor();

    switch (info.commandID)
    {
    case CommandIDs::open:
        if (graphEditor != 0 && graphEditor->graph.saveIfNeededAndUserAgrees() == FileBasedDocument::savedOk)
            graphEditor->graph.loadFromUserSpecifiedFile (true);

        break;

    case CommandIDs::save:
        if (graphEditor != 0)
            graphEditor->graph.save (true, true);
        break;

    case CommandIDs::saveAs:
        if (graphEditor != 0)
            graphEditor->graph.saveAs (File::nonexistent, true, true, true);
        break;

    case CommandIDs::showPluginListEditor:
        if (PluginListWindow::currentPluginListWindow == 0)
            PluginListWindow::currentPluginListWindow = new PluginListWindow (knownPluginList);

        PluginListWindow::currentPluginListWindow->toFront (true);
        break;

    case CommandIDs::showAudioSettings:
        showAudioSettings();
        break;

    case CommandIDs::aboutBox:
        {
/*            AboutBoxComponent aboutComp;

            DialogWindow::showModalDialog (T("About"),
                                           &aboutComp,
                                           this, Colours::white,
                                           true, false, false);
  */      }

        break;

    default:
        return false;
    }

    return true;
}

void MainHostWindow::showAudioSettings()
{
    AudioDeviceSelectorComponent audioSettingsComp (deviceManager,
                                                    0, 256,
                                                    0, 256,
                                                    true);

    audioSettingsComp.setSize (500, 300);

    DialogWindow::showModalDialog (T("Audio Settings"),
                                   &audioSettingsComp,
                                   this,
                                   Colours::azure,
                                   true);

    XmlElement* const audioState = deviceManager.createStateXml();

    ApplicationProperties::getInstance()->getUserSettings()
        ->setValue (T("audioDeviceState"), audioState);

    delete audioState;

    ApplicationProperties::getInstance()->getUserSettings()->saveIfNeeded();

    GraphDocumentComponent* const graphEditor = getGraphEditor();

    if (graphEditor != 0)
        graphEditor->graph.removeIllegalConnections();
}

bool MainHostWindow::filesDropped (const StringArray& files, int x, int y)
{
    if (files.size() == 1 && File (files[0]).hasFileExtension (filenameSuffix))
    {
        GraphDocumentComponent* const graphEditor = getGraphEditor();

        if (graphEditor == 0)
            return false;

        return graphEditor->graph.saveIfNeededAndUserAgrees() == FileBasedDocument::savedOk
                && graphEditor->graph.loadFrom (File (files[0]), true);
    }

    OwnedArray <PluginDescription> typesFound;
    knownPluginList.scanAndAddDragAndDroppedFiles (files, typesFound);

    for (int i = 0; i < jmin (5, typesFound.size()); ++i)
        createPlugin (typesFound.getUnchecked(i), x, y);

    return typesFound.size() > 0;
}

GraphDocumentComponent* MainHostWindow::getGraphEditor() const
{
    return dynamic_cast <GraphDocumentComponent*> (getContentComponent());
}
