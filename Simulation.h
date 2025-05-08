/***********************************************************************
Simulation - Class encapsulating the simulation of the interactions of a
set of structural units, and user interactions upon those units.
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

#ifndef SIMULATION_INCLUDED
#define SIMULATION_INCLUDED

#include <stddef.h>
#include <vector>
#include <Misc/Autopointer.h>
#include <Misc/HashTable.h>
#include <Threads/Spinlock.h>
#include <Threads/TripleBuffer.h>
#include <IO/File.h>

#include "Common.h"
#include "SimulationInterface.h"

/* Forward declarations: */
namespace Misc {
class ConfigurationFileSection;
}

class Simulation:public SimulationInterface
	{
	/* Embedded classes: */
	private:
	struct Bond // Structure to represent bonds between structural units' bonding sites
		{
		/* Elements: */
		public:
		Index unitIndex; // Index of unit in current state array
		Index bondSiteIndex; // Index of linked bonding site
		
		/* Constructors and destructors: */
		Bond(void)
			{
			}
		Bond(Index sUnitIndex,Index sBondSiteIndex)
			:unitIndex(sUnitIndex),bondSiteIndex(sBondSiteIndex)
			{
			}
		
		/* Methods: */
		bool operator!=(const Bond& other) const
			{
			return unitIndex!=other.unitIndex||bondSiteIndex!=other.bondSiteIndex;
			}
		static size_t hash(const Bond& source,size_t tableSize)
			{
			return (((size_t(source.unitIndex)<<16)+size_t(source.bondSiteIndex))<<16)%tableSize;
			}
		};
	
	typedef Misc::HashTable<Bond,Bond> BondMap; // Hash table mapping bonds between structural units
	
	class Grid // Structure for grids accelerating unit interaction computations
		{
		/* Embedded classes: */
		public:
		struct Cell // Structure representing a single grid cell
			{
			/* Elements: */
			public:
			Cell* neighbors[27]; // Pointers to the grid cell's 26 neighbors and itself
			std::vector<Index> unitIndices; // List of indices of units in this grid cell
			};
		
		/* Elements: */
		private:
		Size numCells[3]; // Number of grid cells in the acceleration grid
		Scalar cellSize[3]; // Size of an acceleration grid cell
		Scalar origin[3]; // Position of the grid's origin in model space
		Cell* cells; // 3D array of grid cells
		Size unitCellIndexSize; // Allocated size of current unit grid cell index array
		Index* unitCellIndices; // Array holding the index of the grid cell containing each current unit
		
		/* Constructors and destructors: */
		public:
		Grid(void); // Creates an uninitialized grid
		~Grid(void); // Destroys the grid
		
		/* Methods: */
		void create(const Box& domain,const UnitTypeList& unitTypes,Scalar centralForceOvershoot,Scalar vertexForceRadius); // Creates an empty grid for the given domain, unit types, and simulation parameters
		void reserve(Size numUnits); // Makes enough room in the unit cell index array to hold the given number of units
		const Size* getNumCells(void) const // Returns the number of cells in the grid
			{
			return numCells;
			}
		const Scalar* getCellSize(void) const // Returns the size of a grid cell
			{
			return cellSize;
			}
		void calcCellIndex(const Point& position,Index cellIndex[3]) // Returns the triple index of the grid cell containing the given position
			{
			for(int i=0;i<3;++i)
				cellIndex[i]=Index((position[i]-origin[2])/cellSize[2]);
			}
		Index calcCellIndex(const Point& position) const // Returns the linear index of the grid cell containing the given position
			{
			return (Index((position[2]-origin[2])/cellSize[2])*numCells[1]+Index((position[1]-origin[1])/cellSize[1]))*numCells[0]+Index((position[0]-origin[0])/cellSize[0]);
			}
		const Cell& getCell(const Index cellIndex[3]) const // Returns the grid cell of the given index
			{
			return cells[(cellIndex[2]*numCells[1]+cellIndex[1])*numCells[0]+cellIndex[0]];
			}
		const Cell& getWrappedCell(const int cellIndex[3]) const // Returns the grid cell of the given index wrapped to the grid's size
			{
			int wrappedIndex[3];
			for(int i=0;i<3;++i)
				{
				wrappedIndex[i]=cellIndex[i]%int(numCells[i]);
				if(wrappedIndex[i]<0)
					wrappedIndex[i]+=int(numCells[i]);
				}
			return cells[(wrappedIndex[2]*numCells[1]+wrappedIndex[1])*numCells[0]+wrappedIndex[0]];
			}
		const Cell& getCell(const Point& position) const // Returns the grid cell containing the given position
			{
			return cells[calcCellIndex(position)];
			}
		Cell& getCell(const Point& position) // Ditto
			{
			return cells[calcCellIndex(position)];
			}
		const Cell& getCell(Index unitIndex) const // Returns the grid cell containing the given unit
			{
			return cells[unitCellIndices[unitIndex]];
			}
		Cell& getCell(Index unitIndex) // Ditto
			{
			return cells[unitCellIndices[unitIndex]];
			}
		void insertUnit(Index unitIndex,const UnitState& unit); // Adds a new unit to the grid
		void moveUnit(Index unitIndex,const UnitState& unit); // Updates the grid to reflect movement of the given unit
		void removeUnit(Index unitIndex); // Removes the given unit from the grid without filling the remaining hole in the cell index array
		void changeUnitIndex(Index currentIndex,Index newIndex); // Changes the index assigned to a unit
		void moveUnits(Size numUnits,const UnitState* unitStates); // Updates the grid to reflect movement of the given array of units
		void check(Size numUnits,const UnitState* unitStates) const; // Checks the grid for consistency
		};
	
	struct UIRequest; // Structure to communicate requests from the UI front-end to the simulation back-end
	
	struct PickRecord // Structure keeping track of currently picked units
		{
		/* Elements: */
		public:
		Index unitIndex; // Index of picked unit in current state array
		Vector positionOffset; // Offset from pick position to unit's initial center of gravity
		Rotation orientationOffset; // Offset from pick orientation to unit's initial orientation
		};
	
	typedef std::vector<PickRecord> PickRecordList; // List of pick records assigned to the same pick ID
	typedef Misc::HashTable<PickID,PickRecordList> PickRecordMap; // Hash table mapping pick IDs to pick records
	
	struct CopiedUnitState // Structure representing structural units in a copy buffer
		{
		/* Elements: */
		public:
		UnitTypeID unitType; // Type of this unit
		Vector positionOffset; // Offset from pick position to unit's copied center of gravity
		Rotation orientationOffset; // Offset from pick orientation to unit's copied orientation
		};
	
	/* Elements: */
	
	/* Simulation parameters: */
	Scalar vertexForceRadius,vertexForceRadius2; // Radius and squared radius of vertex force field
	Scalar vertexForceStrength; // Strength of vertex attraction force
	Scalar centralForceOvershoot; // Factor of how much centroid repelling force overshoots units' radii
	Scalar centralForceStrength; // Strength of centroid repelling force
	Threads::TripleBuffer<Parameters> parameters; // Triple buffer of user-changeable simulation parameters
	Parameters* mostRecentParameters; // Pointer to most recent version of simulation parameters
	
	/* Current simulation state: */
	Threads::TripleBuffer<UnitStateArray> unitStates; // Triple buffer of arrays of current unit states
	const UnitStateArray* mostRecentStates; // Unit state array most recently written into
	Grid grid; // Grid to accelerate computation of interaction forces between units
	BondMap bonds; // Map of current bonds between structural units
	
	/* Temporary storage for simulation state integration: */
	Size forceArraySize; // Size of the currently allocated force and torque arrays
	Vector* forces; // Array of forces acting on units
	Vector* torques; // Array of torques acting on units
	
	/* UI state: */
	SessionID loadSessionId; // Session ID associated with the most recent load state or initialization request
	PickID lastPickId; // Most recent ID assigned to a pick record
	Threads::Spinlock uiRequestMutex; // Mutex serializing access to the list of UI requests
	std::vector<UIRequest> uiRequests; // List of pending UI requests
	PickRecordMap pickRecords; // Map of current pick records
	std::vector<CopiedUnitState> copiedUnits; // List of units in the current copy buffer
	std::vector<std::pair<Bond,Bond> > copiedBonds; // List of bonds between units in the current copy buffer
	
	/* Private methods: */
	Vector wrapDistance(const Vector& distance) const; // Returns wrapped distance vector
	Point wrapPosition(const Point& position) const; // Wraps the given position to the simulation domain
	PickID getPickId(void); // Returns a new and currently unused pick ID
	void unpickUnit(PickID pickId,Index unitIndex); // Removes a picked unit from its current pick list
	void pickUnits(UnitState* unitStates,Index unitIndex,const Point& pickPosition,const Rotation& pickOrientation,bool pickConnected,PickRecordMap::Entry& pickRecord); // Creates a pick record entry for the given unit, and optionally all units connected to it
	void calcForces(Size numUnits,const UnitState* states,Vector* forces,Vector* torques) const; // Calculates forces and torques on all structural units based on current state
	void applyForces(Size numUnits,const UnitState* source,UnitState* dest,Vector* forces,Vector* torques,Scalar dt); // Applies forces and torques to transfer source states into destination states
	void updateBonds(Size numUnits,const UnitState* states); // Creates and breaks bonds between structural units based on given state array
	void save(UnitStateArray& states,IO::File& file) const; // Saves the given simulation state to the given file
	void load(IO::File& file,UnitStateArray& states); // Loads the given file into the given simulation state
	
	/* Constructors and destructors: */
	public:
	Simulation(const Misc::ConfigurationFileSection& configFileSection,const Box& sDomain); // Creates an empty simulation with unit types read from the given file, and the given simulation domain
	Simulation(const Misc::ConfigurationFileSection& configFileSection,IO::File& file); // Loads a previously saved simulation from the given binary file
	virtual ~Simulation(void);
	
	/* Methods from class SimulationInterface: */
	virtual bool isSessionValid(void) const;
	virtual const Parameters& getParameters(void) const;
	virtual void setParameters(const Parameters& newParameters);
	virtual bool lockNewState(void);
	virtual bool isLockedStateValid(void) const;
	virtual PickID pick(const Point& pickPosition,Scalar pickRadius,const Rotation& pickOrientation,bool pickConnected);
	virtual PickID pick(const Point& pickPosition,const Vector& pickDirection,const Rotation& pickOrientation,bool pickConnected);
	virtual PickID paste(const Point& newPosition,const Rotation& newOrientation,const Vector& newLinearVelocity,const Vector& newAngularVelocity);
	virtual void create(PickID pickId,UnitTypeID newTypeId,const Point& newPosition,const Rotation& newOrientation,const Vector& newLinearVelocity,const Vector& newAngularVelocity);
	virtual void setState(PickID pickId,const Point& newPosition,const Rotation& newOrientation,const Vector& newLinearVelocity,const Vector& newAngularVelocity);
	virtual void copy(PickID pickId);
	virtual void destroy(PickID pickId);
	virtual void release(PickID pickId);
	virtual void loadState(IO::File& stateFile);
	virtual void saveState(IO::File& stateFile,SaveStateCompleteCallback* completeCallback =0);
	
	/* New methods: */
	void advance(Scalar timeStep); // Advances simulation state by the given real-time time step
	const UnitStateArray& getLockedState(void) const // Returns the currently locked simulation state
		{
		return unitStates.getLockedValue();
		}
	};

#endif
