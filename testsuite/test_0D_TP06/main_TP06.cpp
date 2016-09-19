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
 * \file main_ORd.cpp
 *
 * \class main_ORd
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
 * Created on: Aug 7, 2016
 *
 */

#include <memory>
#include "Electrophysiology/IonicModels/TP06.hpp"
#include "Util/IO/io.hpp"
#include <cmath>
#include <iomanip>
int main()
{
    BeatIt::printBanner(std::cout);

	std::unique_ptr<BeatIt::IonicModel> pORd( BeatIt::IonicModel::IonicModelFactory::Create("TP06") );
	int numVar = pORd->numVariables();
	std::vector<double> variables(numVar, 0.0);

	std::ofstream output("results.txt");
	pORd->initializeSaveData(output);
	pORd->initialize(variables);

	double dt = 0.005;
	int save_iter = 0.5 / dt;
	double TF = 600.0;
	double Ist = 0;
	double time = 0.0;
	BeatIt::saveData(0.0, variables, output);

	int iter = 0;

	// for ctest purposes
	double solution_norm = 0.0;
	while( time <= TF )
	{
		if(time >= 0. && time <= 0.5)
		{
			Ist = 80.0;
		}
		else Ist = 0.0;

		pORd->solve(variables, Ist, dt);
		time += dt;
		++iter;
		if( 0 == iter%save_iter ) BeatIt::saveData(time, variables, output);

		// for ctest purposes
		solution_norm += variables[0];

	}
	output.close();
	//for ctest purposes
	solution_norm /= iter;
	//up to the 16th digit
	std::cout << std::setprecision(18) << "Solution norm = " << solution_norm << std::endl;
	const double reference_solution_norm = -18.2035079050864645;
	//We check only up to 12th
	if( std::abs(solution_norm-reference_solution_norm) < 1e-12) return EXIT_SUCCESS;
	else return EXIT_FAILURE;
}
