/***********************************************************************
NCKClient - Client to run a multi-user version of the Nanotech
Construction Kit.
Copyright (c) 2017-2023 Oliver Kreylos

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

#include "NCKClient.h"

#include <stdexcept>
#include <Misc/SizedTypes.h>
#include <Misc/MessageLogger.h>
#include <Misc/Marshaller.h>
#include <Threads/WorkerPool.h>
#include <Collaboration2/DataType.icpp>
#include <Collaboration2/MessageReader.h>
#include <Collaboration2/MessageContinuation.h>
#include <Collaboration2/NonBlockSocket.h>
#include <Collaboration2/Client.h>
#include <Collaboration2/Plugins/MetadosisClient.h>

#include "IO.h"

// DEBUGGING
#include <iostream>

namespace Collab {

namespace Plugins {

/**************************
Methods of class NCKClient:
**************************/

void NCKClient::sessionInvalidNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Invalidate the session: */
	sessionId=0;
	}

MessageContinuation* NCKClient::sessionUpdateNotificationCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	NonBlockSocket& socket=client->getSocket();
	
	/* Check if this is the start of a new message: */
	if(continuation==0)
		{
		/* Read the new session ID, which invalidates the current session: */
		protocolTypes.read(socket,sessionIdType,&sessionId);
		
		/* Read the new domain: */
		protocolTypes.read(socket,boxType,&domain);
		
		/* Prepare to read into the unit type list: */
		continuation=protocolTypes.prepareReading(unitTypeListType,&unitTypes);
		}
	
	/* Continue reading the unit type list and check whether it is complete: */
	if(protocolTypes.continueReading(socket,continuation))
		{
		/* Call the session changed callback if one is set: */
		if(sessionChangedCallback!=0)
			sessionChangedCallback(sessionId,sessionChangedCallbackData);
		
		/* Delete the continuation object: */
		delete continuation;
		continuation=0;
		}
	
	return continuation;
	}

void NCKClient::setParametersNotificationCallback(unsigned int messageId,MessageReader& message)
	{
	/* Read the new simulation parameters: */
	parameters.read(message);
	
	/* Call the parameters changed callback if one was set: */
	if(parametersChangedCallback!=0)
		parametersChangedCallback(parameters,parametersChangedCallbackData);
	}

MessageContinuation* NCKClient::simulationUpdateNotificationCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	NonBlockSocket& socket=client->getSocket();
	
	/* Check if this is the start of a new message: */
	if(continuation==0)
		{
		/* Prepare a new unit state array: */
		ReducedUnitStateArray& nextUnitStates=unitStates.startNewValue();
		
		/* Prepare to read into the new unit state array: */
		continuation=protocolTypes.prepareReading(serverMessageTypes[SimulationUpdateNotification],&nextUnitStates);
		}
	
	/* Continue reading the message and check whether it is complete: */
	if(protocolTypes.continueReading(socket,continuation))
		{
		/* Push the new unit state array to the front end: */
		unitStates.postNewValue();
		
		/* Call the new data callback if one is set: */
		if(newDataCallback!=0)
			newDataCallback(newDataCallbackData);
		
		/* Delete the continuation object: */
		delete continuation;
		continuation=0;
		}
	
	return continuation;
	}

namespace {

/**************
Helper classes:
**************/

class StateFileSaver:public Threads::WorkerPool::JobFunction
	{
	/* Elements: */
	private:
	MetadosisClient::InStreamPtr instream; // Stream coming from the server
	IO::FilePtr saveFile; // Local file to which to save the server stream
	Misc::Autopointer<SimulationInterface::SaveStateCompleteCallback> completeCallback; // Callback to call when the save operation is complete
	
	/* Constructors and destructors: */
	public:
	StateFileSaver(MetadosisClient::InStream& sInstream,IO::File& sSaveFile,SimulationInterface::SaveStateCompleteCallback* sCompleteCallback)
		:instream(&sInstream),saveFile(&sSaveFile),completeCallback(sCompleteCallback)
		{
		}
	
