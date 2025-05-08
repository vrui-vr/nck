/***********************************************************************
New version of Nanotech Construction Kit for multithreading and remote
collaboration.
Copyright (c) 2017-2025 Oliver Kreylos

The Nanotech Construction Kit is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The Nanotech Construction Kit is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along
with the Nanotech Construction Kit; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
***********************************************************************/

#include "NewNanotechConstructionKit.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <Misc/MessageLogger.h>
#include <Misc/Marshaller.h>
#include <Misc/StandardMarshallers.h>
#include <Misc/CompoundMarshallers.h>
#include <Misc/ConfigurationFile.h>
#include <Realtime/Time.h>
#include <IO/File.h>
#include <IO/OpenFile.h>
#include <Cluster/MulticastPipe.h>
#include <Math/Math.h>
#include <Geometry/OrthogonalTransformation.h>
#include <Geometry/LinearUnit.h>
#include <Geometry/GeometryMarshallers.h>
#include <Geometry/OutputOperators.h>
#include <GL/gl.h>
#include <GL/GLColorTemplates.h>
#include <GL/GLContextData.h>
#include <GL/Extensions/GLARBVertexBufferObject.h>
#include <GL/GLGeometryWrappers.h>
#include <GLMotif/StyleSheet.h>
#include <GLMotif/PopupMenu.h>
#include <GLMotif/PopupWindow.h>
#include <GLMotif/RowColumn.h>
#include <GLMotif/Separator.h>
#include <GLMotif/Label.h>
#include <GLMotif/ToggleButton.h>
#include <Vrui/Vrui.h>
#include <Vrui/InputDevice.h>
#include <Vrui/CoordinateManager.h>
#include <Vrui/GenericAbstractToolFactory.h>
#include <Vrui/ToolManager.h>
#include <Vrui/ClusterSupport.h>
#include <Collaboration2/Client.h>

#include "Common.h"
#include "IO.h"
#include "IndirectSimulationInterface.h"
#include "Simulation.h"
#include "ClusterSlaveSimulation.h"
#include "NCKClient.h"

#include "Config.h"

/*************************************************************
Methods of class NewNanotechConstructionKit::ClusterForwarder:
*************************************************************/

void* NewNanotechConstructionKit::ClusterForwarder::communicationThreadMethod(void)
	{
	/* Run the communication thread until interrupted: */
	Realtime::TimePointMonotonic nextUpdate;
	Realtime::TimeVector interval(distributionInterval);
	while(keepRunning)
		{
		/* Reduce newest simulation state for rendering: */
		if(sim->lockNewState()&&sim->isLockedStateValid())
			{
			const UnitStateArray& states=sim->getLockedState();
			
			/* Reduce the new unit states into a new reduced state array: */
			ReducedUnitStateArray& rStates=unitStates.startNewValue();
			rStates.states.clear();
			rStates.states.reserve(states.states.size());
			ReducedUnitState r;
			for(UnitStateArray::UnitStateList::const_iterator sIt=states.states.begin();sIt!=states.states.end();++sIt)
				{
				r.set(*sIt);
				rStates.states.push_back(r);
				}
			
			/* Forward the new reduced unit state array to the cluster: */
			clusterPipe->write(Misc::UInt8(ClusterSlaveSimulation::UpdateSimulation));
			writeStateArray(rStates,*clusterPipe,true);
			clusterPipe->flush();
			
			/* Push new reduced state to the foreground thread: */
			unitStates.postNewValue();
			}
		
		/* Sleep until the next update time: */
		nextUpdate+=interval;
		Realtime::TimePointMonotonic::sleep(nextUpdate);
		}
	
	return 0;
	}

NewNanotechConstructionKit::ClusterForwarder::ClusterForwarder(Simulation* sSim,Cluster::MulticastPipe* sClusterPipe,double sDistributionInterval)
	:sim(sSim),
	 clusterPipe(sClusterPipe),
	 distributionInterval(sDistributionInterval)
	{
	/* Send list of unit types to the cluster: */
	Misc::write(sim->getUnitTypes(),*clusterPipe);
	
	/* Send domain size to the cluster: */
	Misc::write(sim->getDomain(),*clusterPipe);
	
	/* Send simulation parameters to the cluster: */
	sim->getParameters().write(*clusterPipe);
	
	clusterPipe->flush();
	
	/* Start the communication thread: */
	keepRunning=true;
	communicationThread.start(this,&NewNanotechConstructionKit::ClusterForwarder::communicationThreadMethod);
	}

NewNanotechConstructionKit::ClusterForwarder::~ClusterForwarder(void)
	{
	/* Shut down the communication thread: */
	keepRunning=false;
	communicationThread.join();
	
	/* Send a shutdown signal to the cluster: */
	clusterPipe->write(Misc::UInt8(ClusterSlaveSimulation::Shutdown));
	clusterPipe->flush();
	
	/* Shut down cluster communication: */
	delete clusterPipe;
	}

void NewNanotechConstructionKit::ClusterForwarder::updateSession(void)
	{
	/* How? */
	}

void NewNanotechConstructionKit::ClusterForwarder::updateParameters(void)
	{
	/* How? */
	}

/************************************************************
Methods of class NewNanotechConstructionKit::UnitToolFactory:
************************************************************/

