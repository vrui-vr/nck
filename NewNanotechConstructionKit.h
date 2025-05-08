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

#include <string>
#include <vector>
#include <Threads/Thread.h>
#include <Threads/TripleBuffer.h>
#include <GL/gl.h>
#include <GL/GLMaterial.h>
#include <GL/GLObject.h>
#include <GL/GLSphereRenderer.h>
#include <GL/GLGeometryWrappers.h>
#include <GL/GLGeometryVertex.h>
#include <GLMotif/ToggleButton.h>
#include <GLMotif/TextFieldSlider.h>
#include <GLMotif/FileSelectionDialog.h>
#include <GLMotif/FileSelectionHelper.h>
#include <Vrui/Tool.h>
#include <Vrui/Application.h>

#include "Common.h"
#include "SimulationInterface.h"

/* Forward declarations: */
namespace Cluster {
class MulticastPipe;
}
namespace GLMotif {
class PopupMenu;
class PopupWindow;
}
class SimulationInterface;
class Simulation;

class NewNanotechConstructionKit:public Vrui::Application,public GLObject
	{
	/* Embedded classes: */
	private:
	class ClusterForwarder // Helper class forwarding unit states from a local simulation to a cluster
		{
		/* Elements: */
		private:
		Simulation* sim; // Simulation whose unit states are to be forwarded
		Cluster::MulticastPipe* clusterPipe; // Pipe connected to the cluster's slave nodes
		double distributionInterval; // Interval between state updates in a cluster in seconds
		volatile bool keepRunning; // Flag to keep the communication thread running
		Threads::Thread communicationThread; // Thread forwarding unit states to the slave nodes
		Threads::TripleBuffer<ReducedUnitStateArray> unitStates; // Triple buffer of structural unit states reduced from the simulation
		
		/* Private methods: */
		void* communicationThreadMethod(void); // Method implementing the communication thread
		
		/* Constructors and destructors: */
		public:
		ClusterForwarder(Simulation* sSim,Cluster::MulticastPipe* sClusterPipe,double sDistributionInterval);
		~ClusterForwarder(void);
		
		/* Methods: */
		void updateSession(void); // Sends a session update to the cluster slaves
		void updateParameters(void); // Sends a parameter update to the cluster slaves
		bool lockNewState(void) // Locks newest state array and returns true if states changed
			{
			return unitStates.lockNewValue();
			}
		const ReducedUnitStateArray& getLockedState(void) const // Returns the current state array
			{
			return unitStates.getLockedValue();
			}
		};
	
	class UnitTool;
	
	class UnitToolFactory:public Vrui::ToolFactory // Factory class used for all unit interactions
		{
		friend class UnitTool;
		
		/* Embedded classes: */
		public:
		enum ToolType // Enumerated type for tool types
			{
			PICK,PASTE,CREATE,COPY,DESTROY
			};
		
		/* Elements: */
		private:
		std::string displayName; // Display name for tools of this class
		ToolType toolType; // Type of tools created by this factory
		bool pickConnected; // Flag whether pick tools of this class pick complexes of connected units
		UnitTypeID createUnitType; // Type of units created by create tools
		std::string createUnitTypeName; // Name of the unit type created by create tools
		
		/* Constructors and destructors: */
		public:
		UnitToolFactory(const char* sClassName,const char* sDisplayName,Vrui::ToolFactory* parentClass,ToolType sToolType,bool sPickConnected,Vrui::ToolManager& toolManager); // Creates a non-creating tool class
		UnitToolFactory(const char* sClassName,const char* sDisplayName,Vrui::ToolFactory* parentClass,UnitTypeID sCreateUnitType,const std::string& sCreateUnitTypeName,Vrui::ToolManager& toolManager); // Creates a unit-creating tool class
		
		/* Methods from class Vrui::ToolFactory: */
		virtual const char* getName(void) const;
		virtual const char* getButtonFunction(int buttonSlotIndex) const;
		virtual Vrui::Tool* createTool(const Vrui::ToolInputAssignment& inputAssignment) const;
		virtual void destroyTool(Vrui::Tool* tool) const;
		
		/* New methods: */
		const std::string& getUnitTypeName(void) const // Returns the name of the unit type created by creation tools
			{
			return createUnitTypeName;
			}
		void setCreateUnitType(UnitTypeID newCreateUnitType); // Sets the type of units created by create tools
		};
	
	class UnitTool:public Vrui::Tool,public Vrui::Application::Tool<NewNanotechConstructionKit>
		{
		friend class UnitToolFactory;
		
		/* Elements: */
		private:
		const UnitToolFactory* factory; // Pointer to the factory object that created this tool object
		PickID pickId; // Pick ID associated with this tool while the tool button is pressed
		
		/* Constructors and destructors: */
		public:
		UnitTool(const UnitToolFactory* sFactory,const Vrui::ToolInputAssignment& inputAssignment);
		
		/* Methods from class Vrui::Tool: */
		virtual const Vrui::ToolFactory* getFactory(void) const;
		virtual void buttonCallback(int buttonSlotIndex,Vrui::InputDevice::ButtonCallbackData* cbData);
		virtual void frame(void);
		virtual void display(GLContextData& contextData) const;
		};
	
	typedef GLGeometry::Vertex<void,0,void,0,GLfloat,GLfloat,3> Vertex; // Type to render unit type triangle meshes
	
	struct DataItem:public GLObject::DataItem // Structure to store per-OpenGL context state
		{
		/* Elements: */
		public:
		GLuint vertexBufferId; // ID of vertex buffer object holding all unit type meshes
		GLuint* meshStartIndices; // Array of indices at which each unit type's mesh starts
		SessionID sessionId; // ID of the session for which the unit type meshes were generated
		
		/* Constructors and destructors: */
		DataItem(void);
		virtual ~DataItem(void);
		};
	
	/* Elements: */
	SimulationInterface* sim; // Pointer to a local or remote simulation interface
	SimulationInterface::Parameters parameters; // Local copy of current simulation parameters
	ClusterForwarder* forwarder; // Pointer to simulation state forwarder on a cluster's master node
	Threads::Thread simulationThread; // Thread to run the simulation in the background
	volatile bool keepRunning; // Flag to keep the simulation thread running
	GLMotif::FileSelectionHelper unitFileHelper; // Helper object to load/save unit files
	GLMotif::PopupMenu* mainMenu; // Program's main menu
	GLMotif::PopupWindow* simulationDialog; // Dialog window to control simulation parameters
	Vrui::ToolFactory* unitCreatorToolBase; // Base tool class for unit-creating tools
	std::vector<UnitToolFactory*> createToolClasses; // List of existing tool classes that create structural units
	GLMaterial unitMaterial; // OpenGL material properties to render units
	GLSphereRenderer pickSphereRenderer; // Renderer for unit tools' picking spheres
	GLMaterial pickSphereMaterial; // OpenGL material properties to render tools' picking spheres
	
	/* Private methods: */
	void createUnitCreationTools(void); // Creates the set of tool classes to create structural unit types in the current simulation session
	static void parametersUpdatedCallback(const SimulationInterface::Parameters& newParameters,void* userData); // Callback called when new simulation parameters arrive from a remote server
	static bool frontendSessionChangedCallback(void* userData); // Callback called in the front-end after the simulation session is updated by a remote server
	static void sessionChangedCallback(SessionID newSessionId,void* userData); // Callback called after the simulation session is updated by a remote server
	static void newDataCallback(void* userData); // Callback called when new simulation data arrives from a remote server
	void* simulationThreadMethod(void); // Method running the background simulation thread
	void loadUnitFileCallback(GLMotif::FileSelectionDialog::OKCallbackData* cbData);
	void saveUnitFileCompleteCallback(IO::File& file);
	void saveUnitFileCallback(GLMotif::FileSelectionDialog::OKCallbackData* cbData);
	void showSimulationDialogCallback(Misc::CallbackData* cbData);
	void createMainMenu(void);
	void parametersChangedCallback(Misc::CallbackData* cbData);
	void createSimulationDialog(void);
	
	/* Constructors and destructors: */
	public:
	NewNanotechConstructionKit(int& argc,char**& argv);
	virtual ~NewNanotechConstructionKit(void);
	
	/* Methods from Vrui::Application: */
	virtual void frame(void);
	virtual void display(GLContextData& contextData) const;
	virtual void resetNavigation(void);
	
	/* Methods from GLObject: */
	virtual void initContext(GLContextData& contextData) const;
	};
