/***********************************************************************
ClusterSlaveSimulation - Proxy for local simulations run on the master
node of a cluster.
Copyright (c) 2019-2022 Oliver Kreylos

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

#ifndef CLUSTERSLAVESIMULATION_INCLUDED
#define CLUSTERSLAVESIMULATION_INCLUDED

#include <Threads/Thread.h>
#include <Threads/TripleBuffer.h>

#include "Common.h"
#include "IndirectSimulationInterface.h"

/* Forward declarations: */
namespace Cluster {
class MulticastPipe;
}

class ClusterSlaveSimulation:public IndirectSimulationInterface
	{
	/* Embedded classes: */
	public:
	enum MessageTypes // Enumerated type for types of messages received from the master
		{
		SetParameters=0,
		UpdateSession,
		UpdateSimulation,
		Shutdown
		};
	
	/* Elements: */
	private:
	Cluster::MulticastPipe* clusterPipe; // Pipe connected to the cluster's master node
	Parameters parameters; // Simulation parameters
	Threads::Thread communicationThread; // Thread receiving messages from the cluster master
	Threads::TripleBuffer<ReducedUnitStateArray> unitStates; // Triple buffer of structural unit states received from the cluster master
	
	/* Private methods: */
	void* communicationThreadMethod(void); // Method receiving messages from the server
	
	/* Constructors and destructors: */
	public:
	ClusterSlaveSimulation(Cluster::MulticastPipe* sClusterPipe); // Creates a proxy for the given cluster pipe
	virtual ~ClusterSlaveSimulation(void);
	
	/* Methods from class SimulationInterface: */
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
	
	/* Methods from class IndirectSimulationInterface: */
	virtual const ReducedUnitStateArray& getLockedState(void) const;
	};

#endif
