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
 * \file PacingProtocolSpirit.cpp
 *
 * \class PacingProtocolSpirit
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
 * Created on: Aug 21, 2016
 *
 */

#include "Electrophysiology/Pacing/PacingProtocolSpirit.hpp"
#include "Util/SpiritFunction.hpp"
#include "libmesh/getpot.h"
#include "libmesh/point.h"

namespace BeatIt
{

PacingProtocol* createPacingProtocolSpirit()
{
	return new PacingProtocolSpirit;
}


PacingProtocolSpirit::PacingProtocolSpirit()
{
    M_pacing = new SpiritFunction() ;
}

PacingProtocolSpirit::~PacingProtocolSpirit()
{
	delete M_pacing;
}


void
PacingProtocolSpirit::setup(const GetPot& data, std::string section )
{
    std::cout << "* PacingProtocolSpirit: reading function from input file" << std::endl;
    std::string str = data( section+"/function", "1.0");
    std::cout << "* PacingProtocolSpirit: " << section+"/function = " << str << std::endl;
    dynamic_cast< SpiritFunction* >(M_pacing)->read(str);
}

double
PacingProtocolSpirit::eval(const Point & p,
                           const double time)
{
    return dynamic_cast< SpiritFunction*>(M_pacing)->operator()(time, p(0), p(1), p(2), 0);
}

void
PacingProtocolSpirit::showMe()
{
    dynamic_cast< SpiritFunction* >(M_pacing)->showMe();
}


} /* namespace BeatIt */
