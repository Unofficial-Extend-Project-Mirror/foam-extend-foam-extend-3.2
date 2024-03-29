/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | foam-extend: Open Source CFD
   \\    /   O peration     | Version:     3.2
    \\  /    A nd           | Web:         http://www.foam-extend.org
     \\/     M anipulation  | For copyright notice see file Copyright
-------------------------------------------------------------------------------
License
    This file is part of foam-extend.

    foam-extend is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the
    Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    foam-extend is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with foam-extend.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

#include "polyMesh.H"
#include "foamTime.H"
#include "cellIOList.H"
#include "wedgePolyPatch.H"
#include "emptyPolyPatch.H"
#include "globalMeshData.H"
#include "processorPolyPatch.H"
#include "indexedOctree.H"
#include "treeDataCell.H"
#include "MeshObject.H"
#include "pointMesh.H"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

defineTypeNameAndDebug(Foam::polyMesh, 0);


Foam::word Foam::polyMesh::defaultRegion = "region0";
Foam::word Foam::polyMesh::meshSubDir = "polyMesh";


// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

void Foam::polyMesh::calcDirections() const
{
    for (direction cmpt = 0; cmpt < vector::nComponents; cmpt++)
    {
        solutionD_[cmpt] = 1;
    }

    // Knock out empty and wedge directions. Note:they will be present on all
    // domains.

    vector emptyDirVec = vector::zero;
    vector wedgeDirVec = vector::zero;

    forAll (boundaryMesh(), patchi)
    {
        if (boundaryMesh()[patchi].size())
        {
            if (isA<emptyPolyPatch>(boundaryMesh()[patchi]))
            {
                emptyDirVec +=
                    sum(cmptMag(boundaryMesh()[patchi].faceAreas()));
            }
            else if (isA<wedgePolyPatch>(boundaryMesh()[patchi]))
            {
                const wedgePolyPatch& wpp = refCast<const wedgePolyPatch>
                (
                    boundaryMesh()[patchi]
                );

                wedgeDirVec += cmptMag(wpp.centreNormal());
            }
        }
    }

    vector globalEmptyDirVec = emptyDirVec;
    reduce(globalEmptyDirVec, sumOp<vector>());

    if (mag(emptyDirVec) > SMALL)
    {
        emptyDirVec /= mag(emptyDirVec);
    }

    if (Pstream::parRun() && mag(globalEmptyDirVec) > SMALL)
    {
        globalEmptyDirVec /= mag(globalEmptyDirVec);

        // Check if all processors see the same 2-D from empty patches
        if (mag(globalEmptyDirVec - emptyDirVec) > SMALL)
        {
            FatalErrorIn
            (
                "void polyMesh::calcDirections() const"
            )   << "Some processors detect different empty (2-D) "
                << "directions.  Probably using empty patches on a "
                << "bad parallel decomposition." << nl
                << "Please check your geometry and empty patches"
                << abort(FatalError);
        }
    }

    if (mag(emptyDirVec) > SMALL)
    {
        for (direction cmpt = 0; cmpt < vector::nComponents; cmpt++)
        {
            if (emptyDirVec[cmpt] > 1e-6)
            {
                solutionD_[cmpt] = -1;
            }
            else
            {
                solutionD_[cmpt] = 1;
            }
        }
    }


    // Knock out wedge directions

    geometricD_ = solutionD_;

    vector globalWedgeDirVec = wedgeDirVec;
    reduce(globalWedgeDirVec, sumOp<vector>());

    if (mag(wedgeDirVec) > SMALL)
    {
        wedgeDirVec /= mag(wedgeDirVec);
    }

    if (Pstream::parRun() && mag(globalWedgeDirVec) > SMALL)
    {
        globalWedgeDirVec /= mag(globalWedgeDirVec);

        // Check if all processors see the same 2-D from wedge patches
        if (mag(globalWedgeDirVec - wedgeDirVec) > SMALL)
        {
            FatalErrorIn
            (
                "void polyMesh::calcDirections() const"
            )   << "Some processors detect different wedge (2-D) "
                << "directions.  Probably using wedge patches on a "
                << "bad parallel decomposition." << nl
                << "Please check your geometry and wedge patches"
                << abort(FatalError);
        }
    }

    if (mag(wedgeDirVec) > SMALL)
    {
        for (direction cmpt = 0; cmpt < vector::nComponents; cmpt++)
        {
            if (wedgeDirVec[cmpt] > 1e-6)
            {
                geometricD_[cmpt] = -1;
            }
            else
            {
                geometricD_[cmpt] = 1;
            }
        }
    }
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::polyMesh::polyMesh(const IOobject& io)
:
    objectRegistry(io),
    primitiveMesh(),
    allPoints_
    (
        IOobject
        (
            "points",
            time().findInstance(meshDir(), "points"),
            meshSubDir,
            *this,
            IOobject::MUST_READ,
            IOobject::NO_WRITE
        )
    ),
    // To be re-sliced later.  HJ, 19/oct/2008
    points_(allPoints_, allPoints_.size()),
    allFaces_
    (
        IOobject
        (
            "faces",
            time().findInstance(meshDir(), "faces"),
            meshSubDir,
            *this,
            IOobject::MUST_READ,
            IOobject::NO_WRITE
        )
    ),
    // To be re-sliced later.  HJ, 19/oct/2008
    faces_(allFaces_, allFaces_.size()),
    owner_
    (
        IOobject
        (
            "owner",
            time().findInstance(meshDir(), "faces"),
            meshSubDir,
            *this,
            IOobject::READ_IF_PRESENT,
            IOobject::NO_WRITE
        )
    ),
    neighbour_
    (
        IOobject
        (
            "neighbour",
            time().findInstance(meshDir(), "faces"),
            meshSubDir,
            *this,
            IOobject::READ_IF_PRESENT,
            IOobject::NO_WRITE
        )
    ),
    clearedPrimitives_(false),
    boundary_
    (
        IOobject
        (
            "boundary",
            time().findInstance(meshDir(), "boundary"),
            meshSubDir,
            *this,
            IOobject::MUST_READ,
            IOobject::NO_WRITE
        ),
        *this
    ),
    bounds_(allPoints_),
    geometricD_(Vector<label>::zero),
    solutionD_(Vector<label>::zero),
    pointZones_
    (
        IOobject
        (
            "pointZones",
            time().findInstance
            (
                meshDir(),
                "pointZones",
                IOobject::READ_IF_PRESENT
            ),
            meshSubDir,
            *this,
            IOobject::READ_IF_PRESENT,
            IOobject::NO_WRITE
        ),
        *this
    ),
    faceZones_
    (
        IOobject
        (
            "faceZones",
            time().findInstance
            (
                meshDir(),
                "faceZones",
                IOobject::READ_IF_PRESENT
            ),
            meshSubDir,
            *this,
            IOobject::READ_IF_PRESENT,
            IOobject::NO_WRITE
        ),
        *this
    ),
    cellZones_
    (
        IOobject
        (
            "cellZones",
            time().findInstance
            (
                meshDir(),
                "cellZones",
                IOobject::READ_IF_PRESENT
            ),
            meshSubDir,
            *this,
            IOobject::READ_IF_PRESENT,
            IOobject::NO_WRITE
        ),
        *this
    ),
    globalMeshDataPtr_(NULL),
    moving_(false),
    changing_(false),
    curMotionTimeIndex_(time().timeIndex()),
    oldAllPointsPtr_(NULL),
    oldPointsPtr_(NULL)
{
    if (exists(owner_.objectPath()))
    {
        initMesh();
    }
    else
    {
        cellIOList cLst
        (
            IOobject
            (
                "cells",
                // Find the cells file on the basis of the faces file
                // HJ, 8/Jul/2009
//                 time().findInstance(meshDir(), "cells"),
                time().findInstance(meshDir(), "faces"),
                meshSubDir,
                *this,
                IOobject::MUST_READ,
                IOobject::NO_WRITE
            )
        );


        // Set the primitive mesh
        initMesh(cLst);

        owner_.write();
        neighbour_.write();
    }

    // Calculate topology for the patches (processor-processor comms etc.)
    boundary_.updateMesh();

    // Calculate the geometry for the patches (transformation tensors etc.)
    boundary_.calcGeometry();

    // Warn if global empty mesh (constructs globalData!)
    if (globalData().nTotalPoints() == 0)
    {
        WarningIn("polyMesh(const IOobject&)")
            << "no points in mesh" << endl;
    }
    if (globalData().nTotalCells() == 0)
    {
        WarningIn("polyMesh(const IOobject&)")
            << "no cells in mesh" << endl;
    }
}


Foam::polyMesh::polyMesh
(
    const IOobject& io,
    const Xfer<pointField>& points,
    const Xfer<faceList>& faces,
    const Xfer<labelList>& owner,
    const Xfer<labelList>& neighbour,
    const bool syncPar
)
:
    objectRegistry(io),
    primitiveMesh(),
    allPoints_
    (
        IOobject
        (
            "points",
            instance(),
            meshSubDir,
            *this,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        points
    ),
    // To be re-sliced later.  HJ, 19/Oct/2008
    points_(allPoints_, allPoints_.size()),
    allFaces_
    (
        IOobject
        (
            "faces",
            instance(),
            meshSubDir,
            *this,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        faces
    ),
    // To be re-sliced later.  HJ, 19/Oct/2008
    faces_(allFaces_, allFaces_.size()),
    owner_
    (
        IOobject
        (
            "owner",
            instance(),
            meshSubDir,
            *this,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        owner
    ),
    neighbour_
    (
        IOobject
        (
            "neighbour",
            instance(),
            meshSubDir,
            *this,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        neighbour
    ),
    clearedPrimitives_(false),
    boundary_
    (
        IOobject
        (
            "boundary",
            instance(),
            meshSubDir,
            *this,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        *this,
        0
    ),
    bounds_(allPoints_, syncPar),
    geometricD_(Vector<label>::zero),
    solutionD_(Vector<label>::zero),
    pointZones_
    (
        IOobject
        (
            "pointZones",
            instance(),
            meshSubDir,
            *this,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        *this,
        0
    ),
    faceZones_
    (
        IOobject
        (
            "faceZones",
            instance(),
            meshSubDir,
            *this,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        *this,
        0
    ),
    cellZones_
    (
        IOobject
        (
            "cellZones",
            instance(),
            meshSubDir,
            *this,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        *this,
        0
    ),
    globalMeshDataPtr_(NULL),
    moving_(false),
    changing_(false),
    curMotionTimeIndex_(time().timeIndex()),
    oldAllPointsPtr_(NULL),
    oldPointsPtr_(NULL)
{
    // Check if the faces and cells are valid
    forAll (allFaces_, faceI)
    {
        const face& curFace = allFaces_[faceI];

        if (min(curFace) < 0 || max(curFace) > allPoints_.size())
        {
            FatalErrorIn
            (
                "polyMesh::polyMesh\n"
                "(\n"
                "    const IOobject& io,\n"
                "    const pointField& points,\n"
                "    const faceList& faces,\n"
                "    const cellList& cells\n"
                ")\n"
            )   << "Face " << faceI << "contains vertex labels out of range: "
                << curFace << " Max point index = " << allPoints_.size()
                << abort(FatalError);
        }
    }

    // Set the primitive mesh
    initMesh();
}


Foam::polyMesh::polyMesh
(
    const IOobject& io,
    const Xfer<pointField>& points,
    const Xfer<faceList>& faces,
    const Xfer<cellList>& cells,
    const bool syncPar
)
:
    objectRegistry(io),
    primitiveMesh(),
    allPoints_
    (
        IOobject
        (
            "points",
            instance(),
            meshSubDir,
            *this,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        points
    ),
    // To be re-sliced later.  HJ, 19/Oct/2008
    points_(allPoints_, allPoints_.size()),
    allFaces_
    (
        IOobject
        (
            "faces",
            instance(),
            meshSubDir,
            *this,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        faces
    ),
    faces_(allFaces_, allFaces_.size()),
    owner_
    (
        IOobject
        (
            "owner",
            instance(),
            meshSubDir,
            *this,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        0
    ),
    neighbour_
    (
        IOobject
        (
            "neighbour",
            instance(),
            meshSubDir,
            *this,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        0
    ),
    clearedPrimitives_(false),
    boundary_
    (
        IOobject
        (
            "boundary",
            instance(),
            meshSubDir,
            *this,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        *this,
        0
    ),
    bounds_(allPoints_, syncPar),
    geometricD_(Vector<label>::zero),
    solutionD_(Vector<label>::zero),
    pointZones_
    (
        IOobject
        (
            "pointZones",
            instance(),
            meshSubDir,
            *this,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        *this,
        0
    ),
    faceZones_
    (
        IOobject
        (
            "faceZones",
            instance(),
            meshSubDir,
            *this,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        *this,
        0
    ),
    cellZones_
    (
        IOobject
        (
            "cellZones",
            instance(),
            meshSubDir,
            *this,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        *this,
        0
    ),
    globalMeshDataPtr_(NULL),
    moving_(false),
    changing_(false),
    curMotionTimeIndex_(time().timeIndex()),
    oldAllPointsPtr_(NULL),
    oldPointsPtr_(NULL)
{
    // Check if the faces and cells are valid
    forAll (allFaces_, faceI)
    {
        const face& curFace = allFaces_[faceI];

        if (min(curFace) < 0 || max(curFace) > allPoints_.size())
        {
            FatalErrorIn
            (
                "polyMesh::polyMesh\n"
                "(\n"
                "    const IOobject&,\n"
                "    const Xfer<pointField>&,\n"
                "    const Xfer<faceList>&,\n"
                "    const Xfer<cellList>&\n"
                ")\n"
            )   << "Face " << faceI << "contains vertex labels out of range: "
                << curFace << " Max point index = " << allPoints_.size()
                << abort(FatalError);
        }
    }

    // transfer in cell list
    cellList cLst(cells);

    // Check if cells are valid
    forAll (cLst, cellI)
    {
        const cell& curCell = cLst[cellI];

        if (min(curCell) < 0 || max(curCell) > allFaces_.size())
        {
            FatalErrorIn
            (
                "polyMesh::polyMesh\n"
                "(\n"
                "    const IOobject&,\n"
                "    const Xfer<pointField>&,\n"
                "    const Xfer<faceList>&,\n"
                "    const Xfer<cellList>&\n"
                ")\n"
            )   << "Cell " << cellI << "contains face labels out of range: "
                << curCell << " Max face index = " << allFaces_.size()
                << abort(FatalError);
        }
    }

    // Set the primitive mesh
    initMesh(cLst);
}


void Foam::polyMesh::resetPrimitives
(
    const Xfer<pointField>& pts,
    const Xfer<faceList>& fcs,
    const Xfer<labelList>& own,
    const Xfer<labelList>& nei,
    const labelList& patchSizes,
    const labelList& patchStarts,
    const bool validBoundary
)
{
    // Clear addressing. Keep geometric props for mapping.
    clearAddressing();

    // Take over new primitive data.
    // Optimized to avoid overwriting data at all
    if (!pts().empty())
    {
        allPoints_.transfer(pts());

         // Recalculate bounds with all points.  HJ, 17/Oct/2008
         // ... if  points have change.  HJ, 19/Aug/2010
        bounds_ = boundBox(allPoints_, validBoundary);
    }

    if (!fcs().empty())
    {
        allFaces_.transfer(fcs());
        // Faces will be reset in initMesh(), using size of owner list
    }

    if (!own().empty())
    {
        owner_.transfer(own());
    }

    if (!nei().empty())
    {
        neighbour_.transfer(nei());
    }


    // Reset patch sizes and starts
    forAll (boundary_, patchI)
    {
        boundary_[patchI] = polyPatch
        (
            boundary_[patchI].name(),
            patchSizes[patchI],
            patchStarts[patchI],
            patchI,
            boundary_
        );
    }


    // Flags the mesh files as being changed
    setInstance(time().timeName());

    // Check if the faces and cells are valid
    forAll (allFaces_, faceI)
    {
        const face& curFace = allFaces_[faceI];

        if (curFace.size() == 0)
        {
            FatalErrorIn
            (
                "polyMesh::polyMesh::resetPrimitives\n"
                "(\n"
                "    const Xfer<pointField>& points,\n"
                "    const Xfer<faceList>& faces,\n"
                "    const Xfer<labelList>& owner,\n"
                "    const Xfer<labelList>& neighbour,\n"
                "    const labelList& patchSizes,\n"
                "    const labelList& patchStarts\n"
                ")\n"
            )   << "Face " << faceI << " contains no vertex labels"
                << abort(FatalError);
        }
        else if (min(curFace) < 0 || max(curFace) > allPoints_.size())
        {
            FatalErrorIn
            (
                "polyMesh::polyMesh::resetPrimitives\n"
                "(\n"
                "    const Xfer<pointField>& points,\n"
                "    const Xfer<faceList>& faces,\n"
                "    const Xfer<labelList>& owner,\n"
                "    const Xfer<labelList>& neighbour,\n"
                "    const labelList& patchSizes,\n"
                "    const labelList& patchStarts\n"
                ")\n"
            )   << "Face " << faceI << " contains vertex labels out of range: "
                << curFace << " Max point index = " << allPoints_.size()
                << abort(FatalError);
        }
    }


    // Set the primitive mesh from the owner_, neighbour_.
    // Works out from patch end where the active faces stop.
    initMesh();


    if (validBoundary)
    {
        // Note that we assume that all the patches stay the same and are
        // correct etc. so we can already use the patches to do
        // processor-processor comms.

        // Calculate topology for the patches (processor-processor comms etc.)
        boundary_.updateMesh();

        // Calculate the geometry for the patches (transformation tensors etc.)
        boundary_.calcGeometry();

        // Warn if global empty mesh (constructs globalData!)
        if
        (
            globalData().nTotalPoints() == 0
         || globalData().nTotalCells() == 0
        )
        {
            FatalErrorIn
            (
                "polyMesh::polyMesh::resetPrimitives\n"
                "(\n"
                "    const Xfer<pointField>&,\n"
                "    const Xfer<faceList>&,\n"
                "    const Xfer<labelList>& owner,\n"
                "    const Xfer<labelList>& neighbour,\n"
                "    const labelList& patchSizes,\n"
                "    const labelList& patchStarts\n"
                ")\n"
            )
                << "no points or no cells in mesh" << endl;
        }
    }

    // Update zones.  1.6.x merge.  HJ, 30/Aug/2010
    pointZones_.updateMesh();
    faceZones_.updateMesh();
    cellZones_.updateMesh();
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

Foam::polyMesh::~polyMesh()
{
    clearOut();
    resetMotion();
}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

const Foam::fileName& Foam::polyMesh::dbDir() const
{
    if (objectRegistry::dbDir() == defaultRegion)
    {
        return parent().dbDir();
    }
    else
    {
        return objectRegistry::dbDir();
    }
}


Foam::fileName Foam::polyMesh::meshDir() const
{
    return dbDir()/meshSubDir;
}


const Foam::fileName& Foam::polyMesh::pointsInstance() const
{
    return allPoints_.instance();
}


const Foam::fileName& Foam::polyMesh::facesInstance() const
{
    return allFaces_.instance();
}


const Foam::Vector<Foam::label>& Foam::polyMesh::geometricD() const
{
    if (geometricD_.x() == 0)
    {
        calcDirections();
    }

    return geometricD_;
}


Foam::label Foam::polyMesh::nGeometricD() const
{
    return cmptSum(geometricD() + Vector<label>::one)/2;
}


const Foam::Vector<Foam::label>& Foam::polyMesh::solutionD() const
{
    if (solutionD_.x() == 0)
    {
        calcDirections();
    }

    return solutionD_;
}


Foam::label Foam::polyMesh::nSolutionD() const
{
    return cmptSum(solutionD() + Vector<label>::one)/2;
}


// Add boundary patches. Constructor helper
void Foam::polyMesh::addPatches
(
    const List<polyPatch*>& p,
    const bool validBoundary
)
{
    if (boundaryMesh().size())
    {
        FatalErrorIn
        (
            "void polyMesh::addPatches(const List<polyPatch*>&, const bool)"
        )   << "boundary already exists"
            << abort(FatalError);
    }

    // Reset valid directions
    geometricD_ = Vector<label>::zero;
    solutionD_ = Vector<label>::zero;

    boundary_.setSize(p.size());

    // Copy the patch pointers
    forAll (p, pI)
    {
        boundary_.set(pI, p[pI]);
    }

    // parallelData depends on the processorPatch ordering so force
    // recalculation. Problem: should really be done in removeBoundary but
    // there is some info in parallelData which might be interesting inbetween
    // removeBoundary and addPatches.
    deleteDemandDrivenData(globalMeshDataPtr_);

    if (validBoundary)
    {
        // Calculate topology for the patches (processor-processor comms etc.)
        boundary_.updateMesh();

        // Calculate the geometry for the patches (transformation tensors etc.)
        boundary_.calcGeometry();

        boundary_.checkDefinition();
    }
}


// Add mesh zones. Constructor helper
void Foam::polyMesh::addZones
(
    const List<pointZone*>& pz,
    const List<faceZone*>& fz,
    const List<cellZone*>& cz
)
{
    if
    (
        pointZones().size() > 0
     || faceZones().size() > 0
     || cellZones().size() > 0
    )
    {
        FatalErrorIn
        (
            "void addZones\n"
            "(\n"
            "    const List<pointZone*>&,\n"
            "    const List<faceZone*>&,\n"
            "    const List<cellZone*>&\n"
            ")"
        )   << "point, face or cell zone already exists"
            << abort(FatalError);
    }

    // Point zones
    if (pz.size())
    {
        pointZones_.setSize(pz.size());

        // Copy the zone pointers
        forAll (pz, pI)
        {
            pointZones_.set(pI, pz[pI]);
        }

        pointZones_.writeOpt() = IOobject::AUTO_WRITE;

        // Temporarily disable zone writing on creation
        // Auto-write should be sufficient.  HJ, 20/Aug/2009
//         pointZones_.write();
    }

    // Face zones
    if (fz.size())
    {
        faceZones_.setSize(fz.size());

        // Copy the zone pointers
        forAll (fz, fI)
        {
            faceZones_.set(fI, fz[fI]);
        }

        faceZones_.writeOpt() = IOobject::AUTO_WRITE;

        // Temporarily disable zone writing on creation
        // Auto-write should be sufficient.  HJ, 20/Aug/2009
//         faceZones_.write();
    }

    // Cell zones
    if (cz.size())
    {
        cellZones_.setSize(cz.size());

        // Copy the zone pointers
        forAll (cz, cI)
        {
            cellZones_.set(cI, cz[cI]);
        }

        cellZones_.writeOpt() = IOobject::AUTO_WRITE;

        // Temporarily disable zone writing on creation
        // Auto-write should be sufficient.  HJ, 20/Aug/2009
//         cellZones_.write();
    }
}


const Foam::pointField& Foam::polyMesh::allPoints() const
{
    if (clearedPrimitives_)
    {
        FatalErrorIn("const pointField& polyMesh::allPoints() const")
            << "allPoints deallocated"
            << abort(FatalError);
    }

    return allPoints_;
}


const Foam::pointField& Foam::polyMesh::points() const
{
    if (clearedPrimitives_)
    {
        FatalErrorIn("const pointField& polyMesh::points() const")
            << "points deallocated"
            << abort(FatalError);
    }

    return points_;
}


const Foam::faceList& Foam::polyMesh::allFaces() const
{
    if (clearedPrimitives_)
    {
        FatalErrorIn("const faceList& polyMesh::allFaces() const")
            << "allFaces deallocated"
            << abort(FatalError);
    }

    return allFaces_;
}


const Foam::faceList& Foam::polyMesh::faces() const
{
    if (clearedPrimitives_)
    {
        FatalErrorIn("const faceList& polyMesh::faces() const")
            << "faces deallocated"
            << abort(FatalError);
    }

    return faces_;
}


const Foam::labelList& Foam::polyMesh::faceOwner() const
{
    return owner_;
}


const Foam::labelList& Foam::polyMesh::faceNeighbour() const
{
    return neighbour_;
}


// Return old mesh motion points
const Foam::pointField& Foam::polyMesh::oldAllPoints() const
{
    if (!oldAllPointsPtr_)
    {
        if (debug)
        {
            WarningIn("const pointField& polyMesh::oldAllPoints() const")
                << "Old points not available.  Forcing storage of old points"
                << endl;
        }

        oldAllPointsPtr_ = new pointField(allPoints_);
        curMotionTimeIndex_ = time().timeIndex();
    }

    return *oldAllPointsPtr_;
}


// Return old mesh motion points
const Foam::pointField& Foam::polyMesh::oldPoints() const
{
    if (!oldPointsPtr_)
    {
        oldPointsPtr_ = new pointField::subField(oldAllPoints(), nPoints());
    }

    return *oldPointsPtr_;
}


Foam::tmp<Foam::scalarField> Foam::polyMesh::movePoints
(
    const pointField& newPoints
)
{
    if (debug)
    {
        Info<< "tmp<scalarField> polyMesh::movePoints(const pointField&) : "
            << " Moving points for time " << time().value()
            << " index " << time().timeIndex() << endl;
    }

    moving(true);

    // Pick up old points
    if (curMotionTimeIndex_ != time().timeIndex())
    {
        // Mesh motion in the new time step
        deleteDemandDrivenData(oldAllPointsPtr_);
        deleteDemandDrivenData(oldPointsPtr_);
        oldAllPointsPtr_ = new pointField(allPoints_);
        curMotionTimeIndex_ = time().timeIndex();
    }

    allPoints_ = newPoints;

    if (debug > 1)
    {
        // Check mesh motion
        if (primitiveMesh::checkMeshMotion(allPoints_, true))
        {
            Info<< "tmp<scalarField> polyMesh::movePoints"
                << "(const pointField&) : "
                << "Moving the mesh with given points will "
                << "invalidate the mesh." << nl
                << "Mesh motion should not be executed." << endl;
        }
    }

    allPoints_.writeOpt() = IOobject::AUTO_WRITE;
    allPoints_.instance() = time().timeName();

    points_.reset(allPoints_, nPoints());

    tmp<scalarField> sweptVols = primitiveMesh::movePoints
    (
        points_,
        oldPoints()
    );

    // Adjust parallel shared points
    if (globalMeshDataPtr_)
    {
        globalMeshDataPtr_->movePoints(allPoints_);
    }

    // Force recalculation of all geometric data with new points

    bounds_ = boundBox(allPoints_);
    boundary_.movePoints(allPoints_);

    pointZones_.movePoints(allPoints_);
    faceZones_.movePoints(allPoints_);
    cellZones_.movePoints(allPoints_);

    // Reset valid directions (could change with rotation)
    geometricD_ = Vector<label>::zero;
    solutionD_ = Vector<label>::zero;

    // Update all function objects
    // Moved from fvMesh.C in 1.6.x merge.  HJ, 29/Aug/2010
    meshObjectBase::allMovePoints<polyMesh>(*this);

    return sweptVols;
}


// Reset motion by deleting old points
void Foam::polyMesh::resetMotion() const
{
    curMotionTimeIndex_ = 0;
    deleteDemandDrivenData(oldAllPointsPtr_);
    deleteDemandDrivenData(oldPointsPtr_);
}


// Reset motion by deleting old points
void Foam::polyMesh::setOldPoints
(
    const pointField& setPoints
)
{
    if(setPoints.size() != allPoints_.size())
    {
            FatalErrorIn
            (
                "polyMesh::setOldPoints\n"
                "(\n"
                "    const pointField& setPoints\n"
                ")\n"
            )   << "setPoints size " << setPoints.size()
                << "different from the mesh points size "
                << allPoints_.size()
                << abort(FatalError);
    }

    moving(false);

    // Mesh motion in the new time step
    deleteDemandDrivenData(oldAllPointsPtr_);
    deleteDemandDrivenData(oldPointsPtr_);
    allPoints_ = setPoints;
    oldAllPointsPtr_ = new pointField(allPoints_);
    oldPointsPtr_ = new pointField::subField(oldAllPoints(), nPoints());
    curMotionTimeIndex_ = 0;
    primitiveMesh::clearGeom();
}


// Return parallel info
const Foam::globalMeshData& Foam::polyMesh::globalData() const
{
    if (!globalMeshDataPtr_)
    {
        if (debug)
        {
            Pout<< "polyMesh::globalData() const : "
                << "Constructing parallelData from processor topology"
                << endl;
        }
        // Construct globalMeshData using processorPatch information only.
        globalMeshDataPtr_ = new globalMeshData(*this);

        // Old method.  HJ, 6/Dec/2006

//         // Check for parallel boundaries
//         bool parBoundaries = false;

//         forAll (boundaryMesh(), patchI)
//         {
//             if
//             (
//                 typeid(boundaryMesh()[patchI])
//              == typeid(processorPolyPatch)
//             )
//             {
//                 parBoundaries = true;
//                 break;
//             }
//         }

//         if (parBoundaries)
//         {
//             // All is well - read the parallel data

//             globalDataPtr_ =
//                 new globalMeshData
//                 (
//                     IOobject
//                     (
//                         "globalData",
//                         time().findInstance(meshDir(), "globalData"),
//                         meshSubDir,
//                         *this,
//                         IOobject::MUST_READ,
//                         IOobject::NO_WRITE
//                     ),
//                     *this
//                 );
//         }
//         else
//         {
//             // The mesh has no parallel boundaries.  Create and hook a
//             // "non-parallel" parallel info
//             globalDataPtr_ =
//                 new globalMeshData
//                 (
//                     *this,
//                     false,
//                     false,  // cyclicParallel.  Remove when fixed
//                     nPoints(),
//                     nFaces(),
//                     nCells(),
//                     0,
//                     labelList(0),
//                     labelList(0),
//                     labelList(0)
//                 );
//         }
    }

    return *globalMeshDataPtr_;
}


// Remove all files and some subdirs (eg, sets)
void Foam::polyMesh::removeFiles(const fileName& instanceDir) const
{
    fileName meshFilesPath = thisDb().path()/instanceDir/meshDir();

    rm(meshFilesPath/"points");
    rm(meshFilesPath/"faces");
    rm(meshFilesPath/"owner");
    rm(meshFilesPath/"neighbour");
    rm(meshFilesPath/"cells");
    rm(meshFilesPath/"boundary");
    rm(meshFilesPath/"pointZones");
    rm(meshFilesPath/"faceZones");
    rm(meshFilesPath/"cellZones");
    rm(meshFilesPath/"meshModifiers");
    rm(meshFilesPath/"parallelData");

    // remove subdirectories
    if (isDir(meshFilesPath/"sets"))
    {
        rmDir(meshFilesPath/"sets");
    }
}


void Foam::polyMesh::removeFiles() const
{
    removeFiles(instance());
}


Foam::label Foam::polyMesh::findCell
(
    const point& location
) const
{
    if (nCells() == 0)
    {
        return -1;
    }

    // Find the nearest cell centre to this location
    label cellI = findNearestCell(location);

    // If point is in the nearest cell return
    if (pointInCell(location, cellI))
    {
        return cellI;
    }
    else // point is not in the nearest cell so search all cells
    {
        for (label cellI = 0; cellI < nCells(); cellI++)
        {
            if (pointInCell(location, cellI))
            {
                return cellI;
            }
        }
        return -1;
    }
}


// ************************************************************************* //
