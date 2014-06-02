//*****************************************************************//
//    Albany 2.0:  Copyright 2012 Sandia Corporation               //
//    This Software is released under the BSD license detailed     //
//    in the file "license.txt" in the top-level Albany directory  //
//*****************************************************************//

#ifndef AERAS_XZHYDROSTATICPROBLEM_HPP
#define AERAS_XZHYDROSTATICPROBLEM_HPP

#include "Teuchos_RCP.hpp"
#include "Teuchos_ParameterList.hpp"

#include "Albany_AbstractProblem.hpp"

#include "Phalanx.hpp"
#include "PHAL_Workset.hpp"
#include "PHAL_Dimension.hpp"
#include "Aeras_Layouts.hpp"
#include "Aeras_GatherSolution.hpp"
#include "Aeras_ScatterResidual.hpp"
#include "Aeras_DOFInterpolation.hpp"
#include "Aeras_DOFGradInterpolation.hpp"
#include "Aeras_XZHydrostatic_VelResid.hpp"
#include "Aeras_XZHydrostatic_TemperatureResid.hpp"
#include "Aeras_XZHydrostatic_SPressureResid.hpp"
#include "Aeras_XZHydrostatic_KineticEnergy.hpp"

namespace Aeras {

  /*!
   * \brief Abstract interface for representing a 1-D finite element
   * problem.
   */
  class XZHydrostaticProblem : public Albany::AbstractProblem {
  public:
  
    //! Default constructor
    XZHydrostaticProblem(const Teuchos::RCP<Teuchos::ParameterList>& params,
		 const Teuchos::RCP<ParamLib>& paramLib,
		 const int numDim_);

    //! Destructor
    ~XZHydrostaticProblem();

    //! Return number of spatial dimensions
    virtual int spatialDimension() const { return numDim; }

    //! Build the PDE instantiations, boundary conditions, and initial solution
    virtual void buildProblem(
      Teuchos::ArrayRCP<Teuchos::RCP<Albany::MeshSpecsStruct> >  meshSpecs,
      Albany::StateManager& stateMgr);

    // Build evaluators
    virtual Teuchos::Array< Teuchos::RCP<const PHX::FieldTag> >
    buildEvaluators(
      PHX::FieldManager<PHAL::AlbanyTraits>& fm0,
      const Albany::MeshSpecsStruct& meshSpecs,
      Albany::StateManager& stateMgr,
      Albany::FieldManagerChoice fmchoice,
      const Teuchos::RCP<Teuchos::ParameterList>& responseList);

    //! Each problem must generate it's list of valid parameters
    Teuchos::RCP<const Teuchos::ParameterList>
    getValidProblemParameters() const;

  private:

    //! Private to prohibit copying
    XZHydrostaticProblem(const XZHydrostaticProblem&);
    
    //! Private to prohibit copying
    XZHydrostaticProblem& operator=(const XZHydrostaticProblem&);

  public:

    //! Main problem setup routine. Not directly called, but
    //! indirectly by following functions
    template <typename EvalT> Teuchos::RCP<const PHX::FieldTag>
    constructEvaluators(
      PHX::FieldManager<PHAL::AlbanyTraits>& fm0,
      const Albany::MeshSpecsStruct& meshSpecs,
      Albany::StateManager& stateMgr,
      Albany::FieldManagerChoice fmchoice,
      const Teuchos::RCP<Teuchos::ParameterList>& responseList);

    void constructDirichletEvaluators(const Albany::MeshSpecsStruct& meshSpecs);
    void constructNeumannEvaluators(const Teuchos::RCP<Albany::MeshSpecsStruct>& meshSpecs);

  protected:
    int numDim;
    Teuchos::RCP<Aeras::Layouts> dl;
    const int numLevels;
    const int numTracers;
    Teuchos::ArrayRCP<std::string> dof_names_tracers;
  };

}

#include "Intrepid_FieldContainer.hpp"
#include "Intrepid_DefaultCubatureFactory.hpp"
#include "Shards_CellTopology.hpp"

#include "Albany_Utils.hpp"
#include "Albany_ProblemUtils.hpp"
#include "Albany_EvaluatorUtils.hpp"
#include "Albany_ResponseUtilities.hpp"
#include "PHAL_Neumann.hpp"

#include "Aeras_XZHydrostaticResid.hpp"

