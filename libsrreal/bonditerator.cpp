/***********************************************************************
* $Id$
***********************************************************************/

#include <map>
#include <vector>

#include "bonditerator.h"
#include "PointsInSphere.h"

// From ObjCryst distribution
#include "CrystVector/CrystVector.h"
#include "ObjCryst/ScatteringPower.h"
#include "ObjCryst/SpaceGroup.h"

#include "assert.h"

using namespace SrReal;
using std::set;
using std::vector;

namespace {

float rtod = 180.0/M_PI;

} // End anonymous namespace

/******************************************************************************
***** BondIterator implementation *********************************************
******************************************************************************/

SrReal::BondIterator::
BondIterator (ObjCryst::Crystal& _crystal)
    : crystal(_crystal), rmin(0), rmax(0)
    
{
    sph = NULL;
    sc = NULL;
    itclock.Reset();
    latclock.Reset();
    rewind();
}

SrReal::BondIterator::
BondIterator (ObjCryst::Crystal& _crystal, float _rmin, float _rmax)
    : crystal(_crystal), rmin(_rmin), rmax(_rmax)
    
{
    sph = NULL;
    sc = NULL;
    itclock.Reset();
    latclock.Reset();
    rewind();
}

SrReal::BondIterator::
BondIterator(const BondIterator& other) 
    : crystal(other.crystal), rmin(other.rmin), rmax(other.rmax)
{
    sph = NULL;
    sc = NULL;
    itclock.Reset();
    latclock.Reset();
    rewind();
}

SrReal::BondIterator::
~BondIterator()
{
    if( sph != NULL )
    {
        delete sph;
    }
}

void 
SrReal::BondIterator::
setBondRange(float _rmin, float _rmax)
{
    rmin = _rmin;
    rmax = _rmax;
    itclock.Reset();
    latclock.Reset();
    rewind();
    return;
}

void 
SrReal::BondIterator::
setScatteringComponent(const ObjCryst::ScatteringComponent& _sc)
{
    sc = &_sc;
    calculateDegeneracy();

    // Assign the first half of the bp
    float x, y, z;
    x = sc->mX;
    y = sc->mY;
    z = sc->mZ;
    crystal.FractionalToOrthonormalCoords(x,y,z);
    bp.xyz1[0] = x;
    bp.xyz1[1] = y;
    bp.xyz1[2] = z;
    bp.sc1 = sc;

    //rewind();
    return;
}

/* Rewind the iterator.
 *
 * This detects changes in the crystal and regenerates the unit cell if
 * necessary.
 */
void
SrReal::BondIterator::
rewind()
{

    // update the iterator (if necessary).
    update();

    if( sc == NULL ) 
    {
        isfinished = true;
        return;
    }

    if(sscvec.size() == 0) 
    {
        isfinished = true;
        return;
    }

    if(rmax == 0)
    {
        isfinished = true;
    }
    
    // Prepare for the incrementor.
    iteri = sscvec.begin();
    sph->rewind();
    isfinished = false;

    // Initialize the first bond
    next();

    return;

}

/* This sets bp to the next bond in the iteration sequence.
 *
 * The duty of this function is to call the incrementer. It is the
 * responsibility of the incrementer to set bp and indicate if it was
 * successful.
 */
void
SrReal::BondIterator::
next()
{

    if( isfinished ) return;

    isfinished = !increment();

    return;

}

bool
SrReal::BondIterator::
finished()
{
    return isfinished;
}

/* Get the bond pair from the iterator */
BondPair
SrReal::BondIterator::
getBondPair()
{
    return bp;
}

/*****************************************************************************/

/* This expands primitive cell of the crystal and fills sscvec if the crystal
 * clock is greater than the iterator clock.
 */
void
SrReal::BondIterator::
update()
{
    //std::cout << "crystal clock: "; 
    //crystclock.Print();
    //std::cout << "iterator clock: ";
    //itclock.Print();

    // Get out of here if there is nothing to update
    if(itclock >= crystal.GetClockMaster()) return;

    // Get out of here if there's no range to iterate over
    if(rmax == 0) return;

    // Synchronize the clocks
    itclock = crystal.GetClockMaster();

    // FIXME - don't need to recalculate when only atom occupancies change.
    
    // Reset the sscvec 
    sscvec.clear();
    sscvec = SrReal::getUnitCell(crystal);

    // Calculate the degeracy of sc in the new unit cell.
    calculateDegeneracy();

    if(latclock >= crystal.GetClockLatticePar()) return;
    latclock = crystal.GetClockLatticePar();

    if(sph != NULL) delete sph;
    sph = new PointsInSphere((float) rmin, (float) rmax, 
        crystal.GetLatticePar(0),
        crystal.GetLatticePar(1),
        crystal.GetLatticePar(2),
        rtod*crystal.GetLatticePar(3),
        rtod*crystal.GetLatticePar(4),
        rtod*crystal.GetLatticePar(5)
       );

    return;
}


