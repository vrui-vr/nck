/***********************************************************************
NCKServer - Server to run a multi-user version of the Nanotech
Construction Kit.
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

#include "NCKServer.h"

#include <unistd.h>
#include <iostream>
#include <Misc/StandardValueCoders.h>
#include <Misc/ConfigurationFile.h>
#include <Misc/MessageLogger.h>
#include <Misc/Marshaller.h>
#include <Misc/CommandDispatcher.h>
#include <IO/File.h>
#include <IO/OpenFile.h>
#include <Realtime/Time.h>
#include <Geometry/GeometryValueCoders.h>
#include <Collaboration2/DataType.icpp>
#include <Collaboration2/MessageWriter.h>
#include <Collaboration2/NonBlockSocket.h>
#include <Collaboration2/Server.h>
#include <Collaboration2/Plugins/MetadosisServer.h>

#include "IO.h"
#include "Simulation.h"

#include "Config.h"

namespace Collab {

namespace Plugins {

/**********************************
Methods of class NCKServer::Client:
**********************************/

NCKServer::Client::Client(void)
	:pickIdMap(17)
	{
	}

/**************************
Methods of class NCKServer:
**************************/

void NCKServer::sendMessage(unsigned int clientId,bool broadcast,unsigned int messageId,const void* messageStructure)
	{
	/* Create a message writer: */
	MessageWriter message(MessageBuffer::create(serverMessageBase+messageId,protocolTypes.calcSize(serverMessageTypes[messageId],messageStructure)));
	
	/* Write the message structure into the message: */
	protocolTypes.write(serverMessageTypes[messageId],messageStructure,message);
	
	/* Broadcast or send the message: */
	if(broadcast)
		broadcastMessage(clientId,message.getBuffer());
	else
		server->queueMessage(clientId,message.getBuffer());
	}

void* NCKServer::simulationThreadMethod(void)
	{
	/* Run the simulation thread until told to shut down: */
	Realtime::TimePointMonotonic timer;
	while(true)
		{
		/* Check if the simulation thread should be paused: */
		{
		Threads::MutexCond::Lock pauseSimulationThreadLock(pauseSimulationThreadCond);
		
		/* Check if the simulation thread should go back to sleep after having been woken up for an I/O operation: */
		if(pauseSimulationThreadAfterIO)
			{
			pauseSimulationThread=true;
			pauseSimulationThreadAfterIO=false;
			}
		
		/* Sleep until woken up: */
		while(pauseSimulationThread)
			{
			pauseSimulationThreadCond.wait(pauseSimulationThreadLock);
			
			/* Reset the simulation timers: */
			timer.set();
			}
		}
		
		/* Bail out if shutting down: */
		if(!keepSimulationThreadRunning)
			break;
		
		/* Update the simulation to the current time: */
		Scalar deltaT(double(timer.setAndDiff()));
		sim->advance(deltaT);
		
		/* Sleep until at least the minimum simulation interval has passed: */
		Realtime::TimePointMonotonic::sleep(timer+Realtime::TimeVector(0,1000000)); // 1ms minimum update interval
		}
	
	return 0;
	}

void NCKServer::sessionChangedCallback(SessionID sessionId,void* userData)
	{
	/* Notify the state machine that the session became valid: */
	NCKServer* thisPtr=static_cast<NCKServer*>(userData);
	thisPtr->server->getDispatcher().signal(thisPtr->sessionChangedSignalKey,0);
	}

void NCKServer::frontendSessionChangedCallback(Threads::EventDispatcher::SignalEvent& event)
	{
	/* Broadcast a session update notification to all connected clients: */
	SessionUpdateNotificationMsg message;
	message.sessionId=sim->getSessionId();
	message.domain=sim->getDomain();
	message.unitTypes=sim->getUnitTypes();
	sendMessage(0,true,SessionUpdateNotification,&message);
	}

