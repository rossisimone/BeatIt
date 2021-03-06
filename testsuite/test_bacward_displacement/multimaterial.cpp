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

/*
 * main.cpp
 *
 *  Created on: Sep 14, 2016
 *      Author: srossi
 */
// Basic include files needed for the mesh functionality.
#include "Elasticity/MixedElasticity.hpp"
#include "libmesh/mesh_base.h"
#include "libmesh/linear_implicit_system.h"
#include "libmesh/transient_system.h"
#include "libmesh/numeric_vector.h"

#include "libmesh/wrapped_functor.h"
#include "libmesh/mesh.h"
#include "libmesh/mesh_generation.h"
#include "Util/SpiritFunction.hpp"

#include "libmesh/numeric_vector.h"
#include "libmesh/elem.h"
#include "Util/CTestUtil.hpp"
#include "libmesh/dof_map.h"
#include <iomanip>
#include "Util/CTestUtil.hpp"
#include <libmesh/vtk_io.h>
#include <libmesh/exodusII_io.h>


enum Formulation { Primal,  Mixed, Incompressible };
enum CL { Linear,  NH };

void moveMesh(BeatIt::Elasticity* elas);
double checkConfiguration(BeatIt::Elasticity* elas);

int main (int argc, char ** argv)
{
    // Bring in everything from the libMesh namespace

    using namespace libMesh;
    // Initialize libraries, like in example 2.
    LibMeshInit init (argc, argv, MPI_COMM_WORLD);


    // Use the MeshTools::Generation mesh generator to create a uniform
    // 3D grid
    // We build a linear tetrahedral mesh (TET4) on  [0,2]x[0,0.7]x[0,0.3]
    // the number of elements on each side is read from the input file
    GetPot commandLine ( argc, argv );
    std::string datafile_name = commandLine.follow ( "data.beat", 2, "-i", "--input" );
    GetPot data(datafile_name);
    // allow us to use higher-order approximation.
    // Create a mesh, with dimension to be overridden later, on the
    // default MPI communicator.
    libMesh::Mesh mesh(init.comm());

    std::string mesh_filename = data("mesh", "NONE");
    mesh.read(mesh_filename);
    mesh.all_second_order();
    libMesh::EquationSystems es(mesh);

    Formulation f;
    std::string formulation = data("elasticity/formulation", "NO_FORMULATION");
    std::cout << "Creating " << formulation << std::endl;

    std::cout << "Forming pointer" << std::endl;
    BeatIt::Elasticity* elas = new BeatIt::Elasticity(es, "Elasticity");
    std::cout << "Calling Elasticity Setup" << std::endl;
    elas->setup(data,"elasticity");
    std::cout << "Forming pointer" << std::endl;
    std::cout << "Initializing output ... " << std::endl;
    elas->init_exo_output("solution.exo");
    int save_iter = 1;
    elas->save_exo("solution.exo", save_iter, 0.0);
    save_iter++;
    double ramp_dt = data("ramp/dt", 0.1);
    double ramp_end_time = data("ramp/end_time", 0.9);
    double time = 0.0;

    double error = 0.0;
	int bd_iter = 0;
    while(time < ramp_end_time)
    {
        std::cout << "Solving ... " << std::endl;
        time+=ramp_dt;
        elas->setTime(time);
        std::cout << "\n ====== Time: " << time << std::endl;
		elas->newton();
		bd_iter = 0;
		std::cout << "Backward Displacement iteration: " << bd_iter << std::endl;
		error = checkConfiguration(elas);
        //        elas->save("solution.gmv", save_iter);
//        {
//            std::ostringstream ss;
//            ss << std::setw(8) << std::setfill('0') << save_iter;
//            std::string step_str = ss.str();
//            libMesh::VTKIO(mesh).write_equation_systems("solution_" + step_str + ".pvtu", es);
//        }
        elas->save_exo("solution.exo", save_iter, time);
        save_iter++;

//        double tol = 5e-2;
//        while(error > tol)
//        {
//        	bd_iter++;
//    		moveMesh(elas);
//			elas->newton();
//			std::cout << "Backward Displacement iteration: " << bd_iter << std::endl;
//			error = checkConfiguration(elas);
////	        elas->save_exo("solution.exo", save_iter, time);
////	        elas->save("solution.gmv", save_iter);
////	        {
////	            std::ostringstream ss;
////	            ss << std::setw(8) << std::setfill('0') << save_iter;
////	            std::string step_str = ss.str();
////	            libMesh::VTKIO(mesh).write_equation_systems("solution_" + step_str + ".pvtu", es);
////	        }
//	        save_iter++;
//        }

    }

//    typedef libMesh::TransientLinearImplicitSystem LinearSystem;
//    auto& system = elas->M_equationSystems.get_system < LinearSystem
//            > ("Elasticity");
//    system.solution->print(std::cout);

    delete elas;
    std::cout << std::setprecision(20) << "error: " << error << std::endl;
    std::cout << "backward displacement iterations: " << bd_iter << std::endl;
	const double reference_value = 9.0839119559760206357e-09;
	if(bd_iter != 7) return EXIT_FAILURE;
	else return BeatIt::CTest::check_test(error, reference_value, 1e-6);

    return 0;
}