/* This increments the iterator. We don't assume any symmetry in the position of
 * the origin atom, so the degeneracy is 1.
 *
 * Returns true when the incrementer progressed, false otherwise.
 */
bool
SrReal::BondIterator::
increment()
{
    /* iteri = slow iterator (outer loop)
     * sph = fast iterator (inner loop)
     */

    // Terminate when the outer loop finishes.
    if(iteri == sscvec.end())
    {
        return false;
    }
    // Skip this scatterer with itself
    // FIXME - this comparison is not right. Must create a ShiftedSC from the
    // ScatteringComponent as is done in getUnitCell.
    //if( sph->mno[0] == 0 &&
    //    sph->mno[0] == 0 &&
    //    sph->mno[0] == 0 &&
    //  )
    //{

    //    // Increment sph
    //    sph->next();
    //    // If sph is finished, then we reset it and increment iteri
    //    if( sph->finished() )
    //    {
    //        sph->rewind();
    //        ++iteri;
    //    }
    //    return increment();
    //}

    // If we got here, then we can record the bond
    for(size_t l=0;l<3;++l)
    {
        bp.xyz2[l] = iteri->xyz[l];
    }
    placeInSphere(bp.xyz2);
    bp.sc2 = iteri->sc;
    bp.multiplicity = degen;

    // Increment sph
    sph->next();

    // If sph is finished, then we reset it and increment iteri
    if( sph->finished() )
    {
        sph->rewind();
        ++iteri;
    }

    return true;
}

/* Place cartesian coordinates xyz in the location defined by the PointsInSphere
 * iterator.
 */
void
SrReal::BondIterator::
placeInSphere(float *xyz)
{ 
    float dxyz[3];
    for(size_t l=0; l<3; ++l)
    {
        dxyz[l] = sph->mno[l];
    }
    crystal.FractionalToOrthonormalCoords(dxyz[0], dxyz[1], dxyz[2]);
    for(size_t l=0; l<3; ++l) 
    {
        xyz[l] += dxyz[l];
    }
    return;
}

/* Calculate the degeneracy of the ScatteringComponent */
void 
SrReal::BondIterator::
calculateDegeneracy()
{
    degen = 0;

    if(sc == NULL) return;

    if(crystal.GetSpaceGroup().GetName() == ObjCryst::SpaceGroup().GetName())
    {
        degen = 1;
        return;
    }

    // FIXME This is a slow way to do things, but it works for now.
    for(iteri=sscvec.begin(); iteri != sscvec.end(); ++iteri)
    {
        if( (*(iteri->sc)) == *sc ) ++degen;
    }
}

/******************************************************************************
***** ShiftedSC implementation ************************************************
******************************************************************************/

ShiftedSC::
ShiftedSC(const ObjCryst::ScatteringComponent *_sc,
    const float x, const float y, const float z, const int _id) :
    sc(_sc), id(_id)
{
    //sc->Print();
    xyz[0] = x;
    xyz[1] = y;
    xyz[2] = z;
    //std::cout << x << ' ' << y << ' ' << z << endl;
}

ShiftedSC::
ShiftedSC(const ShiftedSC& _ssc)
{
    id = _ssc.id;
    sc = _ssc.sc;
    //sc->Print();
    xyz[0] = _ssc.xyz[0];
    xyz[1] = _ssc.xyz[1];
    xyz[2] = _ssc.xyz[2];
    //std::cout << x << ' ' << y << ' ' << z << endl;
}

// Be careful of dangling references
ShiftedSC::
ShiftedSC()
{
    xyz[0] = xyz[1] = xyz[2] = 0;
    id = -1;
    sc = NULL;
}

