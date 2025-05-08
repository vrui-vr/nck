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

#include "Simulation.h"

#include <string.h>
#include <utility>
#include <algorithm>
#include <stdexcept>
#include <Misc/SizedTypes.h>
#include <Misc/MessageLogger.h>
#include <Misc/OneTimeQueue.h>
#include <Misc/Marshaller.h>
#include <Misc/StandardValueCoders.h>
#include <Misc/ConfigurationFile.h>
#include <Misc/CompoundValueCoders.h>
#include <IO/File.h>
#include <Math/Math.h>
#include <Geometry/GeometryValueCoders.h>

#include "IO.h"

// DEBUGGING
#include <assert.h>
#include <iostream>

/*******************************************
Declaration of struct Simulation::UIRequest:
*******************************************/

struct Simulation::UIRequest
	{
	/* Embedded classes: */
	public:
	enum RequestType // Enumerated type for types of requests
		{
		PICK_POS,PICK_RAY,PASTE,CREATE,SET_STATE,COPY,DESTROY,RELEASE,SAVE_STATE,LOAD_STATE,NUM_REQUESTTYPES
		};
	
	/* Elements: */
	RequestType requestType; // Type of this request
	PickID pickId; // Pick ID associated with this request
	Point pickPos; // Position at which to pick
	Scalar pickRadius; // Radius for point pick requests
	Vector pickDir; // Direction along which to pick
	bool pickConnected; // Flag whether pick operations pick connected complexes
	UnitTypeID createTypeId; // Type of unit to create
	Point setPosition; // Position to set
	Rotation setOrientation; // Orientation to set
	Vector setLinearVelocity; // Linear velocity to set
	Vector setAngularVelocity; // Angular velocity to set
	IO::FilePtr file; // Pointer to the file from/to which to load/save state
	Misc::Autopointer<SaveStateCompleteCallback> saveCompleteCallback; // Pointer to callback to call when a save operation is complete
	SessionID loadSessionId; // Session ID associated with a load state request
	
	/* Constructors and destructors: */
	UIRequest(void) // Dummy constructor to avoid a ton of compiler warnings
		:requestType(NUM_REQUESTTYPES),
		 pickId(0),
		 pickPos(Point::origin),pickRadius(0),pickDir(Vector::zero),pickConnected(false),
		 createTypeId(0),
		 setPosition(Point::origin),setLinearVelocity(Vector::zero),setAngularVelocity(Vector::zero),
		 loadSessionId(0)
		{
		}
	};

/****************
Helper functions:
****************/

template <class ScalarParam>
ScalarParam increment(ScalarParam value); // Increments the given positive value to the next bigger representable value

template <>
Misc::Float32 increment(Misc::Float32 value)
	{
	/* Helper union: */
	union
		{
		/* Elements: */
		public:
		Misc::Float32 f;
		Misc::SInt32 i;
		} incrementer;
	
	/* Assign the value as float, increment as integer, and return as float: */
	incrementer.f=value;
	++incrementer.i;
	return incrementer.f;
	}

template <>
Misc::Float64 increment(Misc::Float64 value)
	{
	/* Helper union: */
	union
		{
		/* Elements: */
		public:
		Misc::Float64 f;
		Misc::SInt64 i;
		} incrementer;
	
	/* Assign the value as float, increment as integer, and return as float: */
	incrementer.f=value;
	++incrementer.i;
	return incrementer.f;
	}

/*********************************
Methods of class Simulation::Grid:
*********************************/

Simulation::Grid::Grid(void)
	:cells(0),
	 unitCellIndexSize(0),unitCellIndices(0)
	{
	}

Simulation::Grid::~Grid(void)
	{
	delete[] cells;
	delete[] unitCellIndices;
	}

void Simulation::Grid::create(const Box& domain,const UnitTypeList& unitTypes,Scalar centralForceOvershoot,Scalar vertexForceRadius)
	{
	/* Calculate the minimum size of a grid cell based on the largest unit type, central force overshoot, and vertex force radius: */
	Scalar minCellSize(0);
	for(Misc::Vector<UnitType>::const_iterator utIt=unitTypes.begin();utIt!=unitTypes.end();++utIt)
		{
		/* Calculate the unit type's central force radius: */
		Scalar centralForceRadius=utIt->radius*Scalar(2)+centralForceOvershoot;
		if(minCellSize<centralForceRadius)
			minCellSize=centralForceRadius;
		
		/* Calculate the unit type's maximum vertex force radius: */
		for(Misc::Vector<BondSite>::const_iterator bsIt=utIt->bondSites.begin();bsIt!=utIt->bondSites.end();++bsIt)
			{
			Scalar bondVertexForceRadius=Geometry::mag(bsIt->offset)*Scalar(2)+vertexForceRadius;
			if(minCellSize<bondVertexForceRadius)
				minCellSize=bondVertexForceRadius;
			}
		}
	
	/* Calculate the size and layout of the acceleration grid: */
	for(int i=0;i<3;++i)
		{
		/* Calculate the number of cells in this direction: */
		numCells[i]=Size(Math::floor(domain.getSize(i)/minCellSize));
		
		/* Calculate the grid cell size in this direction: */
		cellSize[i]=domain.getSize(i)/Scalar(numCells[i]);
		
		/* Slightly increase grid cell size until there is no chance of grid overshoot by rounding error: */
		while(Index((domain.max[i]-domain.min[i])/cellSize[i])>=numCells[i])
			cellSize[i]=increment(cellSize[i]);
		
		/* Store the domain origin: */
		origin[i]=domain.min[i];
		}
	
	// DEBUGGING
	#if 0
	std::cout<<"Grid size: "<<numCells[0]<<'x'<<numCells[1]<<'x'<<numCells[2]<<std::endl;
	std::cout<<"Grid dell size: "<<cellSize[0]<<'x'<<cellSize[1]<<'x'<<cellSize[2]<<std::endl;
	std::cout<<"Top indices: "<<int((domain.max[0]-origin[0])/cellSize[0])<<", "<<int((domain.max[1]-origin[1])/cellSize[1])<<", "<<int((domain.max[2]-origin[2])/cellSize[2])<<std::endl;
	#endif
	
	/* Allocate and initialize the acceleration grid: */
	delete[] cells;
	cells=new Cell[numCells[2]*numCells[1]*numCells[0]];
	Cell* gcPtr=cells;
	ptrdiff_t strides[3];
	strides[0]=1;
	for(int i=1;i<3;++i)
		strides[i]=strides[i-1]*numCells[i-1];
	ptrdiff_t offsets[6];
	for(Index z=0;z<numCells[2];++z)
		{
		offsets[4]=z>0?-strides[2]:strides[2]*ptrdiff_t(numCells[2]-1);
		offsets[5]=z<numCells[2]-1?strides[2]:-strides[2]*ptrdiff_t(numCells[2]-1);
		for(Index y=0;y<numCells[1];++y)
			{
			offsets[2]=y>0?-strides[1]:strides[1]*ptrdiff_t(numCells[1]-1);
			offsets[3]=y<numCells[1]-1?strides[1]:-strides[1]*ptrdiff_t(numCells[1]-1);
			for(Index x=0;x<numCells[0];++x,++gcPtr)
				{
				offsets[0]=x>0?-strides[0]:strides[0]*ptrdiff_t(numCells[0]-1);
				offsets[1]=x<numCells[0]-1?strides[0]:-strides[0]*ptrdiff_t(numCells[0]-1);
				
				/* Store pointers to the grid cell's neighbors and itself: */
				gcPtr->neighbors[0]=gcPtr+offsets[0]+offsets[2]+offsets[4];
				gcPtr->neighbors[1]=gcPtr+offsets[2]+offsets[4];
				gcPtr->neighbors[2]=gcPtr+offsets[1]+offsets[2]+offsets[4];
				gcPtr->neighbors[3]=gcPtr+offsets[0]+offsets[4];
				gcPtr->neighbors[4]=gcPtr+offsets[4];
				gcPtr->neighbors[5]=gcPtr+offsets[1]+offsets[4];
				gcPtr->neighbors[6]=gcPtr+offsets[0]+offsets[3]+offsets[4];
				gcPtr->neighbors[7]=gcPtr+offsets[3]+offsets[4];
				gcPtr->neighbors[8]=gcPtr+offsets[1]+offsets[3]+offsets[4];
				gcPtr->neighbors[9]=gcPtr+offsets[0]+offsets[2];
				gcPtr->neighbors[10]=gcPtr+offsets[2];
				gcPtr->neighbors[11]=gcPtr+offsets[1]+offsets[2];
				gcPtr->neighbors[12]=gcPtr+offsets[0];
				gcPtr->neighbors[13]=gcPtr;
				gcPtr->neighbors[14]=gcPtr+offsets[1];
				gcPtr->neighbors[15]=gcPtr+offsets[0]+offsets[3];
				gcPtr->neighbors[16]=gcPtr+offsets[3];
				gcPtr->neighbors[17]=gcPtr+offsets[1]+offsets[3];
				gcPtr->neighbors[18]=gcPtr+offsets[0]+offsets[2]+offsets[5];
				gcPtr->neighbors[19]=gcPtr+offsets[2]+offsets[5];
				gcPtr->neighbors[20]=gcPtr+offsets[1]+offsets[2]+offsets[5];
				gcPtr->neighbors[21]=gcPtr+offsets[0]+offsets[5];
				gcPtr->neighbors[22]=gcPtr+offsets[5];
				gcPtr->neighbors[23]=gcPtr+offsets[1]+offsets[5];
				gcPtr->neighbors[24]=gcPtr+offsets[0]+offsets[3]+offsets[5];
				gcPtr->neighbors[25]=gcPtr+offsets[3]+offsets[5];
				gcPtr->neighbors[26]=gcPtr+offsets[1]+offsets[3]+offsets[5];
				}
			}
		}
	
	/* Reset the unit cell index array: */
	delete[] unitCellIndices;
	unitCellIndexSize=0;
	unitCellIndices=0;
	}