void moveMesh(BeatIt::Elasticity* elas)
{
	std::cout << "Moving the mesh" << std::endl;
	const libMesh::MeshBase & mesh = elas->M_equationSystems.get_mesh();
	const unsigned int dim = mesh.mesh_dimension();

	auto it =  elas->M_equationSystems.get_mesh().local_nodes_begin();
	auto it_end =  elas->M_equationSystems.get_mesh().local_nodes_end();

	typedef libMesh::TransientLinearImplicitSystem LinearSystem;
	auto& system = elas->M_equationSystems.get_system < LinearSystem
			> ("Elasticity");
	auto sys_num = system.number();
	const libMesh::DofMap & dof_map = system.get_dof_map();

	for (; it != it_end; it++)
	{
		auto * node = *it;
		auto n_vars = node->n_vars(sys_num);
		for (auto v = 0; v != n_vars; ++v)
		{
			double value = 0.0;
			if (v < dim) // 3 may be the pressure field
			{
				value = (*node)(v);

				// the number of components will always be 1 in this test
				// because the components of the displacement are defined as
				// separate scalar variables
				auto nc = node->n_comp(sys_num, v);
				auto dof_id = node->dof_number(sys_num, v, 0);
				auto disp = (*system.solution)(dof_id);
				auto x = system.get_vector("X")(dof_id);
				double X = x - disp;
				(*node)(v) = X;

//				std::cout << "position after: " << value << std::endl;
//				for (auto c = 0; c != nc; ++c)
//				{
//					//system.get_vector("X").set(dof_id, value);
//				}
			}
		}
	}
}


double checkConfiguration(BeatIt::Elasticity* elas)
{
	std::cout << "====================================" << std::endl;
	std::cout << "Checking error in the configuration" << std::endl;
	const libMesh::MeshBase & mesh = elas->M_equationSystems.get_mesh();
	const unsigned int dim = mesh.mesh_dimension();

	auto it =  elas->M_equationSystems.get_mesh().local_nodes_begin();
	auto it_end =  elas->M_equationSystems.get_mesh().local_nodes_end();

	typedef libMesh::TransientLinearImplicitSystem LinearSystem;
	auto& system = elas->M_equationSystems.get_system < LinearSystem
			> ("Elasticity");
	auto sys_num = system.number();
	const libMesh::DofMap & dof_map = system.get_dof_map();

	double error_l_inf = -1;

	for (; it != it_end; it++)
	{
		auto * node = *it;
		auto n_vars = node->n_vars(sys_num);
		for (auto v = 0; v != n_vars; ++v)
		{
			double value = 0.0;
			if (v < dim) // 3 may be the pressure field
			{
				// the number of components will always be 1 in this test
				// because the components of the displacement are defined as
				// separate scalar variables
				auto nc = node->n_comp(sys_num, v);
				auto dof_id = node->dof_number(sys_num, v, 0);
				auto disp = (*system.solution)(dof_id);
				auto x = system.get_vector("X")(dof_id);
				auto X = (*node)(v);
				error_l_inf = std::max(error_l_inf, std::abs(x - X - disp ) );
//				std::cout << "position after: " << value << std::endl;
//				for (auto c = 0; c != nc; ++c)
//				{
//					//system.get_vector("X").set(dof_id, value);
//				}
			}
		}
	}
	std::cout << "errol_l_inf: " << error_l_inf << std::endl;
	std::cout << "====================================" << std::endl;

	return error_l_inf;
}
