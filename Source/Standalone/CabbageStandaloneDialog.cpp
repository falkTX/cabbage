/*
  Copyright (C) 2009 Rory Walsh

  Cabbage is free software; you can redistribute it
  and/or modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  Cabbage is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with Csound; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
  02111-1307 USA
*/

#include "CabbageStandaloneDialog.h"



#define MAXBYTES 16777216

//==============================================================================
//  Somewhere in the codebase of your plugin, you need to implement this function
//  and make it create an instance of the filter subclass that you're building.
extern CabbagePluginAudioProcessor* JUCE_CALLTYPE createCabbagePluginFilter(String inputfile, bool guiOnOff, int plugType);


//==============================================================================
StandaloneFilterWindow::StandaloneFilterWindow (const String& title,
        const Colour& backgroundColour, String commandLineParams)
    : DocumentWindow (title, backgroundColour,
                      DocumentWindow::minimiseButton
                      | DocumentWindow::closeButton),
    optionsButton ("Options"),
    isGUIOn(false),
    pipeOpenedOk(false),
    AudioEnabled(true),
    isAFileOpen(false),
    standaloneMode(false),
    updateEditorOutputConsole(false),
	hasEditorBeingOpened(false)
{
	
    String defaultCSDFile;

	if(File(commandLineParams.removeCharacters("\"")).existsAsFile())
	{
		defaultCSDFile = commandLineParams.removeCharacters("\"");
	}
	else
	{
		defaultCSDFile = File(File::getSpecialLocation(File::currentExecutableFile)).withFileExtension(".csd").getFullPathName();
	}
    
										  
	consoleMessages = "";
    cabbageDance = 0;
    setTitleBarButtonsRequired (DocumentWindow::minimiseButton | DocumentWindow::closeButton, false);
    Component::addAndMakeVisible (&optionsButton);
    optionsButton.addListener (this);
    timerRunning = false;
    yAxis = 0;
    this->addKeyListener(this);
    optionsButton.setTriggeredOnMouseDown (true);

    bool alwaysontop = getPreference(appProperties, "SetAlwaysOnTop");
    setAlwaysOnTop(alwaysontop);
    this->setResizable(false, false);
    this->startTimer(true);
    lookAndFeel = new CabbageLookAndFeel();
    this->setLookAndFeel(lookAndFeel);
    oldLookAndFeel = new LookAndFeel_V1();
// MOD - Stefano Bonetti
#ifdef Cabbage_Named_Pipe
//    ipConnection = new socketConnection(*this);
//    pipeOpenedOk = ipConnection->createPipe(String("cabbage"));
//    if(pipeOpenedOk) Logger::writeToLog(String("Namedpipe created ..."));
#endif
// MOD - End

    JUCE_TRY
    {
        filter = createCabbagePluginFilter("", false, AUDIO_PLUGIN);
        filter->addChangeListener(this);
        filter->addActionListener(this);
        filter->sendChangeMessage();
        filter->createGUI("", true);
    }
    JUCE_CATCH_ALL

    if (filter == nullptr)
    {
        jassertfalse    // Your filter didn't create correctly! In a standalone app that's not too great.
        JUCEApplication::quit();
    }

    filter->setPlayConfigDetails (JucePlugin_MaxNumInputChannels,
                                  JucePlugin_MaxNumOutputChannels,
                                  44100, 512);

    PropertySet* const globalSettings = getGlobalSettings();


    deviceManager = new AudioDeviceManager();
    deviceManager->addMidiInputCallback (String::empty, &player);
    deviceManager->closeAudioDevice();



    ScopedPointer<XmlElement> savedState;
    if (globalSettings != nullptr)
        savedState = globalSettings->getXmlValue ("audioSetup");

    deviceManager->initialise(filter->getNumInputChannels(),
                              filter->getNumOutputChannels(), savedState, false);
							  
							  
    //deviceManager->closeAudioDevice();

    filter->stopProcessing = true;

    int runningCabbageIO = getPreference(appProperties, "UseCabbageIO");
    if(runningCabbageIO)
    {
        player.setProcessor (filter);
        if (globalSettings != nullptr)
        {
            MemoryBlock data;

            if (data.fromBase64Encoding (globalSettings->getValue ("filterState"))
                    && data.getSize() > 0)
            {
                filter->setStateInformation (data.getData(), data.getSize());
            }
        }


    }
    //deviceManager->closeAudioDevice();

    setContentOwned (filter->createEditorIfNeeded(), true);

    const int x = globalSettings->getIntValue ("windowX", -100);
    const int y = globalSettings->getIntValue ("windowY", -100);

    if (x != -100 && y != -100)
        setBoundsConstrained (Rectangle<int> (x, y, getWidth(), getHeight()));
    else
        centreWithSize (getWidth(), getHeight());

    //setPreference(appProperties, "ExternalEditor",0);

    //create editor but don't display it yet...
    cabbageCsoundEditor = new CodeWindow(csdFile.getFileName());
    cabbageCsoundEditor->setVisible(false);
    //cabbageCsoundEditor->setSize(800, 500);
    setupWindowDimensions();
    showEditorConsole();

    cabbageCsoundEditor->addActionListener(this);
    cabbageCsoundEditor->setLookAndFeel(lookAndFeel);

    filter->codeEditor = cabbageCsoundEditor->textEditor;

    //opens a default file that matches the name of the current executable
    //this can be used to create more 'standalone' like apps

	
    if(File(defaultCSDFile).existsAsFile())
    {
        standaloneMode = true;
        openFile(defaultCSDFile);
    }


    //filter->codeWindow = cabbageCsoundEditor->textEditor;
    //start timer for output message, and autoupdate if it's on
    startTimer(100);
}
//==============================================================================
// Destructor
//==============================================================================
StandaloneFilterWindow::~StandaloneFilterWindow()
{
//cabbageCsoundEditor.release();
//outputConsole.release();
#ifdef Cabbage_Named_Pipe
// MOD - Stefano Bonetti
    if(ipConnection)
    {
        ipConnection->disconnect();
    }
// MOD - End
#endif

    PropertySet* const globalSettings = getGlobalSettings();

    if (globalSettings != nullptr)
    {
        globalSettings->setValue ("windowX", getX());
        globalSettings->setValue ("windowY", getY());

        if (deviceManager != nullptr)
        {
            ScopedPointer<XmlElement> xml (deviceManager->createStateXml());
            globalSettings->setValue ("audioSetup", xml);
        }
    }

    deviceManager->removeMidiInputCallback (String::empty, &player);
    deviceManager->removeAudioCallback (&player);
    deviceManager = nullptr;

    if (globalSettings != nullptr && filter != nullptr)
    {
        MemoryBlock data;
        filter->getStateInformation (data);

        globalSettings->setValue ("filterState", data.toBase64Encoding());
    }

    deleteFilter();

}

//==============================================================================
// insane Cabbage dancing....
//==============================================================================
void StandaloneFilterWindow::timerCallback()
{

    if(getPreference(appProperties, "ExternalEditor"))
    {
        int64 diskTime = csdFile.getLastModificationTime().toMilliseconds();
        int64 tempTime = lastSaveTime.toMilliseconds();
        if(diskTime>tempTime)
        {
            resetFilter(false);
            lastSaveTime = csdFile.getLastModificationTime();
            //csdFile.replaceWithText(File(csdFile.getFullPathName()).loadFileAsString());
        }
    }

    //cout << csdFile.getLastModificationTime().toString(true, true, false, false);
    if(cabbageDance)
    {
        float moveY = sin(yAxis*2*3.14*5.f/100.f);
        float moveX = sin(yAxis*2*3.14*5.f/100.f);
        yAxis+=1;
        this->setTopLeftPosition(this->getScreenX()+(moveX*10), this->getScreenY()+(moveY*10));
    }


    if(outputConsole)
        if(outputConsole->getText()!=filter->getCsoundOutput())
            outputConsole->setText(filter->getCsoundOutput());

    if(cabbageCsoundEditor)
    {
        if(cabbageCsoundEditor->csoundOutputComponent->getText()!=filter->getCsoundOutput())
            cabbageCsoundEditor->csoundOutputComponent->setText(filter->getCsoundOutput());
#ifdef BUILD_DEBUGGER
        cabbageCsoundEditor->csoundDebuggerComponent->setText(filter->getDebuggerOutput());
#endif
    }

//	updateEditorOutputConsole=false;
//	}
}