void Simulation::Grid::reserve(Size numUnits)
	{
	/* Check if the unit cell index array is too small: */
	if(unitCellIndexSize<numUnits)
		{
		/* Increase the size of the cell index array: */
		Size newUnitCellIndexSize=unitCellIndexSize;
		while(newUnitCellIndexSize<numUnits)
			newUnitCellIndexSize=(newUnitCellIndexSize*5)/4+1;
		
		/* Allocate a new array and copy over current indices: */
		Index* newUnitCellIndices=new Index[newUnitCellIndexSize];
		for(Index i=0;i<unitCellIndexSize;++i)
			newUnitCellIndices[i]=unitCellIndices[i];
		
		/* Replace the old array: */
		delete[] unitCellIndices;
		unitCellIndexSize=newUnitCellIndexSize;
		unitCellIndices=newUnitCellIndices;
		}
	}

void Simulation::Grid::insertUnit(Index unitIndex,const UnitState& unit)
	{
	/* Find the grid cell containing the new unit: */
	Index cellIndex=calcCellIndex(unit.position);
	
	/* Add the unit to its grid cell's unit list: */
	cells[cellIndex].unitIndices.push_back(unitIndex);
	unitCellIndices[unitIndex]=cellIndex;
	}

void Simulation::Grid::moveUnit(Index unitIndex,const UnitState& unit)
	{
	/* Find the new grid cell containing the given unit: */
	Index cellIndex=calcCellIndex(unit.position);
	
	/* Check if the unit changed grid cells: */
	if(unitCellIndices[unitIndex]!=cellIndex)
		{
		/* Remove the unit from its previous grid cell's unit list: */
		Cell& gc=cells[unitCellIndices[unitIndex]];
		for(std::vector<Index>::iterator uiIt=gc.unitIndices.begin();uiIt!=gc.unitIndices.end();++uiIt)
			if(*uiIt==unitIndex)
				{
				/* Move the list's last entry to the front: */
				*uiIt=gc.unitIndices.back();
				
				/* Remove the copied last entry: */
				gc.unitIndices.pop_back();
				
				/* Stop searching: */
				break;
				}
		
		/* Add the unit to its new grid cell's unit list: */
		cells[cellIndex].unitIndices.push_back(unitIndex);
		unitCellIndices[unitIndex]=cellIndex;
		}
	}

void Simulation::Grid::removeUnit(Index unitIndex)
	{
	/* Remove the removed unit from its grid cell's unit list: */
	Cell& gc=cells[unitCellIndices[unitIndex]];
	for(std::vector<Index>::iterator uiIt=gc.unitIndices.begin();uiIt!=gc.unitIndices.end();++uiIt)
		if(*uiIt==unitIndex)
			{
			/* Move the list's last entry to the current slot: */
			*uiIt=gc.unitIndices.back();
			
			/* Remove the copied last entry: */
			gc.unitIndices.pop_back();
			
			/* Stop searching: */
			break;
			}
	}

void Simulation::Grid::changeUnitIndex(Index currentUnitIndex,Index newUnitIndex)
	{
	/* Move the unit's grid cell index to its new slot: */
	unitCellIndices[newUnitIndex]=unitCellIndices[currentUnitIndex];
	
	/* Change the index of the unit in its grid cell's unit list: */
	Cell& gc=cells[unitCellIndices[newUnitIndex]];
	for(std::vector<Index>::iterator uiIt=gc.unitIndices.begin();uiIt!=gc.unitIndices.end();++uiIt)
		if(*uiIt==currentUnitIndex)
			{
			/* Change the index to the new value: */
			*uiIt=newUnitIndex;
			
			/* Stop searching: */
			break;
			}
	}

void Simulation::Grid::moveUnits(Size numUnits,const UnitState* unitStates)
	{
	const UnitState* uPtr=unitStates;
	for(Index unitIndex=0;unitIndex<numUnits;++unitIndex,++uPtr)
		{
		/* Find the new grid cell containing the unit: */
		Index cellIndex=calcCellIndex(uPtr->position);
		
		/* Check if the unit changed grid cells: */
		if(unitCellIndices[unitIndex]!=cellIndex)
			{
			/* Remove the unit from its previous grid cell's unit list: */
			Cell& gc=cells[unitCellIndices[unitIndex]];
			for(std::vector<Index>::iterator uiIt=gc.unitIndices.begin();uiIt!=gc.unitIndices.end();++uiIt)
				if(*uiIt==unitIndex)
					{
					/* Move the list's last entry to the front: */
					*uiIt=gc.unitIndices.back();
					
					/* Remove the copied last entry: */
					gc.unitIndices.pop_back();
					
					/* Stop searching: */
					break;
					}
			
			/* Add the unit to its new grid cell's unit list: */
			cells[cellIndex].unitIndices.push_back(unitIndex);
			unitCellIndices[unitIndex]=cellIndex;
			}
		}
	}

void Simulation::Grid::check(Size numUnits,const UnitState* unitStates) const
	{
	/* Check the grid for consistency: */
	for(Index gci=0;gci<numCells[2]*numCells[1]*numCells[0];++gci)
		{
		Cell& gc=cells[gci];
		for(std::vector<Index>::iterator uiIt=gc.unitIndices.begin();uiIt!=gc.unitIndices.end();++uiIt)
			assert(unitCellIndices[*uiIt]==gci);
		}
	
	for(Index ui=0;ui<numUnits;++ui)
		{
		Index gci=calcCellIndex(unitStates[ui].position);
		assert(unitCellIndices[ui]==gci);
		
		Cell& gc=cells[unitCellIndices[ui]];
		Index numInstances=0;
		for(std::vector<Index>::iterator uiIt=gc.unitIndices.begin();uiIt!=gc.unitIndices.end();++uiIt)
			if(*uiIt==ui)
				++numInstances;
		assert(numInstances==1);
		}
	}

/***************************
Methods of class Simulation:
***************************/

Vector Simulation::wrapDistance(const Vector& distance) const
	{
	Vector result=distance;
	for(int i=0;i<3;++i)
		{
		Scalar ds=domain.getSize(i);
		if(result[i]>Math::div2(ds))
			result[i]-=ds;
		else if(result[i]<-Math::div2(ds))
			result[i]+=ds;
		}
	
	return result;
	}

Point Simulation::wrapPosition(const Point& position) const
	{
	Point result=position;
	for(int i=0;i<3;++i)
		{
		/* Wrap the position along the current axis: */
		Scalar ds=domain.getSize(i);
		while(result[i]<domain.min[i])
			result[i]+=ds;
		while(result[i]>=domain.max[i])
			result[i]-=ds;
		}
	
	return result;
	}