void NCKServer::sendSimulationUpdateCallback(Threads::EventDispatcher::TimerEvent& event)
	{
	/* Check if there is a new valid simulation state: */
	if(sim->lockNewState()&&sim->isLockedStateValid())
		{
		/* Reduce the locked simulation state for network transmission: */
		const UnitStateArray& states=sim->getLockedState();
		reducedStates.sessionId=states.sessionId;
		reducedStates.timeStamp=states.timeStamp;
		reducedStates.states.clear();
		reducedStates.states.reserve(states.states.size());
		ReducedUnitState r;
		for(UnitStateArray::UnitStateList::const_iterator sIt=states.states.begin();sIt!=states.states.end();++sIt)
			{
			r.set(*sIt);
			reducedStates.states.push_back(r);
			}
		
		/* Broadcast a simulation state update notification to all connected clients: */
		sendMessage(0,true,SimulationUpdateNotification,&reducedStates);
		}
	}

MessageContinuation* NCKServer::setParametersRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object and its TCP socket: */
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	
	/* Read the new simulation parameters and update the simulation: */
	SimulationInterface::Parameters newParameters;
	protocolTypes.read(socket,clientMessageTypes[SetParametersRequest],&newParameters);
	sim->setParameters(newParameters);
	
	/* Forward the new simulation parameters to all other connected clients: */
	sendMessage(clientId,true,SetParametersNotification,&newParameters);
	
	/* Done with message: */
	return 0;
	}

MessageContinuation* NCKServer::pointPickRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object, its TCP socket, and the NCK client object: */
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* nckClient=client->getPlugin<Client>(pluginIndex);
	
	/* Read the request message: */
	PointPickRequestMsg message;
	protocolTypes.read(socket,clientMessageTypes[PointPickRequest],&message);
	
	/* Forward the request to the simulation: */
	PickID serverPickId=sim->pick(message.pickPosition,message.pickRadius,message.pickOrientation,message.pickConnected);
	
	/* Enter the pick ID pair into the client's active pick map: */
	nckClient->pickIdMap[message.pickId]=serverPickId;
	
	/* Done with message: */
	return 0;
	}

MessageContinuation* NCKServer::rayPickRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object, its TCP socket, and the NCK client object: */
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* nckClient=client->getPlugin<Client>(pluginIndex);
	
	/* Read the request message: */
	RayPickRequestMsg message;
	protocolTypes.read(socket,clientMessageTypes[RayPickRequest],&message);
	
	/* Forward the request to the simulation: */
	PickID serverPickId=sim->pick(message.pickPosition,message.pickDirection,message.pickOrientation,message.pickConnected);
	
	/* Enter the pick ID pair into the client's active pick map: */
	nckClient->pickIdMap[message.pickId]=serverPickId;
	
	/* Done with message: */
	return 0;
	}

MessageContinuation* NCKServer::pasteUnitRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object, its TCP socket, and the NCK client object: */
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* nckClient=client->getPlugin<Client>(pluginIndex);
	
	/* Read the request message: */
	PasteUnitRequestMsg message;
	protocolTypes.read(socket,clientMessageTypes[PasteUnitRequest],&message);
	
	/* Forward the request to the simulation: */
	PickID serverPickId=sim->paste(message.position,message.orientation,message.linearVelocity,message.angularVelocity);
	
	/* Enter the pick ID pair into the client's active pick map: */
	nckClient->pickIdMap[message.pickId]=serverPickId;
	
	/* Done with message: */
	return 0;
	}

MessageContinuation* NCKServer::createUnitRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object, its TCP socket, and the NCK client object: */
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* nckClient=client->getPlugin<Client>(pluginIndex);
	
	/* Read the request message: */
	CreateUnitRequestMsg message;
	protocolTypes.read(socket,clientMessageTypes[CreateUnitRequest],&message);
	
	/* Forward the request to the simulation: */
	PickID serverPickId=nckClient->pickIdMap.getEntry(message.pickId).getDest();
	sim->create(serverPickId,message.unitTypeId,message.position,message.orientation,message.linearVelocity,message.angularVelocity);
	
	/* Done with message: */
	return 0;
	}

MessageContinuation* NCKServer::setUnitStateRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object, its TCP socket, and the NCK client object: */
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* nckClient=client->getPlugin<Client>(pluginIndex);
	
	/* Read the request message: */
	SetUnitStateRequestMsg message;
	protocolTypes.read(socket,clientMessageTypes[SetUnitStateRequest],&message);
	
	/* Forward the request to the simulation: */
	PickID serverPickId=nckClient->pickIdMap.getEntry(message.pickId).getDest();
	sim->setState(serverPickId,message.position,message.orientation,message.linearVelocity,message.angularVelocity);
	
	/* Done with message: */
	return 0;
	}

