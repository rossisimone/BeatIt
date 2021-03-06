// The libMesh Finite Element Library.
// Copyright (C) 2002-2017 Benjamin S. Kirk, John W. Peterson, Roy H. Stogner

// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

// <h1>Subdomains Example 2 - Subdomain-Restricted Variables</h1>
// \author Benjamin S. Kirk
// \date 2011
//
// This example builds on the fourth example program by showing how
// to restrict solution fields to a subdomain (or union of
// subdomains).

// C++ include files that we need
#include "Electrophysiology/Bidomain/BidomainWithBath.hpp"
#include "Electrophysiology/Monodomain/MonodomainUtil.hpp"

#include "libmesh/linear_implicit_system.h"
#include "libmesh/transient_system.h"

#include "libmesh/wrapped_functor.h"
#include "libmesh/parallel_mesh.h"
#include "libmesh/elem.h"

#include "libmesh/mesh_generation.h"
#include "libmesh/mesh_modification.h"
#include "Util/SpiritFunction.hpp"
#include "libmesh/dof_map.h"

#include "libmesh/numeric_vector.h"
#include <libmesh/boundary_mesh.h>
#include <libmesh/serial_mesh.h>
#include "libmesh/string_to_enum.h"

#include "libmesh/analytic_function.h"
#include "Util/CTestUtil.hpp"
#include "Util/GenerateFibers.hpp"
#include <iomanip>
//#include "libmesh/vtk_io.h"
#include "libmesh/exodusII_io.h"
#include "Util/Timer.hpp"
#include "Util/IO/io.hpp"
#include <libmesh/point_locator_tree.h>
#include <libmesh/mesh_function.h>
//#include <libmesh/node.h>
#include <stdlib.h>     /* srand, rand */
#include <time.h>       /* time */
#include <libmesh/dof_map.h>
#include "libmesh/sparse_matrix.h"

// Bring in everything from the libMesh namespace
using namespace libMesh;

void find_nodeID( libMesh::MeshBase& mesh,
                  std::vector<libMesh::Point>& points,
                  std::vector<libMesh::Node *>& nodes,
                  std::vector<unsigned int>& ranks,
                  double tol = 1e-10)
{
    libMesh::MeshBase::const_node_iterator node = mesh.nodes_begin();
    const libMesh::MeshBase::const_node_iterator end_node = mesh.nodes_end();
    unsigned int my_rank = mesh.comm().rank();
    for (; node != end_node; ++node)
    {
        libMesh::Node * nn = *node;
        auto dataID = nn->id();
        libMesh::Point p_mesh((*nn)(0), (*nn)(1), (*nn)(2));
        for(auto && p : points)
        {
            libMesh::Point distance = p_mesh - p;
            double dist = distance.norm_sq();
            if(dist < tol)
            {
                nodes.push_back(nn);
                ranks.push_back(my_rank);
            }
        }
    }
}

void record_data(std::ofstream& file,
                 std::vector<libMesh::Node *>& nodes,
                 std::vector<unsigned int>& ranks,
                 libMesh::TransientLinearImplicitSystem& bid_sys,
                 double time = 0.0)
{
    int my_rank = bid_sys.comm().rank();
    file << time ;


    libMesh::DofMap& dof_map = bid_sys.get_dof_map();

    int n_nodes = nodes.size();
    std::vector<libMesh::dof_id_type> dof_indices;
    int ve_var = 1;
    for (int i = 0; i < n_nodes; ++i)
    {
        if(my_rank == ranks[i])
        {
            dof_map.dof_indices(nodes[i], dof_indices, ve_var);
            double ve =  (*bid_sys.current_local_solution)(dof_indices[0]);
            file << ", " << ve;
        }

    }
//    std::cout << "locator address: " << &locator << std::endl;
//    file << time << " ";
//    for(auto && p : points )
//    {
//        p.print();
////        double ve = locator(p);
//        double ve = 1.0;
//        file << ve << " ";
//    }
    file << std::endl;
}



