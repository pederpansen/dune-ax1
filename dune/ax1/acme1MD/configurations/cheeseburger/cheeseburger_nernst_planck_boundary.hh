/*
 * default_nernst_planck_boundary.hh
 *
 *  Created on: Jan 17, 2012
 *      Author: jpods
 */

#ifndef DUNE_AX1_CHEESEBURGER_NERNST_PLANCK_BOUNDARY_HH
#define DUNE_AX1_CHEESEBURGER_NERNST_PLANCK_BOUNDARY_HH

#include<dune/pdelab/localoperator/convectiondiffusionparameter.hh>

template<typename GV, typename RF, typename GF_INITIAL_CON>
class CheeseburgerNernstPlanckBoundary
{


  public:
    typedef Dune::PDELab::ConvectionDiffusionBoundaryConditions::Type BCType;
    typedef Dune::PDELab::ConvectionDiffusionParameterTraits<GV,RF> Traits;

    typedef GF_INITIAL_CON GFInitialCon;

    CheeseburgerNernstPlanckBoundary(const GF_INITIAL_CON& gfInitialCon_)
    : gfInitialCon(gfInitialCon_)
    {}

    //! boundary condition type function
    BCType
    bctype (const typename Traits::IntersectionType& is, const typename Traits::IntersectionDomainType& x,
        double time, int ionSpecies, bool isMembraneInterface = false) const
    {
      //return Dune::PDELab::ConvectionDiffusionBoundaryConditions::Neumann;
      return Dune::PDELab::ConvectionDiffusionBoundaryConditions::Outflow;
    }

    //! Dirichlet boundary condition value
    typename Traits::RangeFieldType
    g (const typename Traits::ElementType& e, const typename Traits::DomainType& x,
        double time, int ionSpecies) const
    {
      //typename Traits::DomainType xglobal = e.geometry().global(x);
      //typename Traits::RangeFieldType norm = xglobal.two_norm2();
      return 0.0;
    }

    //! Neumann boundary condition
    typename Traits::RangeFieldType
    j (const typename Traits::IntersectionType& is, const typename Traits::IntersectionDomainType& x,
        double time, int ionSpecies) const
    {
    	return std::abs(is.geometry().global(x) / ( (1.0+time)*(1.0+time) ));
    }

    //! outflow boundary condition
    typename Traits::RangeFieldType
    o (const typename Traits::IntersectionType& is, const typename Traits::IntersectionDomainType& x,
        double time, int ionSpecies) const
    {
      return 0.0;
    }

  private:
    const GF_INITIAL_CON& gfInitialCon;
};

#endif /* DUNE_AX1_CHEESEBURGER_NERNST_PLANCK_BOUNDARY_HH */