bool StandaloneFilterWindow::keyPressed(const juce::KeyPress &key ,Component *)
{
    Logger::writeToLog("MainForm:"+key.getTextDescription());
    return false;
}

//==============================================================================
// action Callback - updates instrument according to changes in source code
//==============================================================================
void StandaloneFilterWindow::actionListenerCallback (const String& message)
{
    if(message == "GUI Update, PropsPanel")
    {
        //if something changes in the properties panel we need to update our GUI so
        //that the changes are reflected in the on screen components
        filter->createGUI(csdFile.loadFileAsString(), true);
        if(cabbageCsoundEditor)
        {
            //cabbageCsoundEditor->csoundDoc.replaceAllContent(filter->getCsoundInputFile().loadFileAsString());
            cabbageCsoundEditor->textEditor->highlightLine(filter->getCurrentLineText());
        }
    }

    else if(message == "Score Updated")
    {
        if(cabbageCsoundEditor)
        {
            //cabbageCsoundEditor->csoundDoc.replaceAllContent(filter->getCsoundInputFile().loadFileAsString());
            cabbageCsoundEditor->textEditor->highlightLine(filter->getCurrentLineText());
        }
    }

    else if(message == "GUI Updated, controls added, resized")
    {
        //no need to update our entire GUI here as only on-screen sizes change
        setCurrentLine(filter->getCurrentLine()+1);
        if(cabbageCsoundEditor)
        {
            //cabbageCsoundEditor->csoundDoc.replaceAllContent(filter->getCsoundInputFile().loadFileAsString());
            cabbageCsoundEditor->textEditor->highlightLine(filter->getCurrentLineText());
        }
    }

    else if(message == "GUI Updated, controls deleted")
    {
        //controls have already been deleted, we just need to update our file
        if(cabbageCsoundEditor)
            cabbageCsoundEditor->csoundDoc.replaceAllContent(csdFile.loadFileAsString());
    }

    else if(message.equalsIgnoreCase("fileSaved"))
    {
        saveFile();
        if(filter->isGuiEnabled())
        {
            startTimer(100);
            ((CabbagePluginAudioProcessorEditor*)filter->getActiveEditor())->setEditMode(false);
            filter->setGuiEnabled(false);
        }
    }

    else if(message.contains("fileOpen"))
    {
        openFile("");
    }

    else if(message.contains("audioSettings"))
    {
        showAudioSettingsDialog();
    }

    else if(message.contains("ContinueDebug"))
    {
        filter->continueCsoundDebug();
    }

    else if(message.contains("RemoveAllBreakpoints"))
    {
        filter->cleanCsoundDebug();
    }

    else if(message.contains("NextDebug"))
    {
        filter->nextCsoundDebug();
    }

    else if(message.contains("InstrumentBreakpoint"))
    {
        if(message.contains("Set"))
        {
            int instrNumber = message.substring(24).getIntValue();
            filter->setCsoundInstrumentBreakpoint(instrNumber, 0);
        }
        else
        {
            int instrNumber = message.substring(27).getIntValue();
            filter->removeCsoundInstrumentBreakpoint(instrNumber);

        }
    }

    else if(message.contains("hideOutputWindow"))
    {
        if(outputConsole)
            outputConsole->setVisible(false);
    }

    else if(message.contains("fileSaveAs"))
    {
        saveFileAs();
    }

    else if(message.contains("fileExportSynth"))
    {
        exportPlugin(String("VSTi"), false);
    }

    else if(message.contains("fileExportEffect"))
    {
        exportPlugin(String("VST"), false);
    }

    else if(message.contains("fileUpdateGUI"))
    {
        filter->createGUI(cabbageCsoundEditor->getText(), true);
        csdFile.replaceWithText(cabbageCsoundEditor->getText());
        if(cabbageCsoundEditor)
        {
            cabbageCsoundEditor->setName(csdFile.getFullPathName());
        }
    }

#ifdef Cabbage_Build_Standalone
    else if(message.contains("MENU COMMAND: manual update instrument"))
        resetFilter(true);

    else if(message.contains("MENU COMMAND: open instrument"))
        openFile("");

    else if(message.contains("MENU COMMAND: manual update GUI"))
        filter->createGUI(csdFile.loadFileAsString(), true);

    else if(message.contains("MENU COMMAND: toggle edit"))
    {
        int val = getPreference(appProperties, "DisableGUIEditModeWarning");
        if(!val)
            showMessage("", "Warning!! This feature is still under development! Whilst every effort has been made to make it as usable as possible, there might still be some teething problems that need sorting out. If you find a problem, please try to recreate it, note the steps involved, and report it to the Cabbage users forum (www.TheCabbageFoundation.org). Thank you. ge, disable this warning under the 'Preferences' menu command and try 'Edit Mode' again, otherwise just let it be...", lookAndFeel, this);
        else
        {
            if(isAFileOpen == true)
                if(filter->isGuiEnabled())
                {
                    ((CabbagePluginAudioProcessorEditor*)filter->getActiveEditor())->setEditMode(false);
                    filter->setGuiEnabled(false);
					//filter->suspendProcessing(false);
                }
                else
                {
                    ((CabbagePluginAudioProcessorEditor*)filter->getActiveEditor())->setEditMode(true);
                    filter->setGuiEnabled(true);
					//filter->suspendProcessing(true);

                    //stopTimer();
                    //setPreference(appProperties, "ExternalEditor", 0);
                }
        }
    }

    else if(message.contains("MENU COMMAND: suspend audio"))
        if(AudioEnabled)
        {
            filter->stopProcessing = true;
            AudioEnabled = false;
        }
        else
        {
            AudioEnabled = true;
            filter->stopProcessing = false;
        }
    else {}
#endif

}
//==============================================================================

//==============================================================================
// listener Callback - updates WinXound compiler output with Cabbage messages
//==============================================================================
void StandaloneFilterWindow::changeListenerCallback(juce::ChangeBroadcaster* /*source*/)
{
    /*
    String text;
    if(!cabbageCsoundEditor || !outputConsole){
    	for(int i=0;i<filter->getDebugMessageArray().size();i++)
    		  {
    			  if(filter->getDebugMessageArray().getReference(i).length()>0)
    			  {
    				  text += String(filter->getDebugMessageArray().getReference(i).toUTF8());

    			  }

    		  }
    	consoleMessages = consoleMessages+text+"\n";
    	}
    */
    updateEditorOutputConsole=true;
}
//==============================================================================
// Delete filter
//==============================================================================
void StandaloneFilterWindow::deleteFilter()
{
    player.setProcessor (nullptr);

    if (filter != nullptr && getContentComponent() != nullptr)
    {
        filter->editorBeingDeleted (dynamic_cast <AudioProcessorEditor*> (getContentComponent()));
        clearContentComponent();
    }

    filter = nullptr;
}

