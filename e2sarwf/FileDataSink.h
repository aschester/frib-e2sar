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
 * @file FileDataSink.h
 * @brief As the name implies, a file sink for NSCLDAQ data
 */

#ifndef FILEDATASINK_H
#define FILEDATASINK_H

#include "DataSink.h"

#include <string>

/**
 * @class FileDataSink
 * @brief A file data sink
 * @details
 * Owns and manages a general file object. The user should prefer constructing 
 * from a filename rather than a file descriptor because this reduces the risk 
 * for leaking a file.
 */
class FileDataSink : public DataSink
{
private: 
    int m_fd; //!< The file descriptor

public:
    /**
     * @brief Construct from a file descriptor
     * @param fd a file descriptor to define the sink
     * @throw std::string If fd is writable
     */
    FileDataSink(int fd);
    /**
     * @brief Construct from a file name
     * @param fname A file descriptor to define the sink
     * @throw CErrnoException On failure opening file
     * @throw std::string If file is not writable
     */
    FileDataSink(std::string pathname);
    /** @brief Destructor */
    virtual ~FileDataSink ();    

private:
    /**
     * @name DeletedOperations
     * @brief Copy and assignment are not sensible b/c ownership of the file 
     * becomes ambiguous
     */
     /**@{*/
    FileDataSink(const FileDataSink&);
    FileDataSink& operator=(const FileDataSink&);
    /**@}*/

public:
    /**
     * @brief Write ring item to the sink
     * @param item Refers to a CRingItem that contains the data
     * @throw CErrnoException When io failure
     */
    virtual void putItem(const ufmt::CRingItem& item);
    /**
     * @brief Puts an arbitrary chunk of data to the sink (file)
     * @param pData  Pointer to the buffer containing the data.
     * @param nBytes Number of bytes of data to put.
     * @throw CErrnoException If the underlying write call fails
     */
    virtual void put(const void* pData, size_t nBytes);
    /**
     * @brief Puts data from iovecs into the sink (file)
     * @param iovs Pointer to iovec data
     * @param iovcnt Number of iovecs
     * @throw CErrnoException If the underlying write call fails
     */
    virtual void putV(iovec* iovs, size_t iovcnt);

    /** @brief Flush file to syncronize */
    void flush();

private:
    /**
     * @brief Check if write operates are allowed on file
     * @throw CErrnoException if fcntl failed while checking file status
     * @return True if the file is writeable, false otherwise
     */
    bool isWritable();

};

#endif