NewNanotechConstructionKit::UnitToolFactory::UnitToolFactory(const char* sClassName,const char* sDisplayName,Vrui::ToolFactory* parentClass,NewNanotechConstructionKit::UnitToolFactory::ToolType sToolType,bool sPickConnected,Vrui::ToolManager& toolManager)
	:Vrui::ToolFactory(sClassName,toolManager),
	 displayName(sDisplayName),
	 toolType(sToolType),
	 pickConnected(sPickConnected),
	 createUnitType(0)
	{
	/* Initialize tool layout: */
	layout.setNumButtons(1);
	
	if(parentClass!=0)
		{
		/* Insert the class into the tool hierarchy: */
		parentClass->addChildClass(this);
		addParentClass(parentClass);
		}
	}

NewNanotechConstructionKit::UnitToolFactory::UnitToolFactory(const char* sClassName,const char* sDisplayName,Vrui::ToolFactory* parentClass,UnitTypeID sCreateUnitType,const std::string& sCreateUnitTypeName,Vrui::ToolManager& toolManager)
	:Vrui::ToolFactory(sClassName,toolManager),
	 displayName(sDisplayName),
	 toolType(CREATE),
	 pickConnected(false),
	 createUnitType(sCreateUnitType),createUnitTypeName(sCreateUnitTypeName)
	{
	/* Initialize tool layout: */
	layout.setNumButtons(1);
	
	if(parentClass!=0)
		{
		/* Insert the class into the tool hierarchy: */
		parentClass->addChildClass(this);
		addParentClass(parentClass);
		}
	}

const char* NewNanotechConstructionKit::UnitToolFactory::getName(void) const
	{
	return displayName.c_str();
	}

const char* NewNanotechConstructionKit::UnitToolFactory::getButtonFunction(int buttonSlotIndex) const
	{
	switch(toolType)
		{
		case PICK:
			if(pickConnected)
				return "Pick Complex";
			else
				return "Pick Unit";
		
		case PASTE:
			return "Paste Copy Buffer";
		
		case CREATE:
			if(pickConnected)
				return "Pick Complex/Create Unit";
			else
				return "Pick/Create Unit";
		
		case COPY:
			if(pickConnected)
				return "Copy Complex";
			else
				return "Copy Unit";
		
		case DESTROY:
			if(pickConnected)
				return "Destroy Complex";
			else
				return "Destroy Unit";
		
		default:
			return 0;
		}
	}

Vrui::Tool* NewNanotechConstructionKit::UnitToolFactory::createTool(const Vrui::ToolInputAssignment& inputAssignment) const
	{
	return new UnitTool(this,inputAssignment);
	}

void NewNanotechConstructionKit::UnitToolFactory::destroyTool(Vrui::Tool* tool) const
	{
	delete tool;
	}

void NewNanotechConstructionKit::UnitToolFactory::setCreateUnitType(UnitTypeID newCreateUnitType)
	{
	createUnitType=newCreateUnitType;
	}

/*****************************************************
Methods of class NewNanotechConstructionKit::UnitTool:
*****************************************************/

NewNanotechConstructionKit::UnitTool::UnitTool(const NewNanotechConstructionKit::UnitToolFactory* sFactory,const Vrui::ToolInputAssignment& inputAssignment)
	:Vrui::Tool(sFactory,inputAssignment),
	 factory(sFactory),
	 pickId(0)
	{
	}

const Vrui::ToolFactory* NewNanotechConstructionKit::UnitTool::getFactory(void) const
	{
	return factory;
	}

void NewNanotechConstructionKit::UnitTool::buttonCallback(int buttonSlotIndex,Vrui::InputDevice::ButtonCallbackData* cbData)
	{
	const Vrui::InputDevice* dev=getButtonDevice(buttonSlotIndex);
	
	if(cbData->newButtonState) // Button has just been pressed
		{
		switch(factory->toolType)
			{
			case UnitToolFactory::PICK:
			case UnitToolFactory::CREATE:
			case UnitToolFactory::COPY:
			case UnitToolFactory::DESTROY:
				{
				/* Execute a pick operation: */
				Point devPos=Vrui::getInverseNavigationTransformation().transform(dev->getPosition());
				Rotation devOrient=Vrui::getInverseNavigationTransformation().getRotation()*dev->getOrientation();
				pickId=application->sim->pick(devPos,Vrui::getPointPickDistance(),devOrient,factory->pickConnected);
				
				/* Execute special behavior: */
				switch(factory->toolType)
					{
					case UnitToolFactory::CREATE:
						{
						/* Execute a create operation in case the pick operation failed: */
						Vector devLinear=Vrui::getInverseNavigationTransformation().transform(dev->getLinearVelocity());
						Vector devAngular=Vrui::getInverseNavigationTransformation().getRotation().transform(dev->getAngularVelocity());
						application->sim->create(pickId,factory->createUnitType,devPos,devOrient,devLinear,devAngular);
						
						break;
						}
					
					case UnitToolFactory::COPY:
						/* Execute a copy operation: */
						application->sim->copy(pickId);
						
						break;
					
					case UnitToolFactory::DESTROY:
						/* Destroy the current pick: */
						application->sim->destroy(pickId);
						
						break;
					
					default:
						; // Do nothing
					}
				
				break;
				}
			
			case UnitToolFactory::PASTE:
				{
				/* Execute a paste operation: */
				Point devPos=Vrui::getInverseNavigationTransformation().transform(dev->getPosition());
				Rotation devOrient=Vrui::getInverseNavigationTransformation().getRotation()*dev->getOrientation();
				Vector devLinear=Vrui::getInverseNavigationTransformation().transform(dev->getLinearVelocity());
				Vector devAngular=Vrui::getInverseNavigationTransformation().getRotation().transform(dev->getAngularVelocity());
				pickId=application->sim->paste(devPos,devOrient,devLinear,devAngular);
				
				break;
				}
			}
		}
	else // Button has just been released
		{
		switch(factory->toolType)
			{
			case UnitToolFactory::PICK:
			case UnitToolFactory::PASTE:
			case UnitToolFactory::CREATE:
			case UnitToolFactory::COPY:
				{
				/* Check if the tool's device is inside the tool kill zone: */
				if(Vrui::getToolManager()->isDeviceInToolKillZone(dev))
					{
					/* Destroy the current pick: */
					application->sim->destroy(pickId);
					}
				}
			
			default:
				; // Do nothing
			}
		
		/* Finish the current operation: */
		application->sim->release(pickId);
		pickId=0;
		}
	}

