/*
 * ax1_output2d.hh
 *
 *  Created on: Mar 31, 2013
 *      Author: jpods
 */

#ifndef DUNE_AX1_OUTPUT2D_HH
#define DUNE_AX1_OUTPUT2D_HH


#include <valarray>
#include <vector>
#include <algorithm>
#include <iomanip>

#include <dune/geometry/virtualrefinement.hh>
#include <dune/geometry/quadraturerules.hh>
#include <dune/grid/common/gridenums.hh>
#include <dune/grid/common/scsgmapper.hh>

#include <dune/ax1/common/constants.hh>
#include <dune/ax1/common/tools.hh>
#include <dune/ax1/common/output.hh>
#include <dune/ax1/common/ax1_gridfunction_outputstrategies.hh>
#include <dune/ax1/acme2_cyl/common/acme2_cyl_geometrywrapper.hh>


template<typename AcmeOutputTraits>
class Ax1Output2D
{
  public:
    static const int prec = 12;
    static constexpr double eps = 1e-8;

    //! Information about the solution vector dimensions generated by getSolutionVector()
    struct SolutionVectorInfo
    {
      SolutionVectorInfo()
      : dimensions(2,-1),
        nValuesPerElement(2,-1),
        tElapsed(0.0)
      {}

      std::vector<int> dimensions;
      std::vector<int> nValuesPerElement;
      double tElapsed;
    };

//    template<typename PHYSICS, typename DGF>
//    static void getSolutionVector(
//        const PHYSICS& physics,
//        const DGF& udgf,
//        int subSamplingPoints,
//        std::vector<typename DGF::Traits::DomainType>& pos,
//        std::vector<typename DGF::Traits::RangeType>& sol)
//    {
//      SolutionVectorInfo info;
//      return Ax1Output2D<AcmeOutputTraits>::getSolutionVector(physics,udgf,subSamplingPoints,pos,sol,info);
//    }

    template<typename PHYSICS, typename DGF>
    static void getSolutionVector(
        const PHYSICS& physics,
        const DGF& udgf,
        int subSamplingPoints,
        std::vector<typename PHYSICS::Element::Geometry::GlobalCoordinate>& pos,
        std::vector<typename DGF::Traits::RangeType>& sol,
        SolutionVectorInfo& info)
    {
      Dune::Timer timer;
//      debug_jochen << "GF "
//          << Tools::getTypeName(udgf)
//          << " has output strategy "
//          << OutputStrategy<AcmeOutputTraits,DGF>::value << std::endl;

      // Note to self: I originally wanted to call a helper function template here, which is specialized
      // for all values of 'OutputStrategy<AcmeOutputTraits,DGF>::value'. But since function templates cannot
      // be partially specialized, the function has to be wrapped into types which CAN be partially specialized.
      // This is done below in struct 'strategy'.
      strategy<PHYSICS,DGF,OutputStrategy<AcmeOutputTraits,DGF>::value>::getSolutionVector(
          physics,udgf,subSamplingPoints,pos,sol,info);

      info.tElapsed = timer.elapsed();
    }

    template<typename PHYSICS, typename DGF, int>
    struct strategy
    {

      static void getSolutionVector(const PHYSICS& physics,
          const DGF& udgf,
          int subSamplingPoints,
          std::vector<typename PHYSICS::Element::Geometry::GlobalCoordinate>& pos,
          std::vector<typename DGF::Traits::RangeType>& sol,
          SolutionVectorInfo& info)
      {
        DUNE_THROW(Dune::Exception, "Could not find an output strategy for GF type "
                  << Tools::getTypeName(udgf));
      }

    };

    template<typename PHYSICS, typename DGF>
    struct strategy<PHYSICS,DGF,OutputPoints::OUTPUT_VERTICES_NONCONFORMING>
    {
      static void getSolutionVector(const PHYSICS& physics,
          const DGF& udgf,
          int subSamplingPoints,
          std::vector<typename PHYSICS::Element::Geometry::GlobalCoordinate>& pos,
          std::vector<typename DGF::Traits::RangeType>& sol,
          SolutionVectorInfo& info)
      {
        Ax1Output2D::getSubSampledSolutionVector(physics, udgf, subSamplingPoints, pos, sol, info);
      }
    };

    template<typename PHYSICS, typename DGF>
    struct strategy<PHYSICS,DGF,OutputPoints::OUTPUT_VERTICES_CONFORMING>
    {
      static void getSolutionVector(const PHYSICS& physics,
          const DGF& udgf,
          int subSamplingPoints,
          std::vector<typename PHYSICS::Element::Geometry::GlobalCoordinate>& pos,
          std::vector<typename DGF::Traits::RangeType>& sol,
          SolutionVectorInfo& info)
      {
        DUNE_THROW(Dune::NotImplemented, "Conforming output not implemented!");
      }
    };