//==============================================================================
// Reset filter
//==============================================================================
void StandaloneFilterWindow::resetFilter(bool shouldResetFilter)
{
//first we check that the audio device is up and running ok
    stopTimer();

    filter->stopProcessing=true;
    deviceManager->addAudioCallback (&player);
	deviceManager->addMidiInputCallback (String::empty, &player);

    if(shouldResetFilter)
    {
        deviceManager->closeAudioDevice();
        deleteFilter();
		
		PropertySet* const globalSettings = getGlobalSettings();
		ScopedPointer<XmlElement> savedState;
		if (globalSettings != nullptr)
			savedState = globalSettings->getXmlValue ("audioSetup");	
		
        filter = createCabbagePluginFilter(csdFile.getFullPathName(), false, AUDIO_PLUGIN);
        filter->addChangeListener(this);
        filter->addActionListener(this);
        if(cabbageCsoundEditor)
        {
            cabbageCsoundEditor->setName(csdFile.getFileName());
            cabbageCsoundEditor->textEditor->editor[0]->loadContent(csdFile.loadFileAsString());
        }

			deviceManager->initialise(filter->getNumInputChannels(),
								  filter->getNumOutputChannels(), savedState, false);
    }
    else
    {
        //deviceManager->closeAudioDevice();
        filter->reCompileCsound(csdFile);
    }
//	filter->sendChangeMessage();
    filter->createGUI(csdFile.loadFileAsString(), true);

    setName(filter->getPluginName());

    int runningCabbageProcess = getPreference(appProperties, "UseCabbageIO");

    setContentOwned (filter->createEditorIfNeeded(), true);
    //filter->addChangeListener(filter->getActiveEditor());
    CabbagePluginAudioProcessorEditor* editor = dynamic_cast<CabbagePluginAudioProcessorEditor*> (filter->getActiveEditor());
    if(editor)
        filter->addChangeListener(editor);

    if(runningCabbageProcess)
    {
        if (filter != nullptr)
        {
            if (deviceManager != nullptr)
            {
                player.setProcessor (filter);
                //deviceManager->setAudioDeviceSetup(audioDeviceSetup, true);
                //deviceManager->restartLastAudioDevice();
            }
        }

    }
    else
    {
        filter->performEntireScore();
    }


    PropertySet* const globalSettings = getGlobalSettings();

    if (globalSettings != nullptr)
        globalSettings->removeValue ("filterState");


    startTimer(100);

    if(cabbageCsoundEditor)
    {
        cabbageCsoundEditor->setName(csdFile.getFileName());
        cabbageCsoundEditor->setText(csdFile.loadFileAsString(), csdFile.getFullPathName());

        cabbageCsoundEditor->textEditor->textChanged = false;
        filter->codeEditor = cabbageCsoundEditor->textEditor;
        //cabbageCsoundEditor->textEditor->setSavePoint();
    }
	
}

//==============================================================================
void StandaloneFilterWindow::saveState()
{
    PropertySet* const globalSettings = getGlobalSettings();

    FileChooser fc (TRANS("Save current state"),
                    globalSettings != nullptr ? File (globalSettings->getValue ("lastStateFile"))
                    : File::nonexistent);

    if (fc.browseForFileToSave (true))
    {
        MemoryBlock data;
        filter->getStateInformation (data);

        if (! fc.getResult().replaceWithData (data.getData(), data.getSize()))
        {
            AlertWindow::showMessageBox (AlertWindow::WarningIcon,
                                         TRANS("Error whilst saving"),
                                         TRANS("Couldn't write to the specified file!"));
        }
    }
}

void StandaloneFilterWindow::loadState()
{
    PropertySet* const globalSettings = getGlobalSettings();

    FileChooser fc (TRANS("Load a saved state"),
                    globalSettings != nullptr ? File (globalSettings->getValue ("lastStateFile"))
                    : File::nonexistent);

    if (fc.browseForFileToOpen())
    {
        MemoryBlock data;

        if (fc.getResult().loadFileAsData (data))
        {
            filter->setStateInformation (data.getData(), data.getSize());
        }
        else
        {
            AlertWindow::showMessageBox (AlertWindow::WarningIcon,
                                         TRANS("Error whilst loading"),
                                         TRANS("Couldn't read from the specified file!"));
        }
    }
}

//==============================================================================
PropertySet* StandaloneFilterWindow::getGlobalSettings()
{
    /* If you want this class to store the plugin's settings, you can set up an
       ApplicationProperties object and use this method as it is, or override this
       method to return your own custom PropertySet.

       If using this method without changing it, you'll probably need to call
       ApplicationProperties::setStorageParameters() in your plugin's constructor to
       tell it where to save the file.
    */
    getPreference(appProperties, "htmlHelp", "some directory");
    return appProperties->getUserSettings();
}

void StandaloneFilterWindow::showAudioSettingsDialog()
{
    const int numIns = filter->getNumInputChannels() <= 0 ? JucePlugin_MaxNumInputChannels : filter->getNumInputChannels();
    const int numOuts = filter->getNumOutputChannels() <= 0 ? JucePlugin_MaxNumOutputChannels : filter->getNumOutputChannels();
	filter->stopProcessing = true;
    CabbageAudioDeviceSelectorComponent selectorComp (*deviceManager,
            numIns, numIns, numOuts, numOuts,
            true, false, true, false);
    selectorComp.setSize (400, 450);
    setAlwaysOnTop(false);
    selectorComp.setLookAndFeel(lookAndFeel);
    Colour col(24, 24, 24);
    DialogWindow::showModalDialog(TRANS("Audio Settings"), &selectorComp, this, col, true, false, false);
    bool alwaysontop = getPreference(appProperties, "SetAlwaysOnTop");
    setAlwaysOnTop(alwaysontop);
	resetFilter(true);
	filter->stopProcessing = false;

}
//==============================================================================
void StandaloneFilterWindow::closeButtonPressed()
{
    stopTimer();
    if(filter)
    {
        if(!getPreference(appProperties, "ExternalEditor"))
            if(!filter->saveEditorFiles())
                return;
        filter->getCallbackLock().enter();
        deleteFilter();
    }
    JUCEApplication::quit();
}

void StandaloneFilterWindow::resized()
{
    DocumentWindow::resized();
    optionsButton.setBounds (8, 6, 60, getTitleBarHeight() - 8);

    /*
    if(filter){
    CabbagePluginAudioProcessorEditor* editor = dynamic_cast<CabbagePluginAudioProcessorEditor*>(filter->getActiveEditor());
    if(editor)
    	editor->updateSize();
    }*/
}


