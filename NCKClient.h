/***********************************************************************
NCKClient - Client to run a multi-user version of the Nanotech
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

#ifndef NCKCLIENT_INCLUDED
#define NCKCLIENT_INCLUDED

#include <Misc/Autopointer.h>
#include <Threads/TripleBuffer.h>
#include <IO/File.h>
#include <Collaboration2/MessageBuffer.h>
#include <Collaboration2/MessageWriter.h>
#include <Collaboration2/Client.h>
#include <Collaboration2/PluginClient.h>

#include "Common.h"
#include "IndirectSimulationInterface.h"
#include "NCKProtocol.h"

/* Forward declarations: */
namespace Collab {
class MessageReader;
class MessageContinuation;
namespace Plugins {
class MetadosisClient;
}
}

namespace Collab {

namespace Plugins {

class NCKClient:public PluginClient,public NCKProtocol,public IndirectSimulationInterface
	{
	/* Embedded classes: */
	public:
	typedef void (*NewDataCallback)(void* userData); // Type for callbacks when new data arrived from the server
	
	/* Elements: */
	private:
	MetadosisClient* metadosis; // Pointer to the Metadosis protocol client
	Parameters parameters; // Current simulation parameters
	Threads::TripleBuffer<ReducedUnitStateArray> unitStates; // Triple buffer of structural unit states received from the server
	NewDataCallback newDataCallback; // Function called when new data arrives from the server
	void* newDataCallbackData; // Opaque data pointer passed to the new data callback
	PickID lastPickId; // ID assigned to the most recent pick request
	IO::FilePtr saveFile; // File to which to save incoming server simulation state
	Misc::Autopointer<SaveStateCompleteCallback> saveCompleteCallback; // Callback to be called when server simulation state has been saved
	
	/* Private methods: */
	void queueServerMessage(unsigned int messageId,const void* messageStructure) // Writes a message structure for the given message ID into a buffer and queues to send it to the server
		{
		/* Create a message writer: */
		MessageWriter message(MessageBuffer::create(clientMessageBase+messageId,protocolTypes.calcSize(clientMessageTypes[messageId],messageStructure)));
		
		/* Write the message structure into the message: */
		protocolTypes.write(clientMessageTypes[messageId],messageStructure,message);
		
		/* Queue the message for delivery to the server: */
		client->queueServerMessage(message.getBuffer());
		}
	void sessionInvalidNotificationCallback(unsigned int messageId,MessageReader& message);
	MessageContinuation* sessionUpdateNotificationCallback(unsigned int messageId,MessageContinuation* continuation);
	void setParametersNotificationCallback(unsigned int messageId,MessageReader& message);
	MessageContinuation* simulationUpdateNotificationCallback(unsigned int messageId,MessageContinuation* continuation);
	MessageContinuation* saveStateReplyCallback(unsigned int messageId,MessageContinuation* continuation);
	PickID getPickId(void); // Returns an unused pick ID
	
	/* Constructors and destructors: */
	public:
	NCKClient(Client* sClient,NewDataCallback sNewDataCallback,void* sNewDataCallbackData); // Creates a remote simulation client
	virtual ~NCKClient(void);
	
	/* Methods from class PluginClient: */
	virtual const char* getName(void) const;
	virtual unsigned int getVersion(void) const;
	virtual unsigned int getNumClientMessages(void) const;
	virtual unsigned int getNumServerMessages(void) const;
	virtual void setMessageBases(unsigned int newClientMessageBase,unsigned int newServerMessageBase);
	virtual void start(void);
	
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

}

}

#endif