PickID Simulation::getPickId(void)
	{
	/* Advance the pick ID until it is valid and unused: */
	do
		{
		++lastPickId;
		}
	while(lastPickId==PickID(0)||pickRecords.isEntry(lastPickId));
	
	/* Return the next pick ID: */
	return lastPickId;
	}

void Simulation::unpickUnit(PickID pickId,Index unitIndex)
	{
	/* Get the unit's current pick record list: */
	PickRecordList& prl=pickRecords[pickId].getDest();
	
	/* Remove the unit's record from the list: */
	for(PickRecordList::iterator prlIt=prl.begin();prlIt!=prl.end();++prlIt)
		if(prlIt->unitIndex==unitIndex)
			{
			/* Move the last pick record list entry to the front: */
			*prlIt=prl.back();
			prl.pop_back();
			
			/* Stop searching: */
			break;
			}
	}

void Simulation::pickUnits(UnitState* unitStates,Index unitIndex,const Point& pickPosition,const Rotation& pickOrientation,bool pickConnected,Simulation::PickRecordMap::Entry& pickRecord)
	{
	/* Check whether to pick connected units: */
	if(pickConnected)
		{
		/* Create a queue of connected units and start with the given one: */
		Misc::OneTimeQueue<Index> connectedUnits(17);
		connectedUnits.push(unitIndex);
		
		/* Follow connected bonds until the queue is empty: */
		while(!connectedUnits.empty())
			{
			/* Pick and create a pick record entry for the first unit in the queue, if it is not picked already: */
			PickRecord pr;
			pr.unitIndex=connectedUnits.front();
			connectedUnits.pop();
			UnitState& u=unitStates[pr.unitIndex];
			
			/* Unpick the unit if it is already part of another pick: */
			if(u.pickId!=0)
				unpickUnit(u.pickId,pr.unitIndex);
			
			/* Pick the unit: */
			u.pickId=pickRecord.getSource();
			Rotation poInv=Geometry::invert(pickOrientation);
			pr.positionOffset=poInv.transform(wrapDistance(u.position-pickPosition));
			pr.orientationOffset=poInv*u.orientation;
			pickRecord.getDest().push_back(pr);
			
			/* Find all units bonded to the first one: */
			const UnitType& ut=unitTypes[u.unitType];
			for(Index bsi=0;bsi<ut.bondSites.size();++bsi)
				{
				Bond b(pr.unitIndex,bsi);
				BondMap::Iterator bIt=bonds.findEntry(b);
				if(!bIt.isFinished())
					{
					/* Put the unit at the other end of the bond into the queue: */
					connectedUnits.push(bIt->getDest().unitIndex);
					}
				}
			}
		}
	else
		{
		/* Create a pick record entry for the given unit: */
		PickRecord pr;
		pr.unitIndex=unitIndex;
		UnitState& u=unitStates[unitIndex];
		
		/* Unpick the unit if it is already part of another pick: */
		if(u.pickId!=0)
			unpickUnit(u.pickId,unitIndex);
		
		/* Pick the unit: */
		u.pickId=pickRecord.getSource();
		Rotation poInv=Geometry::invert(pickOrientation);
		pr.positionOffset=poInv.transform(wrapDistance(u.position-pickPosition));
		pr.orientationOffset=poInv*u.orientation;
		pickRecord.getDest().push_back(pr);
		}
	}

void Simulation::calcForces(Size numUnits,const UnitState* states,Vector* forces,Vector* torques) const
	{
	/* Zero out force arrays: */
	for(Index i=0;i<numUnits;++i)
		{
		forces[i]=Vector::zero;
		torques[i]=Vector::zero;
		}
	
	/* Retrieve dampening factors: */
	Scalar ld=parameters.getLockedValue().linearDampening;
	Scalar ad=parameters.getLockedValue().angularDampening;
	
	/* Calculate central repelling forces between all pairs of units: */
	for(Index ui0=0;ui0<numUnits;++ui0)
		{
		const UnitState& u0=states[ui0];
		const UnitType& ut0=unitTypes[u0.unitType];
		Scalar r0=ut0.radius;
		
		/* Find all near-by units by searching neighbors of the unit's grid cell: */
		const Grid::Cell& gc=grid.getCell(ui0);
		for(int neighborIndex=0;neighborIndex<27;++neighborIndex)
			{
			const Grid::Cell* nPtr=gc.neighbors[neighborIndex];
			for(std::vector<Index>::const_iterator ui1It=nPtr->unitIndices.begin();ui1It!=nPtr->unitIndices.end();++ui1It)
				if(*ui1It>ui0)
					{
					const UnitState& u1=states[*ui1It];
					const UnitType& ut1=unitTypes[u1.unitType];
					Scalar r1=ut1.radius;
					
					/* Calculate the wrapped distance vector between the units: */
					Vector dist=wrapDistance(u1.position-u0.position);
					Scalar distLen2=Geometry::sqr(dist);
					
					/* Calculate the central repelling force between the two units: */
					Scalar centralForceRadius=r0+r1+centralForceOvershoot;
					Scalar centralForceRadius2=Math::sqr(centralForceRadius);
					if(distLen2<centralForceRadius2)
						{
						Vector force=dist*(centralForceStrength*(Math::sqrt(distLen2)-centralForceRadius)/centralForceRadius2);
						// DEBUGGING
						// std::cout<<"Central interaction "<<ui0<<"<->"<<*ui1It<<" = "<<force<<std::endl;
						forces[ui0]+=force;
						forces[*ui1It]-=force;
						}
					}
			}
		}
	
	/* Calculate attracting forces and torques from all bonds: */
	for(BondMap::ConstIterator bIt=bonds.begin();!bIt.isFinished();++bIt)
		{
		/* Only process the "up-facing" subset of all bonds: */
		if(bIt->getSource().unitIndex<bIt->getDest().unitIndex)
			{
			/* Retrieve the two bonded units: */
			Index ui0=bIt->getSource().unitIndex;
			const UnitState& u0=states[ui0];
			const UnitType& ut0=unitTypes[u0.unitType];
			Index bsi0=bIt->getSource().bondSiteIndex;
			Index ui1=bIt->getDest().unitIndex;
			const UnitState& u1=states[ui1];
			const UnitType& ut1=unitTypes[u1.unitType];
			Index bsi1=bIt->getDest().bondSiteIndex;
			
			/* Calculate the wrapped distance vector between the units: */
			Vector dist=wrapDistance(u1.position-u0.position);
			
			/* Calculate the distance vector between the two bonding sites: */
			Vector bs0=u0.orientation.transform(ut0.bondSites[bsi0].offset);
			dist-=bs0;
			Vector bs1=u1.orientation.transform(ut1.bondSites[bsi1].offset);
			dist+=bs1;
			
			/* Calculate the vertex attraction force: */
			Scalar distLen2=Geometry::sqr(dist);
			if(distLen2<=vertexForceRadius2)
				{
				/* Calculate an attractive force between the bond sites: */
				Vector force=dist*(vertexForceStrength*(vertexForceRadius-Math::sqrt(distLen2))/vertexForceRadius2);
				
				/* Calculate the linear velocity difference between the two bond sites: */
				Vector dv=u1.linearVelocity;
				dv+=u1.angularVelocity^bs1;
				dv-=u0.linearVelocity;
				dv-=u0.angularVelocity^bs0;
				
				/* Calculate a dampening force between the bond sites: */
				force+=dv*ld;
				
				// DEBUGGING
				// std::cout<<"Vertex interaction ("<<ui0<<", "<<bsi0<<")<->("<<ui1<<", "<<bsi1<<") = "<<force<<std::endl;
				forces[ui0]+=force;
				forces[ui1]-=force;
				
				/* Calculate torques: */
				torques[ui0]+=bs0^force;
				torques[ui1]-=bs1^force;
				
				/* Calculate the angular velocity difference along the unit's distance vector: */
				Vector domega=u1.angularVelocity;
				domega-=u0.angularVelocity;
				Vector torque=domega*ad; // dist*((domega*dist)*ad/distLen2);
				torques[ui0]+=torque;
				torques[ui1]-=torque;
				}
			}
		}
	}