void NewNanotechConstructionKit::UnitTool::frame(void)
	{
	/* Check if the tool has a current pick: */
	if(pickId!=0)
		{
		switch(factory->toolType)
			{
			case UnitToolFactory::PICK:
			case UnitToolFactory::PASTE:
			case UnitToolFactory::CREATE:
				{
				/* Update the picked unit's state: */
				const Vrui::InputDevice* dev=getButtonDevice(0);
				Point devPos=Vrui::getInverseNavigationTransformation().transform(dev->getPosition());
				Rotation devOrient=Vrui::getInverseNavigationTransformation().getRotation()*dev->getOrientation();
				Vector devLinear=Vrui::getInverseNavigationTransformation().transform(dev->getLinearVelocity());
				Vector devAngular=Vrui::getInverseNavigationTransformation().getRotation().transform(dev->getAngularVelocity());
				application->sim->setState(pickId,devPos,devOrient,devLinear,devAngular);
				
				break;
				}
			
			case UnitToolFactory::COPY:
			case UnitToolFactory::DESTROY:
				/* Do nothing */
				break;
			}
		}
	}

void NewNanotechConstructionKit::UnitTool::display(GLContextData& contextData) const
	{
	/* Enable the pick sphere renderer: */
	application->pickSphereRenderer.enable(1.0f,contextData);
	
	/* Draw the pick sphere: */
	glMaterial(GLMaterialEnums::FRONT,application->pickSphereMaterial);
	glBegin(GL_POINTS);
	glVertex(getButtonDevicePosition(0));
	glEnd();
	
	/* Disable the pick sphere renderer: */
	application->pickSphereRenderer.disable(contextData);
	}

/*****************************************************
Methods of class NewNanotechConstructionKit::DataItem:
*****************************************************/

NewNanotechConstructionKit::DataItem::DataItem(void)
	:vertexBufferId(0),meshStartIndices(0),
	 sessionId(0)
	{
	/* Initialize required OpenGL extensions: */
	GLARBVertexBufferObject::initExtension();
	
	/* Create a vertex buffer: */
	glGenBuffersARB(1,&vertexBufferId);
	}

NewNanotechConstructionKit::DataItem::~DataItem(void)
	{
	/* Destroy the vertex buffer: */
	glDeleteBuffersARB(1,&vertexBufferId);
	
	/* Delete the index array: */
	delete[] meshStartIndices;
	}

/*******************************************
Methods of class NewNanotechConstructionKit:
*******************************************/

void NewNanotechConstructionKit::createUnitCreationTools(void)
	{
	/* Embedded classes: */
	class SessionUnitSorter // Class to sort unit types in the current session by name
		{
		/* Elements: */
		private:
		const Misc::Vector<UnitType>& unitTypes; // List of current session unit types
		
		/* Constructors and destructors: */
		public:
		SessionUnitSorter(const Misc::Vector<UnitType>& sUnitTypes)
			:unitTypes(sUnitTypes)
			{
			}
		
		/* Methods: */
		bool operator()(const UnitTypeID& uti1,const UnitTypeID& uti2) const
			{
			return unitTypes[uti1].name<unitTypes[uti2].name;
			}
		};
	
	class CreateToolSorter // Class to sort unit creation tools by the name of the units they create
		{
		/* Methods: */
		public:
		bool operator()(const UnitToolFactory* utf1,const UnitToolFactory* utf2) const
			{
			return utf1->getUnitTypeName()<utf2->getUnitTypeName();
			}
		};
	
	/* Sort the list of structural unit types in the current session by name: */
	const Misc::Vector<UnitType>& unitTypes=sim->getUnitTypes();
	std::vector<UnitTypeID> sessionUnitTypes;
	for(size_t uti=0;uti<unitTypes.size();++uti)
		sessionUnitTypes.push_back(UnitTypeID(uti));
	std::sort(sessionUnitTypes.begin(),sessionUnitTypes.end(),SessionUnitSorter(unitTypes));
	
	/* Sort the list of unit creation tools by name of the unit they create: */
	std::sort(createToolClasses.begin(),createToolClasses.end(),CreateToolSorter());
	
	/* Create or delete unit creation tool classes to make the two lists match: */
	Vrui::ToolManager& tm=*Vrui::getToolManager();
	std::vector<UnitToolFactory*> newCreateToolClasses;
	std::vector<UnitTypeID>::iterator sutIt=sessionUnitTypes.begin();
	std::vector<UnitToolFactory*>::iterator ctcIt=createToolClasses.begin();
	while(sutIt!=sessionUnitTypes.end()||ctcIt!=createToolClasses.end())
		{
		if(sutIt!=sessionUnitTypes.end()&&(ctcIt==createToolClasses.end()||unitTypes[*sutIt].name<(*ctcIt)->getUnitTypeName()))
			{
			/* Create a new unit creation tool class: */
			const UnitType& ut=unitTypes[*sutIt];
			std::string className="Create";
			className.append(ut.name);
			className.append("Tool");
			std::string displayName="Create ";
			displayName.append(ut.name);
			displayName.append(" Unit");
			UnitToolFactory* factory=new UnitToolFactory(className.c_str(),displayName.c_str(),unitCreatorToolBase,*sutIt,ut.name,tm);
			
			/* Add the factory class to the creation tool list and to Vrui's tool manager: */
			newCreateToolClasses.push_back(factory);
			tm.addClass(factory,Vrui::ToolManager::defaultToolFactoryDestructor);
			
			++sutIt;
			}
		else if(ctcIt!=createToolClasses.end()&&(sutIt==sessionUnitTypes.end()||(*ctcIt)->getUnitTypeName()<unitTypes[*sutIt].name))
			{
			/* Destroy the unit creation tool class: */
			tm.releaseClass(*ctcIt);
			
			++ctcIt;
			}
		else
			{
			/* Keep the unit creation tool class but reset its unit type just in case: */
			(*ctcIt)->setCreateUnitType(*sutIt);
			newCreateToolClasses.push_back(*ctcIt);
			
			++sutIt;
			++ctcIt;
			}
		}
	
	/* Exchange the old and new unit creation tool class lists: */
	std::swap(createToolClasses,newCreateToolClasses);
	}

