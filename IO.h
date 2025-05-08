/***********************************************************************
IO - Helper functions to read/write common classes used by the new
Nanotech Construction Kit from/to files.
Copyright (c) 2019-2020 Oliver Kreylos

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

#ifndef IO_INCLUDED
#define IO_INCLUDED

#include <Misc/Marshaller.h>
#include <Misc/StandardMarshallers.h>
#include <Misc/CompoundMarshallers.h>
#include <Misc/StandardValueCoders.h>
#include <Misc/CompoundValueCoders.h>
#include <Geometry/GeometryMarshallers.h>
#include <Geometry/GeometryValueCoders.h>

#include "Common.h"

namespace Misc {

template <>
class Marshaller<BondSite> // Class to read/write structural unit bond sites from/to binary files
	{
	/* Methods: */
	public:
	static size_t getSize(const BondSite& value)
		{
		return Misc::Marshaller<::Vector>::getSize(value.offset);
		}
	template <class DataSinkParam>
	static void write(const BondSite& value,DataSinkParam& sink)
		{
		Misc::Marshaller<::Vector>::write(value.offset,sink);
		}
	template <class DataSourceParam>
	static BondSite& read(DataSourceParam& source,BondSite& value)
		{
		Misc::Marshaller<::Vector>::read(source,value.offset);
		return value;
		}
	template <class DataSourceParam>
	static BondSite read(DataSourceParam& source)
		{
		BondSite result;
		Misc::Marshaller<::Vector>::read(source,result.offset);
		return result;
		}
	};

template <>
class ValueCoder<BondSite> // Class to read/write structural unit bond sites from/to configuration files
	{
	/* Methods: */
	public:
	static std::string encode(const BondSite& value)
		{
		std::string result;
		
		/* Encode the bond site position: */
		result=ValueCoder<::Vector>::encode(value.offset);
		
		return result;
		}
	static BondSite decode(const char* start,const char* end,const char** decodeEnd =0)
		{
		BondSite result;
		
		/* Decode the bond site position: */
		result.offset=ValueCoder<::Vector>::decode(start,end,decodeEnd);
		
		return result;
		}
	};

template <>
class Marshaller<UnitType> // Class to read/write unit types from/to binary files
	{
	/* Methods: */
	public:
	static size_t getSize(const UnitType& value)
		{
		return Misc::Marshaller<std::string>::getSize(value.name)
		       +sizeof(Scalar)*2
		       +Misc::Marshaller<Tensor>::getSize(value.momentOfInertia)
		       +Misc::Marshaller<Vector<BondSite> >::getSize(value.bondSites)
		       +Misc::Marshaller<Vector<Point> >::getSize(value.meshVertices)
		       +Misc::Marshaller<Vector<Index> >::getSize(value.meshTriangles);
		}
	template <class DataSinkParam>
	static void write(const UnitType& value,DataSinkParam& sink)
		{
		Misc::Marshaller<std::string>::write(value.name,sink);
		sink.template write<Scalar>(value.radius);
		sink.template write<Scalar>(value.mass);
		Misc::Marshaller<Tensor>::write(value.momentOfInertia,sink);
		Misc::Marshaller<Vector<BondSite> >::write(value.bondSites,sink);
		Misc::Marshaller<Vector<Point> >::write(value.meshVertices,sink);
		Misc::Marshaller<Vector<Index> >::write(value.meshTriangles,sink);
		}
	template <class DataSourceParam>
	static UnitType& read(DataSourceParam& source,UnitType& value)
		{
		Misc::Marshaller<std::string>::read(source,value.name);
		value.radius=source.template read<Scalar>();
		value.mass=source.template read<Scalar>();
		value.invMass=Scalar(1)/value.mass;
		Misc::Marshaller<Tensor>::read(source,value.momentOfInertia);
		value.invMomentOfInertia=Geometry::invert(value.momentOfInertia);
		Misc::Marshaller<Vector<BondSite> >::read(source,value.bondSites);
		Misc::Marshaller<Vector<Point> >::read(source,value.meshVertices);
		Misc::Marshaller<Vector<Index> >::read(source,value.meshTriangles);
		return value;
		}
	template <class DataSourceParam>
	static UnitType read(DataSourceParam& source)
		{
		UnitType result;
		Misc::Marshaller<std::string>::read(source,result.name);
		result.radius=source.template read<Scalar>();
		result.mass=source.template read<Scalar>();
		result.invMass=Scalar(1)/result.mass;
		Misc::Marshaller<Tensor>::read(source,result.momentOfInertia);
		result.invMomentOfInertia=Geometry::invert(result.momentOfInertia);
		Misc::Marshaller<Vector<BondSite> >::read(source,result.bondSites);
		Misc::Marshaller<Vector<Point> >::read(source,result.meshVertices);
		Misc::Marshaller<Vector<Index> >::read(source,result.meshTriangles);
		return result;
		}
	};