void Simulation::applyForces(Size numUnits,const UnitState* source,UnitState* dest,Vector* forces,Vector* torques,Scalar dt)
	{
	/* Process all units: */
	Scalar att=Math::pow(parameters.getLockedValue().attenuation,dt);
	const UnitState* sPtr=source;
	UnitState* dPtr=dest;
	for(Index ui=0;ui<numUnits;++ui,++sPtr,++dPtr)
		{
		/* Copy basic unit state: */
		dPtr->unitType=sPtr->unitType;
		const UnitType& ut=unitTypes[sPtr->unitType];
		dPtr->pickId=sPtr->pickId;
		
		/* Update linear and angular velocities unless the unit is locked by a pick record: */
		dPtr->linearVelocity=sPtr->linearVelocity;
		dPtr->angularVelocity=sPtr->angularVelocity;
		if(sPtr->pickId==0)
			{
			Vector linearAcceleration=forces[ui];
			linearAcceleration*=ut.invMass*dt;
			dPtr->linearVelocity+=linearAcceleration;
			Vector angularAcceleration=Vector(ut.invMomentOfInertia*torques[ui]);
			angularAcceleration*=dt;
			dPtr->angularVelocity+=angularAcceleration;
			}
		
		/* Update position and orientation: */
		dPtr->position=wrapPosition(sPtr->position+dPtr->linearVelocity*dt);
		dPtr->orientation=Rotation(dPtr->angularVelocity*dt)*sPtr->orientation;
		dPtr->orientation.renormalize();
		
		if(sPtr->pickId==0)
			{
			/* Attenuate velocities: */
			dPtr->linearVelocity*=att;
			dPtr->angularVelocity*=att;
			}
		}
	
	/* Update the acceleration grid: */
	grid.moveUnits(numUnits,dest);
	}

void Simulation::updateBonds(Size numUnits,const UnitState* states)
	{
	/* Process all units: */
	for(Index ui0=0;ui0<numUnits;++ui0)
		{
		const UnitState& u0=states[ui0];
		const UnitType& ut0=unitTypes[u0.unitType];
		// Scalar r0=ut0.radius; // Currently not used
		
		/* Process all the unit's bond sites: */
		for(Index bsi0=0;bsi0<ut0.bondSites.size();++bsi0)
			{
			/* Transform the bond site's offset to global coordinates: */
			Vector bs0=u0.orientation.transform(ut0.bondSites[bsi0].offset);
			
			/* Check if the bond site is already bonded: */
			Bond b0(ui0,bsi0);
			BondMap::Iterator bIt=bonds.findEntry(b0);
			if(!bIt.isFinished())
				{
				/* Check if this is the "up" direction of the bond: */
				if(bIt->getDest().unitIndex>ui0)
					{
					/* Check if the bond has become invalid: */
					Index ui1=bIt->getDest().unitIndex;
					const UnitState& u1=states[ui1];
					const UnitType& ut1=unitTypes[u1.unitType];
					Index bsi1=bIt->getDest().bondSiteIndex;
					Vector bs1=u1.orientation.transform(ut1.bondSites[bsi1].offset);
					
					/* Calculate the wrapped distance vector between the bond sites: */
					Vector dist=wrapDistance(u1.position-u0.position);
					dist-=bs0;
					dist+=bs1;
					if(Geometry::sqr(dist)>vertexForceRadius2)
						{
						/* Break the bond by removing both the "up" and "down" directions: */
						// DEBUGGING
						// std::cout<<"Breaking bond ("<<ui0<<", "<<bsi0<<")<->("<<ui1<<", "<<bsi1<<")"<<std::endl;
						bonds.removeEntry(bIt);
						bonds.removeEntry(Bond(ui1,bsi1));
						}
					}
				}
			else
				{
				/* Check if the bond site can bond with another near-by unit by searching neighbors of the unit's grid cell: */
				const Grid::Cell& gc=grid.getCell(ui0);
				for(int neighborIndex=0;neighborIndex<27;++neighborIndex)
					{
					const Grid::Cell* nPtr=gc.neighbors[neighborIndex];
					for(std::vector<Index>::const_iterator ui1It=nPtr->unitIndices.begin();ui1It!=nPtr->unitIndices.end();++ui1It)
						if(*ui1It>ui0)
							{
							const UnitState& u1=states[*ui1It];
							const UnitType& ut1=unitTypes[u1.unitType];
							Scalar r1=ut1.radius;
							
							/* Calculate the wrapped distance vector between the units and offset it for the first bond site: */
							Vector dist=wrapDistance(u1.position-u0.position);
							dist-=bs0;
							Scalar dLen2=Geometry::sqr(dist);
							
							/* Check if the other unit is close enough to potentially bond: */
							if(dLen2<=Math::sqr(r1+vertexForceRadius))
								{
								/* Check all bond sites on the other unit: */
								for(Index bsi1=0;bsi1<ut1.bondSites.size();++bsi1)
									{
									/* Check that the other bond site is not already bonded: */
									Bond b1(*ui1It,bsi1);
									if(!bonds.isEntry(b1))
										{
										/* Calculate the bond site distance: */
										Vector bDist=dist+u1.orientation.transform(ut1.bondSites[bsi1].offset);
										if(Geometry::sqr(bDist)<=vertexForceRadius2)
											{
											/* Create a bond by inserting both the "up" and "down" halves into the bond map: */
											// DEBUGGING
											// std::cout<<"Creating bond ("<<ui0<<", "<<bsi0<<")<->("<<*ui1It<<", "<<bsi1<<")"<<std::endl;
											bonds[b0]=b1;
											bonds[b1]=b0;
											
											/* Stop looking for bonding opportunities: */
											goto doneCheckingBondSite;
											}
										}
									}
								}
							}
					}
				
				doneCheckingBondSite:
				; // Just to make compiler happy
				}
			}
		}
	}

void Simulation::save(UnitStateArray& states,IO::File& file) const
	{
	/* Write a file identifier: */
	char tag[32];
	memset(tag,0,sizeof(tag));
	strcpy(tag,"NanotechConstructionKit 2.0\r\n");
	file.write(tag,sizeof(tag));
	
	/* Write the list of unit types: */
	Misc::write(unitTypes,file);
	
	/* Write the domain size: */
	Misc::write(domain,file);
	
	/* Write simulation parameters: */
	file.write<Scalar>(vertexForceRadius);
	file.write<Scalar>(vertexForceStrength);
	file.write<Scalar>(centralForceOvershoot);
	file.write<Scalar>(centralForceStrength);
	
	/* Write the given unit state array: */
	writeStateArray(states,file,false);
	
	/* Write all bonds: */
	file.write<Size>(bonds.getNumEntries()/2); // Only write "up" halves of bonds
	for(BondMap::ConstIterator bIt=bonds.begin();!bIt.isFinished();++bIt)
		{
		/* Check if this is the "up" half of a bond: */
		if(bIt->getSource().unitIndex<bIt->getDest().unitIndex)
			{
			file.write<Index>(bIt->getSource().unitIndex);
			file.write<Index>(bIt->getSource().bondSiteIndex);
			file.write<Index>(bIt->getDest().unitIndex);
			file.write<Index>(bIt->getDest().bondSiteIndex);
			}
		}
	}

void Simulation::load(IO::File& file,UnitStateArray& states)
	{
	/* Check the file identifier: */
	char tag[32];
	file.read(tag,sizeof(tag));
	if(strcmp(tag,"NanotechConstructionKit 2.0\r\n")!=0)
		throw std::runtime_error("Input file is not a unit file");
	
	/* Read the list of unit types: */
	Misc::read(file,unitTypes);
	
	/* Read the domain size: */
	Misc::read(file,domain);
	
	/* Read simulation parameters: */
	vertexForceRadius=file.read<Scalar>();
	vertexForceRadius2=Math::sqr(vertexForceRadius);
	vertexForceStrength=file.read<Scalar>();
	centralForceOvershoot=file.read<Scalar>();
	centralForceStrength=file.read<Scalar>();
	
	/* Create the acceleration grid: */
	grid.create(domain,unitTypes,centralForceOvershoot,vertexForceRadius);
	
	/* Read units into the given unit state array: */
	readStateArray(file,states,false);
	
	/* Sort the read units into their appropriate grid cells: */
	grid.reserve(states.states.size());
	Index unitIndex=0;
	for(UnitStateArray::UnitStateList::iterator sIt=states.states.begin();sIt!=states.states.end();++sIt,++unitIndex)
		{
		/* Add the unit to the acceleration grid: */
		grid.insertUnit(unitIndex,*sIt);
		}
	
	/* Read bonds: */
	Size numBonds=file.read<Size>();
	bonds.clear();
	for(Index i=0;i<numBonds;++i)
		{
		/* Read the bond: */
		Bond b0;
		b0.unitIndex=file.read<Index>();
		b0.bondSiteIndex=file.read<Index>();
		Bond b1;
		b1.unitIndex=file.read<Index>();
		b1.bondSiteIndex=file.read<Index>();
		
		/* Insert the "up" and "down" halves of the bond into the bond map: */
		bonds[b0]=b1;
		bonds[b1]=b0;
		}
	
	// DEBUGGING
	std::cout<<"Loaded file: "<<states.states.size()<<" units, "<<numBonds<<" bonds"<<std::endl;
	}