void NewNanotechConstructionKit::parametersUpdatedCallback(const SimulationInterface::Parameters& newParameters,void* userData)
	{
	NewNanotechConstructionKit* thisPtr=static_cast<NewNanotechConstructionKit*>(userData);
	
	/* Set the new parameters: */
	thisPtr->parameters=newParameters;
	
	/* Update the UI: */
	thisPtr->simulationDialog->updateVariables();
	}

bool NewNanotechConstructionKit::frontendSessionChangedCallback(void* userData)
	{
	/* Create the unit creation tool classes: */
	NewNanotechConstructionKit* thisPtr=static_cast<NewNanotechConstructionKit*>(userData);
	thisPtr->createUnitCreationTools();
	
	/* Reset the navigation transformation: */
	thisPtr->resetNavigation();
	
	/* Remove this callback again: */
	return true;
	}

void NewNanotechConstructionKit::sessionChangedCallback(SessionID newSessionId,void* userData)
	{
	NewNanotechConstructionKit* thisPtr=static_cast<NewNanotechConstructionKit*>(userData);
	
	/* Install a one-shot callback to be called in the front-end on the next frame: */
	Vrui::addFrameCallback(&NewNanotechConstructionKit::frontendSessionChangedCallback,thisPtr);
	}

void NewNanotechConstructionKit::newDataCallback(void* userData)
	{
	/* Request a new frame: */
	Vrui::requestUpdate();
	}

void* NewNanotechConstructionKit::simulationThreadMethod(void)
	{
	/* Access the local simulation object: */
	Simulation* localSim=static_cast<Simulation*>(sim);
	
	/* Run the simulation thread until interrupted: */
	Realtime::TimePointMonotonic timer;
	while(keepRunning)
		{
		/* Advance simulation to the current time: */
		Scalar deltaT(double(timer.setAndDiff()));
		localSim->advance(deltaT);
		
		/* Sleep until at least the minimum simulation interval has passed: */
		Realtime::TimePointMonotonic::sleep(timer+Realtime::TimeVector(0,1000000)); // 1ms minimum update interval
		}
	
	return 0;
	}

void NewNanotechConstructionKit::loadUnitFileCallback(GLMotif::FileSelectionDialog::OKCallbackData* cbData)
	{
	try
		{
		/* Open the file through a (cluster- and zip file-transparent) IO::Directory abstraction: */
		IO::FilePtr file=cbData->selectedDirectory->openFile(cbData->selectedFileName);
		
		/* Ask the simulation object to load the file: */
		file->setEndianness(Misc::LittleEndian);
		sim->loadState(*file);
		}
	catch(const std::runtime_error& err)
		{
		/* Show an error message: */
		Vrui::showErrorMessage("Load Unit File...",err.what());
		}
	}

void NewNanotechConstructionKit::saveUnitFileCompleteCallback(IO::File& file)
	{
	Misc::userNote("Save Unit File...: Unit file has been saved");
	}

void NewNanotechConstructionKit::saveUnitFileCallback(GLMotif::FileSelectionDialog::OKCallbackData* cbData)
	{
	try
		{
		/* Open the file through a (cluster- and zip file-transparent) IO::Directory abstraction: */
		IO::FilePtr file=cbData->selectedDirectory->openFile(cbData->selectedFileName,IO::File::WriteOnly);
		
		/* Ask the simulation object to save the file: */
		file->setEndianness(Misc::LittleEndian);
		sim->saveState(*file,Threads::createFunctionCall(this,&NewNanotechConstructionKit::saveUnitFileCompleteCallback));
		}
	catch(const std::runtime_error& err)
		{
		/* Show an error message: */
		Vrui::showErrorMessage("Save Unit File...",err.what());
		}
	}

