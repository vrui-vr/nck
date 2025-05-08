/***********************************************************************
NCKProtocol - Classes defining the Nanotech Construction Kit's client-
server protocol.
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

#include "NCKProtocol.h"

#include <Collaboration2/DataType.icpp>
#include <Collaboration2/Plugins/MetadosisProtocol.h>

namespace Collab {

namespace Plugins {

/************************************
Static elements of class NCKProtocol:
************************************/

const char* NCKProtocol::protocolName="NCK";

/****************************
Methods of class NCKProtocol:
****************************/

NCKProtocol::NCKProtocol(void)
	{
	/* Create basic protocol types: */
	DataType::TypeID unitTypeIdType=DataType::getAtomicType<UnitTypeID>();
	sessionIdType=DataType::getAtomicType<SessionID>();
	DataType::TypeID indexType=DataType::getAtomicType<Index>();
	// DataType::TypeID sizeType=DataType::getAtomicType<Size>(); // Not currently used in protocol
	DataType::TypeID pickIdType=DataType::getAtomicType<PickID>();
	DataType::TypeID scalarType=DataType::getAtomicType<Scalar>();
	
	/* Geometry types: */
	DataType::TypeID scalar3Type=protocolTypes.createFixedArray(3,scalarType);
	DataType::TypeID pointType=scalar3Type;
	DataType::TypeID vectorType=scalar3Type;
	DataType::TypeID rotationType=protocolTypes.createFixedArray(4,scalarType);
	DataType::TypeID tensorType=protocolTypes.createFixedArray(3,scalar3Type);
	DataType::StructureElement boxElements[]=
		{
		{pointType,offsetof(Box,min)},
		{pointType,offsetof(Box,max)}
		};
	boxType=protocolTypes.createStructure(2,boxElements,sizeof(Box));
	
	/* Class BondSite: */
	DataType::StructureElement bondSiteElements[]=
		{
		{vectorType,offsetof(BondSite,offset)}
		};
	DataType::TypeID bondSiteType=protocolTypes.createStructure(1,bondSiteElements,sizeof(BondSite));
	
	/* Class UnitType: */
	DataType::StructureElement unitTypeElements[]=
		{
		{DataType::String,offsetof(UnitType,name)},
		{scalarType,offsetof(UnitType,radius)},
		{scalarType,offsetof(UnitType,mass)},
		{tensorType,offsetof(UnitType,momentOfInertia)},
		{protocolTypes.createVector(bondSiteType),offsetof(UnitType,bondSites)},
		{protocolTypes.createVector(pointType),offsetof(UnitType,meshVertices)},
		{protocolTypes.createVector(indexType),offsetof(UnitType,meshTriangles)}
		};
	DataType::TypeID unitTypeType=protocolTypes.createStructure(7,unitTypeElements,sizeof(UnitType));
	
	unitTypeListType=protocolTypes.createVector(unitTypeType);
	
	/* Class ReducedUnitState: */
	DataType::StructureElement reducedUnitStateElements[]=
		{
		{unitTypeIdType,offsetof(ReducedUnitState,unitType)},
		{protocolTypes.createFixedArray(3,DataType::getAtomicType<ReducedUnitState::Scalar>()),offsetof(ReducedUnitState,position)},
		{protocolTypes.createFixedArray(4,DataType::getAtomicType<ReducedUnitState::Scalar>()),offsetof(ReducedUnitState,orientation)}
		};
	DataType::TypeID reducedUnitStateType=protocolTypes.createStructure(3,reducedUnitStateElements,sizeof(ReducedUnitState));
	
	/* Class ReducedUnitStateArray: */
	DataType::StructureElement reducedUnitStateArrayElements[]=
		{
		{sessionIdType,offsetof(ReducedUnitStateArray,sessionId)},
		{indexType,offsetof(ReducedUnitStateArray,timeStamp)},
		{protocolTypes.createVector(reducedUnitStateType),offsetof(ReducedUnitStateArray,states)}
		};
	DataType::TypeID reducedUnitStateArrayType=protocolTypes.createStructure(3,reducedUnitStateArrayElements,sizeof(ReducedUnitStateArray));
	
	/* Struct SimulationInterface::Parameters: */
	DataType::StructureElement parametersElements[]=
		{
		{scalarType,offsetof(SimulationInterface::Parameters,linearDampening)},
		{scalarType,offsetof(SimulationInterface::Parameters,angularDampening)},
		{scalarType,offsetof(SimulationInterface::Parameters,attenuation)},
		{scalarType,offsetof(SimulationInterface::Parameters,timeFactor)}
		};
	DataType::TypeID parametersType=protocolTypes.createStructure(4,parametersElements,sizeof(SimulationInterface::Parameters));
	
	/* Create types for client protocol messages: */
	clientMessageTypes[SetParametersRequest]=parametersType;
	
	DataType::StructureElement pointPickRequestElements[]=
		{
		{pickIdType,offsetof(PointPickRequestMsg,pickId)},
		{pointType,offsetof(PointPickRequestMsg,pickPosition)},
		{scalarType,offsetof(PointPickRequestMsg,pickRadius)},
		{rotationType,offsetof(PointPickRequestMsg,pickOrientation)},
		{DataType::Bool,offsetof(PointPickRequestMsg,pickConnected)}
		};
	clientMessageTypes[PointPickRequest]=protocolTypes.createStructure(5,pointPickRequestElements,sizeof(PointPickRequestMsg));
	
	DataType::StructureElement rayPickRequestElements[]=
		{
		{pickIdType,offsetof(RayPickRequestMsg,pickId)},
		{pointType,offsetof(RayPickRequestMsg,pickPosition)},
		{vectorType,offsetof(RayPickRequestMsg,pickDirection)},
		{rotationType,offsetof(RayPickRequestMsg,pickOrientation)},
		{DataType::Bool,offsetof(RayPickRequestMsg,pickConnected)}
		};
	clientMessageTypes[RayPickRequest]=protocolTypes.createStructure(5,rayPickRequestElements,sizeof(RayPickRequestMsg));
	
	DataType::StructureElement pasteUnitRequestElements[]=
		{
		{pickIdType,offsetof(PasteUnitRequestMsg,pickId)},
		{pointType,offsetof(PasteUnitRequestMsg,position)},
		{rotationType,offsetof(PasteUnitRequestMsg,orientation)},
		{vectorType,offsetof(PasteUnitRequestMsg,linearVelocity)},
		{vectorType,offsetof(PasteUnitRequestMsg,angularVelocity)}
		};
	clientMessageTypes[PasteUnitRequest]=protocolTypes.createStructure(5,pasteUnitRequestElements,sizeof(PasteUnitRequestMsg));
	
	DataType::StructureElement createUnitRequestElements[]=
		{
		{pickIdType,offsetof(CreateUnitRequestMsg,pickId)},
		{unitTypeIdType,offsetof(CreateUnitRequestMsg,unitTypeId)},
		{pointType,offsetof(CreateUnitRequestMsg,position)},
		{rotationType,offsetof(CreateUnitRequestMsg,orientation)},
		{vectorType,offsetof(CreateUnitRequestMsg,linearVelocity)},
		{vectorType,offsetof(CreateUnitRequestMsg,angularVelocity)}
		};
	clientMessageTypes[CreateUnitRequest]=protocolTypes.createStructure(6,createUnitRequestElements,sizeof(CreateUnitRequestMsg));
	
	DataType::StructureElement setUnitStateRequestElements[]=
		{
		{pickIdType,offsetof(SetUnitStateRequestMsg,pickId)},
		{pointType,offsetof(SetUnitStateRequestMsg,position)},
		{rotationType,offsetof(SetUnitStateRequestMsg,orientation)},
		{vectorType,offsetof(SetUnitStateRequestMsg,linearVelocity)},
		{vectorType,offsetof(SetUnitStateRequestMsg,angularVelocity)}
		};
	clientMessageTypes[SetUnitStateRequest]=protocolTypes.createStructure(5,setUnitStateRequestElements,sizeof(SetUnitStateRequestMsg));
	
	clientMessageTypes[CopyUnitRequest]=pickIdType;
	clientMessageTypes[DestroyUnitRequest]=pickIdType;
	clientMessageTypes[ReleaseRequest]=pickIdType;
	clientMessageTypes[LoadStateRequest]=DataType::getAtomicType<MetadosisProtocol::StreamID>();
	clientMessageTypes[SaveStateRequest]=0; // Doesn't have an associated protocol message
	
	/* Create types for server protocol messages: */
	serverMessageTypes[SessionInvalidNotification]=0; // Doesn't have an associated protocol message
	
	DataType::StructureElement sessionUpdateNotificationElements[]=
		{
		{sessionIdType,offsetof(SessionUpdateNotificationMsg,sessionId)},
		{boxType,offsetof(SessionUpdateNotificationMsg,domain)},
		{unitTypeListType,offsetof(SessionUpdateNotificationMsg,unitTypes)}
		};
	serverMessageTypes[SessionUpdateNotification]=protocolTypes.createStructure(3,sessionUpdateNotificationElements,sizeof(SessionUpdateNotificationMsg));
	
	serverMessageTypes[SetParametersNotification]=parametersType;
	serverMessageTypes[SimulationUpdateNotification]=reducedUnitStateArrayType;
	serverMessageTypes[SaveStateReply]=DataType::getAtomicType<MetadosisProtocol::StreamID>();
	}

}

}