    template<typename PHYSICS, typename DGF>
    struct strategy<PHYSICS,DGF,OutputPoints::OUTPUT_CELL_CENTERS>
    {
      static void getSolutionVector(const PHYSICS& physics,
          const DGF& udgf,
          int subSamplingPoints,
          std::vector<typename PHYSICS::Element::Geometry::GlobalCoordinate>& pos,
          std::vector<typename DGF::Traits::RangeType>& sol,
          SolutionVectorInfo& info)
      {
        Ax1Output2D::getVirtuallyRefinedSolutionVector(physics, udgf, subSamplingPoints, pos, sol, info);
      }
    };

    template<typename PHYSICS, typename DGF>
    struct strategy<PHYSICS,DGF,OutputPoints::OUTPUT_MEMBRANE>
    {
      static void getSolutionVector(const PHYSICS& physics,
          const DGF& udgf,
          int subSamplingPoints,
          std::vector<typename PHYSICS::Element::Geometry::GlobalCoordinate>& pos,
          std::vector<typename DGF::Traits::RangeType>& sol,
          SolutionVectorInfo& info)
      {
        // New method specifically for membrane output: Evaluate on membrane intersections
        Ax1Output2D::getMembraneSolutionVector(physics, udgf, pos, sol, info);
      }

    };

    //! \brief helper TMP to evaluate a (normal or intersection GF) on the boundary
    template<typename PHYSICS, typename GF, int outputType>
    struct evaluateGFOnBoundary
    {
      static void getSolutionVector(const PHYSICS& physics,
          const GF& gf,
          std::vector<std::vector<typename PHYSICS::Element::Geometry::GlobalCoordinate> >& pos,
          std::vector<std::vector<typename GF::Traits::RangeType> >& sol,
          std::vector<SolutionVectorInfo>& info)
      {
        Ax1Output2D::getBoundarySolutionVector(physics,gf,pos,sol,info);
      }
    };

    // \brief specialization that does the evaluation for intersection GFs
    template<typename PHYSICS, typename GF>
    struct evaluateGFOnBoundary<PHYSICS, GF, OutputPoints::OUTPUT_MEMBRANE>
    {
      static void getSolutionVector(const PHYSICS& physics,
        const GF& gf,
        std::vector<std::vector<typename PHYSICS::Element::Geometry::GlobalCoordinate> >& pos,
        std::vector<std::vector<typename GF::Traits::RangeType> >& sol,
        std::vector<SolutionVectorInfo>& info)
      {
        Ax1Output2D::getBoundaryIntersectionSolutionVector(physics,gf,pos,sol,info);
      }
    };


//    //! \brief specialization that does the evaluation for intersection GFs
//    template<typename PHYSICS, typename GF>
//    struct evaluateGFOnBoundary<PHYSICS, GF, OutputPoints::OUTPUT_MEMBRANE>
//    {
//      static void evaluate(const GF& udgf,
//          typename PHYSICS::Element& e,
//          typename PHYSICS::Element::Geometry::GlobalCoordinate& xlocalInside,
//          const int b,
//          typename GF::Traits::RangeType y)
//      {
//        debug_jochen << "  - global " << e.geometry().global(xlocalInside) << std::endl;
//
//        for(typename PHYSICS::ElementIntersectionIterator iit = udgf.getGridView().ibegin(e);
//            iit != udgf.getGridView().iend(e); ++iit)
//        {
//          // Only consider boundary intersections
//          if(iit->boundary())
//          {
//            // Now check orientation of intersection
//            Acme2CylGeometry<typename PHYSICS::ElementIntersection::Geometry> cylGeo(iit->geometry());
//
//            bool correctOrientation = false;
//            switch(b)
//            {
//              case BoundaryPositions::BOUNDARY_BOTTOM :
//              case BoundaryPositions::BOUNDARY_TOP :
//              {
//                if(cylGeo.isOrientedAxially())
//                  correctOrientation = true;
//                break;
//              }
//              case BoundaryPositions::BOUNDARY_LEFT :
//              case BoundaryPositions::BOUNDARY_RIGHT :
//              {
//                if(! cylGeo.isOrientedAxially())
//                  correctOrientation = true;
//                break;
//              }
//              default:
//                DUNE_THROW(Dune::Exception, "Unknown boundary position type b=" << b << "!");
//            }
//
//            // Intersection has correct orientation, now check actual position
//            // (paranoia check in case there are more than two boundary intersections on this element, that should
//            // rarely be the case)
//            if(correctOrientation)
//            {
//              const typename GF::Traits::DomainType xlocal = iit->geometryInInside().local(xlocalInside);
//
//              bool isIn = Tools::lessOrEqualThan(xlocal, one, 1e-6)
//                       && Tools::greaterOrEqualThan(xlocal, zero, 1e-6);
//
//              debug_jochen << "    trying intersection [" << iit->geometry().corner(0) << ", "
//                  << iit->geometry().corner(iit->geometry().corners()-1) << "], boundary: " << b
//                  << ", correctOrientation: " << correctOrientation
//                  << ", local: " << xlocal
//                  << ", isIn: " << isIn << std::endl;
//
//              if(isIn)
//              {
//                udgf.evaluate(*iit, xlocal, y);
//                return;
//              }
//            }
//          }
//        }
//
//        DUNE_THROW(Dune::Exception, "evaluateGFOnBoundary<GridDomains::DOMAIN_MEMB_INTERFACE>: did not find an"
//            << " intersection matching with the desired element-local coordinate " << xlocalInside
//            << " (global " << e.geometry().global(xlocalInside) << ")!");
//
//      }
//
//      static const typename GF::Traits::DomainType zero;
//      static const typename GF::Traits::DomainType one;
//    };