	/* Methods from class Threads::WorkerPool::JobFunction: */
	virtual void operator()(int parameter) const
		{
		throw std::runtime_error("NCKClient::StateFileSaver::operator(): Cannot call const method");
		}
	virtual void operator()(int parameter)
		{
		/* Copy the incoming stream to the save file: */
		while(true)
			{
			/* Read from the incoming file's buffer: */
			void* buffer;
			size_t bufferSize=instream->readInBuffer(buffer);
			
			/* Bail out if the file has been received completely: */
			if(bufferSize==0)
				break;
			
			/* Write the buffer to the save file: */
			saveFile->writeRaw(buffer,bufferSize);
			}
		
		/* Call the completion callback if there is one: */
		if(completeCallback!=0)
			(*completeCallback)(*saveFile);
		}
	};

}

MessageContinuation* NCKClient::saveStateReplyCallback(unsigned int messageId,MessageContinuation* continuation)
	{
	NonBlockSocket& socket=client->getSocket();
	
	// DEBUGGING
	std::cout<<"NCKClient: Received save state reply from server"<<std::endl;
	
	/* Kick off a background job to save the incoming stream: */
	StateFileSaver* job=new StateFileSaver(*metadosis->acceptInStream(socket.read<MetadosisProtocol::StreamID>()),*saveFile,saveCompleteCallback.getPointer());
	Threads::WorkerPool::submitJob(*job);
	saveFile=0;
	saveCompleteCallback=0;
	
	/* Done with message: */
	return 0;
	}

PickID NCKClient::getPickId(void)
	{
	do
		{
		++lastPickId;
		}
	while(lastPickId==PickID(0));
	
	return lastPickId;
	}

NCKClient::NCKClient(Client* sClient,NewDataCallback sNewDataCallback,void* sNewDataCallbackData)
	:PluginClient(sClient),
	 metadosis(MetadosisClient::requestClient(client)),
	 newDataCallback(sNewDataCallback),newDataCallbackData(sNewDataCallbackData),
	 lastPickId(0)
	{
	}

NCKClient::~NCKClient(void)
	{
	}

const char* NCKClient::getName(void) const
	{
	return protocolName;
	}

unsigned int NCKClient::getVersion(void) const
	{
	return protocolVersion;
	}

unsigned int NCKClient::getNumClientMessages(void) const
	{
	return NumClientMessages;
	}

unsigned int NCKClient::getNumServerMessages(void) const
	{
	return NumServerMessages;
	}

void NCKClient::setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase)
	{
	/* Call the base class method: */
	PluginClient::setMessageBases(newClientMessageBase,newServerMessageBase);
	
	/* Register message handlers: */
	client->setMessageForwarder(serverMessageBase+SessionInvalidNotification,Client::wrapMethod<NCKClient,&NCKClient::sessionInvalidNotificationCallback>,this,0);
	client->setTCPMessageHandler(serverMessageBase+SessionUpdateNotification,Client::wrapMethod<NCKClient,&NCKClient::sessionUpdateNotificationCallback>,this,getServerMsgSize(SessionUpdateNotification));
	client->setMessageForwarder(serverMessageBase+SetParametersNotification,Client::wrapMethod<NCKClient,&NCKClient::setParametersNotificationCallback>,this,getServerMsgSize(SetParametersNotification));
	client->setTCPMessageHandler(serverMessageBase+SimulationUpdateNotification,Client::wrapMethod<NCKClient,&NCKClient::simulationUpdateNotificationCallback>,this,getServerMsgSize(SimulationUpdateNotification));
	client->setTCPMessageHandler(serverMessageBase+SaveStateReply,Client::wrapMethod<NCKClient,&NCKClient::saveStateReplyCallback>,this,getServerMsgSize(SaveStateReply));
	}

void NCKClient::start(void)
	{
	}

