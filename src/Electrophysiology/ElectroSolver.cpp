/*
 * ElectroSolver.cpp
 *
 *  Created on: Oct 19, 2017
 *      Author: srossi
 */


// Basic include files needed for the mesh functionality.
#include "libmesh/mesh.h"
#include "libmesh/type_tensor.h"
//#include "PoissonSolver/Poisson.hpp"

// Include files that define a simple steady system
#include "libmesh/linear_implicit_system.h"
#include "libmesh/transient_system.h"
#include "libmesh/explicit_system.h"
//
#include "libmesh/vector_value.h"
#include "libmesh/linear_solver.h"
// Define the Finite Element object.
#include "libmesh/fe.h"


// Define useful datatypes for finite element
// matrix and vector components.
#include "libmesh/sparse_matrix.h"
#include "libmesh/numeric_vector.h"
#include "libmesh/petsc_vector.h"
#include "libmesh/elem.h"

// Define the DofMap, which handles degree of freedom
// indexing.
#include "libmesh/dof_map.h"

#include "libmesh/exodusII_io.h"
//#include "libmesh/exodusII_io_helper.h"
#include "libmesh/gmv_io.h"

#include "libmesh/perf_log.h"

#include <sys/stat.h>

#include "Electrophysiology/IonicModels/NashPanfilov.hpp"
#include "Electrophysiology/IonicModels/Grandi11.hpp"
#include "Electrophysiology/IonicModels/ORd.hpp"
#include "Electrophysiology/IonicModels/TP06.hpp"
#include "Electrophysiology/ElectroSolver.hpp"
#include "Util/SpiritFunction.hpp"

#include "libmesh/petsc_linear_solver.h"
#include "libmesh/petsc_vector.h"
#include "libmesh/petsc_matrix.h"
#include "Electrophysiology/Pacing/PacingProtocolSpirit.hpp"