    //! \brief get positions and solution out of the discrete grid function; Insert the values into
    //! the solution vectors in an ordered way such that gnuplot can directly generate surface plots!
    template<typename PHYSICS, typename DGF>
    static void getVirtuallyRefinedSolutionVector(
        const PHYSICS& physics,
        const DGF& udgf,
        int numSamplingPoints,
        std::vector<typename PHYSICS::Element::Geometry::GlobalCoordinate>& pos,
        std::vector<typename DGF::Traits::RangeType>& sol,
        SolutionVectorInfo& info)
    {
      typedef typename DGF::Traits::GridViewType GV;
      typedef typename DGF::Traits::DomainFieldType DF;
      typedef typename GV::template Codim<0>::Entity Entity;
      typedef typename GV::template Codim<0>::EntityPointer EntityPointer;
      typedef typename GV::template Codim<0>::template Partition<Dune::PartitionIteratorType::Interior_Partition>
        ::Iterator ElementLeafIterator;

      const GV& gv = udgf.getGridView();

      // We need at least one sampling point! numSamplingPoints == 1 => no virtual refinement (lvl 0)
      numSamplingPoints = std::max(numSamplingPoints, 1);
      int vRefinementLevel = 0;
      if(numSamplingPoints > 1)
      {
        // Force numSampling points to be power of 2
        vRefinementLevel = (int) std::floor(std::log2(numSamplingPoints));
        numSamplingPoints = std::pow(2, vRefinementLevel);
      }

      // dimensions
      const int dim = DGF::Traits::ElementType::Geometry::dimension;
      const int dimw = DGF::Traits::ElementType::Geometry::dimensionworld;

      int subBlockSize = physics.nElements(gv,0) * numSamplingPoints;

      // gv.size(0) gives the number of elements for the All_Partition! => use custom method from physics
      int solSize = physics.nElements(gv) * std::pow(numSamplingPoints, dim); // + gv.size(1) + offset;
      pos.resize(solSize);
      sol.resize(solSize);

      //debug_jochen << std::endl;
      //debug_jochen << "subBlockSize = " << subBlockSize << std::endl;
      //debug_jochen << "solSize = " << solSize << std::endl;

      typename DGF::Traits::DomainFieldType yCurrent(0.0);

      typename DGF::Traits::DomainType x(0.0);
      typename DGF::Traits::RangeType y;

      int minimalSubBlockIndex = 0;

      int max_i = 0;
      int totalElemCount = 0;
      int elemCount = 0;
      int iGlobal = 0;
      for (ElementLeafIterator eit = gv.template begin<0,Dune::PartitionIteratorType::Interior_Partition> ();
          eit != gv.template end<0,Dune::PartitionIteratorType::Interior_Partition> (); ++eit)
      {
        typename DGF::Traits::ElementType& e = *eit;

        int iLocal = 0;
        int localSubBlockIndex = 0;
        int globalSubBlockIndex = 0;
        int localIndexInSubBlock = 0;
        int globalIndexInSubBlock = 0;

        if(iGlobal == 0)
        {
          // Save the y coordinate of the lower left corner of the first element in this grid view
          yCurrent = e.geometry().corner(0)[1];
        }

        // Compare y coordinate of element's lower left corner with yCurrent
        if(std::abs(yCurrent - e.geometry().corner(0)[1]) > eps)
        {
          // Open up a new row of elements if x coordinates do not match
          yCurrent = e.geometry().corner(0)[1];
          minimalSubBlockIndex += numSamplingPoints;

          // Fill info struct after completion of first x-row
          if(info.dimensions[0] < 0)
          {
            info.dimensions[0] = iGlobal;
            info.nValuesPerElement[0] = elemCount / iGlobal;
          }

          elemCount = 0;
        }

        // Get reference to a VirtualRefinement object for subsampling
        Dune::GeometryType gt = e.geometry().type();
        Dune::VirtualRefinement<dim, DF>& vRefinement = Dune::template buildRefinement<dim,DF>(gt, gt);

        for (typename Dune::VirtualRefinement<dim, DF>::ElementIterator
                it=vRefinement.eBegin(vRefinementLevel); it != vRefinement.eEnd(vRefinementLevel); ++it)
        {
          localSubBlockIndex = iLocal / numSamplingPoints;
          globalSubBlockIndex = minimalSubBlockIndex + localSubBlockIndex;

          localIndexInSubBlock = iLocal % numSamplingPoints;
          globalIndexInSubBlock = elemCount * numSamplingPoints + localIndexInSubBlock;

          // flat index for the large solution vector
          int i = globalSubBlockIndex * subBlockSize + globalIndexInSubBlock;
          int max_i = std::max(i, max_i);

          x = it.coords();
          udgf.evaluate(e, x, y);

          //debug_jochen << "#" << i << " x = " << e.geometry().global(x) << ", y = " << y << std::endl;
          assert(i < solSize);

          pos[i] = e.geometry().global(x);
          sol[i] = y;

          iGlobal++;
          iLocal++;
        }
        elemCount++;
        totalElemCount++;
      }


      // Fill info struct after vector was set up completely
      if(info.dimensions[0] < 0) // There was only one layer of elements (membrane?)
      {
        info.dimensions[0] = elemCount;
        info.nValuesPerElement[0] = elemCount / iGlobal;
      }
      info.dimensions[1] = sol.size() / info.dimensions[0];
      info.nValuesPerElement[1] = info.nValuesPerElement[0]; // Assume equal number of values in each direction

//      debug_jochen << "sol.size() = " << sol.size() << std::endl;
//      debug_jochen << "dimensions[0] = " << info.dimensions[0] << std::endl;
//      debug_jochen << "dimensions[1] = " << info.dimensions[1] << std::endl;
//      debug_jochen << "nValuesPerElement[0] = " << info.nValuesPerElement[0] << std::endl;
//      debug_jochen << "nValuesPerElement[1] = " << info.nValuesPerElement[1] << std::endl;

      assert(info.dimensions[0]*info.dimensions[1] == sol.size());
    }