void setup_fibrosis( libMesh::EquationSystems& es, double fibrosis, bool extra = true)
{
    /* initialize random seed: */
    srand (time(NULL));
    typedef libMesh::ExplicitSystem ParameterSystem;
    ParameterSystem& intra_conductivity_system = es.get_system<ParameterSystem>("intra_conductivity");
    ParameterSystem& extra_conductivity_system = es.get_system<ParameterSystem>("extra_conductivity");


    libMesh::MeshBase & mesh = es.get_mesh();

    libMesh::MeshBase::const_element_iterator el_start = mesh.active_local_elements_begin();
    libMesh::MeshBase::const_element_iterator el = mesh.active_local_elements_begin();
    const libMesh::MeshBase::const_element_iterator end_el = mesh.active_local_elements_end();

    const libMesh::DofMap & dof_map = intra_conductivity_system.get_dof_map();
    std::vector<libMesh::dof_id_type> dof_indices;

    const libMesh::DofMap & dof_map_extra = extra_conductivity_system.get_dof_map();
    std::vector<libMesh::dof_id_type> dof_indices_extra;

    double Dffi = 0.;
    double Dssi = 0.;
    double Dnni = 0.;
    double Dffe = 0.01;
    double Dsse = 0.01;
    double Dnne = 0.01;

    for (; el != end_el; ++el)
    {
        /* generate secret number between 1 and 10: */
        int random_el = std::rand() % 100 + 1;

        if(random_el <= fibrosis)
        {

            const libMesh::Elem * elem = *el;
            auto subdomainID = elem->subdomain_id();
            if(subdomainID == 1)
            {
                dof_map.dof_indices(elem, dof_indices);
                dof_map_extra.dof_indices(elem, dof_indices_extra);

                intra_conductivity_system.solution->set(dof_indices[0], Dffi);
                intra_conductivity_system.solution->set(dof_indices[1], Dssi);
                intra_conductivity_system.solution->set(dof_indices[2], Dnni);

                if(extra)
                {
                    extra_conductivity_system.solution->set(dof_indices_extra[0], Dffe);
                    extra_conductivity_system.solution->set(dof_indices_extra[1], Dsse);
                    extra_conductivity_system.solution->set(dof_indices_extra[2], Dnne);
                }
            }
        }

    }
}

void remove_elements( libMesh::MeshBase& mesh, double fibrosis)
{
    /* initialize random seed: */
    srand (time(NULL));

    libMesh::MeshBase::const_element_iterator el_start = mesh.active_local_elements_begin();
    libMesh::MeshBase::const_element_iterator el = mesh.active_local_elements_begin();
    const libMesh::MeshBase::const_element_iterator end_el = mesh.active_local_elements_end();

    for (; el != end_el; ++el)
    {
        /* generate secret number between 1 and 10: */
        int random_el = std::rand() % 100 + 1;
        if(random_el <= fibrosis)
        {
            libMesh::Elem * elem = *el;
            auto subdomainID = elem->subdomain_id();
            if(subdomainID == 1)
            {
                mesh.delete_elem(elem);
            }
        }
    }
}