const SimulationInterface::Parameters& NCKClient::getParameters(void) const
	{
	return parameters;
	}

void NCKClient::setParameters(const SimulationInterface::Parameters& newParameters)
	{
	/* Update the simulation parameters: */
	parameters=newParameters;
	
	/* Send the updated simulation parameters to the server: */
	queueServerMessage(SetParametersRequest,&parameters);
	}

bool NCKClient::lockNewState(void)
	{
	return unitStates.lockNewValue();
	}

bool NCKClient::isLockedStateValid(void) const
	{
	return unitStates.getLockedValue().sessionId==sessionId;
	}

PickID NCKClient::pick(const Point& pickPosition,Scalar pickRadius,const Rotation& pickOrientation,bool pickConnected)
	{
	/* Send a point-based pick request to the server: */
	PointPickRequestMsg message;
	message.pickId=getPickId();
	message.pickPosition=pickPosition;
	message.pickRadius=pickRadius;
	message.pickOrientation=pickOrientation;
	message.pickConnected=pickConnected;
	queueServerMessage(PointPickRequest,&message);
	
	return message.pickId;
	}

PickID NCKClient::pick(const Point& pickPosition,const Vector& pickDirection,const Rotation& pickOrientation,bool pickConnected)
	{
	/* Send a ray-based pick request to the server: */
	RayPickRequestMsg message;
	message.pickId=getPickId();
	message.pickPosition=pickPosition;
	message.pickDirection=pickDirection;
	message.pickOrientation=pickOrientation;
	message.pickConnected=pickConnected;
	queueServerMessage(RayPickRequest,&message);
	
	return message.pickId;
	}

PickID NCKClient::paste(const Point& newPosition,const Rotation& newOrientation,const Vector& newLinearVelocity,const Vector& newAngularVelocity)
	{
	/* Send a paste unit request to the server: */
	PasteUnitRequestMsg message;
	message.pickId=getPickId();
	message.position=newPosition;
	message.orientation=newOrientation;
	message.linearVelocity=newLinearVelocity;
	message.angularVelocity=newAngularVelocity;
	queueServerMessage(PasteUnitRequest,&message);
	
	return message.pickId;
	}

void NCKClient::create(PickID pickId,UnitTypeID newTypeId,const Point& newPosition,const Rotation& newOrientation,const Vector& newLinearVelocity,const Vector& newAngularVelocity)
	{
	/* Send a create unit request to the server: */
	CreateUnitRequestMsg message;
	message.pickId=pickId;
	message.unitTypeId=newTypeId;
	message.position=newPosition;
	message.orientation=newOrientation;
	message.linearVelocity=newLinearVelocity;
	message.angularVelocity=newAngularVelocity;
	queueServerMessage(CreateUnitRequest,&message);
	}

void NCKClient::setState(PickID pickId,const Point& newPosition,const Rotation& newOrientation,const Vector& newLinearVelocity,const Vector& newAngularVelocity)
	{
	/* Send a set unit state request to the server: */
	SetUnitStateRequestMsg message;
	message.pickId=pickId;
	message.position=newPosition;
	message.orientation=newOrientation;
	message.linearVelocity=newLinearVelocity;
	message.angularVelocity=newAngularVelocity;
	queueServerMessage(SetUnitStateRequest,&message);
	}

void NCKClient::copy(PickID pickId)
	{
	/* Send a copy unit request to the server: */
	queueServerMessage(CopyUnitRequest,&pickId);
	}

void NCKClient::destroy(PickID pickId)
	{
	/* Send a destroy unit request to the server: */
	queueServerMessage(DestroyUnitRequest,&pickId);
	}

void NCKClient::release(PickID pickId)
	{
	/* Send a release request to the server: */
	queueServerMessage(ReleaseRequest,&pickId);
	}

#if 0