template <typename EvalT>
Teuchos::RCP<const PHX::FieldTag>
Aeras::XZHydrostaticProblem::constructEvaluators(
  PHX::FieldManager<PHAL::AlbanyTraits>& fm0,
  const Albany::MeshSpecsStruct& meshSpecs,
  Albany::StateManager& stateMgr,
  Albany::FieldManagerChoice fieldManagerChoice,
  const Teuchos::RCP<Teuchos::ParameterList>& responseList)
{
  using Teuchos::RCP;
  using Teuchos::rcp;
  using Teuchos::ParameterList;
  using PHX::DataLayout;
  using PHX::MDALayout;
  using std::vector;
  using std::string;
  using std::map;
  using PHAL::AlbanyTraits;
  
  RCP<Intrepid::Basis<RealType, Intrepid::FieldContainer<RealType> > >
    intrepidBasis = Albany::getIntrepidBasis(meshSpecs.ctd);
  RCP<shards::CellTopology> cellType = rcp(new shards::CellTopology (&meshSpecs.ctd));
  
  const int numNodes = intrepidBasis->getCardinality();
  const int worksetSize = meshSpecs.worksetSize;
  
  Intrepid::DefaultCubatureFactory<RealType> cubFactory;
  RCP <Intrepid::Cubature<RealType> > cubature = cubFactory.create(*cellType, meshSpecs.cubatureDegree);
  
  const int numQPts = cubature->getNumPoints();
  const int numVertices = cellType->getNodeCount();
  
  *out << "Field Dimensions: Workset=" << worksetSize 
       << ", Vertices  = " << numVertices
       << ", Nodes     = " << numNodes
       << ", QuadPts   = " << numQPts
       << ", Dim       = " << numDim 
       << ", Neq       = " << neq 
       << ", VecDim    = " << 1 
       << ", numLevels = " << numLevels 
       << ", numTracers= " << numTracers << std::endl;
  
  //Evaluators for DOFs that depend on levels
  dl = rcp(new Aeras::Layouts(worksetSize,numVertices,numNodes,numQPts,numDim, 1, numLevels));
  Albany::EvaluatorUtils<EvalT, PHAL::AlbanyTraits> evalUtils(dl);

  // Temporary variable used numerous times below
  Teuchos::RCP<PHX::Evaluator<AlbanyTraits> > ev;

  // Define Node Field Names
  Teuchos::ArrayRCP<std::string> dof_names_nodes(1);
  Teuchos::ArrayRCP<std::string> dof_names_nodes_dot(1);
  Teuchos::ArrayRCP<std::string> dof_names_nodes_gradient(1);
  Teuchos::ArrayRCP<std::string> dof_names_nodes_resid(1);
  dof_names_nodes[0]          = "SPressure";
  dof_names_nodes_dot[0]      = dof_names_nodes[0]+"_dot";
  dof_names_nodes_gradient[0] = dof_names_nodes[0]+"_gradient";
  dof_names_nodes_resid[0]    = dof_names_nodes[0]+"_residual";

  // Define Level Field Names
  int numLevelDOF = 2;
  Teuchos::ArrayRCP<std::string> dof_names_levels(2);
  Teuchos::ArrayRCP<std::string> dof_names_levels_dot(2);
  Teuchos::ArrayRCP<std::string> dof_names_levels_gradient(2);
  Teuchos::ArrayRCP<std::string> dof_names_levels_resid(2);
  dof_names_levels[0]          = "Velx";
  dof_names_levels_dot[0]      = dof_names_levels[0]+"_dot";
  dof_names_levels_gradient[0] = dof_names_levels[0]+"_gradient";
  dof_names_levels_resid[0]    = dof_names_levels[0]+"_residual";
  dof_names_levels[1]          = "Temperature";
  dof_names_levels_dot[1]      = dof_names_levels[1]+"_dot";
  dof_names_levels_gradient[1] = dof_names_levels[1]+"_gradient";
  dof_names_levels_resid[1]    = dof_names_levels[1]+"_residual";

  // Define Tracer Field Names
  Teuchos::ArrayRCP<std::string> dof_names_tracers_dot(numTracers);
  Teuchos::ArrayRCP<std::string> dof_names_tracers_gradient(numTracers);
  Teuchos::ArrayRCP<std::string> dof_names_tracers_resid(numTracers);

  for (int t=0; t<numTracers; ++t) {
    dof_names_tracers_dot     [t] = dof_names_tracers[t]+"_dot";
    dof_names_tracers_gradient[t] = dof_names_tracers[t]+"_gradient";
    dof_names_tracers_resid   [t] = dof_names_tracers[t]+"_residual";
  }
 
  {
    RCP<ParameterList> p = rcp(new ParameterList("DOF Interpolation "+dof_names_nodes[0]));
    p->set<string>("Variable Name", dof_names_nodes[0]);
    p->set<string>("BF Name", "BF");

    ev = rcp(new Aeras::DOFInterpolation<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  {
    RCP<ParameterList> p = rcp(new ParameterList("DOF Interpolation "+dof_names_nodes_dot[0]));
    p->set<string>("Variable Name", dof_names_nodes_dot[0]);
    p->set<string>("BF Name", "BF");

    ev = rcp(new Aeras::DOFInterpolation<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  {
    RCP<ParameterList> p = rcp(new ParameterList("DOF Grad Interpolation "+dof_names_nodes_gradient[0]));
    // Input
    p->set<string>("Variable Name", dof_names_nodes[0]);
    p->set<string>("Gradient BF Name", "Grad BF");
    p->set<string>("Gradient Variable Name", dof_names_nodes_gradient[0]);

    ev = rcp(new Aeras::DOFGradInterpolation<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  fm0.template registerEvaluator<EvalT>
    (evalUtils.constructGatherCoordinateVectorEvaluator());

  fm0.template registerEvaluator<EvalT>
    (evalUtils.constructMapToPhysicalFrameEvaluator(cellType, cubature));

  fm0.template registerEvaluator<EvalT>
    (evalUtils.constructComputeBasisFunctionsEvaluator(cellType, intrepidBasis, cubature));

  { // XZHydrostatic SPressure Resid
    RCP<ParameterList> p = rcp(new ParameterList("Hydrostatic SPressure Resid"));
   
    //Input
    p->set<std::string>("Weighted BF Name", "wBF");
    p->set<std::string>("Weighted Gradient BF Name", "wGrad BF");
    p->set<std::string>("QP Variable Name", dof_names_nodes[0]);
    p->set<std::string>("QP Time Derivative Variable Name", dof_names_nodes_dot[0]);
    p->set<std::string>("Gradient QP Variable Name", dof_names_nodes_gradient[0]);
    p->set<std::string>("QP Coordinate Vector Name", "Coord Vec");
    
    p->set<RCP<ParamLib> >("Parameter Library", paramLib);

    Teuchos::ParameterList& paramList = params->sublist("XZHydrostatic Problem");
    p->set<Teuchos::ParameterList*>("XZHydrostatic Problem", &paramList);

    //Output
    p->set<std::string>("Residual Name", dof_names_nodes_resid[0]);

    ev = rcp(new Aeras::XZHydrostatic_SPressureResid<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  for (int nldof=0; nldof < numLevelDOF; ++nldof) {
    {
      RCP<ParameterList> p = rcp(new ParameterList("DOF Interpolation "+dof_names_levels[nldof]));
      p->set<string>("Variable Name", dof_names_levels[nldof]);
      p->set<string>("BF Name", "BF");
      
      ev = rcp(new Aeras::DOFInterpolation<EvalT,AlbanyTraits>(*p,dl));
      fm0.template registerEvaluator<EvalT>(ev);
    }
      
    {
      RCP<ParameterList> p = rcp(new ParameterList("DOF Interpolation "+dof_names_levels_dot[nldof]));
      p->set<string>("Variable Name", dof_names_levels_dot[nldof]);
      p->set<string>("BF Name", "BF");
      
      ev = rcp(new Aeras::DOFInterpolation<EvalT,AlbanyTraits>(*p,dl));
      fm0.template registerEvaluator<EvalT>(ev);
    }
      
    {
      RCP<ParameterList> p = rcp(new ParameterList("DOF Grad Interpolation "+dof_names_levels[nldof]));
      // Input
      p->set<string>("Variable Name", dof_names_levels[nldof]);
      p->set<string>("Gradient BF Name", "Grad BF");
      p->set<string>("Gradient Variable Name", dof_names_levels_gradient[nldof]);
      
      ev = rcp(new Aeras::DOFGradInterpolation<EvalT,AlbanyTraits>(*p,dl));
      fm0.template registerEvaluator<EvalT>(ev);
    }

  }

  {//Level Kinetic Energy 
    RCP<ParameterList> p = rcp(new ParameterList("KinieticEnergy"));
    p->set<std::string>("Weighted Gradient BF Name", "wGrad BF");
    p->set<string>("Velx", dof_names_levels[0]);
    p->set<string>("Kinetic Energy", "KineticEnergy");
  
    ev = rcp(new Aeras::XZHydrostatic_KineticEnergy<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  {//Gradient Level Kinetic Energy
    RCP<ParameterList> p = rcp(new ParameterList("Grad Kinetic Energy"));
    // Input
    p->set<string>("Variable Name", "KineticEnergy");
    p->set<string>("Gradient BF Name", "Grad BF");
    p->set<string>("Gradient Variable Name", "KineticEnergy_gradient");
  
    ev = rcp(new Aeras::DOFGradInterpolation<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  { // XZHydrostatic Velx Resid
    RCP<ParameterList> p = rcp(new ParameterList("XZHydrostatic_"+dof_names_levels_resid[0]));
   
    //Input
    p->set<std::string>("Weighted BF Name", "wBF");
    p->set<std::string>("Weighted Gradient BF Name", "wGrad BF");
    p->set<std::string>("QP Variable Name", dof_names_levels[0]);
    p->set<std::string>("QP Time Derivative Variable Name", dof_names_levels_dot[0]);
    p->set<std::string>("Gradient QP Kinetic Energy", "KineticEnergy_gradient");
    p->set<std::string>("QP Coordinate Vector Name", "Coord Vec");
    
    p->set<RCP<ParamLib> >("Parameter Library", paramLib);

    Teuchos::ParameterList& paramList = params->sublist("XZHydrostatic Problem");
    p->set<Teuchos::ParameterList*>("XZHydrostatic Problem", &paramList);


    //Output
    p->set<std::string>("Level Residual Names", dof_names_levels_resid[0]);

    ev = rcp(new Aeras::XZHydrostatic_VelResid<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  { // XZHydrostatic Temperature Resid
    RCP<ParameterList> p = rcp(new ParameterList("XZHydrostatic_"+dof_names_levels_resid[1]));
   
    //Input
    p->set<std::string>("Weighted BF Name", "wBF");
    p->set<std::string>("Weighted Gradient BF Name", "wGrad BF");
    p->set<std::string>("QP Variable Name", dof_names_levels[1]);
    p->set<std::string>("QP Time Derivative Variable Name", dof_names_levels_dot[1]);
    p->set<std::string>("Gradient QP Variable Name", dof_names_levels_gradient[1]);
    p->set<std::string>("QP Coordinate Vector Name", "Coord Vec");
    
    p->set<RCP<ParamLib> >("Parameter Library", paramLib);

    Teuchos::ParameterList& paramList = params->sublist("XZHydrostatic Problem");
    p->set<Teuchos::ParameterList*>("XZHydrostatic Problem", &paramList);

    //Output
    p->set<std::string>("Level Residual Names", dof_names_levels_resid[1]);

    ev = rcp(new Aeras::XZHydrostatic_TemperatureResid<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  // Construct Aeras Specific FEM evaluators for Vector equation
  {
    RCP<ParameterList> p = rcp(new ParameterList("Gather Solution"));

    p->set< Teuchos::ArrayRCP<std::string> >("Node Names",                  dof_names_nodes);
    p->set< Teuchos::ArrayRCP<std::string> >("Time Dependent Node Names",   dof_names_nodes_dot);

    p->set< Teuchos::ArrayRCP<std::string> >("Level Names",                 dof_names_levels);
    p->set< Teuchos::ArrayRCP<std::string> >("Time Dependent Level Names",  dof_names_levels_dot);

    p->set< Teuchos::ArrayRCP<std::string> >("Tracer Names",                dof_names_tracers);
    p->set< Teuchos::ArrayRCP<std::string> >("Time Dependent Tracer Names", dof_names_tracers_dot);

    ev = rcp(new Aeras::GatherSolution<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  {
    RCP<ParameterList> p = rcp(new ParameterList("Scatter Residual"));

    p->set< Teuchos::ArrayRCP<string> >("Node Residual Names",   dof_names_nodes_resid);
    p->set< Teuchos::ArrayRCP<string> >("Level Residual Names",  dof_names_levels_resid);
    p->set< Teuchos::ArrayRCP<string> >("Tracer Residual Names", dof_names_tracers_resid);

    p->set<string>("Scatter Field Name", "Scatter XScalarAdvection");

    ev = rcp(new Aeras::ScatterResidual<EvalT,AlbanyTraits>(*p,dl));
    fm0.template registerEvaluator<EvalT>(ev);
  }

  if (fieldManagerChoice == Albany::BUILD_RESID_FM)  {
    PHX::Tag<typename EvalT::ScalarT> res_tag("Scatter XZHydrostatic", dl->dummy);
    fm0.requireField<EvalT>(res_tag);
  }
  else if (fieldManagerChoice == Albany::BUILD_RESPONSE_FM) {
    Albany::ResponseUtilities<EvalT, PHAL::AlbanyTraits> respUtils(dl);
    return respUtils.constructResponses(fm0, *responseList, Teuchos::null, stateMgr);
  }


  return Teuchos::null;
}
#endif // AERAS_XZHYDROSTATICPROBLEM_HPP
