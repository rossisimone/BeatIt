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
 * \file Monodomain.hpp
 *
 * \class Monodomain
 *
 * \brief This class provides the implementation of the monodomain
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

#ifndef SRC_ELECTROPHYSIOLOGY_MONODOMAIN_MONODOMAIN_HPP_
#define SRC_ELECTROPHYSIOLOGY_MONODOMAIN_MONODOMAIN_HPP_

#include "libmesh/parallel.h"

#include "Util/TimeData.hpp"
// Include file that defines (possibly multiple) systems of equations.
#include "libmesh/equation_systems.h"
#include "libmesh/linear_solver.h"

#include <memory>

// Forward Definition
namespace libMesh
{
class Mesh;
class ExodusII_IO;
class VTKIO;
class GMVIO;
class MeshRefinement;
class TimeData;
class ExplicitSystem;
class LinearImplicitSystem;
class PacingProtocol;
class ErrorVector;
class  MeshRefinement;
}



namespace BeatIt
{

/*!
 *  forward declarations
 */
class IonicModel;
class PacingProtocol;

enum class Anisotropy {Isotropic, TransverselyIsotropic, Orthotropic, UserDefined };
enum class EquationType { ReactionDiffusion, Wave };

/// Class
class Monodomain
{
public:
    typedef libMesh::GMVIO Exporter;
    // Another alternative when not using AMR
    typedef libMesh::ExodusII_IO EXOExporter;

    /// Empty construcor
//    Monodomain( libMesh::MeshBase & mesh );
    Monodomain( libMesh::EquationSystems& es );
    ~Monodomain();
    void setup(GetPot& data, std::string section = "monodomain" );

    void init(double time);
    void save(int step);
    void save_exo(int step, double time);
    void init_exo_output();
    void save_potential(int step);
    void save_parameters();

    void amr( libMesh:: MeshRefinement& mesh_refinement, const std::string& type = "kelly" );
    void reinit_linear_solver();
    void update_pacing(double time);
    void update_activation_time(double time, double threshold = 0.8);


    void assemble_matrices();
    void form_system_matrix(double dt, bool useMidpoint = true, const std::string& mass = "lumped_mass");
    void advance();
    void solve_reaction_step( double dt, double time, int step = 0,  bool useMidpoint = true, const std::string& mass = "mass");

    void solve_diffusion_step(double dt, double time,  bool useMidpoint = true, const std::string& mass = "lumped_mass", bool reassemble = true);

    void generate_fibers(   const GetPot& data,
                            const std::string& section = "rule_based_fibers" );
   double last_activation_time();
   double potential_norm();

   void set_potential_on_boundary(unsigned int boundID, double value = 1.0);

   std::string  get_ionic_model_name() const;


   const std::unique_ptr<libMesh::NumericVector<libMesh::Number> >&
   get_fibers();
   const std::unique_ptr<libMesh::NumericVector<libMesh::Number> >&
   get_sheets();
   const std::unique_ptr<libMesh::NumericVector<libMesh::Number> >&
   get_xfibers();
    //protected:

    /// input file
    GetPot                     M_datafile;
    /// Store pointer to the ionic model
    std::unique_ptr<IonicModel> M_ionicModelPtr;
    /// Equation Systems: One for the potential and one for the other variables
    /*!
     *  Use separate systems to avoid saving in all the variables
     */
    libMesh::EquationSystems&  M_equationSystems;

    std::unique_ptr<Exporter> M_monodomainExporter;
    std::set<std::string> M_monodomainExporterNames;
    std::unique_ptr<Exporter> M_ionicModelExporter;
    std::set<std::string> M_ionicModelExporterNames;
    std::unique_ptr<EXOExporter> M_parametersExporter;
    std::set<std::string> M_parametersExporterNames;
    std::unique_ptr<EXOExporter> M_monodomainEXOExporter;

    std::string  M_outputFolder;
    bool M_assembleMatrix;
    bool M_useAMR;

    std::unique_ptr<PacingProtocol> M_pacing;
    std::unique_ptr<libMesh::LinearSolver<libMesh::Number> > M_linearSolver;

    Anisotropy M_anisotropy;
    EquationType M_equationType;
};

} /* namespace BeatIt */

#endif /* SRC_ELECTROPHYSIOLOGY_MONODOMAIN_MONODOMAIN_HPP_ */