//==============================================================================
// Button clicked method
//==============================================================================
void StandaloneFilterWindow::buttonClicked (Button*)
{
    if (filter == nullptr)
        return;

    int examplesOffset = 4000;
    String test;
    PopupMenu subMenu, m;
    m.setLookAndFeel(lookAndFeel);
    subMenu.setLookAndFeel(lookAndFeel);
    PopupMenu recentFilesMenu;
    RecentlyOpenedFilesList recentFiles;
    Array<File> exampleFiles;
    recentFiles.restoreFromString (appProperties->getUserSettings()->getValue ("recentlyOpenedFiles"));

    standaloneMode=false;
    isAFileOpen = true;
    if(!standaloneMode)
    {
        m.addItem(1, String("Open Cabbage Instrument | Ctrl+o"));

        recentFiles.createPopupMenuItems (recentFilesMenu, 9000, false, true);
        m.addSubMenu ("Open recent file", recentFilesMenu);

        String examplesDir = appProperties->getUserSettings()->getValue("ExamplesDir", "");
        if(!File(examplesDir).exists())
        {
#if defined(LINUX) || defined(MACOSX)
            examplesDir = File::getSpecialLocation(File::currentExecutableFile).getParentDirectory().getFullPathName()+"/Examples";
#else
            examplesDir = File::getSpecialLocation(File::currentExecutableFile).getParentDirectory().getFullPathName()+"\\Examples";
#endif
        }

        addFilesToPopupMenu(subMenu, exampleFiles, examplesDir, "*.csd", examplesOffset);
        m.addSubMenu(String("Examples"), subMenu);

        subMenu.clear();

        subMenu.addItem(30, String("Effect"));
        subMenu.addItem(31, String("Instrument"));
        m.addSubMenu(String("New Cabbage..."), subMenu);

        m.addItem(2, String("View Source Editor"));
        m.addItem(3, String("View Csound output"));
        m.addSeparator();
    }

    if(!getPreference(appProperties, "AudioEnabled"))
        m.addItem(400, String("Audio Enabled | Ctrl+m"), true, false);
    else
        m.addItem(400, String("Audio Enabled | Ctrl+m"), true, true);


//	if(getPreference(appProperties, "CabbageEnabled"))
//	m.addItem(400, "Audio Enabled | Ctrl+m", true, true);
//	else
//	m.addItem(400, "Audio Enabled | Ctrl+m", true, false);

    m.addItem(4, TRANS("Audio Settings..."));
    m.addSeparator();
    if(!standaloneMode)
    {
        if(filter->isGuiEnabled() ==  false)
		m.addItem(100, String("Edit-mode"), true, false);
		else
		m.addItem(100, String("Edit-mode"), true, true);
			
        m.addSeparator();
        subMenu.clear();
#ifdef LINUX
        // Make distinction between LV2 and VST on Linux
        subMenu.addItem(17, TRANS("LV2 Plugin Synth"));
        subMenu.addItem(18, TRANS("LV2 Plugin Effect"));
        subMenu.addItem(15, TRANS("VST Plugin Synth"));
        subMenu.addItem(16, TRANS("VST Plugin Effect"));
#else
        subMenu.addItem(15, TRANS("Plugin Synth"));
        subMenu.addItem(16, TRANS("Plugin Effect"));
 #ifdef MACOSX
        subMenu.addItem(150, TRANS("Plugin Synth(Csound bundle)"));
        subMenu.addItem(160, TRANS("Plugin Effect(Csound bundle)"));
 #endif
#endif
        m.addSubMenu(TRANS("Export..."), subMenu);


#ifndef MACOSX
        subMenu.clear();
        subMenu.addItem(5, TRANS("Plugin Synth"));
        subMenu.addItem(6, TRANS("Plugin Effect"));
        m.addSubMenu(TRANS("Export As..."), subMenu);
        subMenu.clear();
        subMenu.addItem(11, TRANS("Effects"));
        subMenu.addItem(12, TRANS("Synths"));
        m.addSubMenu("Batch Convert (Multiple)", subMenu);
        subMenu.clear();
        subMenu.addItem(13, TRANS("Effects"));
        subMenu.addItem(14, TRANS("Synths"));
        m.addSubMenu("Batch Convert (Directory)", subMenu);
#endif
        m.addSeparator();
    }

    if(!standaloneMode)
    {
        m.addItem(8, String("Reload Instrument"));
        /*
        if(filter->getMidiDebug())
        m.addItem(9, TRANS("Show MIDI Debug Information"), true, true);
        else
        m.addItem(9, TRANS("Show MIDI Debug Information"));
        */

        
        	m.addSeparator();
        	if(cabbageDance)
        	m.addItem(99, String("Cabbage Dance"), true, true);
        	else
        	m.addItem(99, String("Cabbage Dance"));
        
        subMenu.clear();

        if(getPreference(appProperties, "SetAlwaysOnTop"))
            subMenu.addItem(7, String("Always on Top"), true, true);
        else
            subMenu.addItem(7, String("Always on Top"), true, false);
        if(getPreference(appProperties, "ShowConsoleWithEditor"))
            subMenu.addItem(20, String("Auto-launch Csound Console with Editor"), true, true);
        else
            subMenu.addItem(20, String("Auto-launch Csound Console with Editor"), true, false);
        //preferences....
        subMenu.addItem(203, "Set Cabbage Plant Directory");
        subMenu.addItem(200, "Set Csound Manual Directory");
        subMenu.addItem(205, "Set Examples Directory");
        if(!getPreference(appProperties, "DisablePluginInfo"))
            subMenu.addItem(201, String("Disable Export Plugin Info"), true, false);
        else
            subMenu.addItem(201, String("Disable Export Plugin Info"), true, true);


        if(!getPreference(appProperties, "ExternalEditor"))
            subMenu.addItem(299, String("Use external editor"), true, false);
        else
            subMenu.addItem(299, String("Use external editor"), true, true);

        if(!getPreference(appProperties, "showTabs"))
            subMenu.addItem(298, String("Show tabs in editor"), true, false);
        else
            subMenu.addItem(298, String("Show tabs in editor"), true, true);

        if(!getPreference(appProperties, "DisableGUIEditModeWarning"))
            subMenu.addItem(202, String("Disable GUI Edit Mode warning"), true, false);
        else
            subMenu.addItem(202, String("Disable GUI Edit Mode warning"), true, true);

        //if(!getPreference(appProperties, "EnablePopupDisplay"))
        //subMenu.addItem(207, String("Enable opcode popup help display"), true, false);
        //else
        //subMenu.addItem(207, String("Enable opcode popup help display"), true, true);


        if(!getPreference(appProperties, "UseCabbageIO"))
            subMenu.addItem(204, String("Use Cabbage IO"), true, false);
        else
            subMenu.addItem(204, String("Use Cabbage IO"), true, true);
//when buiding for inclusion with the Windows Csound installer comment out
//this section
#if !defined(LINUX) && !defined(MACOSX)
        if(!getPreference(appProperties, "UsingCabbageCsound"))
            subMenu.addItem(206, String("Using Cabbage-Csound"), true, false);
        else
            subMenu.addItem(206, String("Using Cabbage-Csound"), true, true);
#endif
//END of commented section for Windows Csound installer
        m.addSubMenu("Preferences", subMenu);
        m.addItem(2000, "About");
        //m.addItem(2345, "Test button");
    }

    int options = m.showAt (&optionsButton);

    //----- open file ------
    if(options==1)
    {
        openFile("");
    }
    else if(options==2345)
    {
//	filter->removeGUIComponent(2);
    }
    else if((options>=examplesOffset) && (options<=examplesOffset+exampleFiles.size()))
    {
        openFile(exampleFiles[options-examplesOffset].getFullPathName());
    }

    else if(options>=9000)
    {
        openFile(recentFiles.getFile(options-9000).getFullPathName());
    }
    else if(options==2000)
    {
        String credits = "				Rory Walsh, Copyright (2008)\n\n";
        credits.append("\t\t\t\tCabbage Farmers:\n", 2056);
        credits.append("\t\t\t\t\tIain McCurdy\n", 2056);
        credits.append("\t\t\t\t\tDamien Rennick\n\n", 2056);
        credits.append("\t\t\t\t\tGiorgio Zucco\n", 2056);
        credits.append("\t\t\t\t\tNil Geisweiller\n", 2056);
        credits.append("\t\t\t\t\tDave Philips\n", 2056);
        credits.append("\t\t\t\t\tEamon Brady\n\n", 2056);
        credits.append("\t\t\t\tUsers Forum:\n", 2056);
        credits.append("\t\t\t\t\twww.thecabbagefoundation.org", 2056);
        String title(CABBAGE_VERSION);
        showMessage("			"+title, credits, lookAndFeel, this);

    }
    //----- view text editor ------
    else if(options==2)
    {
        if(!getPreference(appProperties, "ExternalEditor"))
        {
            if(csdFile.getFileName().length()>0)
            {
                if(!cabbageCsoundEditor)
                {
                    cabbageCsoundEditor = new CodeWindow(csdFile.getFileName());
                    cabbageCsoundEditor->setVisible(false);
                    setupWindowDimensions();
                    cabbageCsoundEditor->addActionListener(this);
                    cabbageCsoundEditor->setLookAndFeel(lookAndFeel);
                    filter->codeEditor = cabbageCsoundEditor->textEditor;
                }
                //cabbageCsoundEditor->setText(csdFile.loadFileAsString());
                this->toBehind(cabbageCsoundEditor);
                cabbageCsoundEditor->setVisible(true);
                setupWindowDimensions();

                cabbageCsoundEditor->toFront(true);

                if(!outputConsole)
                {
                    outputConsole = new CsoundMessageConsole("Csound Output Messages",
                            Colours::black,
                            getPosition().getY()+getHeight(),
                            getPosition().getX());
                    outputConsole->setLookAndFeel(lookAndFeel);
                    outputConsole->setText(filter->getCsoundOutput());
                    if(getPreference(appProperties, "ShowConsoleWithEditor"))
                    {
                        outputConsole->setAlwaysOnTop(true);
                        outputConsole->toFront(true);
                        outputConsole->setVisible(true);
                    }
                    else outputConsole->setVisible(false);
                }
				
                showEditorConsole();
				hasEditorBeingOpened = true;
            }
            else showMessage("Please open or create a file first", lookAndFeel);
        }
        else
            showMessage("Please disable \'Use external editor\' from preferences first", lookAndFeel);
    }
    //-------Csound output console-----
    else if(options==3)
    {
        if(csdFile.getFullPathName().length()>0)
        {
            if(!outputConsole)
            {
                outputConsole = new CsoundMessageConsole("Csound Output Messages",
                        Colours::black,
                        getPosition().getY()+getHeight(),
                        getPosition().getX());
                outputConsole->setLookAndFeel(lookAndFeel);
                outputConsole->setText(filter->getCsoundOutput());
                outputConsole->setAlwaysOnTop(true);
                outputConsole->toFront(true);
                outputConsole->setVisible(true);
            }
            else
            {
                outputConsole->setAlwaysOnTop(true);
                outputConsole->toFront(true);
                outputConsole->setVisible(true);
            }
        }
        else showMessage("Please open or create a file first", lookAndFeel);
    }
    //----- new effect ------
    else if(options==30)
    {
        if(!cabbageCsoundEditor)
        {
            cabbageCsoundEditor = new CodeWindow("");
            cabbageCsoundEditor->addActionListener(this);
            cabbageCsoundEditor->setLookAndFeel(lookAndFeel);
            filter->codeEditor = cabbageCsoundEditor->textEditor;
        }
        cabbageCsoundEditor->setVisible(true);
        cabbageCsoundEditor->newFile("effect");
        saveFileAs();
        setupWindowDimensions();
        showEditorConsole();
        //cabbageCsoundEditor->csoundEditor->textEditor->grabKeyboardFocus();
        isAFileOpen = true;
    }
    //----- new instrument ------
    else if(options==31)
    {
        if(!cabbageCsoundEditor)
        {
            cabbageCsoundEditor = new CodeWindow("");
            cabbageCsoundEditor->setLookAndFeel(lookAndFeel);
            cabbageCsoundEditor->addActionListener(this);
            filter->codeEditor = cabbageCsoundEditor->textEditor;
        }

        cabbageCsoundEditor->setVisible(true);
        cabbageCsoundEditor->newFile("instrument");
        saveFileAs();
        setupWindowDimensions();
        showEditorConsole();
        //cabbageCsoundEditor->csoundEditor->textEditor->grabKeyboardFocus();
        isAFileOpen = true;
    }
    //----- audio settings ------
    else if(options==4)
    {
        showAudioSettingsDialog();
        int val = getPreference(appProperties, "UseCabbageIO");
        if(!val)
            resetFilter(true);
    }

    //suspend audio
    else if(options==400)
    {
        if(getPreference(appProperties, "AudioEnabled"))
        {
            filter->stopProcessing = true;
            setPreference(appProperties, "AudioEnabled", 0);
        }
        else
        {
            AudioEnabled = true;
            filter->stopProcessing = false;
            setPreference(appProperties, "AudioEnabled", 1);
        }
    }

    //----- export ------
    else if(options==15)
        exportPlugin(String("VSTi"), false);

    else if(options==16)
        exportPlugin(String("VST"), false);

    else if(options==17)
        exportPlugin(String("LV2-ins"), false);

    else if(options==18)
        exportPlugin(String("LV2-fx"), false);

    //----- export ------
    else if(options==150)
        exportPlugin(String("CsoundVSTi"), false);

    else if(options==160)
        exportPlugin(String("CsoundVST"), false);

    //----- export as ------
    else if(options==5)
        exportPlugin(String("VSTi"), true);

    else if(options==6)
        exportPlugin(String("VST"), true);

    //----- always on top  ------
    else if(options==7)

        if(getPreference(appProperties, "SetAlwaysOnTop"))
        {
            this->setAlwaysOnTop(false);
            setPreference(appProperties, "SetAlwaysOnTop", 0);
        }
        else
        {
            this->setAlwaysOnTop(true);
            setPreference(appProperties, "SetAlwaysOnTop", 1);
        }

    //----- update instrument  ------
    else if(options==8)
    {
        resetFilter(true);
        //filter->reCompileCsound();
    }
    //----- update GUI only -----
    else if(options==9)
        filter->createGUI(csdFile.loadFileAsString(), true);

    //----- batch process ------
    else if(options==11)
        batchProcess(String("VST"), false);

    else if(options==12)
        batchProcess(String("VSTi"), false);
    //----- batch process ------
    else if(options==13)
        batchProcess(String("VST"), true);

    else if(options==14)
        batchProcess(String("VSTi"), true);

    //----- auto-update file when saved remotely ------
    else if(options==299)
    {
        if(getPreference(appProperties, "ExternalEditor")==0)
        {
            setPreference(appProperties, "ExternalEditor", 1);
            startTimer(100);
        }
        else
        {
            setPreference(appProperties, "ExternalEditor", 0);
            stopTimer();
        }
    }

    //----- show console with editor -------------
    else if(options==20)
    {
        toggleOnOffPreference(appProperties, "ShowConsoleWithEditor");
    }

    //------- preference Csound manual dir ------
    else if(options==200)
    {
        String dir = getPreference(appProperties, "CsoundHelpDir", "");
#ifdef MACOSX
        dir = "Library/Frameworks/CsoundLib64.framework/Versions/6.0/Resources/Manual";
#endif
        FileChooser browser(String("Please select the Csound manual directory...\n"), File(dir), String("*.csd"), UseNativeDialogue);
        if(browser.browseForDirectory())
        {
            setPreference(appProperties, "CsoundHelpDir", browser.getResult().getFullPathName());
        }
    }
    //------- preference Csound manual dir ------
    else if(options==203)
    {
        String dir = getPreference(appProperties, "PlantFileDir", "");
        FileChooser browser(String("Please select your Plant file directory..."), File(dir), String("*.csd"), UseNativeDialogue);
        if(browser.browseForDirectory())
        {
            setPreference(appProperties, "PlantFileDir", browser.getResult().getFullPathName());
        }
    }
    //--------preference Show tabs
    else if(options==298)
    {
        /*
        if(getPreference(appProperties, "showTabs")==0)
        {
        	setPreference(appProperties, "showTabs", 1);
        	//if(!cabbageCsoundEditor){
        		cabbageCsoundEditor->textEditor->showTabs(true);
        		cabbageCsoundEditor->textEditor->showInstrs(true);
        	//}
        }
        else
        {
        	setPreference(appProperties, "showTabs", 0);
        	//if(!cabbageCsoundEditor){
        		cabbageCsoundEditor->textEditor->showTabs(false);
        		cabbageCsoundEditor->textEditor->showInstrs(false);
        	//}
        }*/
    }

    else if(options==204)
    {
        toggleOnOffPreference(appProperties, "UseCabbageIO");
    }

    //--------using Cabbage-Csound
    else if(options==206)
    {
        String homeFolder = File::getSpecialLocation(File::currentExecutableFile).getParentDirectory().getFullPathName();
        //showMessage(homeFolder);
        if(getPreference(appProperties, "UsingCabbageCsound")==0)
        {
            setPreference(appProperties, "UsingCabbageCsound", 1);
            String homeFolder = File::getSpecialLocation(File::currentExecutableFile).getParentDirectory().getFullPathName();
            String pluginFolder = homeFolder+"\\Disabled_CsoundPlugins";
            String csoundDLL = homeFolder+"\\Disabled_csound64.dll";
            if(!File(pluginFolder).moveFileTo(File(homeFolder+"\\CsoundPlugins")))
                showMessage("Could not find Csound plugins folder?", &getLookAndFeel());
            if(!File(csoundDLL).moveFileTo(File(homeFolder+"\\csound64.dll")))
                showMessage("Could not find Csound library dll?", &getLookAndFeel());
        }
        else
        {
            setPreference(appProperties, "UsingCabbageCsound", 0);
            String pluginFolder = homeFolder+"\\CsoundPlugins";
            String csoundDLL = homeFolder+"\\csound64.dll";
            if(!File(pluginFolder).moveFileTo(File(homeFolder+"\\Disabled_CsoundPlugins")))
                showMessage("Could not find Disable_CsoundPlugins folder?", &getLookAndFeel());
            if(!File(csoundDLL).moveFileTo(File(homeFolder+"\\Disabled_csound64.dll")))
                showMessage("Could not find Disabled_Csound64.dll?", &getLookAndFeel());

        }
    }

    //------- preference Examples dir ------
    else if(options==205)
    {
        String dir = getPreference(appProperties, "ExamplesDir", "");
        FileChooser browser(String("Please select your Examples directory..."), File(dir), String("*.csd"), UseNativeDialogue);
        if(browser.browseForDirectory())
        {
            setPreference(appProperties, "ExamplesDir", browser.getResult().getFullPathName());
        }
    }

    //------- preference plugin info ------
    else if(options==201)
    {
        toggleOnOffPreference(appProperties, "DisablePluginInfo");
    }

    //------- preference popup display ------
    else if(options==207)
    {
        toggleOnOffPreference(appProperties, "EnablePopupDisplay");
    }

    //------- preference disable gui edit warning ------
    else if(options==99)
    {
        cabbageDance=!cabbageDance;
		if(cabbageDance)
			startTimer(20);
		else
			startTimer(100);
    }
	
    //------- preference disable gui edit warning ------
    else if(options==202)
    {
        toggleOnOffPreference(appProperties, "DisableGUIEditModeWarning");
    }
    //------- enable GUI edit mode------
    else if(options==100)
    {
        if(!getPreference(appProperties, "DisableGUIEditModeWarning"))
            showMessage("", "Warning!! This feature is still under development! Whilst every effort has been made to make it as usable as possible, there might still be some teething problems that need sorting out. If you find a problem, please try to recreate it, note the steps involved, and report it to the Cabbage users forum (www.TheCabbageFoundation.org). Thank you. You may disable this warning in 'Options->Preferences'", lookAndFeel);
        else
        {
            //if(getPreference(appProperties, "ExternalEditor")==0)
            if(hasEditorBeingOpened==false)
			{
			openTextEditor();
			hasEditorBeingOpened = true;
			}
            if(isAFileOpen == true)
                if(filter->isGuiEnabled())
                {
                    if(getPreference(appProperties, "ExternalEditor")==1)
                        csdFile = File(csdFile.getFullPathName());
                    startTimer(100);
                    //filter->suspendProcessing(false);
                    ((CabbagePluginAudioProcessorEditor*)filter->getActiveEditor())->setEditMode(false);
                    filter->setGuiEnabled(false);
                }
                else
                {
                    ((CabbagePluginAudioProcessorEditor*)filter->getActiveEditor())->setEditMode(true);
                    filter->setGuiEnabled(true);
                    //filter->suspendProcessing(true);
                    stopTimer();
                    //setPreference(appProperties, "ExternalEditor", 0);
                }
            else showMessage("", "Open or create a file first", &getLookAndFeel(), this);
        }
    }
    repaint();
}