void NewNanotechConstructionKit::showSimulationDialogCallback(Misc::CallbackData* cbData)
	{
	/* Show the dialog: */
	Vrui::popupPrimaryWidget(simulationDialog);
	}

void NewNanotechConstructionKit::createMainMenu(void)
	{
	/* Create the main menu shell: */
	mainMenu=new GLMotif::PopupMenu("MainMenu",Vrui::getWidgetManager());
	mainMenu->setTitle("Nanotech Construction Kit");
	
	/* Create buttons to load/save unit files: */
	GLMotif::Button* loadButton=new GLMotif::Button("LoadButton",mainMenu,"Load Unit File...");
	GLMotif::Button* saveButton=new GLMotif::Button("SaveButton",mainMenu,"Save Unit File...");
	
	/* Hook the file selection helper into the pair of buttons: */
	unitFileHelper.addLoadCallback(loadButton,Misc::createFunctionCall(this,&NewNanotechConstructionKit::loadUnitFileCallback));
	unitFileHelper.addSaveCallback(saveButton,Misc::createFunctionCall(this,&NewNanotechConstructionKit::saveUnitFileCallback));
	
	new GLMotif::Separator("Sep1",mainMenu,GLMotif::Separator::HORIZONTAL,0.0f,GLMotif::Separator::LOWERED);
	
	/* Create a button to show the simulation control window: */
	GLMotif::Button* showSimulationDialogButton=new GLMotif::Button("ShowSimulationDialogButton",mainMenu,"Show Simulation Dialog");
	showSimulationDialogButton->getSelectCallbacks().add(this,&NewNanotechConstructionKit::showSimulationDialogCallback);
	
	/* Finish the main menu: */
	mainMenu->manageMenu();
	}

void NewNanotechConstructionKit::parametersChangedCallback(Misc::CallbackData* cbData)
	{
	/* Set new simulation parameters: */
	sim->setParameters(parameters);
	}

void NewNanotechConstructionKit::createSimulationDialog(void)
	{
	const GLMotif::StyleSheet& ss=*Vrui::getUiStyleSheet();
	
	simulationDialog=new GLMotif::PopupWindow("SimulationDialog",Vrui::getWidgetManager(),"Simulation Dialog");
	simulationDialog->setResizableFlags(true,false);
	simulationDialog->setCloseButton(true);
	simulationDialog->popDownOnClose();
	
	GLMotif::RowColumn* settings=new GLMotif::RowColumn("Settings",simulationDialog,false);
	settings->setNumMinorWidgets(2);
	
	new GLMotif::Label("LinearDampeningLabel",settings,"Linear Damp");
	
	GLMotif::TextFieldSlider* linearDampeningSlider=new GLMotif::TextFieldSlider("LinearDampeningSlider",settings,6,ss.fontHeight*10.0f);
	linearDampeningSlider->getTextField()->setFloatFormat(GLMotif::TextField::FIXED);
	linearDampeningSlider->getTextField()->setFieldWidth(4);
	linearDampeningSlider->getTextField()->setPrecision(2);
	linearDampeningSlider->setSliderMapping(GLMotif::TextFieldSlider::LINEAR);
	linearDampeningSlider->setValueType(GLMotif::TextFieldSlider::FLOAT);
	linearDampeningSlider->setValueRange(0.0,10.0,0.01);
	linearDampeningSlider->track(parameters.linearDampening);
	linearDampeningSlider->getValueChangedCallbacks().add(this,&NewNanotechConstructionKit::parametersChangedCallback);
	
	new GLMotif::Label("AngularDampeningLabel",settings,"Angular Damp");
	
	GLMotif::TextFieldSlider* angularDampeningSlider=new GLMotif::TextFieldSlider("AngularDampeningSlider",settings,6,ss.fontHeight*10.0f);
	angularDampeningSlider->getTextField()->setFloatFormat(GLMotif::TextField::FIXED);
	angularDampeningSlider->getTextField()->setFieldWidth(4);
	angularDampeningSlider->getTextField()->setPrecision(2);
	angularDampeningSlider->setSliderMapping(GLMotif::TextFieldSlider::LINEAR);
	angularDampeningSlider->setValueType(GLMotif::TextFieldSlider::FLOAT);
	angularDampeningSlider->setValueRange(0.0,10.0,0.01);
	angularDampeningSlider->track(parameters.angularDampening);
	angularDampeningSlider->getValueChangedCallbacks().add(this,&NewNanotechConstructionKit::parametersChangedCallback);
	
	new GLMotif::Label("AttenuationLabel",settings,"Attenuation");
	
	GLMotif::TextFieldSlider* attenuationSlider=new GLMotif::TextFieldSlider("AttenuationSlider",settings,6,ss.fontHeight*10.0f);
	attenuationSlider->getTextField()->setFloatFormat(GLMotif::TextField::FIXED);
	attenuationSlider->getTextField()->setFieldWidth(4);
	attenuationSlider->getTextField()->setPrecision(2);
	attenuationSlider->setSliderMapping(GLMotif::TextFieldSlider::LINEAR);
	attenuationSlider->setValueType(GLMotif::TextFieldSlider::FLOAT);
	attenuationSlider->setValueRange(0.0,1.0,0.01);
	attenuationSlider->track(parameters.attenuation);
	attenuationSlider->getValueChangedCallbacks().add(this,&NewNanotechConstructionKit::parametersChangedCallback);
	
	new GLMotif::Label("TimeFactorLabel",settings,"Time Factor");
	
	GLMotif::TextFieldSlider* timeFactorSlider=new GLMotif::TextFieldSlider("TimeFactorSlider",settings,6,ss.fontHeight*10.0f);
	timeFactorSlider->getTextField()->setFloatFormat(GLMotif::TextField::FIXED);
	timeFactorSlider->getTextField()->setFieldWidth(4);
	timeFactorSlider->getTextField()->setPrecision(2);
	timeFactorSlider->setSliderMapping(GLMotif::TextFieldSlider::EXP10);
	timeFactorSlider->setValueType(GLMotif::TextFieldSlider::FLOAT);
	timeFactorSlider->setValueRange(0.01,100.0,0.05);
	timeFactorSlider->getSlider()->addNotch(Math::log10(1.0));
	timeFactorSlider->track(parameters.timeFactor);
	timeFactorSlider->getValueChangedCallbacks().add(this,&NewNanotechConstructionKit::parametersChangedCallback);
	
	settings->manageChild();
	}

