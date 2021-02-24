/*
 ============================================================================

 .______    _______     ___   .___________.    __  .___________.
 |   _  \  |   ____|   /   \  |           |   |  | |           |
 |  |_)  | |  |__     /  ^  \ `---|  |----`   |  | `---|  |----`
 |   _  <  |   __|   /  /_\  \    |  |        |  |     |  |     
 |  |_)  | |  |____ /  _____  \   |  |        |  |     |  |     
 |______/  |_______/__/     \__\  |__|        |__|     |__|     
 
 BeatIt - code for cardiovascular simulations
 Copyright (C) 2016 Simone Rossi

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ============================================================================
 */

/**
 * \file Bidomain.cpp
 *
 * \class Bidomain
 *
 * \brief This class provides a simple factory implementation
 *
 * For details on how to use it check the test_factory in the testsuite folder
 *
 *
 * \author srossi
 *
 * \version 0.0
 *
 *
 * Contact: srossi@gmail.com
 *
 * Created on: Aug 11, 2016
 *
 */

// Basic include files needed for the mesh functionality.
#include "libmesh/mesh.h"
#include "libmesh/type_tensor.h"
#include "PoissonSolver/Poisson.hpp"

// Include files that define a simple steady system
#include "libmesh/linear_implicit_system.h"
#include "libmesh/transient_system.h"
#include "libmesh/explicit_system.h"
//
#include "libmesh/vector_value.h"
#include "libmesh/linear_solver.h"
// Define the Finite Element object.
#include "libmesh/fe.h"

// Define Gauss quadrature rules.
#include "libmesh/quadrature_gauss.h"

// Define useful datatypes for finite element
// matrix and vector components.
#include "libmesh/sparse_matrix.h"
#include "libmesh/numeric_vector.h"
#include "libmesh/petsc_vector.h"
#include "libmesh/dense_matrix.h"
#include "libmesh/dense_vector.h"
#include "libmesh/elem.h"

// Define the DofMap, which handles degree of freedom
// indexing.
#include "libmesh/dof_map.h"

#include "libmesh/exodusII_io.h"
//#include "libmesh/exodusII_io_helper.h"
#include "libmesh/gmv_io.h"

#include "libmesh/perf_log.h"

#include "libmesh/error_vector.h"
#include "libmesh/kelly_error_estimator.h"
#include "libmesh/mesh_refinement.h"
#include "libmesh/fourth_error_estimators.h"
#include "libmesh/discontinuity_measure.h"
#include <sys/stat.h>

#include "Electrophysiology/IonicModels/NashPanfilov.hpp"
#include "Electrophysiology/IonicModels/Grandi11.hpp"
#include "Electrophysiology/IonicModels/ORd.hpp"
#include "Electrophysiology/IonicModels/TP06.hpp"
#include "Electrophysiology/Bidomain/Bidomain.hpp"
#include "Util/SpiritFunction.hpp"

#include "libmesh/discontinuity_measure.h"
#include "libmesh/fe_interface.h"
#include "libmesh/petsc_linear_solver.h"
#include "libmesh/petsc_vector.h"
#include "libmesh/petsc_matrix.h"
#include "Electrophysiology/Pacing/PacingProtocolSpirit.hpp"