//=================
// setup window dimensions etc..
//==================
void StandaloneFilterWindow::setupWindowDimensions()
{
#ifdef LINUX
    Rectangle<int> rect(Desktop::getInstance().getDisplays().getMainDisplay().userArea);
    rect.setHeight(rect.getHeight()-25);
    cabbageCsoundEditor->setBounds(rect);
#elif WIN32
    cabbageCsoundEditor->setFullScreen(true);
#else
    cabbageCsoundEditor->setSize(1000, 800);
#endif
}
//==============================================================================
// open text editor
//==============================================================================
void StandaloneFilterWindow::showEditorConsole()
{
    if(getPreference(appProperties, "ShowEditorConsole")==1)
    {
        cabbageCsoundEditor->splitWindow->SetSplitBarPosition(cabbageCsoundEditor->getHeight()-(cabbageCsoundEditor->getHeight()/4));
#ifdef BUILD_DEBUGGER
        cabbageCsoundEditor->splitBottomWindow->SetSplitBarPosition(cabbageCsoundEditor->getWidth()/2);
#endif
    }
    else
        cabbageCsoundEditor->splitWindow->SetSplitBarPosition(cabbageCsoundEditor->getHeight());
}

//==============================================================================
// open text editor
//==============================================================================
void StandaloneFilterWindow::openTextEditor()
{
    if(csdFile.getFileName().length()>0)
    {
        cabbageCsoundEditor->setText(filter->getCsoundInputFileText(), filter->getCsoundInputFile().getFullPathName());
        this->toBehind(cabbageCsoundEditor);
        cabbageCsoundEditor->setVisible(true);
        setupWindowDimensions();
        cabbageCsoundEditor->toFront(true);
        showEditorConsole();


        if(getPreference(appProperties, "ShowConsoleWithEditor"))
            if(!outputConsole)
            {
                outputConsole = new CsoundMessageConsole("Csound Output Messages",
                        Colours::black,
                        getPosition().getY()+getHeight(),
                        getPosition().getX());
                outputConsole->setLookAndFeel(lookAndFeel);
                outputConsole->setText(filter->getCsoundOutput());
                outputConsole->setAlwaysOnTop(true);
                outputConsole->toFront(true);
                outputConsole->setVisible(true);
            }
    }
    else showMessage("Please open or create a file first", lookAndFeel);
}

