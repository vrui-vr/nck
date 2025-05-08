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

#include "ClusterSlaveSimulation.h"

#include <vector>
#include <Misc/SizedTypes.h>
#include <Misc/Marshaller.h>
#include <Cluster/MulticastPipe.h>

#include "IO.h"

/***************************************
Methods of class ClusterSlaveSimulation:
***************************************/

void* ClusterSlaveSimulation::communicationThreadMethod(void)
	{
	/* Receive and process messages from the master until told to stop: */
	bool keepRunning=true;
	while(keepRunning)
		{
		/* Read the message type and handle the message: */
		switch(clusterPipe->read<Misc::UInt8>())
			{
			case SetParameters:
				/* Read the new simulation parameters: */
				parameters.read(*clusterPipe);
				
				/* Call the parameters changed callback if one was set: */
				if(parametersChangedCallback!=0)
					parametersChangedCallback(parameters,parametersChangedCallbackData);
				
				break;
			
			case UpdateSession:
				/* Read the new session ID, which invalidates the session and blocks access to the unit type array: */
				sessionId=clusterPipe->read<SessionID>();
				
				/* Read the new domain size: */
				Misc::read(*clusterPipe,domain);
				
				/* Read the new list of unit types: */
				Misc::read(*clusterPipe,unitTypes);
				
				/* Call the session changed callback if one is set: */
				if(sessionChangedCallback!=0)
					sessionChangedCallback(sessionId,sessionChangedCallbackData);
				
				break;
			
			case UpdateSimulation:
				{
				/* Read a new reduced unit state array from the master and post it to the main thread: */
				ReducedUnitStateArray& states=unitStates.startNewValue();
				readStateArray(*clusterPipe,states,true);
				unitStates.postNewValue();
				
				break;
				}
			
			case Shutdown:
				keepRunning=false;
				break;
			}
		}
	
	return 0;
	}

ClusterSlaveSimulation::ClusterSlaveSimulation(Cluster::MulticastPipe* sClusterPipe)
	:clusterPipe(sClusterPipe)
	{
	/* Start the communication thread: */
	communicationThread.start(this,&ClusterSlaveSimulation::communicationThreadMethod);
	}

ClusterSlaveSimulation::~ClusterSlaveSimulation(void)
	{
	/* Wait for the communication thread to shut down: */
	communicationThread.join();
	}

const SimulationInterface::Parameters& ClusterSlaveSimulation::getParameters(void) const
	{
	return parameters;
	}

void ClusterSlaveSimulation::setParameters(const SimulationInterface::Parameters& newParameters)
	{
	/* Ignore */
	}

bool ClusterSlaveSimulation::lockNewState(void)
	{
	return unitStates.lockNewValue();
	}

bool ClusterSlaveSimulation::isLockedStateValid(void) const
	{
	return unitStates.getLockedValue().sessionId==sessionId;
	}

PickID ClusterSlaveSimulation::pick(const Point& pickPosition,Scalar pickRadius,const Rotation& pickOrientation,bool pickConnected)
	{
	/* Ignore */
	return 0;
	}

PickID ClusterSlaveSimulation::pick(const Point& pickPosition,const Vector& pickDirection,const Rotation& pickOrientation,bool pickConnected)
	{
	/* Ignore */
	return 0;
	}

PickID ClusterSlaveSimulation::paste(const Point& newPosition,const Rotation& newOrientation,const Vector& newLinearVelocity,const Vector& newAngularVelocity)
	{
	/* Ignore */
	return 0;
	}

void ClusterSlaveSimulation::create(PickID pickId,UnitTypeID newTypeId,const Point& newPosition,const Rotation& newOrientation,const Vector& newLinearVelocity,const Vector& newAngularVelocity)
	{
	/* Ignore */
	}

void ClusterSlaveSimulation::setState(PickID pickId,const Point& newPosition,const Rotation& newOrientation,const Vector& newLinearVelocity,const Vector& newAngularVelocity)
	{
	/* Ignore */
	}

void ClusterSlaveSimulation::copy(PickID pickId)
	{
	/* Ignore */
	}

void ClusterSlaveSimulation::destroy(PickID pickId)
	{
	/* Ignore */
	}

void ClusterSlaveSimulation::release(PickID pickId)
	{
	/* Ignore */
	}

void ClusterSlaveSimulation::loadState(IO::File& stateFile)
	{
	/* Ignore */
	}

void ClusterSlaveSimulation::saveState(IO::File& stateFile,SimulationInterface::SaveStateCompleteCallback* completeCallback)
	{
	/* Ignore */
	}

const ReducedUnitStateArray& ClusterSlaveSimulation::getLockedState(void) const
	{
	return unitStates.getLockedValue();
	}