    //! \brief get positions and solution out of the discrete grid function; Insert the values into
    //! the solution vectors in an ordered way such that gnuplot can directly generate surface plots!
    //! This version includes the element corners (vertices) - in contrast to the above method, which
    //! only does virtual refinement on the interior of an element
    template<typename PHYSICS, typename DGF>
    static void getSubSampledSolutionVector(
        const PHYSICS& physics,
        const DGF& udgf,
        int numSamplingPoints,
        std::vector<typename PHYSICS::Element::Geometry::GlobalCoordinate>& pos,
        std::vector<typename DGF::Traits::RangeType>& sol,
        SolutionVectorInfo& info)
    {
      typedef typename DGF::Traits::GridViewType GV;
      typedef typename DGF::Traits::DomainFieldType DF;
      typedef typename GV::template Codim<0>::Entity Entity;
      typedef typename GV::template Codim<0>::EntityPointer EntityPointer;
      typedef typename GV::template Codim<0>::template Partition<Dune::PartitionIteratorType::Interior_Partition>
        ::Iterator ElementLeafIterator;

      const GV& gv = udgf.getGridView();

      // dimensions
      const int dim = DGF::Traits::ElementType::Geometry::dimension;
      const int dimw = DGF::Traits::ElementType::Geometry::dimensionworld;

      // Add 2 for the vertices of each element
      int subBlockSize = physics.nElements(gv,0) * (numSamplingPoints + 2);

      int solSize = physics.nElements(gv) * std::pow(numSamplingPoints + 2, dim); // + gv.size(1) + offset;
      pos.resize(solSize);
      sol.resize(solSize);

//      debug_jochen << std::endl;
//      debug_jochen << "subBlockSize = " << subBlockSize << std::endl;
//      debug_jochen << "solSize = " << solSize << std::endl;

      typename DGF::Traits::DomainFieldType yCurrent(0.0);
      int minimalSubBlockIndex = 0;

      int elemCount = 0;
      int iGlobal = 0;
      for (ElementLeafIterator eit = gv.template begin<0,Dune::PartitionIteratorType::Interior_Partition> ();
          eit != gv.template end<0,Dune::PartitionIteratorType::Interior_Partition> (); ++eit)
      {
        typename DGF::Traits::ElementType& e = *eit;

        int iLocal = 0;
        int localSubBlockIndex = 0;
        int globalSubBlockIndex = 0;
        int localIndexInSubBlock = 0;
        int globalIndexInSubBlock = 0;

        if(iGlobal == 0)
        {
          // Save the y coordinate of the lower left corner of the first element in this grid view
          yCurrent = e.geometry().corner(0)[1];
        }

        // Compare y coordinate of element's lower left corner with yCurrent
        if(std::abs(yCurrent - e.geometry().corner(0)[1]) > eps)
        {
          // Open up a new row of elements if x coordinates do not match
          yCurrent = e.geometry().corner(0)[1];
          minimalSubBlockIndex += (numSamplingPoints + 2);

          // Fill info struct after completion of first x-row
          if(info.dimensions[0] < 0)
          {
            int nValuesPerElement = iGlobal / elemCount;

            // Assume equal number of values in each diretion!
            info.nValuesPerElement[0] = (int) std::sqrt(nValuesPerElement);
            info.dimensions[0] = info.nValuesPerElement[0] * elemCount;

//            debug_jochen << "Setting info.dimensions[0] to " << info.dimensions[0]
//              << " -> info.nValuesPerElement[0] = " << info.nValuesPerElement[0] << std::endl;
          }

          elemCount = 0;
        }

        // Manual subsampling on this element
        typename DGF::Traits::DomainType x(0.0);
        typename DGF::Traits::RangeType y;
        for (int k=0; k<numSamplingPoints+2; k++)
        {
          // Reset x coordinate
          x[0] = 0.0;

          for(int l=0; l<numSamplingPoints+2; l++)
          {
            localSubBlockIndex = iLocal / (numSamplingPoints + 2);
            globalSubBlockIndex = minimalSubBlockIndex + localSubBlockIndex;

            localIndexInSubBlock = iLocal % (numSamplingPoints + 2);
            globalIndexInSubBlock = elemCount * (numSamplingPoints + 2) + localIndexInSubBlock;

            // flat index for the large solution vector
            int i = globalSubBlockIndex * subBlockSize + globalIndexInSubBlock;

            udgf.evaluate(e, x, y);

//            debug_jochen << "#" << i << " x = " << e.geometry().global(x) << ", y = " << y << std::endl;
            assert(i < solSize);

            pos[i] = e.geometry().global(x);
            sol[i] = y;

            iGlobal++;
            iLocal++;

            // Increment x coordinate
            x[0] += 1. / (numSamplingPoints+1);
          }
          // Increment y coordinate
          x[1] += 1./ (numSamplingPoints+1);
        }
        elemCount++;
      }

      // Fill info struct after vector was set up completely
      if(info.dimensions[0] < 0) // There was only one layer of elements (membrane?)
      {
        info.dimensions[0] = elemCount;
        info.nValuesPerElement[0] = elemCount / iGlobal;
      }
      info.dimensions[1] = sol.size() / info.dimensions[0];
      info.nValuesPerElement[1] = info.nValuesPerElement[0]; // Assume equal number of values in each direction

      assert(info.dimensions[0]*info.dimensions[1] == sol.size());
    }