MessageContinuation* NCKServer::copyUnitRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object, its TCP socket, and the NCK client object: */
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* nckClient=client->getPlugin<Client>(pluginIndex);
	
	/* Read the request message: */
	PickID clientPickId;
	protocolTypes.read(socket,clientMessageTypes[CopyUnitRequest],&clientPickId);
	
	/* Forward the request to the simulation: */
	PickID serverPickId=nckClient->pickIdMap.getEntry(clientPickId).getDest();
	sim->copy(serverPickId);
	
	/* Done with message: */
	return 0;
	}

MessageContinuation* NCKServer::destroyUnitRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object, its TCP socket, and the NCK client object: */
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* nckClient=client->getPlugin<Client>(pluginIndex);
	
	/* Read the request message: */
	PickID clientPickId;
	protocolTypes.read(socket,clientMessageTypes[DestroyUnitRequest],&clientPickId);
	
	/* Forward the request to the simulation: */
	PickID serverPickId=nckClient->pickIdMap.getEntry(clientPickId).getDest();
	sim->destroy(serverPickId);
	
	/* Done with message: */
	return 0;
	}

MessageContinuation* NCKServer::releaseRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object, its TCP socket, and the NCK client object: */
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	Client* nckClient=client->getPlugin<Client>(pluginIndex);
	
	/* Read the request message: */
	PickID clientPickId;
	protocolTypes.read(socket,clientMessageTypes[ReleaseRequest],&clientPickId);
	
	/* Find the client pick ID in the pick ID map: */
	Client::PickIDMap::Iterator pimIt=nckClient->pickIdMap.findEntry(clientPickId);
	
	/* Forward the request to the simulation: */
	sim->release(pimIt->getDest());
	
	/* Remove the pick ID from the pick ID map: */
	nckClient->pickIdMap.removeEntry(pimIt);
	
	/* Done with message: */
	return 0;
	}

MessageContinuation* NCKServer::loadStateRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	/* Access the base client state object and its TCP socket: */
	Server::Client* client=server->getClient(clientId);
	NonBlockSocket& socket=client->getSocket();
	
	/* Read the request message: */
	MetadosisProtocol::StreamID streamId;
	protocolTypes.read(socket,clientMessageTypes[LoadStateRequest],&streamId);
	
	/* Ask the simulation to load the incoming stream: */
	sim->loadState(*metadosis->acceptInStream(clientId,streamId));
	
	/* Check if the simulation is currently asleep: */
	{
	Threads::MutexCond::Lock pauseSimulationThreadLock(pauseSimulationThreadCond);
	if(pauseSimulationThread)
		{
		/* Wake up the simulation until the I/O operation is completed: */
		pauseSimulationThread=false;
		pauseSimulationThreadAfterIO=true;
		pauseSimulationThreadCond.signal();
		}
	}
	
	/* Done with message: */
	return 0;
	}

MessageContinuation* NCKServer::saveStateRequestCallback(unsigned int messageId,unsigned int clientId,MessageContinuation* continuation)
	{
	// DEBUGGING
	std::cout<<"NCKServer: Received save state request from client"<<std::endl;
	
	/* Create a Metadosis outstream and schedule it to be forwarded to the requesting client: */
	MetadosisServer::OutStreamPtr stateStream=metadosis->createOutStream();
	MetadosisProtocol::StreamID streamId=metadosis->forwardInStream(clientId,stateStream->getInStream());
	
	/* Notify the client that a state file is coming: */
	sendMessage(clientId,false,SaveStateReply,&streamId);
	
	/* Ask the simulation to save to the outstream: */
	sim->saveState(*stateStream);
	
	/* Check if the simulation is currently asleep: */
	{
	Threads::MutexCond::Lock pauseSimulationThreadLock(pauseSimulationThreadCond);
	if(pauseSimulationThread)
		{
		/* Wake up the simulation until the I/O operation is completed: */
		pauseSimulationThread=false;
		pauseSimulationThreadAfterIO=true;
		pauseSimulationThreadCond.signal();
		}
	}
	
	/* Done with message: */
	return 0;
	}