//==========================================================================


//==============================================================================
// open/save/save as methods
//==============================================================================
void StandaloneFilterWindow::openFile(String _csdfile)
{
    if(_csdfile.length()>4)
    {
        csdFile = File(_csdfile);
        originalCsdFile = csdFile;
        lastSaveTime = csdFile.getLastModificationTime();
        csdFile.setAsCurrentWorkingDirectory();
        resetFilter(true);
    }
    else
    {
#ifdef MACOSX
        FileChooser openFC(String("Open a Cabbage .csd file..."), File::nonexistent, String("*.csd;*.vst"), UseNativeDialogue);
        if(openFC.browseForFileToOpen())
        {
            csdFile = openFC.getResult();
            originalCsdFile = openFC.getResult();
            lastSaveTime = csdFile.getLastModificationTime();
            csdFile.setAsCurrentWorkingDirectory();
            if(csdFile.getFileExtension()==(".vst"))
            {
                String csd = csdFile.getFullPathName();
                csd << "/Contents/" << csdFile.getFileNameWithoutExtension() << ".csd";
                csdFile = File(csd);
            }

            cabbageCsoundEditor->setText(csdFile.loadFileAsString(), csdFile.getFullPathName());
            cabbageCsoundEditor->textEditor->setAllText(csdFile.loadFileAsString());
            isAFileOpen = true;
            resetFilter(true);
        }
#else
        FileChooser openFC(String("Open a Cabbage .csd file..."), File::nonexistent, String("*.csd"), UseNativeDialogue);
        this->setAlwaysOnTop(false);
        if(openFC.browseForFileToOpen())
        {
            csdFile = openFC.getResult();
            csdFile.getParentDirectory().setAsCurrentWorkingDirectory();
            lastSaveTime = csdFile.getLastModificationTime();
            resetFilter(true);
            cabbageCsoundEditor->setText(csdFile.loadFileAsString(), csdFile.getFullPathName());
            cabbageCsoundEditor->textEditor->setAllText(csdFile.loadFileAsString());
            isAFileOpen = true;
        }
        if(getPreference(appProperties, "SetAlwaysOnTop"))
            setAlwaysOnTop((true));
        else
            setAlwaysOnTop(false);
#endif
    }
    RecentlyOpenedFilesList recentFiles;
    recentFiles.restoreFromString (appProperties->getUserSettings()->getValue ("recentlyOpenedFiles"));
    recentFiles.addFile (csdFile);
    appProperties->getUserSettings()->setValue ("recentlyOpenedFiles",
            recentFiles.toString());
}

void StandaloneFilterWindow::saveFile()
{
    if(csdFile.hasWriteAccess())
    {
        csdFile.replaceWithText(cabbageCsoundEditor->getText());
    }
    else
    {
        showMessage("no write access..");
        return;
    }
    resetFilter(false);
    RecentlyOpenedFilesList recentFiles;
    recentFiles.restoreFromString (appProperties->getUserSettings()->getValue ("recentlyOpenedFiles"));
    recentFiles.addFile (csdFile);
    appProperties->getUserSettings()->setValue ("recentlyOpenedFiles",
            recentFiles.toString());

    filter->saveText();
}

