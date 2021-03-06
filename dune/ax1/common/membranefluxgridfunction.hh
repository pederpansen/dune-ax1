/*
 * membranepotentialgridfunction.hh
 *
 *  Created on: Feb 17, 2012
 *      Author: jpods
 */

#ifndef DUNE_AX1_MEMBRANEFLUXGRIDFUNCTION_HH
#define DUNE_AX1_MEMBRANEFLUXGRIDFUNCTION_HH

#include <dune/ax1/common/intersectiongridfunction.hh>
#include <dune/ax1/common/constants.hh>
#include <dune/ax1/common/charge_layer_mori.hh>

/**
 * This is one of the central classes of the dune-ax1 simulator. It is by design a boundary gridfunction,
 * i.e. it is defined on the electrolyte-membrane interfaces and calculates trans-membrane flux
 * by iterating over all channels present on the corresponding membrane element. For each ion species,
 * the conductance with respect to the current potential jump at the membrane as well as the inner and
 * outer concentrations is calculated and cached. The calculation is done in the updateFlux method,
 * while the channel states are advanced one time step in the updateChannels method.
 * This cached raw flux (which is not allowed to change within one time step) is multiplied by the outer
 * normal and scaled by the area of the interface in the evaluate method to yield the total flux for
 * an ion species.
 */
