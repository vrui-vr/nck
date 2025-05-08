/***********************************************************************
Common - Common classes used by the new Nanotech Construction Kit.
Copyright (c) 2017-2020 Oliver Kreylos

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

#ifndef COMMON_INCLUDED
#define COMMON_INCLUDED

#include <string>
#include <Misc/SizedTypes.h>
#include <Misc/Vector.h>
#include <Geometry/Point.h>
#include <Geometry/Vector.h>
#include <Geometry/Rotation.h>
#include <Geometry/Matrix.h>
#include <Geometry/Box.h>

/* Atomic types: */
typedef Misc::UInt16 UnitTypeID;
typedef Misc::UInt16 SessionID;
typedef Misc::UInt32 Index;
typedef Misc::UInt32 Size;
typedef Misc::UInt16 PickID;
typedef Misc::Float32 Scalar;

/* Geometry types: */
typedef Geometry::Point<Scalar,3> Point;
typedef Geometry::Vector<Scalar,3> Vector;
typedef Geometry::Rotation<Scalar,3> Rotation;
typedef Geometry::Matrix<Scalar,3,3> Tensor;
typedef Geometry::Box<Scalar,3> Box;

struct BondSite // Structure for potential bonding sites between two structural units
	{
	/* Elements: */
	public:
	Vector offset; // Bonding site offset from parent unit's center of gravity in local coordinate system
	};

struct UnitType // Class for types of structural units
	{
	/* Elements: */
	public:
	std::string name; // Unit name
	Scalar radius; // Radius of central repelling force
	Scalar mass; // Total mass of this unit
	Scalar invMass; // Inverse of unit mass
	Tensor momentOfInertia; // Moment of inertia of this unit
	Tensor invMomentOfInertia; // Inverse of unit moment of inertia
	Misc::Vector<BondSite> bondSites; // List of potential bonding sites
	Misc::Vector<Point> meshVertices; // List of vertices defining the unit type's rendering triangle mesh
	Misc::Vector<Index> meshTriangles; // List of index triplets defining the unit type's rendering triangle mesh
	};

typedef Misc::Vector<UnitType> UnitTypeList; // Type for lists of unit types

struct UnitState // Structure to represent the current state of a structural unit
	{
	/* Elements: */
	public:
	UnitTypeID unitType; // ID of unit's type
	PickID pickId; // Pick ID currently locking this unit, or 0
	Point position; // Current unit position
	Rotation orientation; // Current unit orientation
	Vector linearVelocity; // Current linear velocity
	Vector angularVelocity; // Current angular velocity
	};

struct ReducedUnitState // Structure to represent the current state of a structural unit, sufficient for rendering
	{
	/* Embedded classes: */
	public:
	typedef Misc::Float32 Scalar;
	typedef Geometry::Point<Scalar,3> Point;
	typedef Geometry::Rotation<Scalar,3> Rotation;
	
	/* Elements: */
	UnitTypeID unitType; // ID of unit's type
	Point position; // Current unit position
	Rotation orientation; // Current unit orientation
	
	/* Methods: */
	void set(const UnitState& unitState) // Reduces the given full unit state
		{
		unitType=unitState.unitType;
		position=Point(unitState.position);
		orientation=Rotation(unitState.orientation);
		}
	};

template <class UnitStateParam>
struct StateArray // Structure for arrays of structural unit states
	{
	/* Embedded classes: */
	public:
	typedef UnitStateParam UnitState; // Type of stored unit states
	typedef Misc::Vector<UnitState> UnitStateList; // Type for lists of unit states
	
	/* Elements: */
	public:
	SessionID sessionId; // ID of the session that produced this unit state array
	Index timeStamp; // Simulation step for which this array's entries are valid
	UnitStateList states; // Array of unit states
	
	/* Constructors and destructors: */
	StateArray(void)
		:sessionId(0),timeStamp(0)
		{
		}
	};

typedef StateArray<UnitState> UnitStateArray; // Array of full unit states
typedef StateArray<ReducedUnitState> ReducedUnitStateArray; // Array of reduced unit states

#endif