namespace {

/**************
Helper classes:
**************/

class StateFileLoader:public Threads::WorkerPool::JobFunction
	{
	/* Elements: */
	private:
	DeiknumiClient& client; // Reference to the Deiknumi client object
	PanelID panelId; // ID of the panel whose contents to replace with the loaded image
	IO::DirectoryPtr directory; // Directory containing the file to be loaded
	std::string fileName; // Name of the file to be loaded
	Images::ImageFileFormat imageFileFormat; // The image file format of the file to be loaded
	Images::BaseImage image; // The loaded image
	
	/* Constructors and destructors: */
	public:
	StateFileLoader(DeiknumiClient& sClient,PanelID sPanelId,IO::Directory& sDirectory,const char* sFileName)
		:client(sClient),panelId(sPanelId),
		 directory(&sDirectory),fileName(sFileName)
		{
		}
	
	/* Methods from class Threads::WorkerPool::JobFunction: */
	virtual void operator()(int parameter) const
		{
		throw std::runtime_error("DeiknumiClient::LocalFileLoader::operator(): Cannot call const method");
		}
	virtual void operator()(int parameter)
		{
		/* Read the selected image file and forward it to the server at the same time: */
		MetadosisClient::ForwardingFilterPtr forwardingFilter=client.metadosis->forwardFile(*directory->openFile(fileName.c_str()));
		
		/* Read the image contained in the file: */
		imageFileFormat=Images::getImageFileFormat(fileName.c_str());
		image=Images::readGenericImageFile(*forwardingFilter,imageFileFormat);
		
		/* Notify the server that the file is underway: */
		
		{
		MessageWriter setPanelContentRequest(SetPanelContentMsg::createMessage(client.clientMessageBase+SetPanelContentRequest));
		setPanelContentRequest.write(ClientID(0));
		setPanelContentRequest.write(panelId);
		setPanelContentRequest.write(Misc::UInt8(imageFileFormat));
		setPanelContentRequest.write(Misc::UInt32(image.getWidth()));
		setPanelContentRequest.write(Misc::UInt32(image.getHeight()));
		setPanelContentRequest.write(forwardingFilter->getStreamId());
		client.client->queueServerMessage(setPanelContentRequest.getBuffer());
		}
		}
	};

}

#endif

void NCKClient::loadState(IO::File& stateFile)
	{
	/* Create a Metadosis outstream: */
	MetadosisClient::OutStreamPtr outStream=metadosis->createOutStream();
	
	/* Send a load state request to the server: */
	MetadosisProtocol::StreamID streamId=outStream->getStreamId();
	queueServerMessage(LoadStateRequest,&streamId);
	
	/* Copy the given state file into the Metadosis outstream: */
	while(true)
		{
		/* Write directly into the outstream's buffer, potentially bypassing the state file's read buffer: */
		void* buffer;
		size_t bufferSize=outStream->writeInBufferPrepare(buffer);
		size_t readSize=stateFile.readUpTo(buffer,bufferSize);
		outStream->writeInBufferFinish(readSize);
		
		/* Bail out if the state file has been read completely: */
		if(readSize==0)
			break;
		}
	}

void NCKClient::saveState(IO::File& stateFile,SimulationInterface::SaveStateCompleteCallback* completeCallback)
	{
	/* Only accept the request if there isn't already is a pending save operation: */
	if(saveFile==0)
		{
		/* Remember the save file and the completion callback: */
		saveFile=&stateFile;
		saveCompleteCallback=completeCallback;
		
		/* Send a save state request to the server: */
		{
		MessageWriter saveStateRequest(MessageBuffer::create(clientMessageBase+SaveStateRequest,0));
		client->queueServerMessage(saveStateRequest.getBuffer());
		}
		
		// DEBUGGING
		std::cout<<"NCKClient: Sent save state request to server"<<std::endl;
		}
	else
		{
		/* Show an error message: */
		Misc::userError("NCKClient::saveState: Saving operation already in progress");
		}
	}

const ReducedUnitStateArray& NCKClient::getLockedState(void) const
	{
	return unitStates.getLockedValue();
	}

}

}