Simulation::Simulation(const Misc::ConfigurationFileSection& configFileSection,const Box& sDomain)
	:bonds(17),
	 forceArraySize(0),forces(0),torques(0),
	 loadSessionId(1),
	 lastPickId(0),pickRecords(17)
	{
	/* Read simulation parameters: */
	vertexForceRadius=configFileSection.retrieveValue<Scalar>("./vertexForceRadius",vertexForceRadius);
	vertexForceRadius2=Math::sqr(vertexForceRadius);
	vertexForceStrength=configFileSection.retrieveValue<Scalar>("./vertexForceStrength",vertexForceStrength);
	centralForceOvershoot=configFileSection.retrieveValue<Scalar>("./centralForceOvershoot",centralForceOvershoot);
	centralForceStrength=configFileSection.retrieveValue<Scalar>("./centralForceStrength",centralForceStrength);
	
	Parameters& p=parameters.startNewValue();
	p.linearDampening=configFileSection.retrieveValue<Scalar>("./linearDampening",Scalar(0));
	p.angularDampening=configFileSection.retrieveValue<Scalar>("./angularDampening",Scalar(0));
	p.attenuation=configFileSection.retrieveValue<Scalar>("./attenuation",Scalar(0.9));
	p.timeFactor=configFileSection.retrieveValue<Scalar>("./timeFactor",Scalar(10));
	parameters.postNewValue();
	mostRecentParameters=&p;
	
	/* Read the list of unit types: */
	std::vector<std::string> unitTypeNames=configFileSection.retrieveValue<std::vector<std::string> >("./structuralUnitTypes");
	for(std::vector<std::string>::iterator utnIt=unitTypeNames.begin();utnIt!=unitTypeNames.end();++utnIt)
		{
		try
			{
			/* Go to the unit type's configuration section: */
			Misc::ConfigurationFileSection utSec=configFileSection.getSection(utnIt->c_str());
			
			/* Read a unit type definition: */
			UnitType newUt;
			newUt.name=utSec.retrieveString("./name",*utnIt);
			newUt.radius=utSec.retrieveValue<Scalar>("./radius");
			newUt.mass=utSec.retrieveValue<Scalar>("./mass");
			newUt.invMass=Scalar(1)/newUt.mass;
			newUt.momentOfInertia=utSec.retrieveValue<Tensor>("./momentOfInertia");
			newUt.invMomentOfInertia=Geometry::invert(newUt.momentOfInertia);
			newUt.bondSites=utSec.retrieveValue<Misc::Vector<BondSite> >("./bondSites");
			newUt.meshVertices=utSec.retrieveValue<Misc::Vector<Point> >("./meshVertices");
			newUt.meshTriangles=utSec.retrieveValue<Misc::Vector<Index> >("./meshTriangles");
			
			/* Store the unit type: */
			unitTypes.push_back(newUt);
			}
		catch(const std::runtime_error& err)
			{
			Misc::formattedUserError("Simulation::Simulation: Ignoring unit type %s due to exception %s",utnIt->c_str(),err.what());
			}
		}
	
	/* Set the simulation domain: */
	domain=sDomain;
	
	/* Create the acceleration grid: */
	grid.create(domain,unitTypes,centralForceOvershoot,vertexForceRadius);
	
	/* Mark the session as valid: */
	sessionId=loadSessionId;
	
	/* Post an initial update to the unit states triple buffer: */
	UnitStateArray& states=unitStates.startNewValue();
	states.sessionId=sessionId;
	states.timeStamp=1;
	unitStates.postNewValue();
	mostRecentStates=&states;
	}

Simulation::Simulation(const Misc::ConfigurationFileSection& configFileSection,IO::File& file)
	:bonds(17),
	 forceArraySize(0),forces(0),torques(0),
	 loadSessionId(0),
	 lastPickId(0),pickRecords(17)
	{
	/* Read simulation parameters: */
	Parameters& p=parameters.startNewValue();
	p.linearDampening=configFileSection.retrieveValue<Scalar>("./linearDampening",Scalar(0));
	p.angularDampening=configFileSection.retrieveValue<Scalar>("./angularDampening",Scalar(0));
	p.attenuation=configFileSection.retrieveValue<Scalar>("./attenuation",Scalar(0.9));
	p.timeFactor=configFileSection.retrieveValue<Scalar>("./timeFactor",Scalar(10));
	parameters.postNewValue();
	mostRecentParameters=&p;
	
	/* Post an initial update to the unit states triple buffer: */
	UnitStateArray& states=unitStates.startNewValue();
	states.sessionId=sessionId;
	states.timeStamp=1;
	unitStates.postNewValue();
	mostRecentStates=&states;
	
	/* Load the requested unit file in the back end: */
	loadState(file);
	}

Simulation::~Simulation(void)
	{
	delete[] forces;
	delete[] torques;
	}

bool Simulation::isSessionValid(void) const
	{
	return sessionId==loadSessionId;
	}

const SimulationInterface::Parameters& Simulation::getParameters(void) const
	{
	return *mostRecentParameters;
	}

void Simulation::setParameters(const SimulationInterface::Parameters& newParameters)
	{
	Parameters& p=parameters.startNewValue();
	p=newParameters;
	parameters.postNewValue();
	mostRecentParameters=&p;
	}

bool Simulation::lockNewState(void)
	{
	return unitStates.lockNewValue();
	}

bool Simulation::isLockedStateValid(void) const
	{
	return unitStates.getLockedValue().sessionId==loadSessionId;
	}

PickID Simulation::pick(const Point& pickPosition,Scalar pickRadius,const Rotation& pickOrientation,bool pickConnected)
	{
	/* Create a new UI request: */
	UIRequest newRequest;
	newRequest.requestType=UIRequest::PICK_POS;
	newRequest.pickId=getPickId();
	newRequest.pickPos=pickPosition;
	newRequest.pickRadius=pickRadius;
	newRequest.pickConnected=pickConnected;
	newRequest.setOrientation=pickOrientation;
	
	/* Put the UI request into the queue: */
	{
	Threads::Spinlock::Lock uiRequestLock(uiRequestMutex);
	uiRequests.push_back(newRequest);
	}
	
	return newRequest.pickId;
	}

PickID Simulation::pick(const Point& pickPosition,const Vector& pickDirection,const Rotation& pickOrientation,bool pickConnected)
	{
	/* Create a new UI request: */
	UIRequest newRequest;
	newRequest.requestType=UIRequest::PICK_RAY;
	newRequest.pickId=getPickId();
	newRequest.pickPos=pickPosition;
	newRequest.pickDir=pickDirection;
	newRequest.pickConnected=pickConnected;
	newRequest.setOrientation=pickOrientation;
	
	/* Put the UI request into the queue: */
	{
	Threads::Spinlock::Lock uiRequestLock(uiRequestMutex);
	uiRequests.push_back(newRequest);
	}
	
	return newRequest.pickId;
	}

PickID Simulation::paste(const Point& newPosition,const Rotation& newOrientation,const Vector& newLinearVelocity,const Vector& newAngularVelocity)
	{
	/* Create a new UI request: */
	UIRequest newRequest;
	newRequest.requestType=UIRequest::PASTE;
	newRequest.pickId=getPickId();
	newRequest.setPosition=newPosition;
	newRequest.setOrientation=newOrientation;
	newRequest.setLinearVelocity=newLinearVelocity;
	newRequest.setAngularVelocity=newAngularVelocity;
	
	/* Put the UI request into the queue: */
	{
	Threads::Spinlock::Lock uiRequestLock(uiRequestMutex);
	uiRequests.push_back(newRequest);
	}
	
	return newRequest.pickId;
	}

