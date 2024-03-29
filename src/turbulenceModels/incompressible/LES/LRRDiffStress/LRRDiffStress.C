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

#include "LRRDiffStress.H"
#include "addToRunTimeSelectionTable.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace incompressible
{
namespace LESModels
{

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

defineTypeNameAndDebug(LRRDiffStress, 0);
addToRunTimeSelectionTable(LESModel, LRRDiffStress, dictionary);

// * * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * //

void LRRDiffStress::updateSubGridScaleFields(const volScalarField& K)
{
    nuSgs_ = ck_*sqrt(K)*delta();
    nuSgs_.correctBoundaryConditions();
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

LRRDiffStress::LRRDiffStress
(
    const volVectorField& U,
    const surfaceScalarField& phi,
    transportModel& transport
)
:
    LESModel(typeName, U, phi, transport),
    GenSGSStress(U, phi, transport),

    ck_
    (
        dimensionedScalar::lookupOrAddToDict
        (
            "ck",
            coeffDict_,
            0.09
        )
    ),
    c1_
    (
        dimensionedScalar::lookupOrAddToDict
        (
            "c1",
            coeffDict_,
            1.8
        )
    ),
    c2_
    (
        dimensionedScalar::lookupOrAddToDict
        (
            "c2",
            coeffDict_,
            0.6
        )
    )
{
    updateSubGridScaleFields(0.5*tr(B_));

    printCoeffs();
}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

void LRRDiffStress::correct(const tmp<volTensorField>& tgradU)
{
    const volTensorField& gradU = tgradU();

    GenSGSStress::correct(gradU);

    volSymmTensorField D = symm(gradU);

    volSymmTensorField P = -twoSymm(B_ & gradU);

    volScalarField K = 0.5*tr(B_);
    volScalarField Epsilon = 2*nuEff()*magSqr(D);

    fvSymmTensorMatrix BEqn
    (
        fvm::ddt(B_)
      + fvm::div(phi(), B_)
      - fvm::laplacian(DBEff(), B_)
      + fvm::Sp(c1_*Epsilon/K, B_)
     ==
        P
      - (0.667*(1.0 - c1_)*I)*Epsilon
      - c2_*(P - 0.333*I*tr(P))
      - (0.667 - 2*c1_)*I*pow(K, 1.5)/delta()
    );

    BEqn.relax();
    BEqn.solve();

    // Bounding the component kinetic energies

    forAll(B_, celli)
    {
        B_[celli].component(symmTensor::XX) =
            max(B_[celli].component(symmTensor::XX), k0().value());
        B_[celli].component(symmTensor::YY) =
            max(B_[celli].component(symmTensor::YY), k0().value());
        B_[celli].component(symmTensor::ZZ) =
            max(B_[celli].component(symmTensor::ZZ), k0().value());
    }

    K = 0.5*tr(B_);
    bound(K, k0());

    updateSubGridScaleFields(K);
}


bool LRRDiffStress::read()
{
    if (GenSGSStress::read())
    {
        ck_.readIfPresent(coeffDict());
        c1_.readIfPresent(coeffDict());
        c2_.readIfPresent(coeffDict());

        return true;
    }
    else
    {
        return false;
    }
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace LESModels
} // End namespace incompressible
} // End namespace Foam

// ************************************************************************* //