namespace BeatIt
{

ElectroSolver* createBidomain(libMesh::EquationSystems& es)
{
    return new Bidomain(es);
}

// ///////////////////////////////////////////////////////////////////////
// ///////////////////////////////////////////////////////////////////////
typedef libMesh::TransientLinearImplicitSystem BidomainSystem;
typedef libMesh::TransientLinearImplicitSystem ElectroSystem;
typedef libMesh::TransientExplicitSystem IonicModelSystem;
typedef libMesh::ExplicitSystem ParameterSystem;

Bidomain::Bidomain(libMesh::EquationSystems& es)
        : ElectroSolver(es, "bidomain")
{

}

Bidomain::~Bidomain()
{
}

void Bidomain::setup_systems(GetPot& data, std::string section)
{
    // ///////////////////////////////////////////////////////////////////////
    // ///////////////////////////////////////////////////////////////////////
    // Starts by creating the equation systems
    // 1) ADR
    std::cout << "* BIDOMAIN: Creating new System for the hyperbolic bidomain equations" << std::endl;
    BidomainSystem& bidomain_system = M_equationSystems.add_system<BidomainSystem>(M_model);
    // TO DO: Generalize to higher order
    bidomain_system.add_variable("Q", libMesh::FIRST);
    bidomain_system.add_variable("Ve", libMesh::FIRST);

    // Add 3 matrices
    bidomain_system.add_matrix("lumped_mass");
    bidomain_system.add_matrix("high_order_mass");
    // Add lumped mass matrix
    bidomain_system.add_vector("lumped_mass_vector");
    bidomain_system.add_matrix("mass");
    bidomain_system.add_matrix("stiffness");
    bidomain_system.add_vector("ionic_currents");
    bidomain_system.add_vector("old_solution");
    bidomain_system.add_vector("nullspace");
    bidomain_system.add_vector("residual");
    M_exporterNames.insert(M_model);

    bidomain_system.init();

    M_constraint_dof_id = -1;

//  bidomain_system.reinit_constraints();
    std::cout << "* BIDOMAIN: reinitialized constraints" << std::endl;

    // WAVE

    BidomainSystem& wave_system = M_equationSystems.add_system<BidomainSystem>("wave");
    wave_system.add_variable("V", libMesh::FIRST);
    wave_system.add_matrix("Ki");
    wave_system.add_vector("KiV");
    wave_system.add_vector("aux");
    M_exporterNames.insert("wave");
    wave_system.init();

    // ///////////////////////////////////////////////////////////////////////
    // ///////////////////////////////////////////////////////////////////////
    // 2) ODEs
    setup_ODE_systems(data, section);


    // ///////////////////////////////////////////////////////////////////////
    // ///////////////////////////////////////////////////////////////////////
    // Distributed Parameters
    std::cout << "* BIDOMAIN: Creating parameters spaces " << std::endl;
    ParameterSystem& intra_conductivity_system = M_equationSystems.add_system<ParameterSystem>("intra_conductivity");
    intra_conductivity_system.add_variable("Dffi", libMesh::CONSTANT, libMesh::MONOMIAL);
    intra_conductivity_system.add_variable("Dssi", libMesh::CONSTANT, libMesh::MONOMIAL);
    intra_conductivity_system.add_variable("Dnni", libMesh::CONSTANT, libMesh::MONOMIAL);
    ParameterSystem& extra_conductivity_system = M_equationSystems.add_system<ParameterSystem>("extra_conductivity");
    extra_conductivity_system.add_variable("Dffe", libMesh::CONSTANT, libMesh::MONOMIAL);
    extra_conductivity_system.add_variable("Dsse", libMesh::CONSTANT, libMesh::MONOMIAL);
    extra_conductivity_system.add_variable("Dnne", libMesh::CONSTANT, libMesh::MONOMIAL);

    std::cout << "* BIDOMAIN: Initializing equation systems " << std::endl;
    // Initializing
    intra_conductivity_system.init();
    extra_conductivity_system.init();


//    M_equationSystems.init();
    //M_equationSystems.print_info();

    // Conductivity Tensor in local coordinates
    // Fiber direction
    // Default Value = 1.3342  kOhm^-1 cm^-1
    double Dffi = M_datafile(section + "/Dffi", 1.3342);
    // Sheet direction
    // Default Value = 1.3342  kOhm^-1 cm^-1
    double Dssi = M_datafile(section + "/Dssi", 0.17606);
    // Cross fiber direction
    // Default Value = 1.3342  kOhm^-1 cm^-1
    double Dnni = M_datafile(section + "/Dnni", 0.17606);
    // Default Value = 1.3342  kOhm^-1 cm^-1
    double Dffe = M_datafile(section + "/Dffe", 1.3342);
    // Sheet direction
    // Default Value = 1.3342  kOhm^-1 cm^-1
    double Dsse = M_datafile(section + "/Dsse", 0.17606);
    // Cross fiber direction
    // Default Value = 1.3342  kOhm^-1 cm^-1
    double Dnne = M_datafile(section + "/Dnne", 0.17606);

    // Surface to volume ratio
    double Chi = M_datafile(section + "/Chi", 1400.0);
    // Intracellular and extracellular relaxation times
    double tau_i = M_datafile(section + "/tau_i", 0.0);
    double tau_e = M_datafile(section + "/tau_e", 0.0);
    M_equationSystems.parameters.set<double>("Chi") = Chi;
    M_equationSystems.parameters.set<double>("tau_i") = tau_i;
    M_equationSystems.parameters.set<double>("tau_e") = tau_e;

    std::string anisotropy = M_datafile(section + "/anisotropy", "orhotropic");
    std::map<std::string, Anisotropy> aniso_map;
    aniso_map["orthotropic"] = Anisotropy::Orthotropic;
    aniso_map["isotropic"] = Anisotropy::Isotropic;
    aniso_map["transverse"] = Anisotropy::TransverselyIsotropic;
    std::cout << "* BIDOMAIN: Parameters: " << std::endl;
    std::cout << "              Chi = " << Chi << std::endl;
    std::cout << "              Dffi = " << Dffi << std::endl;
    std::cout << "              Dssi = " << Dssi << std::endl;
    std::cout << "              Dnni = " << Dnni << std::endl;
    std::cout << "              Dffe = " << Dffe << std::endl;
    std::cout << "              Dsse = " << Dsse << std::endl;
    std::cout << "              Dnne = " << Dnne << std::endl;
    std::cout << "              tau_i = " << tau_i << std::endl;
    std::cout << "              tau_e = " << tau_e << std::endl;
    std::cout << "              anisotropy = " << anisotropy << std::endl;

    M_equationType = EquationType::ParabolicParabolicHyperbolic;
    if (tau_e == tau_i)
        M_equationType = EquationType::ParabolicEllipticHyperbolic;
    if (tau_i == tau_e && 0 == tau_i)
        M_equationType = EquationType::ParabolicEllipticBidomain;

    bool ground_ve = data(section + "/ground_ve", false);
    if(ground_ve) M_ground_ve = Ground::GroundNode;
}

void Bidomain::init_systems(double time)
{
    // WAVE
    ElectroSystem& wave_system = M_equationSystems.get_system<ElectroSystem>("wave");

    std::string v_ic = M_datafile(M_section + "/ic", "");
    if (v_ic != "")
    {
        std::cout << "* BIDOMAIN: Found bidomain initial condition: " << v_ic << std::endl;
        SpiritFunction bidomain_ic;
        bidomain_ic.read(v_ic);
        M_equationSystems.parameters.set<libMesh::Real>("time") = time;
        wave_system.time = time;
        std::cout << "* BIDOMAIN: Projecting initial condition to bidomain system ... " << std::flush;
        wave_system.project_solution(&bidomain_ic);
        std::cout << " done" << std::endl;
    }
    std::cout << "* BIDOMAIN: Copying initial conditions to vectors at n nd at n-1... " << std::flush;

    // Close vectors and update the values in old_local_solution and older_local_solution
    wave_system.solution->close();
    wave_system.old_local_solution->close();
    wave_system.older_local_solution->close();
    advance();
    std::cout << " done" << std::endl;

    IonicModelSystem& istim_system = M_equationSystems.get_system<IonicModelSystem>("istim");
    std::cout << "* BIDOMAIN: Initializing activation times to -1  ... " << std::flush;
    ParameterSystem& activation_times_system = M_equationSystems.get_system<ParameterSystem>("activation_times");
    auto first_local_index = activation_times_system.solution->first_local_index();
    auto last_local_index = activation_times_system.solution->last_local_index();

    for (auto i = first_local_index; i < last_local_index; ++i)
    {
        activation_times_system.solution->set(i, -1.0);
    }
    std::cout << " done" << std::endl;

    std::string fibers_data = M_datafile(M_section + "/fibers", "1.0, 0.0, 0.0");
    std::string sheets_data = M_datafile(M_section + "/sheets", "0.0, 1.0, 0.0");
    std::string xfibers_data = M_datafile(M_section + "/xfibers", "0.0, 0.0, 1.0");

    SpiritFunction fibers_func;
    SpiritFunction sheets_func;
    SpiritFunction xfibers_func;

    fibers_func.read(fibers_data);
    sheets_func.read(sheets_data);
    xfibers_func.read(xfibers_data);

    ParameterSystem& fiber_system = M_equationSystems.get_system<ParameterSystem>("fibers");
    ParameterSystem& sheets_system = M_equationSystems.get_system<ParameterSystem>("sheets");
    ParameterSystem& xfiber_system = M_equationSystems.get_system<ParameterSystem>("xfibers");

    std::cout << "* BIDOMAIN: Creating fibers from function: " << fibers_data << std::flush;
    fiber_system.project_solution(&fibers_func);
    std::cout << " done" << std::endl;
    std::cout << "* BIDOMAIN: Creating sheets from function: " << sheets_data << std::flush;
    sheets_system.project_solution(&sheets_func);
    std::cout << " done" << std::endl;
    std::cout << "* BIDOMAIN: Creating xfibers from function: " << xfibers_data << std::flush;
    xfiber_system.project_solution(&xfibers_func);
    std::cout << " done" << std::endl;

    ParameterSystem& intra_conductivity_system = M_equationSystems.get_system<ParameterSystem>("intra_conductivity");
    std::string Dffi_data = M_datafile(M_section + "/Dffi", "2.0");
    std::string Dssi_data = M_datafile(M_section + "/Dssi", "0.2");
    std::string Dnni_data = M_datafile(M_section + "/Dnni", "0.2");
    SpiritFunction intra_conductivity_func;
    intra_conductivity_func.add_function(Dffi_data);
    intra_conductivity_func.add_function(Dssi_data);
    intra_conductivity_func.add_function(Dnni_data);
    intra_conductivity_system.project_solution(&intra_conductivity_func);
    ParameterSystem& extra_conductivity_system = M_equationSystems.get_system<ParameterSystem>("extra_conductivity");
    std::string Dffe_data = M_datafile(M_section + "/Dffe", "1.5");
    std::string Dsse_data = M_datafile(M_section + "/Dsse", "1.0");
    std::string Dnne_data = M_datafile(M_section + "/Dnne", "1.0");
    SpiritFunction extra_conductivity_func;
    extra_conductivity_func.add_function(Dffe_data);
    extra_conductivity_func.add_function(Dsse_data);
    extra_conductivity_func.add_function(Dnne_data);
    extra_conductivity_system.project_solution(&extra_conductivity_func);

}

void Bidomain::assemble_matrices(double dt)
{
	std::cout << "* BIDOMAIN: Assembling System Matrix" << std::endl;
    using std::unique_ptr;

    // Coefficient for matrix
    double cdt = dt;
    if(M_timestep_counter > 0 && M_timeIntegrator == TimeIntegrator::SecondOrderIMEX)
    {
        cdt = 2.0 / 3.0 * dt;
    }

    const libMesh::MeshBase & mesh = M_equationSystems.get_mesh();
    const unsigned int dim = mesh.mesh_dimension();
    const unsigned int max_dim = 3;
    double Cm = 1.0;
    const libMesh::Real Chi = M_equationSystems.parameters.get<libMesh::Real>("Chi");
    const libMesh::Real tau_e = M_equationSystems.parameters.get<libMesh::Real>("tau_e");
    const libMesh::Real tau_i = M_equationSystems.parameters.get<libMesh::Real>("tau_i");

    // Get a reference to the LinearImplicitSystem we are solving
    BidomainSystem& bidomain_system = M_equationSystems.get_system<BidomainSystem>("bidomain");
    IonicModelSystem& ionic_model_system = M_equationSystems.get_system<IonicModelSystem>("ionic_model");
    BidomainSystem& wave_system = M_equationSystems.get_system<BidomainSystem>("wave");

    unsigned int Q_var = bidomain_system.variable_number("Q");
    unsigned int Ve_var = bidomain_system.variable_number("Ve");

    {
        bidomain_system.matrix->zero();
        typedef libMesh::PetscMatrix<libMesh::Number> Mat;
        Mat * mat = dynamic_cast<Mat *>(bidomain_system.matrix);
        MatSetOption(mat->mat(), MAT_NEW_NONZERO_ALLOCATION_ERR, PETSC_FALSE);
    }
    bidomain_system.get_matrix("mass").zero();
    bidomain_system.get_matrix("lumped_mass").zero();
    bidomain_system.get_matrix("high_order_mass").zero();
    bidomain_system.get_matrix("stiffness").zero();
    bidomain_system.get_vector("lumped_mass_vector").zero();

    ParameterSystem& fiber_system = M_equationSystems.get_system<ParameterSystem>("fibers");
    ParameterSystem& sheets_system = M_equationSystems.get_system<ParameterSystem>("sheets");
    ParameterSystem& xfiber_system = M_equationSystems.get_system<ParameterSystem>("xfibers");
    ParameterSystem& intra_conductivity_system = M_equationSystems.get_system<ParameterSystem>("intra_conductivity");
    ParameterSystem& extra_conductivity_system = M_equationSystems.get_system<ParameterSystem>("extra_conductivity");

    // A reference to the  DofMap object for this system.  The  DofMap
    // object handles the index translation from node and element numbers
    // to degree of freedom numbers.  We will talk more about the  DofMap
    // in future examples.
    const libMesh::DofMap & dof_map_bidomain = bidomain_system.get_dof_map();
    const libMesh::DofMap & dof_map_wave = wave_system.get_dof_map();
    const libMesh::DofMap & dof_map_fibers = fiber_system.get_dof_map();

    // Get a constant reference to the Finite Element type
    // for the first (and only) variable in the system.
    libMesh::FEType fe_type_qp1 = dof_map_bidomain.variable_type(Q_var);

    // Build a Finite Element object of the specified type.  Since the
    // FEBase::build() member dynamically creates memory we will
    // store the object as a std::unique_ptr<FEBase>.  This can be thought
    // of as a pointer that will clean up after itself.  Introduction Example 4
    // describes some advantages of  std::unique_ptr's in the context of
    // quadrature rules.
    std::unique_ptr<libMesh::FEBase> fe_qp1(libMesh::FEBase::build(dim, fe_type_qp1));

    // A 5th order Gauss quadrature rule for numerical integration.
    libMesh::QGauss qrule(dim, libMesh::THIRD);

    // Tell the finite element object to use our quadrature rule.
    fe_qp1->attach_quadrature_rule(&qrule);

    // Here we define some references to cell-specific data that
    // will be used to assemble the linear system.
    //
    // The element Jacobian * quadrature weight at each integration point.
    const std::vector<libMesh::Real> & JxW_qp1 = fe_qp1->get_JxW();

    // The physical XY locations of the quadrature points on the element.
    // These might be useful for evaluating spatially varying material
    // properties at the quadrature points.
    const std::vector<libMesh::Point> & q_point_qp1 = fe_qp1->get_xyz();

    // The element shape functions evaluated at the quadrature points.
    const std::vector<std::vector<libMesh::Real> > & phi_qp1 = fe_qp1->get_phi();

    // The element shape function gradients evaluated at the quadrature
    // points.
    const std::vector<std::vector<libMesh::RealGradient> > & dphi_qp1 = fe_qp1->get_dphi();

    const std::vector<std::vector<libMesh::Real> > & dphidx_qp1 = fe_qp1->get_dphidx();
    const std::vector<std::vector<libMesh::Real> > & dphidy_qp1 = fe_qp1->get_dphidy();
    const std::vector<std::vector<libMesh::Real> > & dphidz_qp1 = fe_qp1->get_dphidz();

    // Define data structures to contain the element matrix
    // and right-hand-side vector contribution.  Following
    // basic finite element terminology we will denote these
    // "Ke" and "Fe".  These datatypes are templated on
    //  Number, which allows the same code to work for real
    // or complex numbers.
    libMesh::DenseMatrix<libMesh::Number> Ke;
    libMesh::DenseMatrix<libMesh::Number> Kie;
    libMesh::DenseMatrix<libMesh::Number> Me;
    libMesh::DenseMatrix<libMesh::Number> Mel;
    libMesh::DenseVector<libMesh::Number> Fe;

    // for interior penalty
//     std::unique_ptr<libMesh::FEBase> fe_elem_face(libMesh::FEBase::build(dim, fe_type_qp1));
//     std::unique_ptr<libMesh::FEBase> fe_neighbor_face(libMesh::FEBase::build(dim, fe_type_qp1));
    // Tell the finite element object to use our quadrature rule.
    libMesh::QGauss qface(dim - 1, fe_type_qp1.default_quadrature_order());

//     fe_elem_face->attach_quadrature_rule(&qface);
//     fe_neighbor_face->attach_quadrature_rule(&qface);
    // Data for surface integrals on the element boundary
//     const std::vector<std::vector<libMesh::Real> > &  phi_face = fe_elem_face->get_phi();
//     const std::vector<std::vector<libMesh::RealGradient> > & dphi_face = fe_elem_face->get_dphi();
//     const std::vector<libMesh::Real> & JxW_face = fe_elem_face->get_JxW();
//     const std::vector<libMesh::Point> & qface_normals = fe_elem_face->get_normals();
//     const std::vector<libMesh::Point> & qface_points = fe_elem_face->get_xyz();
//     // Data for surface integrals on the neighbor boundary
//     const std::vector<std::vector<libMesh::Real> > &  phi_neighbor_face = fe_neighbor_face->get_phi();
//     const std::vector<std::vector<libMesh::RealGradient> > & dphi_neighbor_face = fe_neighbor_face->get_dphi();
    // Data structures to contain the element and neighbor boundary matrix
    // contribution. This matrices will do the coupling beetwen the dofs of
    // the element and those of his neighbors.
    // Ken: matrix coupling elem and neighbor dofs
//     libMesh::DenseMatrix<libMesh::Number> Kne;
//     libMesh::DenseMatrix<libMesh::Number> Ken;
//     libMesh::DenseMatrix<libMesh::Number> Kee;
//     libMesh::DenseMatrix<libMesh::Number> Knn;

    // This vector will hold the degree of freedom indices for
    // the element.  These define where in the global system
    // the element degrees of freedom get mapped.
    std::vector<libMesh::dof_id_type> dof_indices;
    std::vector<libMesh::dof_id_type> dof_indices_Q;
    std::vector<libMesh::dof_id_type> dof_indices_Ve;
    std::vector<libMesh::dof_id_type> dof_indices_V;

    std::vector<libMesh::dof_id_type> dof_indices_fibers;

    // Now we will loop over all the elements in the mesh.
    // We will compute the element matrix and right-hand-side
    // contribution.
    //
    // Element iterators are a nice way to iterate through all the
    // elements, or all the elements that have some property.  The
    // iterator el will iterate from the first to// the last element on
    // the local processor.  The iterator end_el tells us when to stop.
    // It is smart to make this one const so that we don't accidentally
    // mess it up!  In case users later modify this program to include
    // refinement, we will be safe and will only consider the active
    // elements; hence we use a variant of the active_elem_iterator.
    libMesh::MeshBase::const_element_iterator el_start = mesh.active_local_elements_begin();
    libMesh::MeshBase::const_element_iterator el = mesh.active_local_elements_begin();
    const libMesh::MeshBase::const_element_iterator end_el = mesh.active_local_elements_end();

    // Loop over the elements.  Note that  ++el is preferred to
    // el++ since the latter requires an unnecessary temporary
    // object.
    double f0[3];
    double s0[3];
    double n0[3];
    libMesh::RealGradient DigradV;
    libMesh::RealGradient DigradVe;
    libMesh::RealGradient DiegradVe;
    libMesh::TensorValue<double> D0i;
    libMesh::TensorValue<double> D0e;

    //ground dof ID
    if (M_ground_ve == Ground::GroundNode)
    {
        if (0 == M_equationSystems.comm().rank())
        {
            std::cout << "* BIDOMAIN: adding constraint" << std::endl;
            const libMesh::Elem * elem = *el;
            dof_map_bidomain.dof_indices(elem, dof_indices_Ve, Ve_var);

            //libMesh::MeshBase::const_node_iterator first_node = mesh.local_nodes_begin();
            //std::vector < libMesh::dof_id_type > dof_indices_Ve;
            //libMesh::DofMap bidomain_dofmap =  bidomain_system.get_dof_map();
            //bidomain_dofmap.dof_indices(*first_node, dof_indices_Ve, 1);
            M_constraint_dof_id = static_cast<int>(dof_indices_Ve[0]);
            std::cout << "* BIDOMAIN: adding constraint done" << std::endl;
        }
        // FIX ME: GCC 9.1 warning
        // M_equationSystems.comm().max(M_constraint_dof_id);
        std::cout << "* BIDOMAIN: Ground node ID: " << M_constraint_dof_id << std::endl;
    }

    std::cout << "start loop over elements" << std::endl;
    for (; el != end_el; ++el)
    {
        const libMesh::Elem * elem = *el;
        const unsigned int elem_id = elem->id();
        dof_map_bidomain.dof_indices(elem, dof_indices);
        dof_map_bidomain.dof_indices(elem, dof_indices_Q, Q_var);
        dof_map_bidomain.dof_indices(elem, dof_indices_Ve, Ve_var);
        dof_map_wave.dof_indices(elem, dof_indices_V, 0);
        dof_map_fibers.dof_indices(elem, dof_indices_fibers);

        // Compute the element-specific data for the current
        // element.  This involves computing the location of the
        // quadrature points (q_point) and the shape functions
        // (phi, dphi) for the current element.
        fe_qp1->reinit(elem);

        // Zero the element matrix and right-hand side before
        // summing them.  We use the resize member here because
        // the number of degrees of freedom might have changed from
        // the last element.  Note that this will be the case if the
        // element type is different (i.e. the last element was a
        // triangle, now we are on a quadrilateral).

        // The  DenseMatrix::resize() and the  DenseVector::resize()
        // members will automatically zero out the matrix  and vector.
        auto n_dofs = dof_indices.size();
        const unsigned int n_Q_dofs = dof_indices_Q.size();
        auto n_dofs_V = dof_indices_V.size();
        Ke.resize(n_dofs, n_dofs);
        Kie.resize(n_dofs_V, n_dofs_V);
        Me.resize(n_dofs, n_dofs);
        Mel.resize(n_dofs, n_dofs);
        Fe.resize(n_dofs);

        //        std::cout << "Fibers" << std::endl;
        // fiber direction
        f0[0] = (*fiber_system.solution)(dof_indices_fibers[0]);
        f0[1] = (*fiber_system.solution)(dof_indices_fibers[1]);
        f0[2] = (*fiber_system.solution)(dof_indices_fibers[2]);
        // sheet direction
        s0[0] = (*sheets_system.solution)(dof_indices_fibers[0]);
        s0[1] = (*sheets_system.solution)(dof_indices_fibers[1]);
        s0[2] = (*sheets_system.solution)(dof_indices_fibers[2]);
        // crossfiber direction
        n0[0] = (*xfiber_system.solution)(dof_indices_fibers[0]);
        n0[1] = (*xfiber_system.solution)(dof_indices_fibers[1]);
        n0[2] = (*xfiber_system.solution)(dof_indices_fibers[2]);
        // Conductivity tensor
        double Dffe = (*extra_conductivity_system.solution)(dof_indices_fibers[0]);
        double Dsse = (*extra_conductivity_system.solution)(dof_indices_fibers[1]);
        double Dnne = (*extra_conductivity_system.solution)(dof_indices_fibers[2]);
        double Dffi = (*intra_conductivity_system.solution)(dof_indices_fibers[0]);
        double Dssi = (*intra_conductivity_system.solution)(dof_indices_fibers[1]);
        double Dnni = (*intra_conductivity_system.solution)(dof_indices_fibers[2]);

        switch (M_anisotropy)
        {
        case Anisotropy::Isotropic:
        {
            for (int idim = 0; idim < max_dim; ++idim)
            {
                D0i(idim, idim) = Dffi;
                D0e(idim, idim) = Dffe;
            }

            break;
        }

        case Anisotropy::TransverselyIsotropic:
        {
            for (int idim = 0; idim < max_dim; ++idim)
            {
                for (int jdim = 0; jdim < max_dim; ++jdim)
                {

                    D0i(idim, jdim) = (Dffi - Dssi) * f0[idim] * f0[jdim];
                    D0e(idim, jdim) = (Dffe - Dsse) * f0[idim] * f0[jdim];
                }
                D0i(idim, idim) += Dssi;
                D0e(idim, idim) += Dsse;
            }
            break;
        }

        case Anisotropy::Orthotropic:
        default:
        {
            for (int idim = 0; idim < max_dim; ++idim)
            {
                for (int jdim = 0; jdim < max_dim; ++jdim)
                {
                    D0i(idim, jdim) = Dffi * f0[idim] * f0[jdim] + Dssi * s0[idim] * s0[jdim] + Dnni * n0[idim] * n0[jdim];

                    D0e(idim, jdim) = Dffe * f0[idim] * f0[jdim] + Dsse * s0[idim] * s0[jdim] + Dnne * n0[idim] * n0[jdim];
                }
            }
            break;
        }
        }
        D0i /= Chi;
        D0e /= Chi;

//      std::cout << "Dffe: " << Dffe << std::endl;
//        std::cout << "Dsse: " << Dsse << std::endl;
//        std::cout << "Dnne: " << Dnne << std::endl;
//        std::cout << "Dffi: " << Dffi << std::endl;
//        std::cout << "Dssi: " << Dssi << std::endl;
//        std::cout << "Dnni: " << Dnni << std::endl;
//       std::cout << "f  = [" << f0[0] << "," << f0[1]  << ", " << f0[2]  << "]"<< std::endl;
//       std::cout << "s = [" << s0[0] << "," << s0[1]  << ", " << s0[2]  << "]"<< std::endl;
//       std::cout << "n = [" << n0[0] << "," << n0[1]  << ", " << n0[2]  << "]"<< std::endl;
//       std::cout << "D0 = " << std::endl;
//       for(int idim = 0; idim < dim; ++idim )
//         {
//           std::cout << D0(idim, 0) << "," << D0(idim, 1) << ", " << D0(idim, 2) << std::endl;
//         }

        // Assemble Mass terms
        for (unsigned int qp = 0; qp < qrule.n_points(); qp++)
        {
            //  Matrix
            for (unsigned int i = 0; i < phi_qp1.size(); i++)
            {
                DigradV = D0i * dphi_qp1[i][qp];
                DigradVe = D0i * dphi_qp1[i][qp];
                DiegradVe = (D0i + D0e) * dphi_qp1[i][qp];
                for (unsigned int j = 0; j < phi_qp1.size(); j++)
                {
                    // Q Mass term
                    Me(i, j) += JxW_qp1[qp] * (phi_qp1[i][qp] * phi_qp1[j][qp]);
                    Mel(i, i) += JxW_qp1[qp] * (phi_qp1[i][qp] * phi_qp1[j][qp]);
                    Fe(i) += JxW_qp1[qp] * (phi_qp1[i][qp] * phi_qp1[j][qp]);
                    // Ve Mass terms
                    Me(i + n_Q_dofs, j + n_Q_dofs) += JxW_qp1[qp] * (phi_qp1[i][qp] * phi_qp1[j][qp]);
                    Mel(i + n_Q_dofs, i + n_Q_dofs) += JxW_qp1[qp] * (phi_qp1[i][qp] * phi_qp1[j][qp]);
                    Fe(i + n_Q_dofs) += JxW_qp1[qp] * (phi_qp1[i][qp] * phi_qp1[j][qp]);

                    // Block QQ :  ( ( tau_i / cdt + dt ) * Cm * M_L + cdt * Ki )
                    // Using lumped mass matrix
		            Ke(i, i) += ( 1.0 + tau_i/cdt ) * Cm * JxW_qp1[qp] * (phi_qp1[i][qp] * phi_qp1[j][qp]);
                    Ke(i, j) += cdt * JxW_qp1[qp] * DigradV * dphi_qp1[j][qp];
                    // Block QVe : Ki
                    // Using lumped mass matrix
                    Ke(i, j + n_Q_dofs) += JxW_qp1[qp] * DigradVe * dphi_qp1[j][qp];

                    // Block VeQ : (tau_i-tau_e) / cdt * Cm * M + dt * Ki
                    // Using lumped mass matrix
                    Ke(i + n_Q_dofs, i) += (tau_i - tau_e) / cdt * Cm * JxW_qp1[qp] * (phi_qp1[i][qp] * phi_qp1[j][qp]);
                    Ke(i + n_Q_dofs, j) += cdt * JxW_qp1[qp] * DigradV * dphi_qp1[j][qp];

                    // Block VeVe : Kie
                    // Using lumped mass matrix
                    Ke(i + n_Q_dofs, j + n_Q_dofs) += JxW_qp1[qp] * DiegradVe * dphi_qp1[j][qp];
                    // Ki

                    //std::cout << std::endl;
                    Kie(i, j) += JxW_qp1[qp] * DigradV * dphi_qp1[j][qp];

                }
            }
        }

//      dof_map_bidomain.constrain_element_matrix_and_vector(Ke, Fe, dof_indices);
//      dof_map_bidomain.constrain_element_matrix_and_vector(Me, Fe, dof_indices);
//      dof_map_bidomain.constrain_element_matrix_and_vector(Mel, Fe, dof_indices);

        if (el == el_start)
        {
//             Ke(n_Q_dofs, n_Q_dofs) +=  1e5;
        }
        bidomain_system.get_matrix("mass").add_matrix(Me, dof_indices);
        bidomain_system.get_matrix("lumped_mass").add_matrix(Mel, dof_indices);
        bidomain_system.get_vector("lumped_mass_vector").add_vector(Fe, dof_indices);
        bidomain_system.matrix->add_matrix(Ke, dof_indices);
        wave_system.get_matrix("Ki").add_matrix(Kie, dof_indices_V);
    }
    // closing matrices and vectors
    bidomain_system.get_matrix("mass").close();
    bidomain_system.get_matrix("lumped_mass").close();
    bidomain_system.get_matrix("stiffness").close();
    bidomain_system.get_vector("lumped_mass_vector").close();
    bidomain_system.get_matrix("high_order_mass").close();
    bidomain_system.get_matrix("high_order_mass").add(0.5, bidomain_system.get_matrix("mass"));
    bidomain_system.get_matrix("high_order_mass").add(0.5, bidomain_system.get_matrix("lumped_mass"));
    bidomain_system.matrix->close();

    // set diagonal to 1
    if (M_ground_ve == Ground::GroundNode)
    {
        std::vector<unsigned int> rows(1, M_constraint_dof_id);
        bidomain_system.matrix->zero_rows(rows, 1.0);
    }
    else if ( M_ground_ve == Ground::Nullspace )
    {
        typedef libMesh::PetscMatrix<libMesh::Number> Mat;
        typedef libMesh::PetscVector<libMesh::Number> Vec;

        libMesh::DenseVector<libMesh::Number> NSe;
        MatNullSpace nullspace;

        libMesh::MeshBase::const_node_iterator node = mesh.local_nodes_begin();
        const libMesh::MeshBase::const_node_iterator end_node = mesh.local_nodes_end();
        for (; node != end_node; ++node)
        {
            const libMesh::Node * nn = *node;
            dof_map_bidomain.dof_indices(nn, dof_indices_Q, Q_var);
            dof_map_bidomain.dof_indices(nn, dof_indices_Ve, Ve_var);
            bidomain_system.get_vector("nullspace").set(dof_indices_Q[0], 0.0);
            bidomain_system.get_vector("nullspace").set(dof_indices_Ve[0], 1.0);
        }
        bidomain_system.get_vector("nullspace").close();
        int size = bidomain_system.get_vector("nullspace").size();
        bidomain_system.get_vector("nullspace") /= std::sqrt(size / 2);
        // setting solver type
        std::string solver_type = M_datafile(M_section + "/linear_solver/type", "gmres");
        std::cout << "* BIDOMAIN: using " << solver_type << std::endl;
        std::string prec_type = M_datafile(M_section + "/linear_solver/preconditioner", "amg");
        std::cout << "* BIDOMAIN: using " << prec_type << std::endl;
        //M_linearSolver =  libMesh::LinearSolver<libMesh::Number>::build( M_equationSystems.comm() );
        //typedef libMesh::PetscLinearSolver<libMesh::Number> PetscSolver;
        //M_linearSolver.reset(new PetscSolver(M_equationSystems.comm()));

        std::cout << "* BIDOMAIN: setting solver  " << prec_type << std::endl;
//        M_linearSolver->set_solver_type(solver_map.find(solver_type)->second);
//        M_linearSolver->set_preconditioner_type(prec_map.find(prec_type)->second);

        std::cout << "* BIDOMAIN: closing matrix  " << prec_type << std::endl;
        Vec& N = (static_cast<Vec&>(bidomain_system.get_vector("nullspace")));
        std::cout << "* BIDOMAIN: nullspace vector  " << prec_type << std::endl;
        auto vec = N.vec();
        //std::cout << N.size() <<  ",  " <<
        std::cout << "* BIDOMAIN: nullspace vector 1 " << prec_type << std::endl;
        MatNullSpaceCreate(PETSC_COMM_WORLD, PETSC_FALSE, 1, &vec, &nullspace);
        std::cout << "* BIDOMAIN: nullspace vector  2" << prec_type << std::endl;
        Mat * mat = dynamic_cast<Mat *>(bidomain_system.matrix);
        std::cout << "* BIDOMAIN: nullspace created  " << prec_type << std::endl;

        MatSetNullSpace(mat->mat(), nullspace);
        MatNullSpaceDestroy(&nullspace);
    }
//      PetscBool  isSymmetric;
//      double tol = 1e-12;
//      auto code =  MatIsSymmetric(mat->mat(), tol, &isSymmetric);
//      std::cout << "The bidomain matrix is symmetric? " << isSymmetric << std::endl;
    typedef libMesh::PetscMatrix<libMesh::Number> PetscMatrix;
    M_linearSolver->init(dynamic_cast<PetscMatrix *>(bidomain_system.matrix));
//      std::cout << "* BIDOMAIN: initialized linear solver  " << prec_type
//              << std::endl;
//    }
}

void Bidomain::form_system_rhs(double dt, bool useMidpoint, const std::string& mass)
{
double cdt = dt;
        if(M_timestep_counter > 0 && TimeIntegrator::SecondOrderIMEX == M_timeIntegrator)
        {
           cdt = 2.0/3.0*dt;
        }

    BidomainSystem& bidomain_system = M_equationSystems.get_system<BidomainSystem>(M_model);
    // WAVE
    BidomainSystem& wave_system = M_equationSystems.get_system<BidomainSystem>("wave");
    IonicModelSystem& iion_system = M_equationSystems.get_system<IonicModelSystem>("iion");
    IonicModelSystem& istim_system = M_equationSystems.get_system<IonicModelSystem>("istim");
//    istim_system.solution->print();
    bidomain_system.rhs->zero();
    bidomain_system.rhs->close();
    bidomain_system.get_vector("ionic_currents").zero();
    bidomain_system.get_vector("ionic_currents").close();
    bidomain_system.get_vector("old_solution").zero();
    bidomain_system.get_vector("old_solution").close();
    iion_system.get_vector("diion").close();

    double Cm = 1.0; //M_ionicModelPtr->membraneCapacitance();
    const libMesh::Real tau_e = M_equationSystems.parameters.get<libMesh::Real>("tau_e");
    const libMesh::Real tau_i = M_equationSystems.parameters.get<libMesh::Real>("tau_i");

    // Evaluate vector Ki*V^n
    wave_system.get_vector("KiV").zero();
    wave_system.get_vector("KiV").close();
    wave_system.get_vector("aux").zero();
    wave_system.get_vector("aux").close();
    wave_system.get_matrix("Ki").close();

    // Evaluate
    // RECALL: we store in iion -I^n and in diion -dI^n
    // BFE:
    // RHS_Q  = tau_i / cdt * Cm * M * Q^n   + M * I^n + tau_i * M * dI^n - Ki * V^n
    // RHS_Ve  = M * [ ( tau_i - tau_e ) / cdt * Cm * Q^n + ( tau_i - tau_e ) dI^n ) - Ki * V^n
    // SBDF2
    // RHS_Q  = tau_i / cdt * Cm * M * [ 4/3*Q^n  - 1/3*Q^n-1 ]   + M * (2 I^n - I^n-1 + 2 tau_i *  dI^n - tau_i dI^n-1 )  - Ki * [4/3*V^n-1/3*V^n-1]
    // RHS_Ve  = M * [ ( tau_i - tau_e ) / cdt * Cm * [ 4/3*Q^n  - 1/3*Q^n-1 ]  + ( tau_i - tau_e ) ( 2 dI^n - dI^n-1 ) - Ki * [4/3*V^n-1/3*V^n-1]
    double Qn = 0.0;
    double Vn = 0.0;
    double istim = 0.0;
    double stim_i = 0.0;
    double surf_stim_i = 0.0;
    double stim_e = 0.0;
    double surf_stim_e = 0.0;
    double Iion = 0.0;
    double dIion = 0.0;

    double rhsq = 0.0;
    double rhsve = 0.0;
    double rhs_oldq = 0.0;
    double rhs_oldve = 0.0;
    double kv=0.0;

    const libMesh::MeshBase & mesh = M_equationSystems.get_mesh();
    libMesh::MeshBase::const_node_iterator node = mesh.local_nodes_begin();
    const libMesh::MeshBase::const_node_iterator end_node = mesh.local_nodes_end();

    const libMesh::DofMap & dof_map = bidomain_system.get_dof_map();
    const libMesh::DofMap & dof_map_V = wave_system.get_dof_map();

    std::vector<libMesh::dof_id_type> dof_indices;
    std::vector<libMesh::dof_id_type> dof_indices_V;
    std::vector<libMesh::dof_id_type> dof_indices_Ve;
    std::vector<libMesh::dof_id_type> dof_indices_Q;
//    bidomain_system.old_local_solution->print();
    for (; node != end_node; ++node)
    {
        const libMesh::Node * nn = *node;
        dof_map.dof_indices(nn, dof_indices_Q, 0);
        dof_map.dof_indices(nn, dof_indices_Ve, 1);
        dof_map_V.dof_indices(nn, dof_indices_V, 0);

        Qn = (*bidomain_system.old_local_solution)(dof_indices_Q[0]); //Q^n
        Vn = (*wave_system.old_local_solution)(dof_indices_V[0]);
        istim = (*istim_system.solution)(dof_indices_V[0]); //Istim^n+1
        stim_i = istim_system.get_vector("stim_i")(dof_indices_V[0]); //Istim^n+1
        surf_stim_i = istim_system.get_vector("surf_stim_i")(dof_indices_V[0]); //Istim^n+1
        stim_e = istim_system.get_vector("stim_e")(dof_indices_V[0]); //Istim^n+1
        surf_stim_e = istim_system.get_vector("surf_stim_e")(dof_indices_V[0]); //Istim^n+1
        Iion = (*iion_system.solution)(dof_indices_V[0]); //Iion^* (it contains Istim)
        dIion = iion_system.get_vector("diion")(dof_indices_V[0]); // dIion^*

        // BFE:
        // RHS_Q  = tau_i / cdt * Cm * M * Q^n   - M * I^n - tau_i * M * dI^n - Ki * V^n
        // RHS_Ve  = M * [ ( tau_i - tau_e ) / cdt * Cm * Q^n - ( tau_i - tau_e ) dI^n s - Ki * V^n
        // SBDF2
        // RHS_Q  = tau_i / cdt * Cm * M * [ 4/3*Q^n  - 1/3*Q^n-1 ]   - M * (2 I^n - I^n-1 + 2 tau_i *  dI^n - tau_i dI^n-1 )  - Ki * [4/3*V^n-1/3*V^n-1]
        // RHS_Ve  = M * [ ( tau_i - tau_e ) / cdt * Cm * [ 4/3*Q^n  - 1/3*Q^n-1 ]  - ( tau_i - tau_e ) ( 2 dI^n - dI^n-1 ) - Ki * [4/3*V^n-1/3*V^n-1]

        if(M_timestep_counter > 0 && TimeIntegrator::SecondOrderIMEX == M_timeIntegrator)
        {
            Iion = (*iion_system.solution)(dof_indices_V[0]); //Iion^* (it contains Istim)
            dIion = iion_system.get_vector("diion")(dof_indices_V[0]); // dIion^*
            double Q_nm1 = (*bidomain_system.older_local_solution)(dof_indices_Q[0]); //Q^n
            double V_nm1 = (*wave_system.older_local_solution)(dof_indices_V[0]);
            double dIion_old = iion_system.get_vector("diion_old")(dof_indices_V[0]); // dIion^*
            double Iion_old = (*iion_system.old_local_solution)(dof_indices_V[0]); // dIion^*
            // RHS_Q  = tau_i / cdt * Cm * M * [ 4/3*Q^n  - 1/3*Q^n-1 ]   - M * (2 I^n - I^n-1 + 2 tau_i *  dI^n - tau_i dI^n-1 )  - Ki * [4/3*V^n-1/3*V^n-1]
            // RHS_Ve  = M * [ ( tau_i - tau_e ) / cdt * Cm * [ 4/3*Q^n  - 1/3*Q^n-1 ]  - ( tau_i - tau_e ) ( 2 dI^n - dI^n-1 ) - Ki * [4/3*V^n-1/3*V^n-1]


            // RHS_Q  =  - M * (2 I^n - I^n-1 + 2 tau_i *  dI^n - tau_i dI^n-1 )
            rhsq = - (2*Iion - Iion_old + 2 * tau_i * dIion  - tau_i * dIion_old + istim);
            // RHS_Ve =  - ( tau_i - tau_e ) ( 2 dI^n - dI^n-1 )
            rhsve =  (tau_e - tau_i) * ( 2 * dIion - dIion_old ) - 0 * (stim_i + stim_e);
            // RHS_Q  = tau_i / cdt * Cm * M * [ 4/3*Q^n  - 1/3*Q^n-1 ]
            rhs_oldq = tau_i / cdt * Cm * ( 4 * Qn - Q_nm1 ) / 3.0;
            // RHS_Ve  = M * [ ( tau_i - tau_e ) / cdt * Cm * [ 4/3*Q^n  - 1/3*Q^n-1 ]
            rhs_oldve = (tau_i - tau_e) / cdt * Cm * ( 4 * Qn - Q_nm1 ) / 3.0;

            // Form: 4/3*Vn - 1/3 V^n-1
            kv = ( 4 * Vn - V_nm1 ) / 3.0;

        }
        else
        {
            // RHS_Q  = tau_i / cdt * Cm * M * Q^n   - M * I^n - tau_i * M * dI^n - Ki * V^n
            // RHS_Ve  = M * [ ( tau_i - tau_e ) / cdt * Cm * Q^n - ( tau_i - tau_e ) dI^n s - Ki * V^n
            // RHS_Q  = -( M * I^n + tau_i * M * dI^n )
            rhsq = - (Iion + tau_i * dIion + istim);
            // RHS_Ve  = -( tau_i - tau_e ) dI^n
            rhsve = (tau_e - tau_i) * dIion - 0 * (stim_i + stim_e);
            // RHS_Q  = tau_i / cdt * Cm * M * Q^n
            rhs_oldq = tau_i  / cdt * Cm * Qn;
            // RHS_Ve  = M * [ ( tau_i - tau_e ) / cdt * Cm * Q^n
            rhs_oldve = (tau_i - tau_e) / cdt * Cm * Qn;

            // Form: Vn
            kv = Vn;

        }

        bidomain_system.get_vector("old_solution").set(dof_indices_Q[0], rhs_oldq);
        bidomain_system.get_vector("old_solution").set(dof_indices_Ve[0], rhs_oldve);
        bidomain_system.get_vector("ionic_currents").set(dof_indices_Q[0], rhsq);
        bidomain_system.get_vector("ionic_currents").set(dof_indices_Ve[0], rhsve);

        wave_system.get_vector("aux").set(dof_indices_V[0], kv);
    }

    bidomain_system.get_vector("old_solution").close();
    bidomain_system.get_vector("ionic_currents").close();
    wave_system.get_vector("aux").close();
    // RHS * MASS MATRIX
    bidomain_system.rhs->add_vector(bidomain_system.get_vector("ionic_currents"), bidomain_system.get_matrix("mass"));
    // RHS * LUMPED MASS MATRIX
    bidomain_system.rhs->add_vector(bidomain_system.get_vector("old_solution"), bidomain_system.get_matrix("lumped_mass"));
    // NOTE: We use wave_system.solution  because here V has not been updated yet.
    //       So the vector still stores V^n
    wave_system.get_vector("KiV").add_vector(wave_system.get_vector("aux"),wave_system.get_matrix("Ki"));

    // Add KiVn to the rhs
    double KiVn = 0.0;

    node = mesh.local_nodes_begin();

    for (; node != end_node; ++node)
    {
        const libMesh::Node * nn = *node;
        dof_map_V.dof_indices(nn, dof_indices_V, 0);
        dof_map.dof_indices(nn, dof_indices_Q, 0);
        dof_map.dof_indices(nn, dof_indices_Ve, 1);
        KiVn = wave_system.get_vector("KiV")(dof_indices_V[0]);

        bidomain_system.rhs->add(dof_indices_Q[0], -KiVn);
        bidomain_system.rhs->add(dof_indices_Ve[0], -KiVn);
    }

    //bidomain_system.rhs->set(M_constraint_dof_id, 0.0);
    bidomain_system.rhs->close();
    if (M_ground_ve != Ground::Nullspace )
    {
        bidomain_system.rhs->set(static_cast<libMesh::dof_id_type>(M_constraint_dof_id), 0.0);
    }

}

void Bidomain::solve_diffusion_step(double dt, double time, bool useMidpoint, const std::string& mass, bool reassemble)
{
    // If we are using SBDF2 we need to form the matrix at the second timestep
    if(M_timestep_counter == 1 && TimeIntegrator::SecondOrderIMEX == M_timeIntegrator)
    {
        assemble_matrices(dt);
    }
    const libMesh::MeshBase & mesh = M_equationSystems.get_mesh();

    BidomainSystem& bidomain_system = M_equationSystems.get_system<BidomainSystem>(M_model); // Q and Ve
    BidomainSystem& wave_system = M_equationSystems.get_system<BidomainSystem>("wave"); // V

    form_system_rhs(dt, useMidpoint, mass);

    // bidomain_system.matrix->print();
    // bidomain_system.rhs->print();
    double tol = 1e-12;
    double max_iter = 2000;

    std::pair<unsigned int, double> rval = std::make_pair(0, 0.0);
    rval = M_linearSolver->solve(*bidomain_system.matrix, *bidomain_system.solution, *bidomain_system.rhs, tol, max_iter);
//    bidomain_system.solution->print();

    // Update V_n+1 = V_n + dt * Q_n+1:
    // 1) Copy Q_n+1 in wave_system.solution
    // 2) Evaluate V_n + dt * Q_n+1
    libMesh::MeshBase::const_node_iterator node = mesh.local_nodes_begin();
    const libMesh::MeshBase::const_node_iterator end_node = mesh.local_nodes_end();

    const libMesh::DofMap & dof_map = bidomain_system.get_dof_map();
    const libMesh::DofMap & dof_map_V = wave_system.get_dof_map();

    std::vector<libMesh::dof_id_type> dof_indices;
    std::vector<libMesh::dof_id_type> dof_indices_V;
    std::vector<libMesh::dof_id_type> dof_indices_Ve;
    std::vector<libMesh::dof_id_type> dof_indices_Q;

    // Update V_n+1 = V_n + dt * Q_n+1:

    // 1) Copy Q_n+1 in wave_system.solution
    for (; node != end_node; ++node)
    {
        const libMesh::Node * nn = *node;
        dof_map.dof_indices(nn, dof_indices_Q, 0);
        dof_map_V.dof_indices(nn, dof_indices_V, 0);
        wave_system.solution->set(dof_indices_V[0], (*bidomain_system.solution)(dof_indices_Q[0]));
    }

    // 2) Evaluate V_n + dt * Q_n+1
    wave_system.solution->close();

    if(0 == M_timestep_counter || TimeIntegrator::FirstOrderIMEX == M_timeIntegrator)
    {
        wave_system.solution->scale(dt);
        *wave_system.solution += *wave_system.old_local_solution;
    }
    else
    {
        // Use BDF2
        // V^n+1 = 4/3 V^n - 1/3 V^n + 2/3 dt * Q^n+1
        wave_system.solution->scale(2.0/3.0*dt);
        wave_system.solution->add( 4.0/3.0, *wave_system.old_local_solution);
        wave_system.solution->add(-1.0/3.0, *wave_system.older_local_solution);
    }
    M_timestep_counter++;
}

} /* namespace BeatIt */