    //! \brief get positions and solution out of the discrete grid function; Insert the values into
    //! the solution vectors in an ordered way such that gnuplot can directly generate surface plots!
    //! This version includes the element corners (vertices) - in contrast to the above method, which
    //! only does virtual refinement on the interior of an element
    template<typename PHYSICS, typename DGF>
    static void getCellCenterSolutionVector(
        const PHYSICS& physics,
        const DGF& udgf,
        std::vector<typename PHYSICS::Element::Geometry::GlobalCoordinate>& pos,
        std::vector<typename DGF::Traits::RangeType>& sol,
        SolutionVectorInfo& info)
    {
      typedef typename DGF::Traits::GridViewType GV;
      typedef typename DGF::Traits::DomainFieldType DF;
      typedef typename GV::template Codim<0>::Entity Entity;
      typedef typename GV::template Codim<0>::EntityPointer EntityPointer;
      typedef typename GV::template Codim<0>::template Partition<Dune::PartitionIteratorType::Interior_Partition>
        ::Iterator ElementLeafIterator;

      const GV& gv = udgf.getGridView();

      // dimensions
      const int dim = DGF::Traits::ElementType::Geometry::dimension;
      const int dimw = DGF::Traits::ElementType::Geometry::dimensionworld;

      int solSize = physics.nElements(gv);
      pos.resize(solSize);
      sol.resize(solSize);

      //debug_jochen << "subBlockSize = " << subBlockSize << std::endl;
      //debug_jochen << "solSize = " << solSize << std::endl;

      int iGlobal = 0;
      for (ElementLeafIterator eit = gv.template begin<0,Dune::PartitionIteratorType::Interior_Partition> ();
          eit != gv.template end<0,Dune::PartitionIteratorType::Interior_Partition> (); ++eit)
      {
        typename DGF::Traits::ElementType& e = *eit;

        // Evaluate at cell centers only
        typename DGF::Traits::DomainType x(0.5);
        typename DGF::Traits::RangeType y;
        udgf.evaluate(e, x, y);

        //debug_jochen << "#" << iGlobal << " x = " << e.geometry().global(x) << ", y = " << y << std::endl;
        assert(iGlobal < solSize);

        pos[iGlobal] = e.geometry().global(x);
        sol[iGlobal] = y;

        iGlobal++;
      }

      // Fill info struct after vector was set up completely
      info.dimensions[0] = iGlobal;
      info.nValuesPerElement[0] = 1;
      info.dimensions[1] = sol.size() / info.dimensions[0];
      info.nValuesPerElement[1] = 1;

      assert(info.dimensions[0]*info.dimensions[1] == sol.size());
    }

