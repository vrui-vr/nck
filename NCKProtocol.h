/***********************************************************************
NCKProtocol - Classes defining the Nanotech Construction Kit's client-
server protocol.
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

#ifndef NCKPROTOCOL_INCLUDED
#define NCKPROTOCOL_INCLUDED

#include <Misc/SizedTypes.h>
#include <Collaboration2/DataType.h>

#include "Common.h"
#include "IO.h"
#include "SimulationInterface.h"

namespace Collab {

namespace Plugins {

class NCKProtocol
	{
	/* Embedded classes: */
	protected:
	
	/* Protocol message IDs: */
	enum ClientMessages // Enumerated type for protocol message IDs sent by clients
		{
		SetParametersRequest=0,
		PointPickRequest,
		RayPickRequest,
		PasteUnitRequest,
		CreateUnitRequest,
		SetUnitStateRequest,
		CopyUnitRequest,
		DestroyUnitRequest,
		ReleaseRequest,
		LoadStateRequest,
		SaveStateRequest,
		
		NumClientMessages
		};
	
	enum ServerMessages // Enumerated type for protocol message IDs sent by servers
		{
		SessionInvalidNotification=0,
		SessionUpdateNotification,
		SetParametersNotification,
		SimulationUpdateNotification,
		SaveStateReply,
		
		NumServerMessages
		};
	
	/* Protocol message data structure declarations: */
	struct PointPickRequestMsg
		{
		/* Elements: */
		public:
		PickID pickId;
		Point pickPosition;
		Scalar pickRadius;
		Rotation pickOrientation;
		bool pickConnected;
		};
	
	struct RayPickRequestMsg
		{
		/* Elements: */
		public:
		PickID pickId;
		Point pickPosition;
		Vector pickDirection;
		Rotation pickOrientation;
		bool pickConnected;
		};
	
	struct PasteUnitRequestMsg
		{
		/* Elements: */
		public:
		PickID pickId;
		Point position;
		Rotation orientation;
		Vector linearVelocity;
		Vector angularVelocity;
		};
	
	struct CreateUnitRequestMsg
		{
		/* Elements: */
		public:
		PickID pickId;
		UnitTypeID unitTypeId;
		Point position;
		Rotation orientation;
		Vector linearVelocity;
		Vector angularVelocity;
		};
	
	struct SetUnitStateRequestMsg
		{
		/* Elements: */
		public:
		PickID pickId;
		Point position;
		Rotation orientation;
		Vector linearVelocity;
		Vector angularVelocity;
		};
	
	struct SessionUpdateNotificationMsg
		{
		/* Elements: */
		public:
		SessionID sessionId;
		Box domain;
		UnitTypeList unitTypes;
		};
	
	/* Elements: */
	static const char* protocolName;
	static const unsigned int protocolVersion=2U<<16;
	
	/* Protocol data type declarations: */
	DataType protocolTypes; // Definitions of data types used by the NCK protocol
	DataType::TypeID sessionIdType; // Type for session IDs
	DataType::TypeID boxType; // Type for axis-aligned bounding boxes
	DataType::TypeID unitTypeListType; // Type for lists of structural unit type definitions
	DataType::TypeID clientMessageTypes[NumClientMessages]; // Types for message structures sent by clients
	DataType::TypeID serverMessageTypes[NumServerMessages]; // Types for message structures sent by servers
	
	/* Constructors and destructors: */
	NCKProtocol(void); // Creates protocol data types
	
	/* Methods: */
	size_t getClientMsgSize(unsigned int messageId) const // Returns the minimum size of a client protocol message
		{
		return protocolTypes.getMinSize(clientMessageTypes[messageId]);
		}
	size_t getServerMsgSize(unsigned int messageId) const // Returns the minimum size of a server protocol message
		{
		return protocolTypes.getMinSize(serverMessageTypes[messageId]);
		}
	};

}

}

#endif