bool
ShiftedSC::
operator<(const ShiftedSC& rhs) const
{
    // The sign of A-B is equal the sign of the first non-zero component of the
    // vector.

    const float toler = 1e-5;
    size_t l;

    for(l = 0; l < 3; ++l)
    {
        if( fabs(xyz[l] - rhs.xyz[l]) > toler )
        {
            return xyz[l] < rhs.xyz[l];
        }
    }

    // If we get here then the vectors are equal. We compare the addresses of
    // the ScatteringPower member of the ScatteringComponent
    return sc->mpScattPow < rhs.sc->mpScattPow;

}

bool
ShiftedSC::
operator==(const ShiftedSC& rhs) const
{

    //std::cout << id << " vs " << rhs.id << endl;

    return ((xyz[0] == rhs.xyz[0]) 
        && (xyz[1] == rhs.xyz[1]) 
        && (xyz[2] == rhs.xyz[2])
        && (*sc == *(rhs.sc)));
}

/* Utility functions */

vector<ShiftedSC> 
SrReal::
getUnitCell(const ObjCryst::Crystal& crystal)
{
    // Expand each scattering component in the primitive cell and record the new
    // atoms.
    const ObjCryst::ScatteringComponentList& mScattCompList 
        = crystal.GetScatteringComponentList();

    size_t nbComponent = mScattCompList.GetNbComponent();

    // Get the origin of the primitive unit
    const float x0 = crystal.GetSpaceGroup().GetAsymUnit().Xmin();
    const float y0 = crystal.GetSpaceGroup().GetAsymUnit().Ymin();
    const float z0 = crystal.GetSpaceGroup().GetAsymUnit().Zmin();

    size_t nbSymmetrics = crystal.GetSpaceGroup().GetNbSymmetrics();
    std::cout << "nbComponent = " << nbComponent << std::endl;
    std::cout << "nbSymmetrics = " << nbSymmetrics << std::endl;

    float x, y, z;
    float junk;
    CrystMatrix<float> symmetricsCoords;
    set<ShiftedSC> workset;
    vector<ShiftedSC> workvec;
    set<ShiftedSC>::iterator it1;
    ShiftedSC workssc;
    // For each scattering component, find its position in the primitive cell
    // and expand that position. Record this as a ShiftedSC.
    for(size_t i=0;i<nbComponent;++i)
    {
        symmetricsCoords = crystal.GetSpaceGroup().GetAllSymmetrics(
            mScattCompList(i).mX, 
            mScattCompList(i).mY, 
            mScattCompList(i).mZ
            );

        // Put each symmetric position in the unit cell
        for(size_t j=0;j<nbSymmetrics;++j)
        {
            x=modf(symmetricsCoords(j,0)-x0,&junk);
            y=modf(symmetricsCoords(j,1)-y0,&junk);
            z=modf(symmetricsCoords(j,2)-z0,&junk);
            if(x<0) x += 1.;
            if(y<0) y += 1.;
            if(z<0) z += 1.;

            // Get this in cartesian
            crystal.FractionalToOrthonormalCoords(x,y,z);

            // Store it in the scatterer set
            workssc = ShiftedSC(&mScattCompList(i),x,y,z,j);
            //std::cout << workssc << std::endl;
            workset.insert(workssc);
        }
    }

    //std::cout << "Unique Scatterers" << std::endl;
    // Now record the unique scatterers in workvec
    for(it1=workset.begin(); it1!=workset.end(); ++it1)
    {
        workvec.push_back(*it1);
        //std::cout << *it1 << std::endl;
    }

    return workvec;
}

std::ostream& 
SrReal::operator<<(ostream& os, const ShiftedSC& ssc)
{
    os << ssc.sc->mpScattPow->GetSymbol() << '(' << ssc.id << "): ";
    os << ssc.xyz[0] << " ";
    os << ssc.xyz[1] << " ";
    os << ssc.xyz[2];
    return os;
}

std::ostream& 
SrReal::operator<<(ostream& os, const BondPair& bp)
{
    os << "(" << bp.multiplicity << ") ";
    os << bp.sc1->mpScattPow->GetSymbol() << ' ';
    os << "[";
    os << bp.xyz1[0] << ", ";
    os << bp.xyz1[1] << ", ";
    os << bp.xyz1[2] << "]";
    os << " -- ";
    os << bp.sc2->mpScattPow->GetSymbol() << ' ';
    os << "[";
    os << bp.xyz2[0] << ", ";
    os << bp.xyz2[1] << ", ";
    os << bp.xyz2[2] << "]";

    return os;
}