void StandaloneFilterWindow::saveFileAs()
{
    FileChooser saveFC(String("Save Cabbage file as..."), File::nonexistent, String("*.csd"), UseNativeDialogue);
    this->setAlwaysOnTop(false);
    if(saveFC.browseForFileToSave(true))
    {
        csdFile = saveFC.getResult().withFileExtension(String(".csd"));
        csdFile.replaceWithText(cabbageCsoundEditor->getText());
        cabbageCsoundEditor->setText(csdFile.loadFileAsString(), csdFile.getFullPathName());
        resetFilter(true);
    }
    if(getPreference(appProperties, "SetAlwaysOnTop"))
        setAlwaysOnTop(true);
    else
        setAlwaysOnTop(false);

    RecentlyOpenedFilesList recentFiles;
    recentFiles.restoreFromString (appProperties->getUserSettings()->getValue ("recentlyOpenedFiles"));
    recentFiles.addFile (csdFile);
    appProperties->getUserSettings()->setValue ("recentlyOpenedFiles",
            recentFiles.toString());
    filter->saveText();

}
//==============================================================================
// Export plugin method
//==============================================================================
int StandaloneFilterWindow::exportPlugin(String type, bool saveAs)
{
    File dll;
    File loc_csdFile;
#ifndef LINUX
    File thisFile(File::getSpecialLocation(File::currentApplicationFile));
#else
    File thisFile(File::getSpecialLocation(File::currentExecutableFile));
#endif

    if(!csdFile.exists())
    {
        showMessage("", "You need to open a Cabbage instrument before you can export one as a plugin!", lookAndFeel, this);
        return 0;
    }
#ifdef LINUX
    FileChooser saveFC(String("Save as..."), File::nonexistent, String(""), UseNativeDialogue);
    String VST;
    Logger::writeToLog(currentApplicationDirectory);
    if (saveFC.browseForFileToSave(true))
    {
        if(type.contains("VSTi"))
            VST = currentApplicationDirectory + String("/CabbagePluginSynth.so");
        else if(type.contains(String("VST")))
            VST = currentApplicationDirectory + String("/CabbagePluginEffect.so");
        else if(type.contains(String("LV2-ins")))
            VST = currentApplicationDirectory + String("/CabbagePluginSynthLV2.so");
        else if(type.contains(String("LV2-fx")))
            VST = currentApplicationDirectory + String("/CabbagePluginEffectLV2.so");
        else if(type.contains(String("AU")))
        {
            showMessage("", "This feature only works on computers running OSX", lookAndFeel, this);
        }
        //Logger::writeToLog(VST);
        //showMessage(VST);
        File VSTData(VST);
        if(!VSTData.exists())
        {
            this->setMinimised(true);
            showMessage("", VST+" cannot be found?", lookAndFeel, this);
        }

        else
        {
            if (type.contains("LV2"))
            {
                String filename(saveFC.getResult().getFileNameWithoutExtension());
                File bundle(saveFC.getResult().withFileExtension(".lv2").getFullPathName());
                bundle.createDirectory();
                File dll(bundle.getChildFile(filename+".so"));
                Logger::writeToLog(bundle.getFullPathName());
                Logger::writeToLog(dll.getFullPathName());
                if(!VSTData.copyFileTo(dll)) showMessage("", "Can not move lib", lookAndFeel, this);
                File loc_csdFile(bundle.getChildFile(filename+".csd").getFullPathName());
                loc_csdFile.replaceWithText(csdFile.loadFileAsString());

                // this generates the ttl data
                typedef void (*TTL_Generator_Function)(const char* basename);
                DynamicLibrary lib(dll.getFullPathName());
                TTL_Generator_Function genFunc = (TTL_Generator_Function)lib.getFunction("lv2_generate_ttl");
                if(!genFunc) { showMessage("", "Can not generate LV2 data", lookAndFeel, this); return 1; }

                // change CWD for ttl generation
                File oldCWD(File::getCurrentWorkingDirectory());
                bundle.setAsCurrentWorkingDirectory();
                (genFunc)(filename.toRawUTF8());
                oldCWD.setAsCurrentWorkingDirectory();
            }
            else
            {
                File dll(saveFC.getResult().withFileExtension(".so").getFullPathName());
                Logger::writeToLog(dll.getFullPathName());
                if(!VSTData.copyFileTo(dll))	showMessage("", "Can not move lib", lookAndFeel, this);
                File loc_csdFile(saveFC.getResult().withFileExtension(".csd").getFullPathName());
                loc_csdFile.replaceWithText(csdFile.loadFileAsString());
            }
        }
    }
#elif WIN32
    FileChooser saveFC(String("Save plugin as..."), File::nonexistent, String("*.dll"), UseNativeDialogue);
    String VST;
    if(type.contains("VSTi"))
        VST = thisFile.getParentDirectory().getFullPathName() + String("\\CabbagePluginSynth.dat");
    else if(type.contains(String("VST")))
        VST = thisFile.getParentDirectory().getFullPathName() + String("\\CabbagePluginEffect.dat");

    File VSTData(VST);

    if(!VSTData.exists())
    {
        showMessage("", "Cabbage cannot find the plugin libraries. Make sure that Cabbage is situated in the same directory as CabbagePluginSynth.dat and CabbagePluginEffect.dat", oldLookAndFeel, this);
        return 0;
    }
    else
    {
        if(saveAs)
        {
            if (saveFC.browseForFileToSave(true))
            {
                dll = saveFC.getResult().withFileExtension(".dll").getFullPathName();
                loc_csdFile = saveFC.getResult().withFileExtension(".csd").getFullPathName();
            }
            else
                return 0;
        }
        else
        {
            dll = csdFile.withFileExtension(".dll").getFullPathName();
            loc_csdFile = csdFile.withFileExtension(".csd").getFullPathName();

        }
        //showMessage(dll.getFullPathName());
        if(!VSTData.copyFileTo(dll))
            showMessage("", "Problem moving plugin lib, make sure it's not currently open in your plugin host!", lookAndFeel, this);

        loc_csdFile.replaceWithText(csdFile.loadFileAsString());
        setUniquePluginID(dll, loc_csdFile, false);
        String info;
        info = String("Your plugin has been created. It's called:\n\n")+dll.getFullPathName()+String("\n\nIn order to modify this plugin you only have to edit the associated .csd file. You do not need to export every time you make changes.\n\nTo turn off this notice visit 'Preferences' in the main 'options' menu");

        int val = getPreference(appProperties, "DisablePluginInfo");
        if(!val)
            showMessage(info, lookAndFeel);
    }

#endif

#if MACOSX

    FileChooser saveFC(String("Save as..."), File::nonexistent, String("*.vst"), UseNativeDialogue);
    String VST;
    if (saveFC.browseForFileToSave(true))
    {
        //showMessage("name of file is:");
        //showMessage(saveFC.getResult().withFileExtension(".vst").getFullPathName());

        if(type.contains("VSTi"))
            VST = thisFile.getFullPathName()+"/Contents/CabbagePluginSynth.component";
        else if(type.contains(String("VST")))
            VST = thisFile.getFullPathName()+"/Contents/CabbagePluginEffect.component";
        else if(type.contains("CsoundVSTi"))
            VST = thisFile.getFullPathName()+"/Contents/CabbageCsoundPluginSynth.component";
        else if(type.contains(String("CsoundVST")))
            VST = thisFile.getFullPathName()+"/Contents/CabbageCsoundPluginEffect.component";
        else if(type.contains(String("AU")))
        {
            showMessage("this feature is coming soon");
            //VST = thisFile.getParentDirectory().getFullPathName() + String("\\CabbageVSTfx.component");
        }

        //create a copy of the data package and write it to the new location given by user
        File VSTData(VST);
        if(!VSTData.exists())
        {
            showMessage("Cabbage cannot find the plugin libraries. Make sure that Cabbage is situated in the same directory as CabbagePluginSynth.dat and CabbagePluginEffect.dat", lookAndFeel);
            return 0;
        }


        String plugType = ".vst";
//		if(type.contains(String("AU")))
//			plugType = String(".component");
//		else plugType = String(".vst");

        File dll(saveFC.getResult().withFileExtension(plugType).getFullPathName());

        VSTData.copyFileTo(dll);
        //showMessage("copied");



        File pl(dll.getFullPathName()+String("/Contents/Info.plist"));
        String newPList = pl.loadFileAsString();
        //write our identifiers to the plist file
        newPList = newPList.replace("CabbagePlugin", saveFC.getResult().getFileNameWithoutExtension());
        newPList = newPList.replace("NewProject", saveFC.getResult().getFileNameWithoutExtension());
        //write plist file
        pl.replaceWithText(newPList);



        File loc_csdFile(dll.getFullPathName()+String("/Contents/")+saveFC.getResult().getFileNameWithoutExtension()+String(".csd"));
        loc_csdFile.replaceWithText(csdFile.loadFileAsString());
        //showMessage(loc_csdFile.getFullPathName());
        //showMessage(csdFile.loadFileAsString());
        csdFile = loc_csdFile;

        //showMessage(VST+String("/Contents/MacOS/")+saveFC.getResult().getFileNameWithoutExtension());
        //if plugin already exists there is no point in rewriting the binaries
        if(!File(saveFC.getResult().withFileExtension(".vst").getFullPathName()+("/Contents/MacOS/")+saveFC.getResult().getFileNameWithoutExtension()).exists())
        {
            File bin(dll.getFullPathName()+String("/Contents/MacOS/CabbagePlugin"));
            //if(bin.exists())showMessage("binary exists");


            File pluginBinary(dll.getFullPathName()+String("/Contents/MacOS/")+saveFC.getResult().getFileNameWithoutExtension());

            bin.moveFileTo(pluginBinary);
            //else
            //showMessage("could not copy library binary file");

            setUniquePluginID(pluginBinary, loc_csdFile, true);
        }

        String info;
        info = String("Your plugin has been created. It's called:\n\n")+dll.getFullPathName()+String("\n\nIn order to modify this plugin you only have to edit the current .csd file. You do not need to export every time you make changes. To open the current csd file with Cabbage in another session, go to 'Open Cabbage Instrument' and select the .vst file. Cabbage will the load the associated .csd file. \n\nTo turn off this notice visit 'Preferences' in the main 'options' menu");

        int val = appProperties->getUserSettings()->getValue("DisablePluginInfo", var(0)).getFloatValue();
        if(!val)
            showMessage(info, lookAndFeel);


    }

#endif

    return 0;
}