    template<typename PHYSICS, typename DGF>
    static void getMembraneSolutionVector(
        const PHYSICS& physics,
        const DGF& udgf,
        std::vector<typename PHYSICS::ElementIntersection::Entity::Geometry::GlobalCoordinate>& pos,
          // we want global coordinates (dimgrid-sized vector!) for the intersection
        std::vector<typename DGF::Traits::RangeType>& sol,
        SolutionVectorInfo& info)
    {
      typedef typename DGF::Traits::GridViewType GV;
      typedef typename DGF::Traits::DomainFieldType DF;
      typedef typename PHYSICS::ElementIntersection Intersection;
      typedef typename PHYSICS::MIterator MIterator;

      const GV& gv = udgf.getGridView();

      // dimensions
      const int dim = GV::dimension;
      const int dimw = GV::dimensionworld;

      // Get number of values from physics
      int solSize = physics.nElementsForGF(udgf);
      //debug_jochen << "solSize = " << solSize << std::endl;

      pos.resize(solSize);
      sol.resize(solSize);

      int iGlobal = 0;
      for (MIterator mit = physics.mInteriorBegin(); mit != physics.mInteriorEnd(); ++mit)
      {
        const Intersection& is = *mit;

        // Evaluate at cell centers only
        typename DGF::Traits::DomainType x(0.5);
        typename DGF::Traits::RangeType y;

        //debug_jochen << "#" << iGlobal << " x = " << is.geometry().global(x);
        udgf.evaluate(is, x, y);

        //debug_jochen << ", y = " << y << std::endl;
        assert(iGlobal < solSize);

        pos[iGlobal] = is.geometry().global(x);
        sol[iGlobal] = y;

        iGlobal++;
      }

      // Fill info struct after vector was set up completely
      info.dimensions[0] = physics.nElementsForGF(udgf,0);
      info.nValuesPerElement[0] = 1;
      info.dimensions[1] = sol.size() / info.dimensions[0];
      info.nValuesPerElement[1] = 1;

      assert(iGlobal == sol.size());
      assert(info.dimensions[0]*info.dimensions[1] == sol.size());
    }

    //! sol is a 4-component vector containing the 4 boundary vectors
    template<typename PHYSICS, typename DGF>
    static void getBoundarySolutionVector(
       const PHYSICS& physics,
       const DGF& udgf,
       //std::vector<typename PHYSICS::Element::Geometry::GlobalCoordinate>& pos,
       //std::vector<typename DGF::Traits::RangeType>& sol,
       std::vector<std::vector<typename PHYSICS::Element::Geometry::GlobalCoordinate> >& pos,
       std::vector<std::vector<typename DGF::Traits::RangeType> >& sol,
       std::vector<SolutionVectorInfo>& info)
    {
      typedef typename DGF::Traits::GridViewType GV;
      typedef typename DGF::Traits::DomainFieldType DF;
      typedef typename GV::template Codim<0>::Entity Entity;
      typedef typename GV::template Codim<0>::EntityPointer EntityPointer;
      typedef typename GV::template Codim<0>::template Partition<Dune::PartitionIteratorType::Interior_Partition>
       ::Iterator ElementLeafIterator;

      typedef typename Entity::LeafIntersectionIterator IntersectionIterator;
      typedef typename IntersectionIterator::Intersection Intersection;

      const GV& gv = udgf.getGridView();

      // dimensions
      //const int dim = DGF::Traits::ElementType::Geometry::dimension;
      //const int dimw = DGF::Traits::ElementType::Geometry::dimensionworld;

      std::vector<int> subBlockSize(4, -1);
      std::vector<int> solSize(4, -1);

      pos.resize(4);
      sol.resize(4);
      info.resize(4);
      for(int b=0; b<4; b++)
      {
        // number of vertices in x-direction
        switch(b)
        {
          case BoundaryPositions::BOUNDARY_BOTTOM :
          case BoundaryPositions::BOUNDARY_TOP :
          {
            subBlockSize[b] = physics.nElements(gv,0) + 1;
            solSize[b] = subBlockSize[b];
            break;
         }
         case BoundaryPositions::BOUNDARY_LEFT :
         case BoundaryPositions::BOUNDARY_RIGHT :
         {
           subBlockSize[b] = physics.nElements(gv,1) + 1;
           solSize[b] = subBlockSize[b];
           break;
         }
         default :
           DUNE_THROW(Dune::Exception, "Unexpected boundary position identifier!");
        }
        pos[b].resize(solSize[b]);
        sol[b].resize(solSize[b]);

        //debug_jochen << "subBlockSize[" << b << "] = " << subBlockSize[b] << std::endl;
        //debug_jochen << "solSize[" << b << "] = " << solSize[b] << std::endl;
      }


      std::vector<int> iGlobal(4, 0);

//      /*FIXME This is so ridiculously unperformant: In the worst case (which basically is when writing out the
//       * top boundary), the GridFunctionToFunctionAdapter has to iterate over the whole grid for _each_ call
//       * to f.evaluate(). This loop takes about 90 seconds for a 1000x180 grid. Rewrite this!
//       */
//      typedef typename GV::template Codim<GV::dimension>
//       ::template Partition<Dune::PartitionIteratorType::InteriorBorder_Partition>::Iterator NodeIterator;
//      for(NodeIterator nit = gv.template begin<GV::dimension,Dune::PartitionIteratorType::InteriorBorder_Partition>();
//         nit != gv.template end<GV::dimension,Dune::PartitionIteratorType::InteriorBorder_Partition>(); ++nit)
//      {

      for(typename PHYSICS::ElementIterator eit = gv.template begin<0,Dune::PartitionIteratorType::Interior_Partition>();
          eit != gv.template end<0,Dune::PartitionIteratorType::Interior_Partition>(); ++eit)
      {
        for(int i=0; i<eit->geometry().corners(); i++)
        {
          typename PHYSICS::Element::Geometry::GlobalCoordinate x = eit->geometry().corner(i);

          bool top = x[1]+1e-6 > physics.getParams().yMax();
          bool bottom = x[1]-1e-6 < physics.getParams().yMin();
          bool left = x[0]-1e-6 < physics.getParams().xMin();
          bool right = x[0]+1e-6 > physics.getParams().xMax();

          // Loop over possible boundaries (remember: one node may belong to more than one boundary)
          for(int b=0; b<4; b++)
          {
            // Check if current position is on any boundary
            if((top && b == BoundaryPositions::BOUNDARY_TOP)
                || (left && b == BoundaryPositions::BOUNDARY_LEFT)
                || (right && b == BoundaryPositions::BOUNDARY_RIGHT)
                || (bottom && b == BoundaryPositions::BOUNDARY_BOTTOM))
            {
              // Check if current node is already in the solution vector
              bool isIn = false;
              if(iGlobal[b] > 0)
              {
                typename PHYSICS::Element::Geometry::GlobalCoordinate x_diff = pos[b][iGlobal[b]-1];
                x_diff -= x;
                if(x_diff.two_norm() < 1e-6) isIn = true;
              }
              if(! isIn)
              {
                // Evaluate function at boundary vertex
                typename PHYSICS::Element::Geometry::GlobalCoordinate xlocal = eit->geometry().local(x);

                udgf.evaluate(*eit, xlocal, sol[b][iGlobal[b]]);
                pos[b][iGlobal[b]] = x;

                //debug_jochen << "boundary " << b << ", value=" << sol[b][iGlobal[b]]
                //  << " @ position " << pos[b][iGlobal[b]] << std::endl;

                iGlobal[b]++;
              }
            }
          }
        }
      }

      // If less elements than solSize were inserted (e.g. because this processor does not have
      // any boundary vertices at the specified boundary position), resize the vector to its actual size
      for(int b=0; b<4; b++)
      {
        if(iGlobal[b] < solSize[b])
        {
          sol[b].resize(iGlobal[b]);
          pos[b].resize(iGlobal[b]);
        }
        switch(b)
        {
          case BoundaryPositions::BOUNDARY_BOTTOM :
          case BoundaryPositions::BOUNDARY_TOP :
          {
            info[b].dimensions[0] = iGlobal[b];
            info[b].nValuesPerElement[0] = -1;
            info[b].dimensions[1] = 1;
            info[b].nValuesPerElement[1] = -1;

            break;
         }
         case BoundaryPositions::BOUNDARY_LEFT :
         case BoundaryPositions::BOUNDARY_RIGHT :
         {
           info[b].dimensions[0] = 1;
           info[b].nValuesPerElement[0] = -1;
           info[b].dimensions[1] = iGlobal[b];
           info[b].nValuesPerElement[1] = -1;

           break;
         }
         default :
           DUNE_THROW(Dune::Exception, "Unexpected boundary position identifier!");
        }
        assert(info[b].dimensions[0]*info[b].dimensions[1] == sol[b].size());
      }

    }