NewNanotechConstructionKit::NewNanotechConstructionKit(int& argc,char**& argv)
	:Vrui::Application(argc,argv),
	 sim(0),forwarder(0),
	 keepRunning(true),
	 unitFileHelper(Vrui::getWidgetManager(),"UnitFile.units",".units"),
	 mainMenu(0),simulationDialog(0),
	 unitCreatorToolBase(0),
	 unitMaterial(GLMaterial::Color(0.7f,0.7f,0.7f),GLMaterial::Color(0.25f,0.25f,0.25f),16.0f),
	 pickSphereMaterial(GLMaterial::Color(1.0f,0.0f,0.0f),GLMaterial::Color(0.5f,0.5f,0.5f),32.0f)
	{
	/* Parse the command line: */
	const char* unitFileName=0;
	Box domain=Box(Point::origin,Point(100,100,100));
	for(int i=1;i<argc;++i)
		{
		if(argv[i][0]=='-')
			{
			if(strcasecmp(argv[i],"-domain")==0)
				{
				Point max;
				for(int j=0;j<3;++j)
					{
					++i;
					max[j]=atof(argv[i]);
					}
				domain=Box(Point::origin,max);
				}
			}
		else if(unitFileName==0)
			unitFileName=argv[i];
		}
	
	/* Check if there is a collaboration client: */
	Collab::Client* client=Collab::Client::getTheClient();
	if(client!=0)
		{
		/* Register an NCK client: */
		Collab::Plugins::NCKClient* nckClient=new Collab::Plugins::NCKClient(client,newDataCallback,0);
		client->addPluginProtocol(nckClient);
		sim=nckClient;
		}
	else if(Vrui::isHeadNode())
		{
		/* Open the main configuration file: */
		Misc::ConfigurationFile configFile(NCK_CONFIG_ETCDIR "/" NCK_CONFIG_CONFIGFILENAME);
		Misc::ConfigurationFileSection rootSection=configFile.getSection("NewNanotechConstructionKit");
		
		/* Create a local simulation structure: */
		Simulation* localSim=0;
		if(unitFileName!=0)
			{
			/* Load a previously saved simulation: */
			IO::FilePtr unitFile=IO::openFile(unitFileName);
			unitFile->setEndianness(Misc::LittleEndian);
			sim=localSim=new Simulation(rootSection,*unitFile);
			}
		else
			{
			/* Create an empty simulation structure: */
			sim=localSim=new Simulation(rootSection,domain);
			}
		
		/* Start the simulation thread: */
		simulationThread.start(this,&NewNanotechConstructionKit::simulationThreadMethod);
		
		/* Try opening a cluster pipe: */
		Cluster::MulticastPipe* clusterPipe=Vrui::openPipe();
		if(clusterPipe!=0)
			{
			/* Create a cluster forwarder: */
			forwarder=new ClusterForwarder(localSim,clusterPipe,1.0/60.0);
			}
		}
	else
		{
		/* Create a cluster simulation proxy: */
		ClusterSlaveSimulation* slaveSim=0;
		sim=slaveSim=new ClusterSlaveSimulation(Vrui::openPipe());
		}
	
	/* Register parameter update and session changed callbacks: */
	sim->setParametersChangedCallback(&NewNanotechConstructionKit::parametersUpdatedCallback,this);
	sim->setSessionChangedCallback(&NewNanotechConstructionKit::sessionChangedCallback,this);
	
	/* Create application tool classes: */
	Vrui::ToolManager& tm=*Vrui::getToolManager();
	
	/* Create a base class for Nanotech Construction Kit tools: */
	Vrui::ToolFactory* toolBase=new Vrui::GenericAbstractToolFactory<UnitTool>("NCKTool","Nanotech Construction Kit",0,tm);
	tm.addAbstractClass(toolBase,Vrui::ToolManager::defaultToolFactoryDestructor);
	
	/* Create a base class for unit-creating Nanotech Construction Kit tools: */
	unitCreatorToolBase=new Vrui::GenericAbstractToolFactory<UnitTool>("NCKUnitCreatorTool","Create Units",toolBase,tm);
	tm.addAbstractClass(unitCreatorToolBase,Vrui::ToolManager::defaultToolFactoryDestructor);
	
	tm.addClass(new UnitToolFactory("PickUnitTool","Pick Unit",toolBase,UnitToolFactory::PICK,false,tm),Vrui::ToolManager::defaultToolFactoryDestructor);
	tm.addClass(new UnitToolFactory("PickComplexTool","Pick Complex",toolBase,UnitToolFactory::PICK,true,tm),Vrui::ToolManager::defaultToolFactoryDestructor);
	tm.addClass(new UnitToolFactory("PasteUnitTool","Paste Copied Units",toolBase,UnitToolFactory::PASTE,false,tm),Vrui::ToolManager::defaultToolFactoryDestructor);
	tm.addClass(new UnitToolFactory("CopyUnitTool","Copy Unit",toolBase,UnitToolFactory::COPY,false,tm),Vrui::ToolManager::defaultToolFactoryDestructor);
	tm.addClass(new UnitToolFactory("CopyComplexTool","Copy Complex",toolBase,UnitToolFactory::COPY,true,tm),Vrui::ToolManager::defaultToolFactoryDestructor);
	tm.addClass(new UnitToolFactory("DestroyUnitTool","Destroy Unit",toolBase,UnitToolFactory::DESTROY,false,tm),Vrui::ToolManager::defaultToolFactoryDestructor);
	tm.addClass(new UnitToolFactory("DestroyComplexTool","Destroy Complex",toolBase,UnitToolFactory::DESTROY,true,tm),Vrui::ToolManager::defaultToolFactoryDestructor);
	
	if(client==0)
		{
		/* Create the unit creation tool classes: */
		createUnitCreationTools();
		}
	
	/* Create the main menu: */
	createMainMenu();
	Vrui::setMainMenu(mainMenu);
	
	/* Retrieve current simulation parameters and create the simulation control dialog: */
	parameters=sim->getParameters();
	createSimulationDialog();
	
	/* Tell Vrui that navigation space is measured in Angstrom: */
	Vrui::getCoordinateManager()->setUnit(Geometry::LinearUnit(Geometry::LinearUnit::ANGSTROM,1.0));
	
	/* Initialize the pick sphere renderer: */
	pickSphereRenderer.setFixedRadius(Vrui::getPointPickDistance()*Vrui::getNavigationTransformation().getScaling());
	}