void remove_elements_line( libMesh::EquationSystems& es, double fibrosis,
		                   int elX, int elY, int elZ,
						   double Lx = 0.1, double Ly = 0.1, double Lz = 0.025)
{
    /* initialize random seed: */
    srand (time(NULL));
    libMesh::MeshBase & mesh = es.get_mesh();

    std::vector<double> x0;
    std::vector<double> x1;
    std::vector<double> y0;
    std::vector<double> y1;
    std::vector<double> z0;
    std::vector<double> z1;
    double dx = 1.0/elX;
    double dy = 1.0/elY;
    double dz = 0.2 / ( elZ / 3 );

//    double Lx = 0.1;
//    double Ly = 0.1;
//    double Lz = 0.025;
    int n_el = 0;
    for(int i = 0; i < elX; ++i)
    {
    	double x = i * 1.0 / elX  + 0.5*dx;

//    	int random = std::rand() % 100 + 1;
//    	if(random < fibrosis)
        {
			n_el ++;
//			int randomy = std::rand() % elY;
//			double y = randomy * dy + 0.5*dy;
//			int random2 = std::rand() % ( elZ / 3 );
//			double z = random2 * dz  + 0.5 * dz;

//			x0.push_back(x-0.0125);
//			x1.push_back(x+0.0125);
//			y0.push_back(y-0.15);
//			y1.push_back(y+0.15);
//
//			z0.push_back(z-0.35);
//			z1.push_back(z+0.35);

		    for(int j = 0; j < elY; ++j)
		    {
		    	int random = std::rand() % 100 + 1;

				if(random < fibrosis)
		    	{
					n_el ++;
			    	double y = j * 1.0 / elY  + 0.5*dy;

					int randomz = std::rand() % ( elZ / 3 );
					double z = randomz * dz  + 0.5 * dz;

					x0.push_back(x-Lx/2);
					x1.push_back(x+Lx/2);
					y0.push_back(y-Ly/2);
					y1.push_back(y+Ly/2);

					z0.push_back(z-Lz/2);
					z1.push_back(z+Lz/2);

		    	}

		    }
        }
    }

    int size = x0.size();
    std::cout << "N el: " << n_el << ", size: " << size << std::endl;
    for(int n = 0; n < size; n++)
    {
		std::cout  << "Box: " << x0[n] << ", " << x1[n] << ";\t"
							  << y0[n] << ", " << y1[n] << ";\t"
							  << z0[n] << ", " << z1[n] << std::endl;

    }
    libMesh::MeshBase::const_element_iterator el_start = mesh.active_local_elements_begin();
    libMesh::MeshBase::const_element_iterator el = mesh.active_local_elements_begin();
    const libMesh::MeshBase::const_element_iterator end_el = mesh.active_local_elements_end();

    typedef libMesh::ExplicitSystem ParameterSystem;
    ParameterSystem& intra_conductivity_system = es.get_system<ParameterSystem>("intra_conductivity");
    ParameterSystem& extra_conductivity_system = es.get_system<ParameterSystem>("extra_conductivity");

    const libMesh::DofMap & dof_map = intra_conductivity_system.get_dof_map();
    std::vector<libMesh::dof_id_type> dof_indices;

    const libMesh::DofMap & dof_map_extra = extra_conductivity_system.get_dof_map();
    std::vector<libMesh::dof_id_type> dof_indices_extra;

    double Dffi = 0.;
    double Dssi = 0.;
    double Dnni = 0.;
    double Dffe = 0.01;
    double Dsse = 0.01;
    double Dnne = 0.01;

    for (; el != end_el; ++el)
    {
		const libMesh::Elem * elem = *el;
		auto subdomainID = elem->subdomain_id();
		if(subdomainID == 1)
		{
			auto p = elem->centroid();
			for(int n = 0; n < size; ++n)
			{
				if( p(0) > x0[n] &&
					p(0) < x1[n] &&
					p(1) > y0[n] &&
					p(1) < y1[n] &&
					p(2) > z0[n] &&
					p(2) < z1[n] )
				{
					dof_map.dof_indices(elem, dof_indices);
					dof_map_extra.dof_indices(elem, dof_indices_extra);

					intra_conductivity_system.solution->set(dof_indices[0], Dffi);
					intra_conductivity_system.solution->set(dof_indices[1], Dssi);
					intra_conductivity_system.solution->set(dof_indices[2], Dnni);

					//if(extra)
					{
						extra_conductivity_system.solution->set(dof_indices_extra[0], Dffe);
						extra_conductivity_system.solution->set(dof_indices_extra[1], Dsse);
						extra_conductivity_system.solution->set(dof_indices_extra[2], Dnne);
					}

				}
			}
		}
    }

//    int nel = mesh.n_elem();
//
//    for (int c = 0; c < nel; ++c)
//    {
//        /* generate secret number between 1 and 10: */
//    	// int random_el = std::rand() % 100 + 1;
//    	// if(random_el <= fibrosis)
//        {
//        	libMesh::Elem * elem = mesh.elem_ptr(c);
//        	if(elem)
//        	{
//				auto subdomainID = elem->subdomain_id();
//				if(subdomainID == 1)
//				{
//					auto p = elem->centroid();
//					for(int n = 0; n < size; ++n)
//					{
//						if( p(0) > x0[n] &&
//							p(0) < x1[n] &&
//							p(1) < y0[n] &&
//							p(1) < y1[n] &&
//							p(2) < z0[n] &&
//							p(2) < z1[n] )
//						{
//							//mesh.delete_elem(elem);
//						}
//					}
//				}
//        	}
//        }
//    }
}