template <>
class Marshaller<UnitState> // Class to read/write unit states from/to binary files
	{
	/* Methods: */
	public:
	static size_t getSize(const UnitState& value)
		{
		return sizeof(UnitTypeID)
		       +Misc::Marshaller<Point>::getSize(value.position)
		       +Misc::Marshaller<Rotation>::getSize(value.orientation)
		       +Misc::Marshaller<::Vector>::getSize(value.linearVelocity)
		       +Misc::Marshaller<::Vector>::getSize(value.angularVelocity);
		}
	template <class DataSinkParam>
	static void write(const UnitState& value,DataSinkParam& sink)
		{
		sink.template write<UnitTypeID>(value.unitType);
		Misc::Marshaller<Point>::write(value.position,sink);
		Misc::Marshaller<Rotation>::write(value.orientation,sink);
		Misc::Marshaller<::Vector>::write(value.linearVelocity,sink);
		Misc::Marshaller<::Vector>::write(value.angularVelocity,sink);
		}
	template <class DataSourceParam>
	static UnitState& read(DataSourceParam& source,UnitState& value)
		{
		value.unitType=source.template read<UnitTypeID>();
		value.pickId=0;
		Misc::Marshaller<Point>::read(source,value.position);
		Misc::Marshaller<Rotation>::read(source,value.orientation);
		Misc::Marshaller<::Vector>::read(source,value.linearVelocity);
		Misc::Marshaller<::Vector>::read(source,value.angularVelocity);
		return value;
		}
	template <class DataSourceParam>
	static UnitState read(DataSourceParam& source)
		{
		UnitState result;
		result.unitType=source.template read<UnitTypeID>();
		result.pickId=0;
		Misc::Marshaller<Point>::read(source,result.position);
		Misc::Marshaller<Rotation>::read(source,result.orientation);
		Misc::Marshaller<::Vector>::read(source,result.linearVelocity);
		Misc::Marshaller<::Vector>::read(source,result.angularVelocity);
		return result;
		}
	};

template <>
class Marshaller<ReducedUnitState> // Class to read/write reduced unit states from/to binary files
	{
	/* Methods: */
	public:
	static size_t getSize(const ReducedUnitState& value)
		{
		return sizeof(UnitTypeID)
		       +Misc::Marshaller<ReducedUnitState::Point>::getSize(value.position)
		       +Misc::Marshaller<ReducedUnitState::Rotation>::getSize(value.orientation);
		}
	template <class DataSinkParam>
	static void write(const ReducedUnitState& value,DataSinkParam& sink)
		{
		sink.template write<UnitTypeID>(value.unitType);
		Misc::Marshaller<ReducedUnitState::Point>::write(value.position,sink);
		Misc::Marshaller<ReducedUnitState::Rotation>::write(value.orientation,sink);
		}
	template <class DataSourceParam>
	static ReducedUnitState& read(DataSourceParam& source,ReducedUnitState& value)
		{
		value.unitType=source.template read<UnitTypeID>();
		Misc::Marshaller<ReducedUnitState::Point>::read(source,value.position);
		Misc::Marshaller<ReducedUnitState::Rotation>::read(source,value.orientation);
		return value;
		}
	template <class DataSourceParam>
	static ReducedUnitState read(DataSourceParam& source)
		{
		ReducedUnitState result;
		result.unitType=source.template read<UnitTypeID>();
		Misc::Marshaller<ReducedUnitState::Point>::read(source,result.position);
		Misc::Marshaller<ReducedUnitState::Rotation>::read(source,result.orientation);
		return result;
		}
	};

}

template <class UnitStateParam,class DataSinkParam>
void writeStateArray(const StateArray<UnitStateParam>& states,DataSinkParam& sink,bool writeHeader) // Writes an array of unit states to a binary file
	{
	if(writeHeader)
		{
		/* Write the session ID and time step: */
		sink.template write(states.sessionId);
		sink.template write(states.timeStamp);
		}
	
	/* Write the number of unit states in the array: */
	sink.template write(Size(states.states.size()));
	
	/* Write the array of unit states: */
	for(typename StateArray<UnitStateParam>::UnitStateList::const_iterator sIt=states.states.begin();sIt!=states.states.end();++sIt)
		Misc::Marshaller<UnitStateParam>::write(*sIt,sink);
	}

template <class UnitStateParam,class DataSourceParam>
void readStateArray(DataSourceParam& source,StateArray<UnitStateParam>& states,bool readHeader) // Reads an array of unit states from a binary file
	{
	if(readHeader)
		{
		/* Read the session ID and time step: */
		source.template read(states.sessionId);
		source.template read(states.timeStamp);
		}
	
	/* Read the number of unit states in the array: */
	Size numUnits=source.template read<Size>();
	
	/* Clear and make room in the state array: */
	states.states.clear();
	states.states.reserve(numUnits);
	
	/* Read the array of unit states: */
	for(Index i=0;i<numUnits;++i)
		states.states.push_back(Misc::Marshaller<UnitStateParam>::read(source));
	}

template <class DataSinkParam>
void reduceAndWriteStateArray(const UnitStateArray& states,DataSinkParam& sink,bool writeHeader) // Reduces and writes an array of unit states to a binary file
	{
	if(writeHeader)
		{
		/* Write the session ID and time step: */
		sink.template write(states.sessionId);
		sink.template write(states.timeStamp);
		}
	
	/* Write the number of unit states in the array: */
	sink.template write(Size(states.states.size()));
	
	/* Reduce and write the array of unit states: */
	ReducedUnitState r;
	for(UnitStateArray::UnitStateList::const_iterator sIt=states.states.begin();sIt!=states.states.end();++sIt)
		{
		r.set(*sIt);
		Misc::Marshaller<ReducedUnitState>::write(r,sink);
		}
	}

#endif