template<typename DGF_CON, typename DGF_POT, typename PHYSICS, typename SolutionContainer>
class MembraneFluxGridFunction
  : public Dune::PDELab::IntersectionGridFunctionBase<
      Dune::PDELab::IntersectionGridFunctionTraits<typename DGF_CON::Traits::GridViewType,
                                       typename DGF_CON::Traits::RangeFieldType,
                                       DGF_CON::Traits::RangeType::dimension,
                                       typename DGF_CON::Traits::RangeType>,
   MembraneFluxGridFunction<DGF_CON,DGF_POT,PHYSICS,SolutionContainer> >
{
public:
  typedef Dune::PDELab::IntersectionGridFunctionTraits<typename DGF_CON::Traits::GridViewType,   // grid view type
                                           typename DGF_CON::Traits::RangeFieldType, // image field type (double)
                                           DGF_CON::Traits::RangeType::dimension,  // number of components of image
                                           typename DGF_CON::Traits::RangeType // image type (Dune::FieldVector<double, 1>)
                                           > Traits;

  typedef typename PHYSICS::ChannelSet CHANNELS;
  typedef typename PHYSICS::FieldType T;

  enum MembraneInterface { INTERNAL = 1, EXTERNAL = 2};
  enum FluxTerms { TOTAL_FLUX = 0, DIFF_TERM = 1, DRIFT_TERM = 2, LEAK = 3, VOLTAGE_GATED = 4};

  //! constructor
  MembraneFluxGridFunction (DGF_CON& dgfCon_, DGF_POT& dgfPot_, PHYSICS& physics_,
      SolutionContainer& solutionContainer_,
      double tEquilibrium_ = 20000)
    : dgfCon(dgfCon_),
      dgfPot(dgfPot_),
      physics(physics_),
      solutionContainer(solutionContainer_),
      tEquilibrium(tEquilibrium_),
      time(-1),
      dt(-1),
      gridView(dgfCon_.getGridView()),
      channels(physics.getChannelSet()),
      channelsInitialized(false),
      channelsPartiallyInitialized(false),
      timeStepAccepted(true),
      cachedFlux(channels.getMembraneIndices().size(), typename Traits::RangeType(0.0)),
      cachedDiffTerm(channels.getMembraneIndices().size(), typename Traits::RangeType(0.0)),
      cachedDriftTerm(channels.getMembraneIndices().size(), typename Traits::RangeType(0.0)),
      cachedLeakFlux(channels.getMembraneIndices().size(), typename Traits::RangeType(0.0)),
      cachedVoltageGatedFlux(channels.getMembraneIndices().size(), typename Traits::RangeType(0.0)),
      isActive(true),
      iterateMembraneFlux(physics.getParams().boundary.get("fullyImplicitMembraneFlux",false)
          || physics.getParams().general.get("useMoriOperatorSplit",false)),
      lengthScale(physics.getLengthScale()),
      timeScale(physics.getTimeScale()),
      temp(physics.getElectrolyte().getTemperature()),
      stdCon(physics.getElectrolyte().getStdCon())
  {
    assert(DGF_CON::Traits::RangeType::dimension == NUMBER_OF_SPECIES);
  }

  template<typename I>
  inline void evaluate (const I &is,
                        const typename Traits::DomainType &x,
                        typename Traits::RangeType &y,
                        const int fluxTerm = FluxTerms::TOTAL_FLUX) const
  {
    evaluate(is, x, y, fluxTerm, -1, false);
  }

  template<typename I>
  inline void evaluate (const I &is,
                        const typename Traits::DomainType &x,
                        typename Traits::RangeType &y,
                        const int fluxTerm,
                        int ionSpecies,
                        bool implicit,
                        const typename DGF_POT::Traits::RangeType& uPot = typename DGF_POT::Traits::RangeType(-1.0),
                        const typename DGF_CON::Traits::RangeType& uCon = typename DGF_CON::Traits::RangeType(-1.0)) const
  {
    //debug_jochen << "MembFluxGF::evaluate() @ " << is.geometry().global(x) << ", timeStepAccepted="
    //    << timeStepAccepted << std::endl;

    if(! iterateMembraneFlux)
      assert(timeStepAccepted);

    assert(implicit == false);

    y = 0.0;

    if(physics.isMembraneInterface(is))
    {
      int mIndex = physics.getMembraneIndex(is);

      assert(mIndex < cachedFlux.size());

      // Assign requested flux term to y
      switch(fluxTerm)
      {
        case FluxTerms::TOTAL_FLUX :
          y = cachedFlux[mIndex];
          break;
        case FluxTerms::DIFF_TERM :
          y = cachedDiffTerm[mIndex];
          break;
        case FluxTerms::DRIFT_TERM :
          y = cachedDriftTerm[mIndex];
          break;
        case FluxTerms::LEAK :
          y = cachedLeakFlux[mIndex];
          break;
        case FluxTerms::VOLTAGE_GATED :
          y = cachedVoltageGatedFlux[mIndex];
          break;
        default:
          DUNE_THROW(Dune::Exception, "Requested flux term is unknown: " << fluxTerm);
      }

      //debug_jochen << "flux = " << y << std::endl;

      // 2D: Multiply by y-component of outer normal, as the membrane is oriented horizontally
      y *= is.centerUnitOuterNormal()[Traits::GridViewType::Grid::dimension - 1];
    }
  }

  template<typename I>
  inline void evaluateDiffusionTerm (const I &is,
                          const typename Traits::DomainType &x,
                          typename Traits::RangeType &y) const
  {
    assert(timeStepAccepted);

    if(physics.isMembraneInterface(is))
    {
      int mIndex = physics.getMembraneIndex(is);

      assert(mIndex < cachedDiffTerm.size());

      y = cachedDiffTerm[mIndex];

      // 2D: Multiply by y-component of outer normal, as the membrane as oriented horizontally
      y *= is.centerUnitOuterNormal()[Traits::GridViewType::Grid::dimension - 1];
    }
  }

  template<typename I>
  inline void evaluateDriftTerm (const I &is,
                          const typename Traits::DomainType &x,
                          typename Traits::RangeType &y) const
  {
    assert(timeStepAccepted);

    if(physics.isMembraneInterface(is))
    {
      int mIndex = physics.getMembraneIndex(is);

      assert(mIndex < cachedDriftTerm.size());

      y = cachedDriftTerm[mIndex];

      // 2D: Multiply by y-component of outer normal, as the membrane as oriented horizontally
      y *= is.centerUnitOuterNormal()[Traits::GridViewType::Grid::dimension - 1];
    }
  }

  template<typename I>
  inline void evaluateLeakFlux (const I &is,
                                const typename Traits::DomainType &x,
                                typename Traits::RangeType &y) const
  {
    assert(timeStepAccepted);

    if(physics.isMembraneInterface(is))
    {
      int mIndex = physics.getMembraneIndex(is);

      assert(mIndex < cachedLeakFlux.size());

      y = cachedLeakFlux[mIndex];

      // 2D: Multiply by y-component of outer normal, as the membrane as oriented horizontally
      y *= is.centerUnitOuterNormal()[Traits::GridViewType::Grid::dimension - 1];
    }
  }

  template<typename I>
  inline void evaluateVoltageGatedFlux (const I &is,
                                const typename Traits::DomainType &x,
                                typename Traits::RangeType &y) const
  {
    assert(timeStepAccepted);

    if(physics.isMembraneInterface(is))
    {
      int mIndex = physics.getMembraneIndex(is);

      assert(mIndex < cachedVoltageGatedFlux.size());

      y = cachedVoltageGatedFlux[mIndex];

      // 2D: Multiply by y-component of outer normal, as the membrane as oriented horizontally
      y *= is.centerUnitOuterNormal()[Traits::GridViewType::Grid::dimension - 1];
    }
  }


  inline const typename Traits::GridViewType& getGridView () const
  {
    return gridView;
  }


  inline void updateState(double time_, double dt_)
  {
    time = time_;
    dt = dt_;

    updateState();
  }

  inline void updateState(const typename SolutionContainer::U& u)
  {
    DUNE_THROW(Dune::NotImplemented, "updateState(u) currently not supported. Use updateState() instead and make"
        << " sure that solutionContainer contains the desired solution vector(s)!");
    // This function is currently not used. If we ever use it, we for now assume that it is
    // consistent with the solutionContainer. If this should not be the case, we have to
    // think about how to use this consistently!
    assert(u == *solutionContainer.getSolutionPotOld()); // old/new?
    updateState();
  }


  /**
   * Update channel state; if channels were not initialized, they are initialized first
   *
   * @param membGV
   * @param time
   * @param dt
   */
  inline void updateState()
  {
    // Do nothing if membrane flux calculation is deactivated!
    if(!isActive)
      return;

    assert(time > -1 && dt > -1);

    timeStepAccepted = false;

    // Prevent multiple channel initializations within one timestep!
    if(! channelsInitialized && channelsPartiallyInitialized)
      return;

    // We can now use the newtonSolution's pointer to the current solution vector
    // instead of the solution from the last time step. Implement this for a truly
    // fully-implicit numerical method!
    DGF_POT dgfPotNew(solutionContainer.getGfsPot(), *solutionContainer.getSolutionPotNew());
    typename DGF_CON::BASE_DGF dgfConElecNew(solutionContainer.getGfsCon(), *solutionContainer.getSolutionConNew());
    DGF_CON dgfConNew(dgfCon.getGridView(), dgfConElecNew, physics);

    const std::vector<bool> membraneActive = physics.getParams().isMembraneActive();
    for(typename PHYSICS::MIterator mit = physics.mBegin(); mit != physics.mEnd(); ++mit)
    {
      if(membraneActive[physics.getMembraneNumber(*mit)])
      {
        int iIndex = physics.getIntersectionIndex(*mit);

        typename DGF_POT::Traits::RangeType membPot;
        if(iterateMembraneFlux)
        {
          physics.getMembranePotential(*mit, dgfPotNew, membPot);
        } else {
          physics.getMembranePotential(*mit, dgfPot, membPot);
        }
        typename DGF_POT::Traits::RangeType membPot_mV = physics.convertTo_mV(membPot);

        typename DGF_CON::Traits::RangeType conCytosol, conExtra;
        if(iterateMembraneFlux)
        {
          physics.getMembraneConcentrationJump(*mit, dgfConNew, conCytosol, conExtra);
        } else {
          physics.getMembraneConcentrationJump(*mit, dgfCon, conCytosol, conExtra);
        }

        // Update channels (i.e., gating particles)
        if(not channelsInitialized)
        {
          // Init active channels once at time == tEquilibrium
          if(not physics.getParams().doEquilibration() ||
              (std::abs(time-tEquilibrium) < 1e-6 || time > tEquilibrium)) // time >= tEquilibrium
          {

          if(not physics.getParams().general.get("automaticChannelInitialization",true))
          {
            // Initialize channels to a predefined membrane potential; override default setting
            membPot_mV = physics.getParams().general.get("channelRestingPotential",membPot_mV);
          }

          // Init channels
          debug_info << "Initializing channels to membrane potential of " << membPot_mV << " [mV]" << std::endl;
          channels.initChannels(iIndex, membPot_mV, conCytosol, conExtra);
          channelsPartiallyInitialized = true;
          }

        } else {
          // Do one time step for channels
          double real_dt = dt * physics.getTimeScale();

          //debug_jochen << "Calling channels.timeStep()!" << std::endl;
          channels.timeStep(iIndex, real_dt, membPot_mV, conCytosol, conExtra);
        }
      }
    }
  }

//  template<typename I>
//  inline void updateChannel(const I& is, double time, double dt, typename DGF_POT::Traits::RangeType& membPot,
//      typename DGF_CON::Traits::RangeType& conCytosol, typename DGF_CON::Traits::RangeType& conExtra)
//  {
//    timeStepAccepted = false;
//
//    const std::vector<bool> membraneActive = physics.getParams().isMembraneActive();
//    if(membraneActive[physics.getMembraneNumber(is)])
//    {
//      int iIndex = physics.getIntersectionIndex(is);
//
//      typename DGF_POT::Traits::RangeType membPot_mV = physics.convertTo_mV(membPot);
//
//      // Do one time step for channels
//      double real_dt = dt * physics.getTimeScale();
//
//      //debug_jochen << "Calling channels.timeStep()!" << std::endl;
//      channels.timeStep(iIndex, real_dt, membPot_mV, conCytosol, conExtra);
//
//      if(useMori)
//      {
//        chargeLayerMori.timeStep(iIndex, real_dt, membPot_mV, conCytosol, conExtra);
//      }
//    }
//  }

  /**
   * Use this when the time step was accepted, so that channel states can be updated
   *
   * @param accept
   */
  inline void acceptTimeStep()
  {
    // Do nothing if membrane flux calculation is deactivated!
    if(!isActive)
      return;

    timeStepAccepted = true;

    if(channelsPartiallyInitialized)
    {
      channelsInitialized = true;

      debug_info << std::endl;
      debug_info << "============================" << std::endl;
      debug_info << "^^ All channels initialized ^^" << std::endl;
      debug_info << "============================" << std::endl;
      debug_info << std::endl;

      channelsPartiallyInitialized = false;
    }

    channels.acceptState();

    // Set time, dt to default values
    time = -1;
    dt = -1;
  }

  bool isPartiallyInitialized() const
  {
    return channelsPartiallyInitialized;
  }

  bool isCompletelyInitialized() const
  {
    return channelsInitialized;
  }

  bool isTimeStepAccepted() const
  {
    return timeStepAccepted;
  }

  /**
   * This sets an internal flag which marks the active channels as initialized. If it is set to
   * true, the channel states will not be set steady state when tEquilibrium is reached.
   * Conversely, if it is set to false after the first initialization, the channel states can
   * be forced to be re-initialized
   * @param init
   */
  inline void markChannelsInitialized(bool init)
  {
    channelsInitialized = init;
  }

  inline void updateFlux(double time_, double dt_)
  {
    time = time_;
    dt = dt_;

    updateFlux();
  }

  inline void updateFlux(const typename SolutionContainer::U& u)
  {
    assert(u == *solutionContainer->getSolution());
    updateFlux();
  }


  /**
   * Update the trans-membrane fluxes. This is achieved in several steps:
   * 1) Potential jumps and concentrations at membrane interfaces are calculated for each membrane element
   * 2) Check whether equilibration phase is over; in this case, calculate the leak conductances for each
   *    ion species as a fraction of the (user-defined) total leak conductance, depending also on the
   *    steady-state (= resting state) values of voltage-gated conductances
   * 3) The actual fluxes are calculated for each ion species by iterating over all channels (i.e., leak
   *    conductances and voltage-gated conductances) and adding up the fluxes for matching channel types
   *
   * @param membGV
   * @param time
   * @param dt
   */
  inline void updateFlux()
  {
    // Do nothing if membrane flux calculation is deactivated!
    if(!isActive)
      return;

    assert(time > -1 && dt > -1);

    const std::vector<bool> membraneActive = physics.getParams().isMembraneActive();

    debug_verb << "membraneActive: ";
    Output::printVector(membraneActive);

    double real_dt = dt * physics.getTimeScale();

    // We can now use the newtonSolution's pointer to the current solution vector
    // instead of the solution from the last time step. Implement this for a truly
    // fully-implicit numerical method!
    DGF_POT dgfPotNew(solutionContainer.getGfsPot(), *solutionContainer.getSolutionPotNew());
    typename DGF_CON::BASE_DGF dgfConElecNew(solutionContainer.getGfsCon(), *solutionContainer.getSolutionConNew());
    DGF_CON dgfConNew(dgfCon.getGridView(), dgfConElecNew, physics);

    // Iterate over all membrane interfaces and calculate flux
    for(typename PHYSICS::MIterator miit = physics.mBegin(); miit != physics.mEnd(); ++miit)
    {
      int membNumber = physics.getMembraneNumber(*miit);
      // Do nothing when membrane is passive
      if(membraneActive[membNumber])
      {
        int iIndex = physics.getIntersectionIndex(*miit);
        int mIndex = physics.getMembraneIndex(*miit);

        assert(mIndex < cachedFlux.size());

        //if(mIndex == 0) debug_verb.push(true);
        //if(mIndex == 1) debug_verb.pop();

        // Open channels in middle membrane element only
        if(true /*mIndex == (membGV.size(0)/2 - 1)*/)
        {
          debug_verb << std::endl << "====================";
          debug_verb << "Calculating flux for membrane element #" << iIndex << "[" << mIndex << "]"
                << " (memb #" << membNumber << ") ====================" << std::endl;

          //typename DGF_CON::Traits::RangeType conCytosol, conExtracellular;
          typename DGF_CON::Traits::RangeType conRatio;

          // Get concentrations at the two membrane ends, starting from the membrane element
          if(iterateMembraneFlux)
          {
            physics.getMembraneConcentrationRatio(*miit, dgfConNew, conRatio);
          } else {
            physics.getMembraneConcentrationRatio(*miit, dgfCon, conRatio);
          }

          typename DGF_POT::Traits::RangeType potJump;
          if(iterateMembraneFlux)
          {
            physics.getMembranePotentialJump(*miit, dgfPotNew, potJump);
          } else {
            physics.getMembranePotentialJump(*miit, dgfPot, potJump);
          }

          typename DGF_POT::Traits::RangeType potJump_mV = physics.convertTo_mV(potJump);

          // Update flux
          typename Traits::RangeType totalFlux(0.0);
          typename Traits::RangeType totalDiffTerm(0.0);
          typename Traits::RangeType totalDriftTerm(0.0);
          typename Traits::RangeType totalLeakFlux(0.0);
          typename Traits::RangeType totalVoltageGatedFlux(0.0);

          for(int j=0; j<NUMBER_OF_SPECIES; j++)
          {
            debug_verb << "-------------------------------------" << std::endl;

            int valence = physics.getValence(j);

            // Constant C1 to be reused later
            double C1 = (con_k * temp) / con_e;

            // Calculated concentration-dependent reversal potential calculated by Nernst equation
            double reversal_potential = - std::log(conRatio[j]) * (C1 / valence);

            debug_verb << "[" << ION_NAMES[j] << "] Nernst potential: " << 1e3 * reversal_potential << " [mV]"
                << std::endl;

            // Compute conductance and resulting flux for every channel type
            for(int k=0; k<channels.size(); k++)
            {
              // Is this channel 'k' permeable for the current ion species 'j' at all?
              if(channels.getChannel(k).getIonSpecies() == j)
              {
                typename Traits::RangeFieldType flux(0.0);
                typename Traits::RangeFieldType conductance = 0.0;

                // In equilibration phase, only leak channels contribute to the membrane flux!
                if(physics.getParams().doEquilibration() && time < tEquilibrium
                    && not channels.getChannel(k).isLeakChannel())
                {
                  conductance = 0.0;
                } else {
                  conductance = channels.getNewEffConductance(k, iIndex);
                }

                debug_verb << " channel #" << k;

                // ====================== Calculate actual flux ============================================
                //Tools::calcTransMembraneFlux(physics, conductance, conCytosol[j], conExtracellular[j], potJump[0], j, flux);
                //'old' flux condition
                //Tools::calcTransMembraneFluxOLD(physics, conductance, conCytosol[j], conExtracellular[j], potJump[0], j, flux);


                //const typename Traits::RangeFieldType conUp = std::max(conCytosol[j], conExtracellular[j]);
                //const typename Traits::RangeFieldType conDown = std::min(conCytosol[j], conExtracellular[j]);
                //const typename Traits::RangeFieldType conJump = conCytosol[j] - conExtracellular[j];

                // "Diffusion coefficient" 10*g*(k*T)/(e^2 * z^2 * stdCon)
                //double D = (10 * conductance * con_k * temp) / (con_e * con_e * valence * valence * stdCon);

                // Constant C; Factor 10 brings conductance to SI units
                double C = (10 * conductance) / (con_e * valence * stdCon);


                debug_verb << "  [" << ION_NAMES[j] << "]_1 / [" << ION_NAMES[j] << "]_2 = " << conRatio[j]
                    <<  ", [pot] = " << potJump << " (" << potJump_mV << " mV)" << std::endl;
                debug_verb << "  E = " << 1e3 * reversal_potential << " [mV]" << std::endl;
                for(int l=0; l<channels.getChannel(k).numGatingParticles(); l++)
                {
                  debug_verb << "  p_" << l << ": " << channels.getGatingParticle(k,l,iIndex) << std::endl;
                }
                debug_verb << "  g_eff = " << channels.getEffConductance(k, iIndex) << " [mS/cm^2]" << std::endl;
                debug_verb << "  g     = " << conductance << " [mS/cm^2]" << std::endl;
                flux = 0.0;

                // Multiply flux by an arbitrary factor, use this only to speed up equilibration!
                // Attention: This value is only used when time < tEquilibrium!
                if(physics.getParams().doEquilibration() && time < tEquilibrium)
                {
                  C *= physics.getParams().general.get("dummy_value", 1.);
                }

                // Scale channel permeability to match our units
                C *= timeScale / lengthScale;

                //double diffTerm = D * direction * (conUp * std::log(conUp/conDown));
                double diffTerm = C * reversal_potential;

                // Check for equal (esp. zero!) concentrations on both sides
                if(std::abs(conRatio[j]-1) < 1e-8 || std::isnan(conRatio[j])) diffTerm = 0.0;

                // drift term already has the right orientation (by signs of valence and potJump)
                //double driftTerm = D * (valence * conUp * potJump);
                double driftTerm = C * C1 * potJump;

                // Membrane flux f_i from Nernst-Planck equation with units [(TS/LS) * mol / (m^2 * s)]
                flux = driftTerm;
                flux -= diffTerm;

                //debug_jochen << "  D = " << D << std::endl;
                //debug_jochen << "  diffusion term = " << diffTerm
                //  << ", drift term = " << driftTerm
                //  << " (" << std::abs(diffTerm)/std::abs(diffTerm + driftTerm) << ")" << std::endl;

                totalDiffTerm[j] += diffTerm;
                totalDriftTerm[j] += driftTerm;
                // =========================================================================================

                debug_verb << " => " << channels.getChannel(k).getName() << " [" << ION_NAMES[j] << "] flux = "
                    << flux << " (drift: " << driftTerm << ", diff: " << -diffTerm << ")"
                    << std::endl << std::endl;

                // Add to total flux for this ion species
                totalFlux[j] += flux;

                // Store leak and voltage-gated channel contributions separately
                if(channels.getChannel(k).isLeakChannel())
                {
                  totalLeakFlux[j] += flux;
                }
                if(channels.getChannel(k).isVoltageGated())
                {
                  totalVoltageGatedFlux[j] += flux;
                }

              }
            }

            debug_verb << "-------------------------------------" << std::endl;
            debug_verb << "=> Total [" << ION_NAMES[j] << "] flux = " << totalFlux[j] << std::endl;
          }
          debug_verb << "-------------------------------------" << std::endl;
          double sumAllFluxes = 0.0;
          for(int j=0; j<NUMBER_OF_SPECIES; j++)
          {
            sumAllFluxes += totalFlux[j];
          }
          debug_verb << "=> Total sum of all fluxes = " << sumAllFluxes << std::endl;
          debug_verb << "==========================================================" << std::endl << std::endl;

          cachedFlux[mIndex] = totalFlux;
          cachedDiffTerm[mIndex] = totalDiffTerm;
          cachedDriftTerm[mIndex] = totalDriftTerm;
          cachedLeakFlux[mIndex] = totalLeakFlux;
          cachedVoltageGatedFlux[mIndex] = totalVoltageGatedFlux;
        }
      }
    }
  }

  typename Traits::RangeType getMaxFlux()
  {
    typename Traits::RangeType maxFlux(0.0);
    for(int i=0; i<cachedFlux.size(); i++)
    {
      for(int j=0; j<NUMBER_OF_SPECIES; j++)
      {
        if(std::abs(cachedFlux[i][j]) > maxFlux[j]) maxFlux[j] = std::abs(cachedFlux[i][j]);
      }
    }
    return maxFlux;
  }

  void setActive(bool active)
  {
    isActive = active;
  }

  bool active()
  {
    return isActive;
  }


private:
  DGF_CON& dgfCon;
  DGF_POT& dgfPot;
  PHYSICS& physics;
  SolutionContainer& solutionContainer;
  double tEquilibrium;

  double time;
  double dt;

  const typename Traits::GridViewType& gridView;
  CHANNELS& channels;

  bool channelsInitialized;
  bool channelsPartiallyInitialized;
  bool timeStepAccepted;

  std::vector<typename Traits::RangeType> cachedFlux;
  std::vector<typename Traits::RangeType> cachedDiffTerm;
  std::vector<typename Traits::RangeType> cachedDriftTerm;

  std::vector<typename Traits::RangeType> cachedLeakFlux;
  std::vector<typename Traits::RangeType> cachedVoltageGatedFlux;

  bool isActive;
  const bool iterateMembraneFlux;

  const T lengthScale;
  const T timeScale;
  const T temp;
  const T stdCon;

};


#endif /* DUNE_AX1_MEMBRANEFLUXGRIDFUNCTION_HH */