NewNanotechConstructionKit::~NewNanotechConstructionKit(void)
	{
	/* Check if this is a local simulation: */
	Simulation* localSim=dynamic_cast<Simulation*>(sim);
	if(localSim!=0)
		{
		/* Shut down the simulation thread: */
		keepRunning=false;
		simulationThread.join();
		}
	
	/* Delete a potential cluster forwarder: */
	delete forwarder;
	
	/* Check if this is a remote simulation: */
	Collab::Plugins::NCKClient* nckClient=dynamic_cast<Collab::Plugins::NCKClient*>(sim);
	if(nckClient==0)
		{
		/* Delete the simulation interface: */
		delete sim;
		}
	
	/* Clean up: */
	delete mainMenu;
	delete simulationDialog;
	}

void NewNanotechConstructionKit::frame(void)
	{
	/* Lock the most recent simulation state: */
	sim->lockNewState();
	
	/* Request another frame: */
	Vrui::scheduleUpdate(Vrui::getNextAnimationTime());
	}

namespace {

/****************
Helper functions:
****************/

template <class UnitStateParam>
inline
void renderUnits(
	const StateArray<UnitStateParam>& unitStates,
	const GLuint* meshStartIndices)
	{
	for(typename StateArray<UnitStateParam>::UnitStateList::const_iterator sIt=unitStates.states.begin();sIt!=unitStates.states.end();++sIt)
		{
		/* Go to the unit's local coordinate system: */
		glPushMatrix();
		glTranslate(sIt->position[0],sIt->position[1],sIt->position[2]);
		glRotate(sIt->orientation);
		
		/* Draw the unit: */
		glDrawArrays(GL_TRIANGLES,meshStartIndices[sIt->unitType],meshStartIndices[sIt->unitType+1]-meshStartIndices[sIt->unitType]);
		
		/* Go back to navigational coordinates: */
		glPopMatrix();
		}
	}

}