void record_data_old(std::ofstream& file,
                 std::vector<libMesh::Point>& points,
                 libMesh::MeshFunction& locator,
                 double time)
{
    std::cout << "locator address: " << &locator << std::endl;
    file << time << " ";
    for(auto && p : points )
    {
        double ve = locator(p);
//        double ve = 1.0;
        file << ve << " ";
    }
    file << std::endl;
}


void set_nodal_fibrosis_IDs( libMesh::EquationSystems& es,
									 std::set<libMesh::dof_id_type>& fibrosisID,
									 double fibrosis, bool extra = true)
{
	std::cout << "Set Nodal Fibrosis IDs" << std::endl;
    /* initialize random seed: */
    srand (time(NULL));
    libMesh::MeshBase & mesh = es.get_mesh();

	libMesh::MeshBase::const_node_iterator node = mesh.local_nodes_begin();
	const libMesh::MeshBase::const_node_iterator end_node = mesh.local_nodes_end();

    libMesh::TransientLinearImplicitSystem& system = es.get_system<libMesh::TransientLinearImplicitSystem>("bidomainbath");
    const libMesh::DofMap & dof_map = system.get_dof_map();
	std::vector < libMesh::dof_id_type> dofs;

    int c = 0;
    std::set<libMesh::dof_id_type> set;

    int size = set.size();

    libMesh::MeshBase::const_element_iterator el_start = mesh.active_local_elements_begin();
    libMesh::MeshBase::const_element_iterator el = mesh.active_local_elements_begin();
    const libMesh::MeshBase::const_element_iterator end_el = mesh.active_local_elements_end();

    for (; el != end_el; ++el)
    {
		libMesh::Elem * elem = *el;
        auto subdomainID = elem->subdomain_id();
        if(subdomainID == 1)
        {
			int n_nodes = elem->n_nodes();
			for(int n = 0; n < n_nodes; ++n)
			{
				auto* node = elem->node_ptr(n);
                dof_map.dof_indices(node, dofs, 0);

				libMesh::dof_id_type nID = node->id();
				set.insert(nID);
				if(set.size() > size)
				{
					size = set.size();
			        int random = std::rand() % 100 + 1;
			        if(random <= fibrosis)
			        {
			        	fibrosisID.insert(nID);
			        }
				}
			}
        }
    }
}

void set_nodal_fibrosis_by_penalty( libMesh::EquationSystems& es,
									 std::set<libMesh::dof_id_type> fibrosisID,
									 bool set_matrix = false)
{
	std::cout << "Penalize fibrosis" << std::endl;
    libMesh::TransientLinearImplicitSystem& system = es.get_system<libMesh::TransientLinearImplicitSystem>("bidomainbath");
    system.rhs->close();
	for(auto && id : fibrosisID)
    {
        system.rhs->add(id, -57.0e2 );
        if(set_matrix)
        {
        	system.matrix->add(id,id, 1e2);
        }
    }
    system.rhs->close();
}


