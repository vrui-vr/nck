/***********************************************************************
SimulationInterface - Base class to interact with local or remote
simulations of the interactions of a set of structural units.
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

#ifndef SIMULATIONINTERFACE_INCLUDED
#define SIMULATIONINTERFACE_INCLUDED

#include <Threads/FunctionCalls.h>

#include "Common.h"

/* Forward declarations: */
namespace IO {
class File;
}

class SimulationInterface
	{
	/* Embedded classes: */
	public:
	struct Parameters // Structure holding simulation parameters
		{
		/* Elements: */
		public:
		Scalar linearDampening; // Dampening factor for linear velocity differences
		Scalar angularDampening; // Dampening factor for angular velocity differences
		Scalar attenuation; // Overall linear and angular velocity attenuation factor, per second
		Scalar timeFactor; // Speed-up factor from real time to simulation time
		
		/* Constructors and destructors: */
		public:
		Parameters(void) // Creates default parameters
			:linearDampening(0),angularDampening(0),
			 attenuation(1),timeFactor(1)
			{
			}
		Parameters(Scalar sAttenuation,Scalar sTimeFactor) // Element-wise constructor
			:linearDampening(0),angularDampening(0),
			 attenuation(sAttenuation),timeFactor(sTimeFactor)
			{
			}
		
		/* Methods: */
		template <class DataSinkParam>
		void write(DataSinkParam& sink) const // Writes simulation parameters to the given binary sink
			{
			sink.write(linearDampening);
			sink.write(angularDampening);
			sink.write(attenuation);
			sink.write(timeFactor);
			}
		template <class DataSourceParam>
		Parameters& read(DataSourceParam& source) // Reads simulation parameters from the given binary source
			{
			linearDampening=source.template read<Scalar>();
			angularDampening=source.template read<Scalar>();
			attenuation=source.template read<Scalar>();
			timeFactor=source.template read<Scalar>();
			return *this;
			}
		};
	
	typedef void (*ParametersChangedCallback)(const Parameters& newParameters,void* userData); // Type for callbacks called when simulation parameters changed asynchronously
	typedef void (*SessionChangedCallback)(SessionID newSessionId,void* userData); // Type for callbacks called when the simulation session becomes valid
	typedef Threads::FunctionCall<IO::File&> SaveStateCompleteCallback; // Type for callbacks called when simulation state has been saved to a file
	
	/* Elements: */
	protected:
	SessionID sessionId; // Session ID associated with the current list of unit types and simulation domain
	UnitTypeList unitTypes; // List of unit types
	Box domain; // Simulation domain
	ParametersChangedCallback parametersChangedCallback; // Callback to be called when simulation parameters changed asynchronously
	void* parametersChangedCallbackData; // Opaque pointer passed to parameters changed callback
	SessionChangedCallback sessionChangedCallback; // Callback to be called when the simulation session becomes valid
	void* sessionChangedCallbackData; // Opaque pointer passed to session changed callback
	
	/* Constructors and destructors: */
	public:
	SimulationInterface(void) // Creates invalid simulation interface
		:sessionId(0),
		 parametersChangedCallback(0),parametersChangedCallbackData(0),
		 sessionChangedCallback(0),sessionChangedCallbackData(0)
		{
		}
	virtual ~SimulationInterface(void)
		{
		}
	
	/* Methods: */
	SessionID getSessionId(void) const // Returns the current session ID
		{
		return sessionId;
		}
	const UnitTypeList& getUnitTypes(void) const // Returns the list of unit types
		{
		return unitTypes;
		}
	const Box& getDomain(void) const // Returns the simulation domain
		{
		return domain;
		}
	virtual bool isSessionValid(void) const // Returns true if the session is valid
		{
		return sessionId!=SessionID(0);
		}
	virtual bool lockNewState(void) =0; // Locks most recent simulation state; returns true if state is new
	virtual bool isLockedStateValid(void) const =0; // Returns true if the most recent simulation state matches the current session
	
	void setParametersChangedCallback(ParametersChangedCallback newParametersChangedCallback,void* newParametersChangedCallbackData) // Sets the callback to be called when simulation parameters change asynchronously
		{
		/* Set the callback: */
		parametersChangedCallback=newParametersChangedCallback;
		parametersChangedCallbackData=newParametersChangedCallbackData;
		}
	virtual const Parameters& getParameters(void) const =0; // Returns the current simulation parameters
	virtual void setParameters(const Parameters& newParameters) =0; // Sets new simulation parameters
	
	void setSessionChangedCallback(SessionChangedCallback newSessionChangedCallback,void* newSessionChangedCallbackData) // Sets the callback to be called when the simulation session becomes valid
		{
		/* Set the callback: */
		sessionChangedCallback=newSessionChangedCallback;
		sessionChangedCallbackData=newSessionChangedCallbackData;
		}
	
	/* UI request methods: */
	virtual PickID pick(const Point& pickPosition,Scalar pickRadius,const Rotation& pickOrientation,bool pickConnected) =0; // Picks a unit or complex of connected units at the given position with the given pick radius and returns a pick ID
	virtual PickID pick(const Point& pickPosition,const Vector& pickDirection,const Rotation& pickOrientation,bool pickConnected) =0; // Picks a unit or complex of connected units  along the given ray and returns a pick ID
	virtual PickID paste(const Point& newPosition,const Rotation& newOrientation,const Vector& newLinearVelocity,const Vector& newAngularVelocity) =0; // Adds contents of current copy buffer to simulation state relative to the given position and orientation and returns a pick ID
	virtual void create(PickID pickId,UnitTypeID newTypeId,const Point& newPosition,const Rotation& newOrientation,const Vector& newLinearVelocity,const Vector& newAngularVelocity) =0; // Creates a new unit of the given type with the given initial state if the given pick ID is invalid
	virtual void setState(PickID pickId,const Point& newPosition,const Rotation& newOrientation,const Vector& newLinearVelocity,const Vector& newAngularVelocity) =0; // Sets the state of a picked unit
	virtual void copy(PickID pickId) =0; // Copies the picked unit to the copy buffer and destroys previous content
	virtual void destroy(PickID pickId) =0; // Destroys a picked unit
	virtual void release(PickID pickId) =0; // Releases a picked unit
	virtual void loadState(IO::File& stateFile) =0; // Requests to load the given state file and replace the current simulation state; calls a session changed callback when finished
	virtual void saveState(IO::File& stateFile,SaveStateCompleteCallback* completeCallback =0) =0; // Requests to save the current simulation state to the given file; calls optional callback with reference to file when done
	};

#endif