namespace BeatIt
{

// ///////////////////////////////////////////////////////////////////////
// ///////////////////////////////////////////////////////////////////////
typedef libMesh::TransientLinearImplicitSystem ElectroSystem;
typedef libMesh::TransientExplicitSystem IonicModelSystem;
typedef libMesh::ExplicitSystem ParameterSystem;



ElectroSolver::ElectroSolver( libMesh::EquationSystems& es, std::string model )
 : M_equationSystems(es)
 , M_exporter()
 , M_exporterNames()
 , M_ionicModelExporter()
 , M_ionicModelExporterNames()
 , M_parametersExporter()
 , M_parametersExporterNames()
 , M_outputFolder()
 , M_datafile()
 , M_pacing_i()
 , M_pacing_e()
 , M_linearSolver()
 , M_anisotropy(Anisotropy::Orthotropic)
 , M_equationType( EquationType::ParabolicEllipticBidomain)
 , M_timeIntegratorType(DynamicTimeIntegratorType::Implicit)
 , M_useAMR(false)
 , M_assembleMatrix(true)
 , M_systemMass("lumped")
 , M_intraConductivity()
 , M_extraConductivity()
 , M_conductivity()
 , M_meshSize(1.0)
 , M_model(model)
 , M_ground_ve(false)

{
    // TODO Auto-generated constructor stub

}

ElectroSolver::~ElectroSolver()
{
}



void
ElectroSolver::setup(GetPot& data, std::string section)
{
    // ///////////////////////////////////////////////////////////////////////
    // ///////////////////////////////////////////////////////////////////////
    // Read Input File
    M_datafile = data;
    M_section = section;

    //Read output folder from datafile
    std::string output_folder = M_datafile(section + "/output_folder",
            "Output");
    M_outputFolder = "./" + output_folder + "/";


    std::cout << "* " << M_model << ": creating pacing protocol" << std::endl;

    std::string pacing_type = M_datafile(section + "/pacing/type", "NONE");
        M_pacing.reset(
                BeatIt::PacingProtocol::PacingProtocolFactory::Create(pacing_type));
        M_pacing->setup(M_datafile, section + "/pacing");

    std::string pacing_i_type = M_datafile(section + "/pacing_i/type", "NONE");
    if("NONE" !=  pacing_i_type)
    {
        M_pacing_i.reset(
                BeatIt::PacingProtocol::PacingProtocolFactory::Create(pacing_i_type));
        M_pacing_i->setup(M_datafile, section + "/pacing_i");
    }

    std::string pacing_e_type = M_datafile(section + "/pacing_e/type", "NONE");
    if("NONE" !=  pacing_e_type)
    {
        M_pacing_e.reset(
                BeatIt::PacingProtocol::PacingProtocolFactory::Create(pacing_e_type));
        M_pacing_e->setup(M_datafile, section + "/pacing_e");
    }

    std::string surf_pacing_i_type = M_datafile(section + "/surf_pacing_i/type", "NONE");
    if("NONE" !=  surf_pacing_i_type)
    {
        M_surf_pacing_i.reset(
                BeatIt::PacingProtocol::PacingProtocolFactory::Create(surf_pacing_i_type));
        M_surf_pacing_i->setup(M_datafile, section + "/surf_pacing_i");
    }

    std::string surf_pacing_e_type = M_datafile(section + "/surf_pacing_e/type", "NONE");
    if("NONE" !=  surf_pacing_e_type)
    {
        M_surf_pacing_e.reset(
                BeatIt::PacingProtocol::PacingProtocolFactory::Create(surf_pacing_e_type));
        M_surf_pacing_e->setup(M_datafile, section + "/surf_pacing_e");
    }


//    // ///////////////////////////////////////////////////////////////////////
//    // ///////////////////////////////////////////////////////////////////////
//    // Starts by creating the equation systems
//    // 1) ADR
//    std::cout << "* " << M_model << ": Creating new System for the hyperbolic equations"
//            << std::endl;
//    ElectroSystem& electro_system = M_equationSystems.add_system
//            < ElectroSystem > (M_model);
//    // TO DO: Generalize to higher order
//    if(M_transmembranePotentialActiveSubdomains.size() > 0)
//    {
//        electro_system.add_variable("Q", libMesh::FIRST, M_transmembranePotentialActiveSubdomains);
//    }
//    else
//    {
//        electro_system.add_variable("Q", libMesh::FIRST);
//    }
//
//    if(M_modelType != ModelType::Monodomain)
//    {
//        electro_system.add_variable("Ve", libMesh::FIRST);
//    }
//
//    // Add 3 matrices
//    electro_system.add_matrix("lumped_mass");
//    electro_system.add_matrix("high_order_mass");
//    // Add lumped mass matrix
//    electro_system.add_vector("lumped_mass_vector");
//    electro_system.add_matrix("mass");
//    electro_system.add_matrix("stiffness");
//    electro_system.add_vector("ionic_currents");
//    electro_system.add_vector("old_solution");
//    electro_system.add_vector("nullspace");
//    electro_system.add_vector("residual");
//    M_exporterNames.insert(M_model);
//    electro_system.init();
//
//    // WAVE
//    ElectroSystem& wave_system = M_equationSystems.add_system < ElectroSystem
//            > ("wave");
//    wave_system.add_variable("V", libMesh::FIRST);
//    wave_system.add_matrix("Ki");
//    wave_system.add_vector("KiV");
//    M_exporterNames.insert("wave");
//    wave_system.init();
//
//    // ///////////////////////////////////////////////////////////////////////
//    // ///////////////////////////////////////////////////////////////////////
//    // 2) ODEs
//    std::cout << "* " << M_model << ": Creating new System for the ionic model "
//            << std::endl;
//    IonicModelSystem& ionic_model_system = M_equationSystems.add_system
//            < IonicModelSystem > ("ionic_model");
//    // Create Ionic Model
//    std::string ionic_model = M_datafile(section + "/ionic_model",
//            "NashPanfilov");
//    M_ionicModelPtr.reset(
//            BeatIt::IonicModel::IonicModelFactory::Create(ionic_model));
//    M_ionicModelPtr->setup(M_datafile, section);
//    int num_vars = M_ionicModelPtr->numVariables();
//    // TO DO: Generalize to other conditions
//    // We need to exclude the potential
//    // therefore we loop up to num_vars-1
//    for (int nv = 0; nv < num_vars - 1; ++nv)
//    {
//        std::string var_name = M_ionicModelPtr->variableName(nv);
//        // For the time being we use P1 for the variables
//        ionic_model_system.add_variable(&var_name[0], libMesh::FIRST);
//    }
//    ionic_model_system.init();
//
//    // Add the applied current to this system
//    IonicModelSystem& Iion_system = M_equationSystems.add_system
//            < IonicModelSystem > ("iion");
//    Iion_system.add_variable("iion", libMesh::FIRST);
//    Iion_system.add_vector("diion");
//    Iion_system.init();
//    // Applied stimulus
//    IonicModelSystem& istim_system = M_equationSystems.add_system
//            < IonicModelSystem > ("istim");
//    istim_system.add_variable("istim", libMesh::FIRST);
//    istim_system.add_vector("surface_stim");
//    istim_system.init();
//    // Add names for exporter
//    M_ionicModelExporterNames.insert("ionic_model");
//    M_ionicModelExporterNames.insert("iion");
//    M_ionicModelExporterNames.insert("istim");
//
//
//
//    // ///////////////////////////////////////////////////////////////////////
//    // ///////////////////////////////////////////////////////////////////////
//    // Distributed Parameters
//    std::cout << "* " << M_model << ": Creating parameters spaces " << std::endl;
//
//    std::cout << "   activation times system" << std::endl;
//    ParameterSystem& activation_times_system = M_equationSystems.add_system
//            < ParameterSystem > ("activation_times");
//    activation_times_system.add_variable("activation_times", libMesh::FIRST);
//
//    M_parametersExporterNames.insert("activation_times");
//    activation_times_system.init();
//
//    // Fibers
//    std::cout << "   fibers times system" << std::endl;
//    ParameterSystem& fiber_system = M_equationSystems.add_system
//            < ParameterSystem > ("fibers");
//    fiber_system.add_variable("fibersx", libMesh::CONSTANT, libMesh::MONOMIAL);
//    fiber_system.add_variable("fibersy", libMesh::CONSTANT, libMesh::MONOMIAL);
//    fiber_system.add_variable("fibersz", libMesh::CONSTANT, libMesh::MONOMIAL);
//    ParameterSystem& sheets_system = M_equationSystems.add_system
//            < ParameterSystem > ("sheets");
//    sheets_system.add_variable("sheetsx", libMesh::CONSTANT, libMesh::MONOMIAL);
//    sheets_system.add_variable("sheetsy", libMesh::CONSTANT, libMesh::MONOMIAL);
//    sheets_system.add_variable("sheetsz", libMesh::CONSTANT, libMesh::MONOMIAL);
//    ParameterSystem& xfiber_system = M_equationSystems.add_system
//            < ParameterSystem > ("xfibers");
//    xfiber_system.add_variable("xfibersx", libMesh::CONSTANT,
//            libMesh::MONOMIAL);
//    xfiber_system.add_variable("xfibersy", libMesh::CONSTANT,
//            libMesh::MONOMIAL);
//    xfiber_system.add_variable("xfibersz", libMesh::CONSTANT,
//            libMesh::MONOMIAL);
//
//    M_parametersExporterNames.insert("fibers");
//    M_parametersExporterNames.insert("sheets");
//    M_parametersExporterNames.insert("xfibers");
//    fiber_system.init();
//    sheets_system.init();
//    xfiber_system.init();
//
//    // Conductivities
//    std::cout << "   conductivities system" << std::endl;
//    if(ModelType::Monodomain != M_modelType)
//    {
//        ParameterSystem& intra_conductivity_system = M_equationSystems.add_system
//                < ParameterSystem > ("intra_conductivity");
//        intra_conductivity_system.add_variable("Dffi", libMesh::CONSTANT,
//                libMesh::MONOMIAL);
//        intra_conductivity_system.add_variable("Dssi", libMesh::CONSTANT,
//                libMesh::MONOMIAL);
//        intra_conductivity_system.add_variable("Dnni", libMesh::CONSTANT,
//                libMesh::MONOMIAL);
//        ParameterSystem& extra_conductivity_system = M_equationSystems.add_system
//                < ParameterSystem > ("extra_conductivity");
//        extra_conductivity_system.add_variable("Dffe", libMesh::CONSTANT,
//                libMesh::MONOMIAL);
//        extra_conductivity_system.add_variable("Dsse", libMesh::CONSTANT,
//                libMesh::MONOMIAL);
//        extra_conductivity_system.add_variable("Dnne", libMesh::CONSTANT,
//                libMesh::MONOMIAL);
//
//        M_parametersExporterNames.insert("intra_conductivity");
//        M_parametersExporterNames.insert("extra_conductivity");
//        intra_conductivity_system.init();
//        extra_conductivity_system.init();
//
//    }
//    else
//    {
//        ParameterSystem& conductivity_system = M_equationSystems.add_system
//                < ParameterSystem > ("conductivity");
//        conductivity_system.add_variable("Dff", libMesh::CONSTANT,
//                libMesh::MONOMIAL);
//        conductivity_system.add_variable("Dss", libMesh::CONSTANT,
//                libMesh::MONOMIAL);
//        conductivity_system.add_variable("Dnn", libMesh::CONSTANT,
//                libMesh::MONOMIAL);
//
//        M_parametersExporterNames.insert("conductivity");
//
//        conductivity_system.init();
//    }
//
//    if(ModelType::BidomainWithBath == M_modelType)
//    {
//        ParameterSystem& bath_conductivity_system = M_equationSystems.add_system
//                < ParameterSystem > ("bath_conductivity");
//        bath_conductivity_system.add_variable("Dbath", libMesh::CONSTANT,
//                libMesh::MONOMIAL);
//        M_parametersExporterNames.insert("bath_conductivity");
//
//        bath_conductivity_system.init();
//    }
//
//    std::cout << "   Proc ID system" << std::endl;
//    ParameterSystem& procID_system = M_equationSystems.add_system
//            < ParameterSystem > ("ProcID");
//    procID_system.add_variable("ProcID", libMesh::CONSTANT, libMesh::MONOMIAL);
//    M_parametersExporterNames.insert("ProcID");
//    procID_system.init();
//
//    std::cout << "* " << M_model << ": Initializing equation systems " << std::endl;
//
//    // Conductivity Tensor in local coordinates
//    // Fiber direction
//    // Default Value = 1.3342  kOhm^-1 cm^-1
//    double Dffi = M_datafile(section + "/Dffi", 1.7);
//    // Sheet direction
//    // Default Value = 1.3342  kOhm^-1 cm^-1
//    double Dssi = M_datafile(section + "/Dssi", 0.19);
//    // Cross fiber direction
//    // Default Value = 1.3342  kOhm^-1 cm^-1
//    double Dnni = M_datafile(section + "/Dnni", 0.19);
//    // Default Value = 1.3342  kOhm^-1 cm^-1
//    double Dffe = M_datafile(section + "/Dffe", 6.2);
//    // Sheet direction
//    // Default Value = 1.3342  kOhm^-1 cm^-1
//    double Dsse = M_datafile(section + "/Dsse", 2.4);
//    // Cross fiber direction
//    // Default Value = 1.3342  kOhm^-1 cm^-1
//    double Dnne = M_datafile(section + "/Dnne", 2.4);
//
//    // Monodomain Values
//    double Dff = M_datafile(section + "/Dff", -1);
//    if( -1  == Dff) Dff = Dffi*Dffe / (Dffi+Dffe);
//    // Sheet direction
//    // Default Value = 1.3342  kOhm^-1 cm^-1
//    double Dss = M_datafile(section + "/Dss", -1);
//    if( -1  == Dss) Dss = Dssi*Dsse / (Dssi+Dsse);
//    // Cross fiber direction
//    // Default Value = 1.3342  kOhm^-1 cm^-1
//    double Dnn = M_datafile(section + "/Dnn", -1);
//    if( -1  == Dnn) Dnn = Dss;
//
//    // Bath conductivity
//    double Dbath = M_datafile(section + "/Dbath", -1);
//    if( -1  == Dbath) Dbath = Dffe;
//
//
//    // Surface to volume ratio
//    double Chi = M_datafile(section + "/Chi", 1400.0);
//    // Intracellular and extracellular relaxation times
//    double tau_i = M_datafile(section + "/tau_i", 0.0);
//    double tau_e = M_datafile(section + "/tau_e", 0.0);
//    double tau = M_datafile(section + "/tau", 0.0);
//
//    M_equationSystems.parameters.set<double>("Chi") = Chi;
//    M_equationSystems.parameters.set<double>("tau_i") = tau_i;
//    M_equationSystems.parameters.set<double>("tau_e") = tau_e;
//    M_equationSystems.parameters.set<double>("tau") = tau;
//
//    std::string anisotropy = M_datafile(section + "/anisotropy", "orhotropic");
//    std::map<std::string, BidomainAnisotropy> aniso_map;
//    aniso_map["orthotropic"] = BidomainAnisotropy::Orthotropic;
//    aniso_map["isotropic"] = BidomainAnisotropy::Isotropic;
//    aniso_map["transverse"] = BidomainAnisotropy::TransverselyIsotropic;
//    std::cout << "* " << M_model << ": Parameters: " << std::endl;
//    std::cout << "              Chi = " << Chi << std::endl;
//    std::cout << "              Dffi = " << Dffi << std::endl;
//    std::cout << "              Dssi = " << Dssi << std::endl;
//    std::cout << "              Dnni = " << Dnni << std::endl;
//    std::cout << "              Dffe = " << Dffe << std::endl;
//    std::cout << "              Dsse = " << Dsse << std::endl;
//    std::cout << "              Dnne = " << Dnne << std::endl;
//    std::cout << "              Dff = " << Dffe << std::endl;
//    std::cout << "              Dss = " << Dsse << std::endl;
//    std::cout << "              Dnn = " << Dnne << std::endl;
//    std::cout << "              Dbath = " << Dbath << std::endl;
//    std::cout << "              tau_i = " << tau_i << std::endl;
//    std::cout << "              tau_e = " << tau_e << std::endl;
//    std::cout << "              tau = " << tau_e << std::endl;
//    std::cout << "              anisotropy = " << anisotropy << std::endl;


//    ElectroSystem& wave_system = M_equationSystems.get_system < ElectroSystem
//            > ("wave");
    struct stat out_dir;
    if (stat(&M_outputFolder[0], &out_dir) != 0)
    {
        if (M_equationSystems.get_mesh().comm().rank() == 0)
        {
            mkdir(M_outputFolder.c_str(), 0777);
        }
    }

    // call setup system of the specific class
    setupSystems(M_datafile, M_section);

    IonicModelSystem& istim_system = M_equationSystems.add_system
            < IonicModelSystem > ("istim");
    istim_system.add_variable("istim", libMesh::FIRST);
    istim_system.init();
    istim_system.add_vector("stim_i");
    istim_system.add_vector("stim_e");
    istim_system.add_vector("surf_stim_i");
    istim_system.add_vector("surf_stim_e");

    M_ionicModelExporterNames.insert("istim");


    // ///////////////////////////////////////////////////////////////////////
    // ///////////////////////////////////////////////////////////////////////
    // Setup Exporters

    std::cout << "* " << M_model << ": Initializing exporters " << std::endl;
    M_exporter.reset(new Exporter(M_equationSystems.get_mesh()));
    M_ionicModelExporter.reset(new Exporter(M_equationSystems.get_mesh()));
    M_parametersExporter.reset(new EXOExporter(M_equationSystems.get_mesh()));
    M_EXOExporter.reset(new EXOExporter(M_equationSystems.get_mesh()));
    M_potentialEXOExporter.reset(new EXOExporter(M_equationSystems.get_mesh()));


}



void
ElectroSolver::init(double time)
{
    std::cout << "* ElectroSolver: Init: " << std::endl;

    // WAVE
    ElectroSystem& wave_system = M_equationSystems.get_system < ElectroSystem
            > ("wave");
    // Setting initial conditions
    ElectroSystem& electro_system = M_equationSystems.get_system
            < ElectroSystem > (M_model);
    IonicModelSystem& ionic_model_system = M_equationSystems.get_system
            < IonicModelSystem > ("ionic_model");
    int num_vars = ionic_model_system.n_vars();
    std::vector<double> init_values(num_vars + 1, 0.0);
    M_ionicModelPtr->initialize(init_values);

    auto first_local_index = wave_system.solution->first_local_index();
    auto last_local_index = wave_system.solution->last_local_index();
    for (auto i = first_local_index; i < last_local_index; ++i)
    {
        wave_system.solution->set(i, init_values[0]);
    }
    first_local_index = ionic_model_system.solution->first_local_index();
    last_local_index = ionic_model_system.solution->last_local_index();
    std::cout << "* " << M_model << ": Setting initial values ... " << std::flush;
    for (auto i = first_local_index; i < last_local_index;)
    {
        for (int nv = 0; nv < num_vars; ++nv)
        {
            ionic_model_system.solution->set(i, init_values[nv + 1]);
            ++i;
        }
    }
    electro_system.solution->close();
    electro_system.old_local_solution->close();
    electro_system.older_local_solution->close();
    ionic_model_system.solution->close();
    ionic_model_system.old_local_solution->close();
    ionic_model_system.older_local_solution->close();


    std::map < std::string, libMesh::SolverType > solver_map;
    solver_map["cg"] = libMesh::CG;
    solver_map["cgs"] = libMesh::CGS;
    solver_map["gmres"] = libMesh::GMRES;
    std::map < std::string, libMesh::PreconditionerType > prec_map;
    prec_map["jacobi"] = libMesh::JACOBI_PRECOND;
    prec_map["sor"] = libMesh::SOR_PRECOND;
    prec_map["ssor"] = libMesh::SSOR_PRECOND;
    prec_map["cholesky"] = libMesh::CHOLESKY_PRECOND;
    prec_map["lu"] = libMesh::LU_PRECOND;
    prec_map["ilu"] = libMesh::ILU_PRECOND;
    prec_map["amg"] = libMesh::AMG_PRECOND;

    ParameterSystem& procID_system = M_equationSystems.get_system
            < ParameterSystem > ("ProcID");

    first_local_index = procID_system.solution->first_local_index();
    last_local_index = procID_system.solution->last_local_index();

    double myrank = static_cast<double>(M_equationSystems.comm().rank());
    for (int el = first_local_index; el < last_local_index; ++el)
    {
        procID_system.solution->set(el, myrank);
    }
    procID_system.solution->close();

    // Call specific system initializations
    initSystems(time);
//    M_linearSolver =  libMesh::LinearSolver<libMesh::Number>::build( M_equationSystems.comm() );
    typedef libMesh::PetscLinearSolver<libMesh::Number> PetscSolver;
    M_linearSolver.reset(new PetscSolver(M_equationSystems.comm()));
    std::string solver_type = M_datafile("monodomain/linear_solver/type", "gmres");
    std::cout << "* ElectroSolver: using " << solver_type << std::endl;
    std::string prec_type = M_datafile("monodomain/linear_solver/preconditioner", "amg");
    M_linearSolver->set_solver_type(solver_map.find(solver_type)->second);
    M_linearSolver->set_preconditioner_type(prec_map.find(prec_type)->second);
    M_linearSolver->init();

}



void
ElectroSolver::restart( EXOExporter& importer,
                        int step,
                        bool restart )
{
    if (restart)
    {
        auto& nodal_var_names = importer.get_nodal_var_names ();
        auto nodal_first = nodal_var_names.begin();
        auto nodal_end = nodal_var_names.end();
        auto& elem_var_names = importer.get_elem_var_names ();
        auto elem_first = elem_var_names.begin();
        auto elem_end = elem_var_names.end();

        int num_systems = M_equationSystems.n_systems();
        for (int  k = 0; k < num_systems; ++k)
        {
            libMesh::System& system = M_equationSystems.get_system(k);
            std::string name = system.name();
            std::cout << "Importing System: " << name << std::endl;
            int n_vars = system.n_vars();
            for(int l = 0; l < n_vars; ++l)
            {
                std::string var_name =  system.variable_name (l);

                auto elem_it = std::find(elem_first, elem_end, var_name);
                if(elem_it != elem_end)
                {
                    std::cout << "\t elemental variable: " << *elem_it << std::endl;
                    importer.copy_elemental_solution(system, *elem_it, *elem_it, step);
                }
                else
                {
                    auto nodal_it = std::find(nodal_first, nodal_end, var_name);
                    if(nodal_it != nodal_end)
                    {
                        std::cout << "\t nodal variable: " << *nodal_it << std::endl;
                        importer.copy_nodal_solution(system, *nodal_it, *nodal_it, step);
                    }
                }
            }
        }
    }
}

void
ElectroSolver::readFibers( EXOExporter& importer,
                           int step)
{
    const int num_fiber_systems = 3;
    std::vector<std::string> fibers(num_fiber_systems);
    fibers[0] = "fibers";
    fibers[1] = "sheets";
    fibers[2] = "xfibers";

    auto& elem_var_names = importer.get_elem_var_names ();
    auto elem_first = elem_var_names.begin();
    auto elem_end = elem_var_names.end();

    for (int  k = 0; k < num_fiber_systems; ++k)
    {
        libMesh::System& system = M_equationSystems.get_system(fibers[k]);
        std::string name = system.name();
        std::cout << "Importing System: " << name << std::endl;
        int n_vars = system.n_vars();
        for(int l = 0; l < n_vars; ++l)
        {
            std::string var_name =  system.variable_name (l);
            auto elem_it = std::find(elem_first, elem_end, var_name);
            if(elem_it != elem_end)
            {
                std::cout << "\t elemental variable: " << *elem_it << std::endl;
                importer.copy_elemental_solution(system, *elem_it, *elem_it, step);
            }
        }
    }
}


void
ElectroSolver::init_exo_output()
{
    std::cout << "* " << M_model << ": EXODUSII::Exporting " << M_model << ".exo at time 0.0"
            << " in: " << M_outputFolder << " ... " << std::flush;

    M_EXOExporter->write_equation_systems (M_outputFolder + M_model +".exo", M_equationSystems);
    M_EXOExporter->append(true);
    std::vector<std::string> output;
    output.push_back("V");
    output.push_back("Q");
    output.push_back("Ve");
    M_potentialEXOExporter->set_output_variables(output);
    M_potentialEXOExporter->write_equation_systems (M_outputFolder+"potential.exo", M_equationSystems);
    M_potentialEXOExporter->append(true);
    std::cout << " done!" << std::endl;
}

void
ElectroSolver::save_exo_timestep(int step, double time)
{
    std::cout << "* " << M_model << ": EXODUSII::Exporting " << M_model << ".exo at time " << time
            << " in: " << M_outputFolder << " ... " << std::flush;
    M_EXOExporter->write_timestep(M_outputFolder + M_model +".exo",
            M_equationSystems, step, time);
    std::cout << "done " << std::endl;
}

void
ElectroSolver::save_parameters()
{
    std::cout << "* " << M_model << ": EXODUSII::Exporting parameters in: "
            << M_outputFolder << " ... " << std::flush;
    EXOExporter exo( M_equationSystems.get_mesh() );
    exo.write("parameters.exo");
    exo.write_element_data(M_equationSystems);
    std::cout << "done " << std::endl;
}


void
ElectroSolver::save_potential(int step, double time)
{
    std::cout << "* " << M_model << ": EXODUSII::Exporting potential.exo at time "
            << time << " in: " << M_outputFolder << " ... " << std::flush;

    M_potentialEXOExporter->write_timestep(M_outputFolder + "potential.exo",
            M_equationSystems, step, time);
    std::cout << "done " << std::endl;
}

void
ElectroSolver::save_activation_times(int step)
{
    std::cout << "* " << M_model << ": GMVIO::Exporting activaton times: "
              << M_outputFolder << " ... " << std::flush;
    Exporter gmv(M_equationSystems.get_mesh());
    std::set<std::string> output;
    output.insert("activation_times");
    gmv.write_equation_systems( M_outputFolder+"activation_times.gmv."+std::to_string(step),
                                M_equationSystems,
                                &output);
    std::cout << "done " << std::endl;
}

void
ElectroSolver::save(int step)
{
    std::cout << "* " << M_model << ": GMVIO::Exporting " << step << " in: "  << M_outputFolder << " ... " << std::flush;
    M_exporter->write_equation_systems ( M_outputFolder+ M_model +".gmv."+std::to_string(step),
                                           M_equationSystems,
                                           &M_exporterNames);
    M_ionicModelExporter->write_equation_systems ( M_outputFolder+"ionic_model.gmv."+std::to_string(step),
                                                   M_equationSystems,
                                                   &M_ionicModelExporterNames);
    std::cout << "done " << std::endl;
}


void
ElectroSolver::reinit_linear_solver()
{
    M_linearSolver->clear();
    M_linearSolver->set_solver_type(libMesh::CG);
    M_linearSolver->set_preconditioner_type(libMesh::AMG_PRECOND);
    M_linearSolver->init();
}


void
ElectroSolver::update_activation_time(double time, double threshold)
{
    ParameterSystem& activation_times_system = M_equationSystems.get_system
            < ParameterSystem > ("activation_times");
    // WAVE
    ElectroSystem& wave_system = M_equationSystems.get_system < ElectroSystem > ("wave");


    const libMesh::MeshBase & mesh = M_equationSystems.get_mesh();

    libMesh::MeshBase::const_node_iterator node = mesh.local_nodes_begin();
    const libMesh::MeshBase::const_node_iterator end_node =
            mesh.local_nodes_end();

    const libMesh::DofMap & dof_map = wave_system.get_dof_map();
    const libMesh::DofMap & dof_map_at = activation_times_system.get_dof_map();

    std::vector < libMesh::dof_id_type > dof_indices_V;
    std::vector < libMesh::dof_id_type > dof_indices_at;

    double v = 0.0;
    double at = 0.0;

    for (; node != end_node; ++node)
    {
        const libMesh::Node * nn = *node;
        // Are we in the bath?

        dof_map.dof_indices(nn, dof_indices_V, 0);
        dof_map_at.dof_indices(nn, dof_indices_at, 0);

        if(dof_indices_V.size() > 0 )
        {
            v = (*wave_system.solution)(dof_indices_V[0]);
            at = (*activation_times_system.solution)(dof_indices_at[0]);
            if (v > threshold && at < 0.0)
            {
                activation_times_system.solution->set(dof_indices_at[0], time);
            }

        }
    }


    activation_times_system.solution->close();
}

void
ElectroSolver::advance()
{
    ElectroSystem& system = M_equationSystems.get_system
            < ElectroSystem > (M_model);
    IonicModelSystem& ionic_model_system = M_equationSystems.get_system
            < IonicModelSystem > ("ionic_model");

    *system.older_local_solution = *system.old_local_solution;
    *system.old_local_solution = *system.solution;
    *ionic_model_system.older_local_solution = *ionic_model_system.old_local_solution;
    *ionic_model_system.old_local_solution = *ionic_model_system.solution;

    // WAVE
    ElectroSystem& wave_system = M_equationSystems.get_system < ElectroSystem
            > ("wave");
    *wave_system.older_local_solution = *wave_system.old_local_solution;
    *wave_system.old_local_solution = *wave_system.solution;

    IonicModelSystem& iion_system = M_equationSystems.get_system
            < IonicModelSystem > ("iion");
    *iion_system.older_local_solution = *iion_system.old_local_solution;
    *iion_system.old_local_solution = *iion_system.solution;
}


std::string
ElectroSolver::get_ionic_model_name() const
{
    return M_ionicModelPtr->ionicModelName();
}

double
ElectroSolver::last_activation_time()
{
    ParameterSystem& activation_times_system = M_equationSystems.get_system
            < ParameterSystem > ("activation_times");
    return static_cast<double>(activation_times_system.solution->max());
}

double
ElectroSolver::potential_norm()
{
    ElectroSystem& system = M_equationSystems.get_system
            < ElectroSystem > (M_model);
    return system.solution->l1_norm();
}

void
ElectroSolver::set_potential_on_boundary(unsigned int boundID, double value)
{
    std::cout
            << "* " << M_model << ": WARNING:  set_potential_on_boundary works only for TET4"
            << std::endl;
    libMesh::MeshBase & mesh = M_equationSystems.get_mesh();
    const unsigned int dim = mesh.mesh_dimension();

    ElectroSystem& wave_system = M_equationSystems.get_system < ElectroSystem
            > ("wave");

    const libMesh::DofMap & dof_map = wave_system.get_dof_map();
    std::vector < libMesh::dof_id_type > dof_indices;

    libMesh::MeshBase::const_element_iterator el =
            mesh.active_local_elements_begin();
    const libMesh::MeshBase::const_element_iterator end_el =
            mesh.active_local_elements_end();

    for (; el != end_el; ++el)
    {
        const libMesh::Elem * elem = *el;
        dof_map.dof_indices(elem, dof_indices);
        for (unsigned int side = 0; side < elem->n_sides(); side++)
        {
            if (elem->neighbor(side) == libmesh_nullptr)
            {
                const unsigned int boundary_id =
                        mesh.boundary_info->boundary_id(elem, side);
                if (boundary_id == boundID)
                {
                    wave_system.solution->set(dof_indices[0], value);
                    wave_system.solution->set(dof_indices[1], value);
                    wave_system.solution->set(dof_indices[2], value);
                }
            }

        }
    }
    wave_system.solution->close();
}


const libMesh::UniquePtr<libMesh::NumericVector<libMesh::Number> >&
ElectroSolver::get_fibers()
{
    ParameterSystem& fiber_system = M_equationSystems.get_system
            < ParameterSystem > ("fibers");
    return fiber_system.solution;
}

const libMesh::UniquePtr<libMesh::NumericVector<libMesh::Number> >&
ElectroSolver::get_sheets()
{
    ParameterSystem& sheets_system = M_equationSystems.get_system
            < ParameterSystem > ("sheets");
    return sheets_system.solution;
}

const libMesh::UniquePtr<libMesh::NumericVector<libMesh::Number> >&
ElectroSolver::get_xfibers()
{
    ParameterSystem& xfiber_system = M_equationSystems.get_system
            < ParameterSystem > ("xfibers");
    return xfiber_system.solution;
}



void ElectroSolver::solve_reaction_step( double dt,
                                    double time,
                                    int step,
                                    bool useMidpoint,
                                    const std::string& mass,
                                    libMesh::NumericVector<libMesh::Number>* I4f_ptr)
{
    const libMesh::MeshBase & mesh = M_equationSystems.get_mesh();
//    BidomainSystem& bidomain_system = M_equationSystems.get_system
//            < BidomainSystem > ("bidomain"); //Q
    ElectroSystem& system = M_equationSystems.get_system
            < ElectroSystem > (M_model);
    IonicModelSystem& ionic_model_system = M_equationSystems.get_system
            < IonicModelSystem > ("ionic_model");
    IonicModelSystem& istim_system = M_equationSystems.get_system
            < IonicModelSystem > ("istim");
    // WAVE
    ElectroSystem& wave_system = M_equationSystems.get_system < ElectroSystem
            > ("wave");
    IonicModelSystem& iion_system = M_equationSystems.get_system
            < IonicModelSystem > ("iion");

    system.rhs->zero();
    iion_system.solution->zero();
    istim_system.solution->zero();
    istim_system.get_vector("stim_i").zero();
    istim_system.get_vector("stim_e").zero();
    istim_system.get_vector("surf_stim_i").zero();
    istim_system.get_vector("surf_stim_e").zero();
    //iion_system.old_local_solution->zero();
    iion_system.get_vector("diion").zero();

    double Cm = M_ionicModelPtr->membraneCapacitance();
//    const libMesh::Real tau_e = M_equationSystems.parameters.get < libMesh::Real
//            > ("tau_e");
//    const libMesh::Real tau_i = M_equationSystems.parameters.get < libMesh::Real
//            > ("tau_i");

    int num_vars = ionic_model_system.n_vars();
    std::vector<double> values(num_vars + 1, 0.0);
    std::vector<double> old_values(num_vars + 1, 0.0);
    int var_index = 0;

    libMesh::MeshBase::const_node_iterator node = mesh.local_nodes_begin();
    const libMesh::MeshBase::const_node_iterator end_node =
            mesh.local_nodes_end();

    const libMesh::DofMap & dof_map = system.get_dof_map();
    const libMesh::DofMap & dof_map_V = wave_system.get_dof_map();
    const libMesh::DofMap & dof_map_gating = ionic_model_system.get_dof_map();
    const libMesh::DofMap & dof_map_istim = istim_system.get_dof_map();

    std::vector < libMesh::dof_id_type > dof_indices;
    std::vector < libMesh::dof_id_type > dof_indices_V;
    //std::vector < libMesh::dof_id_type > dof_indices_Ve;
    std::vector < libMesh::dof_id_type > dof_indices_Q;
    std::vector < libMesh::dof_id_type > dof_indices_gating;
    std::vector < libMesh::dof_id_type > dof_indices_istim;

    M_pacing->update(time);
    if(M_pacing_i) M_pacing_i->update(time);
    if(M_pacing_e) M_pacing_e->update(time);
    if(M_surf_pacing_i) M_surf_pacing_i->update(time);
    if(M_surf_pacing_e) M_surf_pacing_e->update(time);

    int c = 0;
    for (; node != end_node; ++node)
    {
        double istim = 0.0;
        double istim_zero = 0.0;
        double stim_i = 0.0;  double surf_stim_i = 0.0;
        double stim_e = 0.0;  double surf_stim_e = 0.0;

        double dIion = 0.0;

        const libMesh::Node * nn = *node;
        // Are we in the bath?
        auto n_var = nn->n_vars(system.number() );
        auto n_dofs = nn->n_dofs(system.number() );
        libMesh::Point p((*nn)(0), (*nn)(1), (*nn)(2));
        if(n_var == n_dofs)
        {
            std::cout << "Here 1" << std::endl;
            dof_map.dof_indices(nn, dof_indices_Q, 0);
            std::cout << "Here 2" << std::endl;
            //dof_map.dof_indices(nn, dof_indices_Ve, 1);
            std::cout << "Here 3" << std::endl;
            dof_map_V.dof_indices(nn, dof_indices_V, 0);
            std::cout << "Here 4" << std::endl;
            dof_map_istim.dof_indices(nn, dof_indices_istim, 0);
            std::cout << "Here 5" << std::endl;
            dof_map_gating.dof_indices(nn, dof_indices_gating);
            std::cout << "Here 6" << std::endl;

            //double bd_stim = istim_system.get_vector("surface_stim")(dof_indices_V[0]); //
            //if(M_pacing->M_boundaryID >= 0 ) istim *= bd_stim;
            if(M_pacing_i) stim_i = M_pacing_i->eval(p, time);
            if(M_pacing_e) stim_e = M_pacing_e->eval(p, time);
            if(M_surf_pacing_i) surf_stim_i = M_surf_pacing_i->eval(p, time);
            if(M_surf_pacing_e) surf_stim_e = M_surf_pacing_e->eval(p, time);
            istim = M_pacing->eval(p, time);

            // istim_zero = istim;
            //std::cout << "istim: " << istim << std::endl;
            //if (istim != 0 ) std::cout << istim << std::endl;
            //std::cout << "c: " << c << "(x,y,z) = (" << p(0) << "," << p(1) << "," << p(2) << "); istim: " << istim << std::endl;

            double Iion_old = 0.0;
            values[0] = (*wave_system.old_local_solution)(dof_indices_V[0]); //V^n
            old_values[0] = (*system.old_local_solution)(dof_indices_Q[0]); //Q^n
            Iion_old = (*iion_system.old_local_solution)(dof_indices_istim[0]); // gating

            for (int nv = 0; nv < num_vars; ++nv)
            {
                var_index = dof_indices_gating[nv];
                values[nv + 1] = (*ionic_model_system.old_local_solution)(
                        var_index);
                old_values[nv + 1] = values[nv + 1];
            }

            M_ionicModelPtr->updateVariables(values, istim_zero, dt);

            //for(auto&& v:values) std::cout << v << ", " << std::flush;
            double Iion = M_ionicModelPtr->evaluateIonicCurrent(values, istim_zero,
                    dt);
            //std::cout << "\nIion: " << Iion << std::endl;
    //        std::cout << "Iion: " << Iion << std::endl;
            dIion = M_ionicModelPtr->evaluatedIonicCurrent(values, old_values, dt,
                    M_meshSize);
            double Isac = 0.0;
            if (I4f_ptr)
            {
                double I4f = (*I4f_ptr)(dof_indices_V[0]);
                Isac = M_ionicModelPtr->evaluateSAC(values[0], I4f);
            }
            Iion += Isac;

            iion_system.solution->set(dof_indices_istim[0], Iion); // contains Istim
            istim_system.solution->set(dof_indices_istim[0], istim);
            istim_system.get_vector("stim_i").set(dof_indices_istim[0], stim_i); //Istim^n+1
            istim_system.get_vector("surf_stim_i").set(dof_indices_istim[0], surf_stim_i); //Istim^n+1
            istim_system.get_vector("stim_e").set(dof_indices_istim[0], stim_e); //Istim^n+1
            istim_system.get_vector("surf_stim_e").set(dof_indices_istim[0], surf_stim_e); //Istim^n+1

            iion_system.get_vector("diion").set(dof_indices_istim[0], dIion);
            for (int nv = 0; nv < num_vars; ++nv)
            {
                var_index = dof_indices_gating[nv];

                ionic_model_system.solution->set(var_index, values[nv + 1]);
            }
        }
        c++;

    }
    iion_system.solution->close();

    istim_system.solution->close();
//    std::cout << "Iion sol: " << std::endl;
//    iion_system.solution->print();
//    istim_system.solution->print();
//    std::cout << "Iion sol: " << std::endl;

    istim_system.get_vector("stim_i").close();
    istim_system.get_vector("stim_e").close();
    istim_system.get_vector("surf_stim_i").close();
    istim_system.get_vector("surf_stim_e").close();
    ionic_model_system.solution->close();
    iion_system.get_vector("diion").close();
    iion_system.update();
    ionic_model_system.update();
    istim_system.update();
}



} /* namespace BeatIt */