// Begin the main program.
int main(int argc, char ** argv)
{
    // Initialize libMesh and any dependent libaries, like in example 2.
    LibMeshInit init(argc, argv);

    // Use the MeshTools::Generation mesh generator to create a uniform
    // 3D grid
    // We build a linear tetrahedral mesh (TET4) on  [0,2]x[0,0.7]x[0,0.3]
    // the number of elements on each side is read from the input file
    GetPot commandLine(argc, argv);
    std::string datafile_name = commandLine.follow("data.beat", 2, "-i", "--input");
    GetPot data(datafile_name);

    bool export_data = data("export", false);
    BeatIt::TimeData datatime;
    datatime.setup(data);
    datatime.print();

    // Create a mesh with user-defined dimension on the default MPI
    // communicator.
    ParallelMesh mesh(init.comm());

    std::string meshname = data("mesh/input_mesh_name", "NONE");
    if (meshname == "NONE")
    {
        // allow us to use higher-order approximation.
        int elX = data("mesh/elX", 15);
        int elY = data("mesh/elY", 5);
        int elZ = data("mesh/elZ", 4);
        double minX = data("mesh/minX", 0.0);
        double minY = data("mesh/minY", 0.0);
        double minZ = data("mesh/minZ", 0.0);
        double maxX = data("mesh/maxX", 2.0);
        double maxY = data("mesh/maxY", 0.7);
        double maxZ = data("mesh/maxZ", 0.3);

        // No reason to use high-order geometric elements if we are
        // solving with low-order finite elements.
        if (elZ > 0)
        {
            MeshTools::Generation::build_cube(mesh, elX, elY, elZ, minX, maxX, minY, maxY, minZ, maxZ, TET4);
            //MeshTools::Generation::build_cube(mesh, elX, elY, elZ, minX, maxX, minY, maxY, minZ, maxZ, TET4);
        }
        else
        {
            std::string elType = data("mesh/elType", "TRI3");
            ElemType elt = libMesh::Utility::string_to_enum<ElemType>(elType);
            MeshTools::Generation::build_cube(mesh, elX, elY, elZ, minX, maxX, minY, maxY, minZ, maxZ, elt);
        }

        std::cout << "Creating subdomains!" << std::endl;

        {
            double z_interface = data("mesh/z_interface", 10000.0);
            double r_interface = data("mesh/r_interface", 0.0);
            double y_interface = data("mesh/y_interface", 10000.0);
            std::cout << "z_interface: " << z_interface << std::endl;

            bool epicardial_bath = data("epicardial_bath", false);
            double epi_z_interface = data("mesh/epi_z_interface", -10000.0);
            double epi_y_interface = data("mesh/epi_y_interface", -10000.0);

            MeshBase::element_iterator el = mesh.elements_begin();
            const MeshBase::element_iterator end_el = mesh.elements_end();

            for (; el != end_el; ++el)
            {
                Elem * elem = *el;
                const Point cent = elem->centroid();
                // BATH
                if ( ( cent(2) > z_interface || cent(2) < epi_z_interface )
                  || ( cent(1) > y_interface || cent(1) < epi_y_interface ) )
                {
                    elem->subdomain_id() = 2;
                    double h = elem->hmax();
                }
                // TISSUE
                else
                {
                    elem->subdomain_id() = 1;
                }

            }
            mesh.get_boundary_info().regenerate_id_sets();
            std::cout << "Creating subdomains done!" << std::endl;

            bool circle = data("mesh/circle", false);
            if (circle)
            {
                bool in_tissue = data("mesh/in_tissue", false);
                bool z_bend = data("mesh/z_bend", false);
                double pi_fraction = data("mesh/pi_fraction", 1.0);
                double angle = M_PI / pi_fraction;
                //Map unit square onto cook's membrane
                MeshBase::const_node_iterator nd = mesh.nodes_begin();
                const MeshBase::const_node_iterator end_nd = mesh.nodes_end();
                for (; nd != end_nd; ++nd)
                {
                    double x = (**nd)(0);
                    double y = (**nd)(1);
                    double z = (**nd)(2);
                    if (x > 0)
                    {
                        if (z_bend)
                        {
                            int outside_tissue = (in_tissue == true) ? 1 : -1;
                            double R0 = maxX / angle; // + outside_tissue *  ( y - y_interface);
                            double R = R0 + outside_tissue * z; // + outside_tissue *  ( y - y_interface);
                            double theta = -0.5 * M_PI + angle * x / maxX;
                            (**nd)(0) = R * std::cos(theta);
                            (**nd)(2) = -outside_tissue * (R * std::sin(theta) + R0);
                        }
                        else
                        {
                            int outside_tissue = (in_tissue == true) ? 1 : -1;
                            double R0 = maxX / angle; // + outside_tissue *  ( y - y_interface);
                            double R = R0 + outside_tissue * y; // + outside_tissue *  ( y - y_interface);
                            double theta = -0.5 * M_PI + angle * x / maxX;
                            (**nd)(0) = R * std::cos(theta);
                            (**nd)(1) = -outside_tissue * (R * std::sin(theta) + R0);
                        }
                    }
                }
            }
        }
    }
    else
    {
        // We may need XDR support compiled in to read binary .xdr files
        int n_refinements = data("mesh/n_ref", 0);
        std::cout << "n_refs: " << n_refinements << std::endl;
        BeatIt::serial_mesh_partition(init.comm(), meshname, &mesh, n_refinements);

        double xscale = data("mesh/x_scale", 1);
        double yscale = data("mesh/y_scale", 1);
        double zscale = data("mesh/z_scale", 1);
        if (xscale != 1 || yscale != 1 || zscale != 1)
            libMesh::MeshTools::Modification::scale(mesh, xscale, yscale, zscale);
    }

    unsigned int fibrosis_type = data("type", 0);
    bool modify_conductivities = data("mod_sigma", false);
    bool rm_elements = data("rm_elems", false);
    double fibrosis = data("fibrosis", -1.0);
    double fibrosis2 = data("fibrosis2", -1.0);
    std::cout << "fibrosis percentage: " << fibrosis << std::endl;
    if(1 == fibrosis_type  || rm_elements)
    {
        remove_elements( mesh, fibrosis2);
        mesh.prepare_for_use();
    }
    int elX = data("mesh/elX", 15);
    int elY = data("mesh/elY", 5);
    int elZ = data("mesh/elZ", 4);


    libMesh::ExodusII_IO(mesh).write("mesh.e");
    MeshRefinement refinement(mesh);
    refinement.refine_elements();
    std::cout << "Mesh done!" << std::endl;
    mesh.print_info();

//    auto* node = mesh.query_node_ptr(1);
//    std::cout << mesh.comm().rank() << ": node 1 proc id: " << node->processor_id() << std::endl;
    //mesh.get_boundary_info().print_info(std::cout);
    // Create an equation systems object.
    std::cout << "Create equation system ..." << std::endl;
    EquationSystems es(mesh);

    // Constructor
    std::cout << "Create bidomain with bath..." << std::endl;
    std::string model = data("model", "monowave");
    BeatIt::ElectroSolver* solver = BeatIt::ElectroSolver::ElectroFactory::Create(model, es);
    std::string section = data("section", "monowave");
    std::cout << "Calling setup..." << std::endl;
    solver->setup(data, model);
    std::cout << "Calling init ..." << std::endl;
    solver->init(0.0);

    int save_iter = 0;
    int save_iter_ve = 1;
    //bidomain.save_ve_timestep(save_iter_ve, datatime.M_time);
    std::cout << "Init Output" << std::endl;
    //solver->init_exo_output();
    //solver->save_exo_timestep(save_iter++, datatime.M_time);

    //return 0;
    std::string system_mass = data(model + "/diffusion_mass", "mass");
    std::string iion_mass = data(model + "/reaction_mass", "lumped_mass");
    //bidomain.restart(importer, 1);

    if(0 == fibrosis_type || modify_conductivities)
    {
        bool madify_sigma_e = data("extra", true);
        setup_fibrosis( es, fibrosis, madify_sigma_e);
    }
    if(3 == fibrosis_type)
    {
    	double Lx = data("Lx", 0.1);
    	double Ly = data("Ly", 0.1);
    	double Lz = data("Lz", 0.025);

    	remove_elements_line( es, fibrosis, elX, elY, elZ, Lx, Ly, Lz);
        mesh.prepare_for_use();

    }


    if (export_data)
    {
        solver->save_parameters();
    }

    std::cout << "Assembling matrices" << std::endl;
    solver->assemble_matrices(datatime.M_dt);
    // Record signals at point
    std::cout << "Setting up locator" << std::endl;
    std::string x = data("x", "0.5, 0.5");
    std::string y = data("y", "0.5, 0.5");
    std::string z = data("z", "0.2, 0.22");
    std::vector<double> xs;
    std::vector<double> ys;
    std::vector<double> zs;
    std::cout << "Read lists" << std::endl;
    BeatIt::readList(x,xs);
    for(auto && xi : xs) std::cout << xi << " ";
    std::cout << std::endl;
    BeatIt::readList(y,ys);
    for(auto && xi : ys) std::cout << xi << " ";
    std::cout << std::endl;
    BeatIt::readList(z,zs);
    for(auto && xi : zs) std::cout << xi << " ";
    std::cout << std::endl;
    std::vector<libMesh::Point> points;

    std::set<unsigned short> subdomains;
    subdomains.insert(2);

    std::cout << "Get bidomain" << std::endl;
    typedef libMesh::TransientLinearImplicitSystem BidomainSystem;
    auto& bidomain_system = es.get_system<BidomainSystem>(solver->model());

    std::cout << "Vec" << std::endl;
    auto& vec = *bidomain_system.current_local_solution;
    auto& map = bidomain_system.get_dof_map();
    std::cout << "Locator" << std::endl;
    libMesh::MeshFunction locator(   es, vec, map, 0);
    double tolerance = data("tol", 1e-8);


    //locator.set_point_locator_tolerance(tolerance);

    std::cout << "Loop" << std::endl;
    for ( int i = 0; i < xs.size(); ++i)
    {
        points.push_back( libMesh::Point(xs[i], ys[i], zs[i] ) );
    }

    std::vector<libMesh::Node *> nodes;
    std::vector<unsigned int> ranks;
    find_nodeID(mesh, points, nodes, ranks);
    std::cout<< "Number of points: " << points.size() << std::endl;
    std::cout << "Complete" << std::endl;


    bool useMidpointMethod = false;
    int step0 = 0;
    int step1 = 1;

    std::string output = data("output", "ve.csv");
    std::ofstream file(output);


    std::set<libMesh::dof_id_type> fibrosisID;
    if(2 == fibrosis_type)
    {
    	std::cout << "Setting up nodal fibrosis" << std::endl;
    	set_nodal_fibrosis_IDs( es, fibrosisID, fibrosis);
    	set_nodal_fibrosis_by_penalty( es, fibrosisID, true);
    }
    std::cout << "Fibrosis ID size: " << fibrosisID.size() << std::endl;


    if (export_data)
    {
        //solver->save_potential_nemesis(save_iter, 0.0);
        solver->save_potential(save_iter, 0.0);
        //record_data_old(file, points, locator, 0.0);
        record_data(file, nodes, ranks, bidomain_system);
    }

    std::cout << "Time loop starts:" << std::endl;
    //return 0;
    //export initial condition

    for (; datatime.M_iter < datatime.M_maxIter && datatime.M_time < datatime.M_endTime;)
    {
        datatime.advance();
        std::cout << "Time:" << datatime.M_time << ", Iter: " << datatime.M_iter << std::endl;
        solver->advance();
        //std::cout << "Reaction:" << datatime.M_time << std::endl;
        solver->solve_reaction_step(datatime.M_dt, datatime.M_time, step0, useMidpointMethod, iion_mass);
        //solver->solve_reaction_step(datatime.M_dt, datatime.M_time, step0, useMidpointMethod, "lumpedmass");
        if(2 == fibrosis_type)
        	set_nodal_fibrosis_by_penalty( es, fibrosisID, false);

        //std::cout << "Diffusion:" << datatime.M_time << std::endl;
        solver->solve_diffusion_step(datatime.M_dt, datatime.M_time, useMidpointMethod, iion_mass);
        //solver->solve_diffusion_step(datatime.M_dt, datatime.M_time, useMidpointMethod, "mass");

        //std::cout << "at:" << datatime.M_time << std::endl;
        solver->update_activation_time(datatime.M_time, -5.0);
        //std::cout << "at done:" << datatime.M_time << std::endl;

        if (0 == datatime.M_iter % datatime.M_saveIter && export_data)
        {
            save_iter++;
            solver->save_potential(save_iter, datatime.M_time);
//            solver->save_potential_nemesis(save_iter, datatime.M_time);
//            record_data_old(file, points, locator, datatime.M_time);

        }
        record_data(file, nodes, ranks, bidomain_system,  datatime.M_time);

    }
    if (export_data)
    {
        solver->evaluate_conduction_velocity();
        solver->save_conduction_velocity(save_iter);
    }
    solver->save_activation_times(save_iter);

    delete solver;
    return 0;
}