void Simulation::create(PickID pickId,UnitTypeID newTypeId,const Point& newPosition,const Rotation& newOrientation,const Vector& newLinearVelocity,const Vector& newAngularVelocity)
	{
	/* Create a new UI request: */
	UIRequest newRequest;
	newRequest.requestType=UIRequest::CREATE;
	newRequest.pickId=pickId;
	newRequest.createTypeId=newTypeId;
	newRequest.setPosition=newPosition;
	newRequest.setOrientation=newOrientation;
	newRequest.setLinearVelocity=newLinearVelocity;
	newRequest.setAngularVelocity=newAngularVelocity;
	
	/* Put the UI request into the queue: */
	{
	Threads::Spinlock::Lock uiRequestLock(uiRequestMutex);
	uiRequests.push_back(newRequest);
	}
	}

void Simulation::setState(PickID pickId,const Point& newPosition,const Rotation& newOrientation,const Vector& newLinearVelocity,const Vector& newAngularVelocity)
	{
	/* Create a new UI request: */
	UIRequest newRequest;
	newRequest.requestType=UIRequest::SET_STATE;
	newRequest.pickId=pickId;
	newRequest.setPosition=newPosition;
	newRequest.setOrientation=newOrientation;
	newRequest.setLinearVelocity=newLinearVelocity;
	newRequest.setAngularVelocity=newAngularVelocity;
	
	/* Put the UI request into the queue: */
	{
	Threads::Spinlock::Lock uiRequestLock(uiRequestMutex);
	uiRequests.push_back(newRequest);
	}
	}

void Simulation::copy(PickID pickId)
	{
	/* Create a new UI request: */
	UIRequest newRequest;
	newRequest.requestType=UIRequest::COPY;
	newRequest.pickId=pickId;
	
	/* Put the UI request into the queue: */
	{
	Threads::Spinlock::Lock uiRequestLock(uiRequestMutex);
	uiRequests.push_back(newRequest);
	}
	}

void Simulation::destroy(PickID pickId)
	{
	/* Create a new UI request: */
	UIRequest newRequest;
	newRequest.requestType=UIRequest::DESTROY;
	newRequest.pickId=pickId;
	
	/* Put the UI request into the queue: */
	{
	Threads::Spinlock::Lock uiRequestLock(uiRequestMutex);
	uiRequests.push_back(newRequest);
	}
	}

void Simulation::release(PickID pickId)
	{
	/* Create a new UI request: */
	UIRequest newRequest;
	newRequest.requestType=UIRequest::RELEASE;
	newRequest.pickId=pickId;
	
	/* Put the UI request into the queue: */
	{
	Threads::Spinlock::Lock uiRequestLock(uiRequestMutex);
	uiRequests.push_back(newRequest);
	}
	}

void Simulation::loadState(IO::File& stateFile)
	{
	/* Invalidate the current session: */
	do
		{
		++loadSessionId;
		}
	while(loadSessionId==0);
	
	/* Create a new UI request: */
	UIRequest newRequest;
	newRequest.requestType=UIRequest::LOAD_STATE;
	newRequest.file=&stateFile;
	newRequest.loadSessionId=loadSessionId;
	
	/* Put the UI request into the queue: */
	{
	Threads::Spinlock::Lock uiRequestLock(uiRequestMutex);
	uiRequests.push_back(newRequest);
	}
	}

void Simulation::saveState(IO::File& stateFile,SimulationInterface::SaveStateCompleteCallback* completeCallback)
	{
	/* Create a new UI request: */
	UIRequest newRequest;
	newRequest.requestType=UIRequest::SAVE_STATE;
	newRequest.file=&stateFile;
	newRequest.saveCompleteCallback=completeCallback;
	
	/* Put the UI request into the queue: */
	{
	Threads::Spinlock::Lock uiRequestLock(uiRequestMutex);
	uiRequests.push_back(newRequest);
	}
	}