void NCKServer::setUpdateRateCommandCallback(const char* argumentBegin,const char* argumentEnd)
	{
	/* Read the requested update rate: */
	double updateRate=Misc::ValueCoder<double>::decode(argumentBegin,argumentEnd);
	if(updateRate>0.0)
		{
		/* Set the update rate: */
		simulationUpdateRate=updateRate;
		
		/* Check if there are any clients connected: */
		if(!clients.empty())
			{
			/* There is no API to change a running timer's interval, so we have to remove it and then recreate it: */
			server->getDispatcher().removeTimerEventListener(sendSimulationUpdateTimerKey);
			Threads::EventDispatcher::Time updateInterval(1.0/simulationUpdateRate);
			sendSimulationUpdateTimerKey=server->getDispatcher().addTimerEventListener(Threads::EventDispatcher::Time::now(),updateInterval,Threads::EventDispatcher::wrapMethod<NCKServer,&NCKServer::sendSimulationUpdateCallback>,this);
			}
		}
	else
		std::cout<<"Invalid simulation update rate "<<updateRate<<" requested"<<std::endl;
	}

void NCKServer::loadFileCommandCallback(const char* argumentBegin,const char* argumentEnd)
	{
	/* Open the requested file: */
	std::string fileName(argumentBegin,argumentEnd);
	IO::FilePtr file=IO::openFile(fileName.c_str());
	file->setEndianness(Misc::LittleEndian);
	
	#if 0
	/* Broadcast a session invalid notification to all connected clients: */
	{
	MessageWriter sessionInvalidNotification(MessageBuffer::create(serverMessageBase+SessionInvalidNotification,0));
	broadcastMessage(0,sessionInvalidNotification.getBuffer());
	}
	#endif
	
	/* Ask the simulation to load the requested file: */
	sim->loadState(*file);
	
	/* Check if the simulation is currently asleep: */
	{
	Threads::MutexCond::Lock pauseSimulationThreadLock(pauseSimulationThreadCond);
	if(pauseSimulationThread)
		{
		/* Wake up the simulation until the I/O operation is completed: */
		pauseSimulationThread=false;
		pauseSimulationThreadAfterIO=true;
		pauseSimulationThreadCond.signal();
		}
	}
	}

void NCKServer::saveFileCommandCallback(const char* argumentBegin,const char* argumentEnd)
	{
	/* Open the requested file: */
	std::string fileName(argumentBegin,argumentEnd);
	IO::FilePtr file=IO::openFile(fileName.c_str(),IO::File::WriteOnly);
	file->setEndianness(Misc::LittleEndian);
	
	/* Ask the simulation to save current state to the requested file: */
	sim->saveState(*file);
	
	/* Check if the simulation is currently asleep: */
	{
	Threads::MutexCond::Lock pauseSimulationThreadLock(pauseSimulationThreadCond);
	if(pauseSimulationThread)
		{
		/* Wake up the simulation until the I/O operation is completed: */
		pauseSimulationThread=false;
		pauseSimulationThreadAfterIO=true;
		pauseSimulationThreadCond.signal();
		}
	}
	}