    //! sol is a 4-component vector containing the 4 boundary vectors
    template<typename PHYSICS, typename DGF>
    static void getBoundaryIntersectionSolutionVector(
       const PHYSICS& physics,
       const DGF& udgf,
       //std::vector<typename PHYSICS::Element::Geometry::GlobalCoordinate>& pos,
       //std::vector<typename DGF::Traits::RangeType>& sol,
       std::vector<std::vector<typename PHYSICS::Element::Geometry::GlobalCoordinate> >& pos,
       std::vector<std::vector<typename DGF::Traits::RangeType> >& sol,
       std::vector<SolutionVectorInfo>& info)
    {
      typedef typename DGF::Traits::GridViewType GV;
      typedef typename DGF::Traits::DomainFieldType DF;
      typedef typename GV::template Codim<0>::Entity Entity;
      typedef typename GV::template Codim<0>::EntityPointer EntityPointer;
      typedef typename GV::template Codim<0>::template Partition<Dune::PartitionIteratorType::Interior_Partition>
       ::Iterator ElementLeafIterator;

      typedef typename Entity::LeafIntersectionIterator IntersectionIterator;
      typedef typename IntersectionIterator::Intersection Intersection;

      const GV& gv = udgf.getGridView();

      std::vector<int> subBlockSize(4, -1);
      std::vector<int> solSize(4, -1);

      pos.resize(4);
      sol.resize(4);
      info.resize(4);
      for(int b=0; b<4; b++)
      {
        // number of vertices in x-direction
        switch(b)
        {
          case BoundaryPositions::BOUNDARY_BOTTOM :
          case BoundaryPositions::BOUNDARY_TOP :
          {
            subBlockSize[b] = physics.nElements(gv,0);
            solSize[b] = subBlockSize[b];
            break;
         }
         case BoundaryPositions::BOUNDARY_LEFT :
         case BoundaryPositions::BOUNDARY_RIGHT :
         {
           subBlockSize[b] = physics.nElements(gv,1);
           solSize[b] = subBlockSize[b];
           break;
         }
         default :
           DUNE_THROW(Dune::Exception, "Unexpected boundary position identifier!");
        }
        pos[b].resize(solSize[b]);
        sol[b].resize(solSize[b]);

        //debug_jochen << "subBlockSize[" << b << "] = " << subBlockSize[b] << std::endl;
        //debug_jochen << "solSize[" << b << "] = " << solSize[b] << std::endl;
      }

      std::vector<int> iGlobal(4, 0);
      for(typename PHYSICS::ElementIterator eit = gv.template begin<0,Dune::PartitionIteratorType::Interior_Partition>();
          eit != gv.template end<0,Dune::PartitionIteratorType::Interior_Partition>(); ++eit)
      {

        typename PHYSICS::Element& e = *eit;
        for(typename PHYSICS::ElementIntersectionIterator iit = gv.ibegin(e); iit != gv.iend(e); ++iit)
        {
          if(iit->boundary())
          {
            typename PHYSICS::Element::Geometry::GlobalCoordinate x = iit->geometry().center();

            bool top = x[1]+1e-6 > physics.getParams().yMax();
            bool bottom = x[1]-1e-6 < physics.getParams().yMin();
            bool left = x[0]-1e-6 < physics.getParams().xMin();
            bool right = x[0]+1e-6 > physics.getParams().xMax();

            // Loop over possible boundaries (remember: one node may belong to more than one boundary)
            for(int b=0; b<4; b++)
            {
              // Check if current position is on any boundary
              if((top && b == BoundaryPositions::BOUNDARY_TOP)
                  || (left && b == BoundaryPositions::BOUNDARY_LEFT)
                  || (right && b == BoundaryPositions::BOUNDARY_RIGHT)
                  || (bottom && b == BoundaryPositions::BOUNDARY_BOTTOM))
              {
                // Check if current node is already in the solution vector (superfluous for cell center evaluation)
                bool isIn = false;
                if(iGlobal[b] > 0)
                {
                  typename PHYSICS::Element::Geometry::GlobalCoordinate x_diff = pos[b][iGlobal[b]-1];
                  x_diff -= x;
                  if(x_diff.two_norm() < 1e-6) isIn = true;
                }
                if(! isIn)
                {
                  // Evaluate function at boundary vertex
                  typename DGF::Traits::DomainType xlocal(0.5);

                  //udgf.evaluate(*eit, xlocal, sol[b][iGlobal[b]]);
                  udgf.evaluate(*iit, xlocal, sol[b][iGlobal[b]]);
                  pos[b][iGlobal[b]] = x;

                  //debug_jochen << "boundary " << b << ", value=" << sol[b][iGlobal[b]]
                  //  << " @ position " << pos[b][iGlobal[b]] << std::endl;

                  iGlobal[b]++;
                }
              }
            }
          }
        }
      }

      // If less elements than solSize were inserted (e.g. because this processor does not have
      // any boundary vertices at the specified boundary position), resize the vector to its actual size
      for(int b=0; b<4; b++)
      {
        if(iGlobal[b] < solSize[b])
        {
          sol[b].resize(iGlobal[b]);
          pos[b].resize(iGlobal[b]);
        }
        switch(b)
        {
          case BoundaryPositions::BOUNDARY_BOTTOM :
          case BoundaryPositions::BOUNDARY_TOP :
          {
            info[b].dimensions[0] = iGlobal[b];
            info[b].nValuesPerElement[0] = -1;
            info[b].dimensions[1] = 1;
            info[b].nValuesPerElement[1] = -1;

            break;
         }
         case BoundaryPositions::BOUNDARY_LEFT :
         case BoundaryPositions::BOUNDARY_RIGHT :
         {
           info[b].dimensions[0] = 1;
           info[b].nValuesPerElement[0] = -1;
           info[b].dimensions[1] = iGlobal[b];
           info[b].nValuesPerElement[1] = -1;

           break;
         }
         default :
           DUNE_THROW(Dune::Exception, "Unexpected boundary position identifier!");
        }
        assert(info[b].dimensions[0]*info[b].dimensions[1] == sol[b].size());
      }

    }


};

// phew...
//template<typename AcmeOutputTraits>
//template<typename PHYSICS, typename GF>
//const typename GF::Traits::DomainType Ax1Output2D<AcmeOutputTraits>::
//  template evaluateGFOnBoundary<PHYSICS, GF, OutputPoints::OUTPUT_MEMBRANE>::zero
//    = typename GF::Traits::DomainType(0.0);
//
//template<typename AcmeOutputTraits>
//template<typename PHYSICS, typename GF>
//const typename GF::Traits::DomainType Ax1Output2D<AcmeOutputTraits>::
//  template evaluateGFOnBoundary<PHYSICS, GF, OutputPoints::OUTPUT_MEMBRANE>::one
//    = typename GF::Traits::DomainType(1.0);

#endif /* AX1_OUTPUT2D_HH_ */