void Simulation::advance(Scalar timeStep)
	{
	/* Lock the most recent simulation parameters: */
	parameters.lockNewValue();
	
	/* Apply the time speed-up factor: */
	Scalar tf=parameters.getLockedValue().timeFactor;
	timeStep*=tf;
	
	/* Limit the time step to the safety region (currently hard-coded based on experiments): */
	if(timeStep>0.06)
		timeStep=0.06;
	
	/* Grab the current queue of UI requests: */
	std::vector<UIRequest> newUiRequests;
	{
	Threads::Spinlock::Lock uiRequestLock(uiRequestMutex);
	std::swap(uiRequests,newUiRequests);
	}
	
	/* Increase the sizes of the force and torque arrays if necessary: */
	Size numUnits(mostRecentStates->states.size());
	if(forceArraySize<numUnits)
		{
		/* Resize the force and torque arrays: */
		while(forceArraySize<numUnits)
			forceArraySize=(forceArraySize*5)/4+1;
		delete[] forces;
		delete[] torques;
		forces=new Vector[forceArraySize];
		torques=new Vector[forceArraySize];
		}
	
	/* Prepare a new slot in the state triple buffer: */
	UnitStateArray& nextState=unitStates.startNewValue();
	nextState.timeStamp=mostRecentStates->timeStamp+1;
	
	/* Count how many units might have to be added in this step: */
	Size numNewUnits=0;
	for(std::vector<UIRequest>::iterator uiIt=newUiRequests.begin();uiIt!=newUiRequests.end();++uiIt)
		if(uiIt->requestType==UIRequest::PASTE)
			numNewUnits+=copiedUnits.size();
		else if(uiIt->requestType==UIRequest::CREATE)
			++numNewUnits;
	
	/* Make room in the next state buffer and the acceleration grid to add units after simulation: */
	nextState.states.reserve(numUnits+numNewUnits);
	grid.reserve(numUnits+numNewUnits);
	
	/* Ensure that the new slot contains the correct number of units: */
	Size oldNumUnits(nextState.states.size());
	if(oldNumUnits<numUnits)
		{
		UnitState us;
		for(Size i=oldNumUnits;i<numUnits;++i)
			nextState.states.push_back(us);
		}
	else if(oldNumUnits>numUnits)
		{
		for(Size i=numUnits;i<oldNumUnits;++i)
			nextState.states.pop_back();
		}
	
	/* Calculate forces and torques based on the most recent unit state array: */
	calcForces(numUnits,mostRecentStates->states.data(),forces,torques);
	
	/* Apply the calculated forces and torques for the first half-step: */
	applyForces(numUnits,mostRecentStates->states.data(),nextState.states.data(),forces,torques,Math::div2(timeStep));
	
	/* Calculate forces and torques again based on the first half-step: */
	calcForces(numUnits,nextState.states.data(),forces,torques);
	
	/* Apply the calculated forces and torques for the second half-step: */
	applyForces(numUnits,mostRecentStates->states.data(),nextState.states.data(),forces,torques,timeStep);
	
	/* Process all UI requests in order: */
	for(std::vector<UIRequest>::iterator uiIt=newUiRequests.begin();uiIt!=newUiRequests.end();++uiIt)
		{
		switch(uiIt->requestType)
			{
			case UIRequest::PICK_POS:
				{
				/* Find the grid cell containing the picking position: */
				Point pickPos=wrapPosition(uiIt->pickPos);
				Index cellIndex[3];
				grid.calcCellIndex(pickPos,cellIndex);
				
				/* Find the region of grid cells covered by the pick request: */
				int min[3],max[3];
				for(int i=0;i<3;++i)
					{
					int r=int(Math::ceil(uiIt->pickRadius/grid.getCellSize()[i]))+1;
					min[i]=int(cellIndex[i])-r;
					max[i]=int(cellIndex[i])+r;
					}
				
				/* Check the picking position against all units in the picking position's neighbourhood: */
				Index pickedUnitIndex(nextState.states.size());
				Scalar maxDistLen2=Math::Constants<Scalar>::max;
				int index[3];
				for(index[0]=min[0];index[0]<=max[0];++index[0])
					for(index[1]=min[1];index[1]<=max[1];++index[1])
						for(index[2]=min[2];index[2]<=max[2];++index[2])
							{
							const Grid::Cell& gc=grid.getWrappedCell(index);
							for(std::vector<Index>::const_iterator uIt=gc.unitIndices.begin();uIt!=gc.unitIndices.end();++uIt)
								{
								/* Calculate the wrapped distance between the picking position and the unit: */
								UnitState& u=nextState.states[*uIt];
								Vector dist=wrapDistance(u.position-pickPos);
								Scalar distLen2=Geometry::sqr(dist);
								if(distLen2<=Math::sqr(unitTypes[u.unitType].radius+uiIt->pickRadius)&&maxDistLen2>distLen2)
									{
									/* Tentatively pick this unit: */
									pickedUnitIndex=*uIt;
									maxDistLen2=distLen2;
									}
								}
							}
				
				/* Check if a unit was picked: */
				if(pickedUnitIndex<nextState.states.size())
					{
					// DEBUGGING
					// std::cout<<"Picked unit "<<pickedUnitIndex<<" of type "<<unitTypes[nextStates.state[pickedUnitIndex].unitType].name<<" for ID "<<uiIt->pickId<<std::endl;
					
					/* Create a pick record entry: */
					PickRecordMap::Entry& pr=pickRecords[uiIt->pickId];
					pickUnits(nextState.states.data(),pickedUnitIndex,pickPos,uiIt->setOrientation,uiIt->pickConnected,pr);
					}
				
				break;
				}
			
			case UIRequest::PICK_RAY:
				break;
			
			case UIRequest::PASTE:
				{
				if(!copiedUnits.empty())
					{
					/* Create a pick record entry for all instantiated units: */
					PickRecordList& prl=pickRecords[uiIt->pickId].getDest();
					prl.reserve(copiedUnits.size());
					
					/* Adjust linear and angular velocities for the current time speed-up factor: */
					Vector lv=uiIt->setLinearVelocity/tf;
					Vector av=uiIt->setAngularVelocity/tf;
					
					/* Instantiate each unit in the copy buffer in the current state array: */
					Index firstIndex(nextState.states.size());
					for(std::vector<CopiedUnitState>::iterator cuIt=copiedUnits.begin();cuIt!=copiedUnits.end();++cuIt)
						{
						// DEBUGGING
						// std::cout<<"Pasting unit of type "<<unitTypes[cuIt->unitType].name<<" for ID "<<uiIt->pickId<<std::endl;
						
						/* Create a new unit: */
						UnitState newUnit;
						newUnit.unitType=cuIt->unitType;
						newUnit.pickId=uiIt->pickId;
						
						/* Set the new unit's state: */
						Vector offset=uiIt->setOrientation.transform(cuIt->positionOffset);
						newUnit.position=wrapPosition(uiIt->setPosition+offset);
						newUnit.orientation=uiIt->setOrientation*cuIt->orientationOffset;
						newUnit.orientation.renormalize();
						newUnit.linearVelocity=lv;
						newUnit.linearVelocity+=av^offset;
						newUnit.angularVelocity=av;
						
						/* Add the new unit to the acceleration grid: */
						grid.insertUnit(Index(nextState.states.size()),newUnit);
						
						/* Create a pick record for the new unit: */
						PickRecord pr;
						pr.unitIndex=Index(nextState.states.size());
						pr.positionOffset=cuIt->positionOffset;
						pr.orientationOffset=cuIt->orientationOffset;
						prl.push_back(pr);
						
						/* Add the new unit to the current state array: */
						nextState.states.push_back(newUnit);
						}
					
					/* Instantiate all bonds in the copy buffer: */
					for(std::vector<std::pair<Bond,Bond> >::iterator cbIt=copiedBonds.begin();cbIt!=copiedBonds.end();++cbIt)
						{
						/* Translate the bond from copy buffer index space to state index space and add it to the bond map: */
						Bond b0=cbIt->first;
						b0.unitIndex+=firstIndex;
						Bond b1=cbIt->second;
						b1.unitIndex+=firstIndex;
						bonds[b0]=b1;
						bonds[b1]=b0;
						}
					}
				
				break;
				}
			
			case UIRequest::CREATE:
				{
				/* Find the pick record for the given Pick ID: */
				PickRecordMap::Iterator prIt=pickRecords.findEntry(uiIt->pickId);
				if(prIt.isFinished()) // If there is no pick record, create a new unit
					{
					// DEBUGGING
					// std::cout<<"Creating unit of type "<<unitTypes[uiIt->createTypeId].name<<" for ID "<<uiIt->pickId<<std::endl;
					
					/* Create a new unit: */
					UnitState newUnit;
					newUnit.unitType=uiIt->createTypeId;
					newUnit.pickId=uiIt->pickId;
					newUnit.position=wrapPosition(uiIt->setPosition);
					newUnit.orientation=uiIt->setOrientation;
					newUnit.linearVelocity=uiIt->setLinearVelocity;
					newUnit.angularVelocity=uiIt->setAngularVelocity;
					
					/* Adjust for current time speed-up factor: */
					newUnit.linearVelocity/=tf;
					newUnit.angularVelocity/=tf;
					
					/* Add the new unit to the acceleration grid: */
					grid.insertUnit(Index(nextState.states.size()),newUnit);
					
					/* Create a pick record for the new unit: */
					PickRecord pr;
					pr.unitIndex=Index(nextState.states.size());
					pr.positionOffset=Vector::zero;
					pr.orientationOffset=Rotation::identity;
					pickRecords[uiIt->pickId].getDest().push_back(pr);
					
					/* Add the new unit to the current state array: */
					nextState.states.push_back(newUnit);
					}
				
				break;
				}
			
			case UIRequest::SET_STATE:
				{
				/* Find the pick record for the given Pick ID: */
				PickRecordMap::Iterator prIt=pickRecords.findEntry(uiIt->pickId);
				if(!prIt.isFinished())
					{
					/* Adjust linear and angular velocities for the current time speed-up factor: */
					Vector lv=uiIt->setLinearVelocity/tf;
					Vector av=uiIt->setAngularVelocity/tf;
					
					/* Override the picked units' states: */
					PickRecordList& prl=prIt->getDest();
					for(PickRecordList::iterator prIt=prl.begin();prIt!=prl.end();++prIt)
						{
						UnitState& unit=nextState.states[prIt->unitIndex];
						// DEBUGGING
						// std::cout<<"Setting state of unit "<<prIt->unitIndex<<" of type "<<unitTypes[unit.unitType].name<<" for ID "<<uiIt->pickId<<std::endl;
						Vector offset=uiIt->setOrientation.transform(prIt->positionOffset);
						unit.position=wrapPosition(uiIt->setPosition+offset);
						unit.orientation=uiIt->setOrientation*prIt->orientationOffset;
						unit.orientation.renormalize();
						unit.linearVelocity=lv;
						unit.linearVelocity+=av^offset;
						unit.angularVelocity=av;
						
						/* Move the unit in the acceleration grid: */
						grid.moveUnit(prIt->unitIndex,unit);
						}
					}
				
				break;
				}
			
			case UIRequest::COPY:
				{
				/* Find the pick record for the given Pick ID: */
				PickRecordMap::Iterator prIt=pickRecords.findEntry(uiIt->pickId);
				if(!prIt.isFinished())
					{
					/* Get the pick record: */
					PickRecordList& prl=prIt->getDest();
					
					/* Create a new copy buffer: */
					std::vector<CopiedUnitState> newCopiedUnits;
					newCopiedUnits.reserve(prl.size());
					
					/* Create a map from original unit indices to indices in the copy buffer: */
					Misc::HashTable<Index,Index> unitIndexMap((prl.size()*3)/2);
					
					/* Copy all picked units into the copy buffer: */
					Index copiedUnitIndex=0;
					for(PickRecordList::iterator prIt=prl.begin();prIt!=prl.end();++prIt,++copiedUnitIndex)
						{
						UnitState& unit=nextState.states[prIt->unitIndex];
						// DEBUGGING
						// std::cout<<"Copying unit "<<prIt->unitIndex<<" of type "<<unitTypes[unit.unitType].name<<" for ID "<<uiIt->pickId<<std::endl;
						
						/* Copy the picked unit: */
						CopiedUnitState copiedUnit;
						copiedUnit.unitType=unit.unitType;
						copiedUnit.positionOffset=prIt->positionOffset;
						copiedUnit.orientationOffset=prIt->orientationOffset;
						newCopiedUnits.push_back(copiedUnit);
						
						/* Store the unit's index translation: */
						unitIndexMap[prIt->unitIndex]=copiedUnitIndex;
						}
					
					/* Exchange the previous and new copy buffer entries: */
					std::swap(copiedUnits,newCopiedUnits);
					
					/* Create a new copy bond buffer: */
					std::vector<std::pair<Bond,Bond> > newCopiedBonds;
					
					/* Copy all bonds between picked units: */
					for(PickRecordList::iterator prIt=prl.begin();prIt!=prl.end();++prIt,++copiedUnitIndex)
						{
						UnitState& unit=nextState.states[prIt->unitIndex];
						UnitType& ut=unitTypes[unit.unitType];
						
						/* Check the unit's bonding sites for bonds with other picked units: */
						for(Index bsi=0;bsi<ut.bondSites.size();++bsi)
							{
							/* Find an outgoing bond in the bond map: */
							BondMap::Iterator bIt=bonds.findEntry(Bond(prIt->unitIndex,bsi));
							if(!bIt.isFinished())
								{
								/* Check if this is the "up" half of a bond and the other unit is also being copied: */
								Index otherIndex=bIt->getDest().unitIndex;
								if(otherIndex>prIt->unitIndex&&nextState.states[otherIndex].pickId==uiIt->pickId)
									{
									/* Translate both unit's indices to copy buffer space and store the bond: */
									std::pair<Bond,Bond> newBond(bIt->getSource(),bIt->getDest());
									newBond.first.unitIndex=unitIndexMap[newBond.first.unitIndex].getDest();
									newBond.second.unitIndex=unitIndexMap[newBond.second.unitIndex].getDest();
									newCopiedBonds.push_back(newBond);
									}
								}
							}
						}
					
					/* Exchange the previous and new copy buffer entries: */
					std::swap(copiedBonds,newCopiedBonds);
					}
				
				break;
				}
			
			case UIRequest::DESTROY:
				{
				/* Find the pick record for the given Pick ID: */
				PickRecordMap::Iterator prIt=pickRecords.findEntry(uiIt->pickId);
				if(!prIt.isFinished())
					{
					/* Get the pick record: */
					PickRecordList& prl=prIt->getDest();
					
					/* Remove all units in the pick record from the simulation, leaving holes in various state arrays: */
					std::vector<Index> holes;
					holes.reserve(prl.size());
					for(PickRecordList::iterator prlIt=prl.begin();prlIt!=prl.end();++prlIt)
						{
						/* Remove all bonds involving the to-be-destroyed unit: */
						UnitState& unit=nextState.states[prlIt->unitIndex];
						const UnitType& ut=unitTypes[unit.unitType];
						for(Index bsi=0;bsi<ut.bondSites.size();++bsi)
							{
							/* Check if the bond site is bonded: */
							BondMap::Iterator bIt=bonds.findEntry(Bond(prlIt->unitIndex,bsi));
							if(!bIt.isFinished())
								{
								/* Remove both halves of the bond: */
								bonds.removeEntry(bIt);
								bonds.removeEntry(bIt->getDest());
								}
							}
						
						/* Remove the to-be-destroyed unit from the acceleration grid: */
						grid.removeUnit(prlIt->unitIndex);
						
						/* Remember the new hole in the state arrays: */
						holes.push_back(prlIt->unitIndex);
						}
					
					/* Fill the holes in the state arrays by copying units from the end, in ascending index order: */
					std::sort(holes.begin(),holes.end());
					std::vector<Index>::iterator hIt=holes.begin();
					std::vector<Index>::iterator hEnd=holes.end();
					while(true)
						{
						/* Lop off holes at the end of the state arrays: */
						while(hEnd!=hIt&&*(hEnd-1)==Index(nextState.states.size()-1))
							{
							--hEnd;
							nextState.states.pop_back();
							}
						
						/* Stop if all holes have been filled: */
						if(hIt==hEnd)
							break;
						
						/* Fill the first remaining hole by moving the last unit forward: */
						UnitState& unit=nextState.states[*hIt];
						unit=nextState.states.back();
						nextState.states.pop_back();
						const UnitType& ut=unitTypes[unit.unitType];
						
						/* Adapt the moved unit's bonds: */
						for(Index bsi=0;bsi<ut.bondSites.size();++bsi)
							{
							/* Check if the bond site is bonded: */
							BondMap::Iterator bIt=bonds.findEntry(Bond(Index(nextState.states.size()),bsi));
							if(!bIt.isFinished())
								{
								/* Delete this bond half, insert an adapted one, and adapt the other bond half: */
								Bond b(*hIt,bsi);
								Bond ob=bIt->getDest();
								bonds.removeEntry(bIt);
								bonds[b]=ob;
								bonds[ob]=b;
								}
							}
						
						/* Change the moved unit's grid cell entry: */
						grid.changeUnitIndex(Index(nextState.states.size()),*hIt);
						
						/* Check if the moved unit is picked: */
						if(unit.pickId!=0)
							{
							/* Adapt the moved unit's pick record: */
							PickRecordList& prl2=pickRecords[unit.pickId].getDest();
							for(PickRecordList::iterator prl2It=prl2.begin();prl2It!=prl2.end();++prl2It)
								if(prl2It->unitIndex==Index(nextState.states.size()))
									{
									/* Adapt the pick record's unit index and stop searching: */
									prl2It->unitIndex=*hIt;
									break;
									}
							}
						
						++hIt;
						}
					
					/* Delete the pick record: */
					pickRecords.removeEntry(prIt);
					}
				
				break;
				}
			
			case UIRequest::RELEASE:
				{
				/* Find the pick record for the given Pick ID: */
				PickRecordMap::Iterator prIt=pickRecords.findEntry(uiIt->pickId);
				if(!prIt.isFinished())
					{
					/* Un-pick all picked units and delete the pick record: */
					PickRecordList& prl=prIt->getDest();
					for(PickRecordList::iterator prlIt=prl.begin();prlIt!=prl.end();++prlIt)
						{
						// DEBUGGING
						// std::cout<<"Released pick on unit "<<prlIt->unitIndex<<" of type "<<unitTypes[nextState.states[prlIt->unitIndex].unitType].name<<" for ID "<<uiIt->pickId<<std::endl;
						nextState.states[prlIt->unitIndex].pickId=0;
						}
					pickRecords.removeEntry(prIt);
					}
				else
					{
					// DEBUGGING
					// std::cout<<"No pick record for ID "<<uiIt->pickId<<std::endl;
					}
				
				break;
				}
			
			case UIRequest::SAVE_STATE:
				{
				try
					{
					/* Write the current simulation state to the given file: */
					save(nextState,*uiIt->file);
					}
				catch(const std::runtime_error& err)
					{
					/* Show an error message: */
					Misc::formattedUserError("Simulation::save: Caught exception %s",err.what());
					}
				
				/* Call the complete callback if one is given: */
				if(uiIt->saveCompleteCallback!=0)
					(*uiIt->saveCompleteCallback)(*uiIt->file);
				
				break;
				}
			
			case UIRequest::LOAD_STATE:
				{
				try
					{
					/* Read the current simulation state from the given file: */
					load(*uiIt->file,nextState);
					
					/* Invalidate all picks: */
					pickRecords.clear();
					
					/* Validate the session state: */
					sessionId=uiIt->loadSessionId;
					
					/* Call the session changed callback if one is set: */
					if(sessionChangedCallback!=0)
						sessionChangedCallback(sessionId,sessionChangedCallbackData);
					}
				catch(const std::runtime_error& err)
					{
					/* Show an error message: */
					Misc::formattedUserError("Simulation::load: Caught exception %s",err.what());
					}
				
				break;
				}
			default:
				/* Ignore an invalid request: */
				;
			}
		}
	
	/* Update bonds between units: */
	updateBonds(Size(nextState.states.size()),nextState.states.data());
	
	// DEBUGGING
	// grid.check(nextState.numUnits,nextState.states);
	
	/* Post the updated state slot: */
	nextState.sessionId=sessionId;
	mostRecentStates=&nextState;
	unitStates.postNewValue();
	}
