/***********************************************************************
NCKServer - Server to run a multi-user version of the Nanotech
Construction Kit.
Copyright (c) 2017-2022 Oliver Kreylos

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

#ifndef NCKSERVER_INCLUDED
#define NCKSERVER_INCLUDED

#include <Misc/HashTable.h>
#include <Threads/MutexCond.h>
#include <Threads/Thread.h>
#include <Threads/EventDispatcher.h>
#include <Collaboration2/PluginServer.h>

#include "Common.h"
#include "NCKProtocol.h"

/* Forward declarations: */
namespace Misc {
class ConfigurationFileSection;
}
namespace Collab {
class MessageContinuation;
namespace Plugins {
class MetadosisServer;
}
}
class Simulation;

namespace Collab {

namespace Plugins {

class NCKServer:public PluginServer,public NCKProtocol
	{
	/* Embedded classes: */
	private:
	class Client:public PluginServer::Client // Class representing a client participating in the NCK protocol
		{
		friend class NCKServer;
		
		/* Embedded classes: */
		private:
		typedef Misc::HashTable<PickID,PickID> PickIDMap; // Type for hash tables mapping client pick IDs to server pick IDs
		
		/* Elements: */
		PickIDMap pickIdMap; // Map from client pick IDs to server pick IDs
		
		/* Constructors and destructors: */
		public:
		Client(void);
		};
	
	/* Elements: */
	MetadosisServer* metadosis; // Pointer to the Metadosis server object
	double simulationUpdateRate; // Rate at which simulation updates are broadcast to clients in Hertz
	Simulation* sim; // The simulation object
	volatile bool keepSimulationThreadRunning; // Flag to shut down the simulation thread
	volatile bool pauseSimulationThread; // Flag to pause the simulation thread while no clients are connected
	volatile bool pauseSimulationThreadAfterIO; // Flag to pause the simulation thread after the I/O operation for which it was woken up is completed
	Threads::MutexCond pauseSimulationThreadCond; // Condition variable to wake up the simulation thread from being paused
	Threads::Thread simulationThread; // Background thread simulating the Jell-O crystal
	Threads::EventDispatcher::ListenerKey sessionChangedSignalKey; // Signal event key to signal that the backend has finished (re-)initializing the session
	Threads::EventDispatcher::ListenerKey sendSimulationUpdateTimerKey; // Timer event key to signal that a simulation update should be broadcast to all clients
	ReducedUnitStateArray reducedStates; // Array holding reduced unit states for network transmission
	
	/* Message marshalling methods: */
	void sendMessage(unsigned int clientId,bool broadcast,unsigned int messageId,const void* messageStructure);
	
	void* simulationThreadMethod(void); // Method running the background simulation thread
	static void sessionChangedCallback(SessionID sessionId,void* userData);
	void frontendSessionChangedCallback(Threads::EventDispatcher::SignalEvent& event);
	void sendSimulationUpdateCallback(Threads::EventDispatcher::TimerEvent& event);
	MessageContinuation* setParametersRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* pointPickRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* rayPickRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* pasteUnitRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* createUnitRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* setUnitStateRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* copyUnitRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* destroyUnitRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* releaseRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* loadStateRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	MessageContinuation* saveStateRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation);
	void setUpdateRateCommandCallback(const char* argumentBegin,const char* argumentEnd);
	void loadFileCommandCallback(const char* argumentBegin,const char* argumentEnd);
	void saveFileCommandCallback(const char* argumentBegin,const char* argumentEnd);
	
	/* Constructors and destructors: */
	public:
	NCKServer(Server* sServer);
	virtual ~NCKServer(void);
	
	/* Methods from class PluginServer: */
	virtual const char* getName(void) const;
	virtual unsigned int getVersion(void) const;
	virtual unsigned int getNumClientMessages(void) const;
	virtual unsigned int getNumServerMessages(void) const;
	virtual void setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase);
	virtual void start(void);
	virtual void clientConnected(unsigned int clientId);
	virtual void clientDisconnected(unsigned int clientId);
	};

}

}

#endif