//==============================================================================
// Set unique plugin ID for each plugin based on the file name
//==============================================================================
int StandaloneFilterWindow::setUniquePluginID(File binFile, File csdFile, bool AU)
{
    String newID;
    StringArray csdText;
    csdText.addLines(csdFile.loadFileAsString());
    //read contents of csd file to find pluginID
    for(int i=0; i<csdText.size(); i++)
    {
        StringArray tokes;
        tokes.addTokens(csdText[i].trimEnd(), ", ", "\"");
        if(tokes[0].equalsIgnoreCase(String("form")))
        {
            CabbageGUIClass cAttr(csdText[i].trimEnd(), 0);
            if(cAttr.getStringProp(CabbageIDs::pluginid).length()!=4)
            {
                showMessage(String("Your plugin ID is not the right size. It MUST be 4 characters long. Some hosts may not be able to load your plugin"), lookAndFeel);
                return 0;
            }
            else
            {
                newID = cAttr.getStringProp(CabbageIDs::pluginid);
                i = csdText.size();
            }
        }
    }

    size_t file_size;
    const char *pluginID;
    //if(!AU)
    pluginID = "YROR";
    //else
    //	pluginID = "RORY";


    long loc;
    //showMessage(binFile.getFullPathName(), lookAndFeel);
    fstream mFile(binFile.getFullPathName().toUTF8(), ios_base::in | ios_base::out | ios_base::binary);
    
	if(mFile.is_open())
    {
        mFile.seekg (0, ios::end);
        file_size = mFile.tellg();
        unsigned char* buffer = (unsigned char*)malloc(sizeof(unsigned char)*file_size);
		//set plugin ID, do this a few times in case the plugin ID appear in more than one place.
        for(int r=0; r<10; r++)
        {
			mFile.seekg (0, ios::beg);
            mFile.read((char*)&buffer[0], file_size);
            loc = cabbageFindPluginID(buffer, file_size, pluginID);
            if (loc < 0)
			{
                //showMessage(String("Internel Cabbage Error: The pluginID was not found"));
                break;
			}
            else
            {
                //showMessage(newID);
                mFile.seekg (loc, ios::beg);
                mFile.write(newID.toUTF8(), 4);
            }
        }
        //set plugin name based on .csd file
        const char *pluginName = "CabbageEffectNam";
        String plugLibName = csdFile.getFileNameWithoutExtension();
        if(plugLibName.length()<16)
            for(int y=plugLibName.length(); y<16; y++)
                plugLibName.append(String(" "), 1);

        mFile.seekg (0, ios::beg);
        buffer = (unsigned char*)malloc(sizeof(unsigned char)*file_size);
        mFile.read((char*)&buffer[0], file_size);
        loc = cabbageFindPluginID(buffer, file_size, pluginName);
        if (loc < 0)
            showMessage(String("Plugin name could not be set?!?"), lookAndFeel);
        else
        {
            //showMessage("plugin name set!");
            mFile.seekg (loc, ios::beg);
            mFile.write(csdFile.getFileNameWithoutExtension().toUTF8(), 16);
        }
        //#endif

    }
    else
        showMessage("File could not be opened", lookAndFeel);

    mFile.close();
    return 1;
}

long StandaloneFilterWindow::cabbageFindPluginID(unsigned char *buf, size_t len, const char *s)
{
        long i, j;
        int slen = strlen(s);
        long imax = len - slen - 1;
        long ret = -1;
        int match;

        for(i=0; i<imax; i++)
        {
            match = 1;
            for (j=0; j<slen; j++)
			{
                if (buf[i+j] != s[j])
                {
                    match = 0;
                    break;
                }
			}
            if (match)
            {
                ret = i;
                break;
            }
        }
        //return position of plugin ID
        return ret;
    }

//==============================================================================
// Batch process multiple csd files to convert them to plugins libs.
//==============================================================================
void StandaloneFilterWindow::batchProcess(String type, bool dir)
{
    File thisFile(File::getSpecialLocation(File::currentApplicationFile));
#ifdef WIN32
    FileChooser saveFC(String("Select files..."), File::nonexistent, String("*.csd;"), UseNativeDialogue);
    String VST;

    Array<File> files;
    if(dir)
    {
        if (saveFC.browseForDirectory())
        {
            if(type.contains("VSTi"))
                VST = thisFile.getParentDirectory().getFullPathName() + String("\\CabbagePluginSynth.dat");
            else if(type.contains(String("VST")))
                VST = thisFile.getParentDirectory().getFullPathName() + String("\\CabbagePluginEffect.dat");
            saveFC.getResult().findChildFiles(files, 2, true, "*.csd;");
        }
    }
    else
    {
        if (saveFC.browseForMultipleFilesToOpen())
        {
            if(type.contains("VSTi"))
                VST = thisFile.getParentDirectory().getFullPathName() + String("\\CabbagePluginSynth.dat");
            else if(type.contains(String("VST")))
                VST = thisFile.getParentDirectory().getFullPathName() + String("\\CabbagePluginEffect.dat");
            else if(type.contains(String("AU")))
            {
                showMessage("This feature only works on computers running OSX");
            }
            files = saveFC.getResults();
        }
    }
    File VSTData(VST);
    if(!VSTData.exists())
        showMessage("Cannot find plugin libs", &getLookAndFeel());
    else
    {

 

        for(int i=0; i<files.size(); i++)
        {
            File dll(files.getReference(i).withFileExtension(".dll").getFullPathName());
            //showMessage(dll.getFullPathName());
            if(!VSTData.copyFileTo(dll))
                showMessage("problem moving plugin lib");
            setUniquePluginID(dll, files.getReference(i), false);
        }
        showMessage("Batch Convertion Complete", &getLookAndFeel());
    }

#endif
}
