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

Application
    yPlusLES

Description
    Calculates and reports yPlus for all wall patches, for the specified times.

\*---------------------------------------------------------------------------*/

#include "fvCFD.H"
#include "incompressible/singlePhaseTransportModel/singlePhaseTransportModel.H"
#include "LESModel.H"
#include "nearWallDist.H"
#include "wallDist.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

int main(int argc, char *argv[])
{
    timeSelector::addOptions();
    #include "setRootCase.H"
#   include "createTime.H"
    instantList timeDirs = timeSelector::select0(runTime, args);
#   include "createMesh.H"

    forAll(timeDirs, timeI)
    {
        runTime.setTime(timeDirs[timeI], timeI);
        Info<< "Time = " << runTime.timeName() << endl;
        fvMesh::readUpdateState state = mesh.readUpdate();

        // Wall distance
        if (timeI == 0 || state != fvMesh::UNCHANGED)
        {
            Info<< "Calculating wall distance\n" << endl;
            wallDist y(mesh, true);
            Info<< "Writing wall distance to field "
                << y.name() << nl << endl;
            y.write();
        }


        volScalarField yPlus
        (
            IOobject
            (
                "yPlus",
                runTime.timeName(),
                mesh,
                IOobject::NO_READ,
                IOobject::NO_WRITE
            ),
            mesh,
            dimensionedScalar("yPlus", dimless, 0.0)
        );

        Info<< "Reading field U\n" << endl;
        volVectorField U
        (
            IOobject
            (
                "U",
                runTime.timeName(),
                mesh,
                IOobject::MUST_READ,
                IOobject::NO_WRITE
            ),
            mesh
        );

#       include "createPhi.H"

        singlePhaseTransportModel laminarTransport(U, phi);

        autoPtr<incompressible::LESModel> sgsModel
        (
            incompressible::LESModel::New(U, phi, laminarTransport)
        );

        volScalarField::GeometricBoundaryField d = nearWallDist(mesh).y();
        volScalarField nuEff = sgsModel->nuEff();

        const fvPatchList& patches = mesh.boundary();

        forAll(patches, patchi)
        {
            const fvPatch& currPatch = patches[patchi];

            if (currPatch.isWall())
            {
                yPlus.boundaryField()[patchi] =
                    d[patchi]
                   *sqrt
                    (
                        nuEff.boundaryField()[patchi]
                       *mag(U.boundaryField()[patchi].snGrad())
                    )
                   /sgsModel->nu().boundaryField()[patchi];
                const scalarField& Yp = yPlus.boundaryField()[patchi];

                Info<< "Patch " << patchi
                    << " named " << currPatch.name()
                    << " y+ : min: " << min(Yp) << " max: " << max(Yp)
                    << " average: " << average(Yp) << nl << endl;
            }
        }

        Info<< "Writing yPlus to field "
            << yPlus.name() << nl << endl;

        yPlus.write();
    }

    Info<< "End\n" << endl;

    return 0;
}


// ************************************************************************* //
