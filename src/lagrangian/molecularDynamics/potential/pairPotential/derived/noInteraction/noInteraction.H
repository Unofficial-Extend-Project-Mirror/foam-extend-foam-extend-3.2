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

Class
    Foam::pairPotentials::noInteraction

Description


SourceFiles
    noInteraction.C

\*---------------------------------------------------------------------------*/

#ifndef noInteraction_H
#define noInteraction_H

#include "pairPotential.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
namespace pairPotentials
{

/*---------------------------------------------------------------------------*\
                       Class noInteraction Declaration
\*---------------------------------------------------------------------------*/

class noInteraction
:
    public pairPotential
{

public:

    //- Runtime type information
    TypeName("noInteraction");


    // Constructors

        //- Construct from components
        noInteraction
        (
            const word& name,
            const dictionary& pairPotentialProperties
        );


    // Destructor

        ~noInteraction()
        {}


    // Member Functions

        scalar unscaledEnergy(const scalar r) const;

        //- Read dictionary
        bool read(const dictionary& pairPotentialProperties);
};


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace pairPotentials
} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