NCKServer::NCKServer(Server* sServer)
	:PluginServer(sServer),
	 metadosis(MetadosisServer::requestServer(server)),
	 simulationUpdateRate(60),
	 sim(0),
	 keepSimulationThreadRunning(false),pauseSimulationThread(true),pauseSimulationThreadAfterIO(false),
	 sessionChangedSignalKey(0),sendSimulationUpdateTimerKey(0)
	{
	/* Depend on Metadosis protocol: */
	metadosis->addDependentPlugin(this);
	
	/* Read server-side configuration: */
	Misc::ConfigurationFileSection serverConfig=server->getPluginConfig(this);
	serverConfig.updateValue("./simulationUpdateRate",simulationUpdateRate);
	Box domain(Point::origin,Point(100,100,100));
	serverConfig.updateValue("./domain",domain);
	
	/* Open the main configuration file: */
	Misc::ConfigurationFile configFile(NCK_CONFIG_ETCDIR "/" NCK_CONFIG_CONFIGFILENAME);
	Misc::ConfigurationFileSection rootSection=configFile.getSection("NewNanotechConstructionKit");
	
	/* Create the simulation object: */
	sim=new Simulation(rootSection,domain);
	
	/* Install a callback to be notified when the simulation session changed: */
	sim->setSessionChangedCallback(&NCKServer::sessionChangedCallback,this);
	
	/* Register a signal with the server's event dispatcher: */
	sessionChangedSignalKey=server->getDispatcher().addSignalListener(Threads::EventDispatcher::wrapMethod<NCKServer,&NCKServer::frontendSessionChangedCallback>,this);
	
	/* Register pipe commands: */
	server->getCommandDispatcher().addCommandCallback("NCK::setUpdateRate",Misc::CommandDispatcher::wrapMethod<NCKServer,&NCKServer::setUpdateRateCommandCallback>,this,"<update rate in Hz>","Sets the rate at which state updates are sent to clients");
	server->getCommandDispatcher().addCommandCallback("NCK::loadFile",Misc::CommandDispatcher::wrapMethod<NCKServer,&NCKServer::loadFileCommandCallback>,this,"<unit file name>","Loads the NCK unit file of the given name");
	server->getCommandDispatcher().addCommandCallback("NCK::saveFile",Misc::CommandDispatcher::wrapMethod<NCKServer,&NCKServer::saveFileCommandCallback>,this,"<unit file name>","Saves the current simulation state to an NCK unit file of the given name");
	}

NCKServer::~NCKServer(void)
	{
	if(!simulationThread.isJoined())
		{
		/* Stop the simulation thread: */
		keepSimulationThreadRunning=false;
		{
		Threads::MutexCond::Lock pauseSimulationThreadLock(pauseSimulationThreadCond);
		pauseSimulationThread=false;
		pauseSimulationThreadCond.signal();
		}
		simulationThread.join();
		}
	
	/* Delete the simulation object: */
	delete sim;
	
	/* Remove all event listeners: */
	server->getDispatcher().removeSignalListener(sessionChangedSignalKey);
	if(sendSimulationUpdateTimerKey!=0)
		server->getDispatcher().removeTimerEventListener(sendSimulationUpdateTimerKey);
	
	/* Remove the pipe command: */
	server->getCommandDispatcher().removeCommandCallback("NCK::setUpdateRate");
	server->getCommandDispatcher().removeCommandCallback("NCK::loadFile");
	server->getCommandDispatcher().removeCommandCallback("NCK::saveFile");
	
	/* Release dependence on Metadosis protocol: */
	metadosis->removeDependentPlugin(this);
	}

const char* NCKServer::getName(void) const
	{
	return protocolName;
	}

unsigned int NCKServer::getVersion(void) const
	{
	return protocolVersion;
	}

unsigned int NCKServer::getNumClientMessages(void) const
	{
	return NumClientMessages;
	}

unsigned int NCKServer::getNumServerMessages(void) const
	{
	return NumServerMessages;
	}

void NCKServer::setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase)
	{
	/* Call the base class method: */
	PluginServer::setMessageBases(newClientMessageBase,newServerMessageBase);
	
	/* Register message handlers: */
	server->setMessageHandler(clientMessageBase+SetParametersRequest,Server::wrapMethod<NCKServer,&NCKServer::setParametersRequestCallback>,this,getClientMsgSize(SetParametersRequest));
	server->setMessageHandler(clientMessageBase+PointPickRequest,Server::wrapMethod<NCKServer,&NCKServer::pointPickRequestCallback>,this,getClientMsgSize(PointPickRequest));
	server->setMessageHandler(clientMessageBase+RayPickRequest,Server::wrapMethod<NCKServer,&NCKServer::rayPickRequestCallback>,this,getClientMsgSize(RayPickRequest));
	server->setMessageHandler(clientMessageBase+PasteUnitRequest,Server::wrapMethod<NCKServer,&NCKServer::pasteUnitRequestCallback>,this,getClientMsgSize(PasteUnitRequest));
	server->setMessageHandler(clientMessageBase+CreateUnitRequest,Server::wrapMethod<NCKServer,&NCKServer::createUnitRequestCallback>,this,getClientMsgSize(CreateUnitRequest));
	server->setMessageHandler(clientMessageBase+SetUnitStateRequest,Server::wrapMethod<NCKServer,&NCKServer::setUnitStateRequestCallback>,this,getClientMsgSize(SetUnitStateRequest));
	server->setMessageHandler(clientMessageBase+CopyUnitRequest,Server::wrapMethod<NCKServer,&NCKServer::copyUnitRequestCallback>,this,getClientMsgSize(CopyUnitRequest));
	server->setMessageHandler(clientMessageBase+DestroyUnitRequest,Server::wrapMethod<NCKServer,&NCKServer::destroyUnitRequestCallback>,this,getClientMsgSize(DestroyUnitRequest));
	server->setMessageHandler(clientMessageBase+ReleaseRequest,Server::wrapMethod<NCKServer,&NCKServer::releaseRequestCallback>,this,getClientMsgSize(ReleaseRequest));
	server->setMessageHandler(clientMessageBase+LoadStateRequest,Server::wrapMethod<NCKServer,&NCKServer::loadStateRequestCallback>,this,getClientMsgSize(LoadStateRequest));
	server->setMessageHandler(clientMessageBase+SaveStateRequest,Server::wrapMethod<NCKServer,&NCKServer::saveStateRequestCallback>,this,getClientMsgSize(SaveStateRequest));
	}

