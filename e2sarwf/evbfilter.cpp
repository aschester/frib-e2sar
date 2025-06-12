/*
    This software is Copyright by the Board of Trustees of Michigan
    State University (c) Copyright 2017.

    You may use this software under the terms of the GNU public license
    (GPL).  The terms of this license are described at:

     http://www.gnu.org/licenses/gpl.txt

     Authors:
             Aaron Chester
             FRIB
             Michigan State University
             East Lansing, MI 48824-1321
*/

/**
 * @file evbfilter.cpp
 * @brief Filter to strip extra header from data processed using 
 * `glom --nobuild`
 */

// NSCLDAQ includes:

#include <CBufferedOutput.h>
#include <fragio.h>

// ufmt includes:

#include <fragment.h>

const unsigned BUFFER_SIZE=1024*1024;

/**
 * @brief Apply the EVB header filter and publish output on stdout.
 */
int
filterData()
{
    
    return EXIT_SUCCESS;
}

/**
 * @brief Application entry point for filter
 * @param argc Number of command-line args
 * @param argv Argument vector
 * @return EXIT_SUCCESS on success, otherwise EXIT_FAILURE
 */
int
main(int argc, char* argv[])
{
    io::CBufferedOutput outputter(STDOUT_FILENO, BUFFER_SIZE);
    outputter.setTimeout(2); // Flush every two seconds if rate is low

    std::cout << "Hello World! From evbfilter.cpp:main()" << std::endl;

    int ct = 0;
    while(ct < 10) {
	ufmt::EVB::pFragment p = CFragIO::readFragment(STDIN_FILENO);
       	std::cout << "event " << ct << " fragment " << p << std::endl;
	ct++;
    }	
    
    return EXIT_SUCCESS;
}