void NewNanotechConstructionKit::display(GLContextData& contextData) const
	{
	/* Bail out if the session is invalid: */
	if(!sim->isSessionValid())
		return;
	
	/* Set up OpenGL state: */
	glPushAttrib(GL_ENABLE_BIT|GL_LINE_BIT|GL_POINT_BIT);
	glDisable(GL_LIGHTING);
	glPointSize(3.0f);
	
	/* Draw the domain box: */
	const Box& domain=sim->getDomain();
	glLineWidth(3.0f);
	glColor(Vrui::getForegroundColor());
	glBegin(GL_LINE_STRIP);
	glVertex(domain.min[0],domain.min[1],domain.min[2]);
	glVertex(domain.max[0],domain.min[1],domain.min[2]);
	glVertex(domain.max[0],domain.max[1],domain.min[2]);
	glVertex(domain.min[0],domain.max[1],domain.min[2]);
	glVertex(domain.min[0],domain.min[1],domain.min[2]);
	glVertex(domain.min[0],domain.min[1],domain.max[2]);
	glVertex(domain.max[0],domain.min[1],domain.max[2]);
	glVertex(domain.max[0],domain.max[1],domain.max[2]);
	glVertex(domain.min[0],domain.max[1],domain.max[2]);
	glVertex(domain.min[0],domain.min[1],domain.max[2]);
	glEnd();
	glBegin(GL_LINES);
	glVertex(domain.max[0],domain.min[1],domain.min[2]);
	glVertex(domain.max[0],domain.min[1],domain.max[2]);
	glVertex(domain.max[0],domain.max[1],domain.min[2]);
	glVertex(domain.max[0],domain.max[1],domain.max[2]);
	glVertex(domain.min[0],domain.max[1],domain.min[2]);
	glVertex(domain.min[0],domain.max[1],domain.max[2]);
	glEnd();
	
	/* Check if the locked unit state array is valid: */
	if(sim->isLockedStateValid())
		{
		/* Retrieve the data item: */
		DataItem* dataItem=contextData.retrieveDataItem<DataItem>(this);
		
		/* Bind the unit type mesh vertex buffer: */
		glBindBufferARB(GL_ARRAY_BUFFER_ARB,dataItem->vertexBufferId);
		
		/* Check if the mesh buffer is outdated: */
		if(dataItem->sessionId!=sim->getSessionId())
			{
			/* Count the total number of vertices needed and initialize the mesh start index array: */
			const UnitTypeList& unitTypes=sim->getUnitTypes();
			Size numTypes=unitTypes.size();
			dataItem->meshStartIndices=new GLuint[numTypes+1]; // One extra to calculate length of last mesh
			dataItem->meshStartIndices[0]=0;
			GLuint numVertices=0;
			for(Index uti=0;uti<numTypes;++uti)
				{
				numVertices+=unitTypes[uti].meshTriangles.size();
				dataItem->meshStartIndices[uti+1]=numVertices;
				}
			
			/* Create all triangle meshes: */
			glBufferDataARB(GL_ARRAY_BUFFER_ARB,numVertices*sizeof(Vertex),0,GL_STATIC_DRAW_ARB);
			Vertex* vPtr=static_cast<Vertex*>(glMapBufferARB(GL_ARRAY_BUFFER_ARB,GL_WRITE_ONLY_ARB));
			for(Index uti=0;uti<numTypes;++uti)
				{
				/* Get the unit type: */
				const UnitType& ut=unitTypes[uti];
				
				/* Create triangles: */
				Size numIndices=ut.meshTriangles.size();
				for(Index i=0;i+2<numIndices;i+=3,vPtr+=3)
					{
					/* Get the triangle's points: */
					Point p0=ut.meshVertices[ut.meshTriangles[i]];
					Point p1=ut.meshVertices[ut.meshTriangles[i+1]];
					Point p2=ut.meshVertices[ut.meshTriangles[i+2]];
					
					/* Calculate the triangle's normal vector: */
					Vector normal=(p1-p0)^(p2-p0);
					normal.normalize();
					
					/* Store the triangle: */
					vPtr[0].normal=normal;
					vPtr[0].position=p0;
					vPtr[1].normal=normal;
					vPtr[1].position=p1;
					vPtr[2].normal=normal;
					vPtr[2].position=p2;
					}
				}
			glUnmapBufferARB(GL_ARRAY_BUFFER_ARB);
			
			/* Mark the mesh buffer as up-to-date: */
			dataItem->sessionId=sim->getSessionId();
			}
		
		/* Set up triangle mesh rendering: */
		glEnable(GL_LIGHTING);
		glMaterial(GLMaterialEnums::FRONT,unitMaterial);
		
		/* Set up vertex array rendering: */
		GLVertexArrayParts::enable(Vertex::getPartsMask());
		glVertexPointer(static_cast<Vertex*>(0));
		
		/* Retrieve current unit states based on whether the simulation is local, forwarded local, or remote: */
		if(forwarder!=0)
			renderUnits(forwarder->getLockedState(),dataItem->meshStartIndices);
		else
			{
			Simulation* localSim=dynamic_cast<Simulation*>(sim);
			if(localSim!=0)
				renderUnits(localSim->getLockedState(),dataItem->meshStartIndices);
			else
				{
				IndirectSimulationInterface* indirectSim=static_cast<IndirectSimulationInterface*>(sim);
				renderUnits(indirectSim->getLockedState(),dataItem->meshStartIndices);
				}
			}
		
		/* Disable vertex array rendering: */
		GLVertexArrayParts::disable(Vertex::getPartsMask());
		
		/* Protect the unit type mesh vertex buffer: */
		glBindBufferARB(GL_ARRAY_BUFFER_ARB,0);
		}
	
	/* Restore OpenGL state: */
	glPopAttrib();
	}

void NewNanotechConstructionKit::resetNavigation(void)
	{
	/* Check if the session is valid: */
	if(sim->isSessionValid())
		{
		/* Show the entire domain: */
		const Box& domain=sim->getDomain();
		Point center=Geometry::mid(domain.min,domain.max);
		Scalar size=Geometry::dist(domain.min,domain.max);
		Vrui::setNavigationTransformation(center,size);
		}
	}

void NewNanotechConstructionKit::initContext(GLContextData& contextData) const
	{
	/* Create a context data item: */
	DataItem* dataItem=new DataItem;
	contextData.addDataItem(this,dataItem);
	}

VRUI_APPLICATION_RUN(NewNanotechConstructionKit)
