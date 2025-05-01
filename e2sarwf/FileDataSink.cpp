/*
    This software is Copyright by the Board of Trustees of Michigan
    State University (c) Copyright 2015.

    You may use this software under the terms of the GNU public license
    (GPL).  The terms of this license are described at:

     http://www.gnu.org/licenses/gpl.txt

     Author:
             Ron Fox
             Aaron Chester
             FRIB
             Michigan State University
             East Lansing, MI 48824-1321
*/

/**
 * @file FileDataSink.cpp
 * @brief Implementation of file sink
 */

#include "FileDataSink.h"

#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <CErrnoException.h>
#include <CRingItem.h>
#include <io.h>

using namespace ufmt;

/**
 * @details
 * Ownership of this file descriptor is transferred to the this object. 
 * Write operations on the file descriptor must be permissible or an 
 * exception is thrown.
 */
FileDataSink::FileDataSink(int fd) :
    m_fd(fd)
{
    if (!isWritable()) {
	throw std::string("FileDataSink::FileDataSink(int) file descriptor "
			  "is not writeable");
    }
}


/** 
 * @details
 * Obtains a file descriptor given a valid pathname. If file doesn't exist
 * a new file is opened with RDWR permissions. If the file exists, its contents
 * are overwritten.
 * @todo (????): Open failures might be best signalled with CErrnoException or,
 * if not, the string should at least have strerror in it for the errno so the 
 * user can know why the file could not be opened.
 */
FileDataSink::FileDataSink(std::string fname) :
    m_fd(-1)
{
    // Open or create if the file doesn't exist:
    
    m_fd = open(fname.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);

    // Check to see if failed:
    
    if (m_fd == -1) {
	std::string errmsg("FileDataSink::FileDataSink(std::string)");
	errmsg += " failed to open file ";
	errmsg += fname;
	throw CErrnoException(errmsg);
    }

    if (!isWritable()) {
	throw std::string("FileDataSink::FileDataSink(std::string) file "
			  "descriptor is not write only");
    }
}

/**
 * @details
 * If the file descriptor does not refer to STDOUT_FILENO and it points to 
 * a valid file, then close it.
 */
FileDataSink::~FileDataSink()
{
    // Can't close stdout:
    
    if (m_fd != STDOUT_FILENO && m_fd > 0) {
	close(m_fd);
    }
}

/**
 * @details
 * Writes the data to the file. This delegates the writing to a static function
 * `fmtio::writeData(int, void*, int)` via `put(void*, size_t)`.
 */
void
FileDataSink::putItem(const CRingItem& item)
{
    // Get the underlying structure containing the state:
    
    const RingItem* pItem = item.getItemPointer();

    // Set up variable for writing it to stream:
    
    put(pItem, item.size());
}

/**
 * @note The underlying implemenation is just
 * `fmtio::writeData(int, void*, int)`. It's int exception is converted to 
 * an errno exception
 */
void
FileDataSink::put(const void* pData, size_t nBytes)
{
    try {
	fmtio::writeData(m_fd, pData, nBytes);
    } catch (int err) {
	errno = err; // CErrnoException captures the global errno.
	std::string errmsg("FileDataSink::putItem(const CRingItem&)"); 
	errmsg += " : writeData failed ";
 
	throw CErrnoException(errmsg);
    }
}

/**
 * @note The underlying implemenation is just 
 * `fmtio::writeDataVUnlimited(int, void*, int)`. It's int exception is 
 * converted to  an errno exception.
 */
void
FileDataSink::putV(iovec* iovs, size_t iovcnt)
{
    try {
	fmtio::writeDataVUnlimited(m_fd, iovs, iovcnt);
    }
    catch (int err) {
	errno = err; // CErrnoException captures the global errno.
	std::string msg("FileDataSink::putV(iovec* iovs, size_t iovcnt) "
			"call to fmtio::writeDataVUnlimited() failed");
	throw CErrnoException(msg);
    }
}

void FileDataSink::flush()
{
    int retval = fsync(m_fd); 
    if (retval<0) {
	throw CErrnoException("FileDataSink::flush() failed");
    }
}

bool
FileDataSink::isWritable() 
{
    // Get the status flags of the file
    int status = fcntl(m_fd, F_GETFL);

    if (status < 0) {
	std::string errmsg ("FileDataSink::isWritable()");
	errmsg += " failed checking file status flags";
	throw CErrnoException(errmsg);
    }; 

    // Check if we can write
    return ( (status&O_WRONLY)!=0 || (status&O_RDWR)!=0 );
}