void NCKServer::start(void)
	{
	/* Start the simulation thread in paused mode: */
	keepSimulationThreadRunning=true;
	simulationThread.start(this,&NCKServer::simulationThreadMethod);
	}

void NCKServer::clientConnected(unsigned int clientId)
	{
	/* Check if this is the first client to connect: */
	if(clients.empty())
		{
		/* Unpause the simulation thread: */
		Misc::logNote("NCK: Unpausing simulation thread");
		{
		Threads::MutexCond::Lock pauseSimulationThreadLock(pauseSimulationThreadCond);
		pauseSimulationThread=false;
		pauseSimulationThreadCond.signal();
		}
		
		/* Add an event listener for regular simulation state update messages: */
		Threads::EventDispatcher::Time updateInterval(1.0/simulationUpdateRate);
		sendSimulationUpdateTimerKey=server->getDispatcher().addTimerEventListener(Threads::EventDispatcher::Time::now(),updateInterval,Threads::EventDispatcher::wrapMethod<NCKServer,&NCKServer::sendSimulationUpdateCallback>,this);
		}
	
	/* Add the new client to the list of clients using this protocol: */
	addClientToList(clientId);
	
	/* Associate a client structure with the new client: */
	Server::Client* client=server->getClient(clientId);
	client->setPlugin(pluginIndex,new Client);
	
	/* Send the current simulation parameters to the new client: */
	sendMessage(clientId,false,SetParametersNotification,&sim->getParameters());
	
	/* Check if the simulation session is valid: */
	if(sim->isSessionValid())
		{
		/* Send a session update notification to the new client: */
		SessionUpdateNotificationMsg message;
		message.sessionId=sim->getSessionId();
		message.domain=sim->getDomain();
		message.unitTypes=sim->getUnitTypes();
		sendMessage(clientId,false,SessionUpdateNotification,&message);
		}
	}

void NCKServer::clientDisconnected(unsigned int clientId)
	{
	/* Remove the client from the list of clients using this protocol: */
	removeClientFromList(clientId);
	
	/* Stop the client's remaining active drag operations: */
	Server::Client* client=server->getClient(clientId);
	Client* nckClient=client->getPlugin<Client>(pluginIndex);
	for(Client::PickIDMap::Iterator pimIt=nckClient->pickIdMap.begin();!pimIt.isFinished();++pimIt)
		sim->release(pimIt->getDest());
	
	/* Check if this is the last client to disconnect: */
	if(clients.empty())
		{
		/* Pause the simulation thread: */
		Misc::logNote("NCK: Pausing simulation thread");
		{
		Threads::MutexCond::Lock pauseSimulationThreadLock(pauseSimulationThreadCond);
		pauseSimulationThread=true;
		}
		
		/* Remove the simulation update event listener: */
		server->getDispatcher().removeTimerEventListener(sendSimulationUpdateTimerKey);
		}
	}

/***********************
DSO loader entry points:
***********************/

extern "C" {

PluginServer* createObject(PluginServerLoader& objectLoader,Server* server)
	{
	return new NCKServer(server);
	}

void destroyObject(PluginServer* object)
	{
	delete object;
	}

}

}

}
