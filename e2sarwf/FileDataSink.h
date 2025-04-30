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

// #include <unistd.h>
// #include <errno.h>
#include <string>

// #include <CErrnoException.h>

/**
 * @class FileDataSink
 * @brief A file data sink
 * @details
 * Owns and manages a general file object. The user should prefer constructing from a filename rather than a file descriptor because this reduces the risk for leaking a file.
 */
class FileDataSink : public DataSink
{
private: 
    int m_fd; //!< The file descriptor

public:
    FileDataSink (int fd);    
    FileDataSink (std::string pathname);

    virtual ~FileDataSink ();    

private:
    // Copy and assignment are not sensible b/c ownership
    // of the file becomes ambiguous
    FileDataSink(const FileDataSink&);
    FileDataSink& operator=(const FileDataSink&);

    // Implementation of required interface:
public:
    virtual void putItem(const ufmt::CRingItem& item);
    virtual void put(const void* pData, size_t nBytes);
    virtual void putV(iovec* iovs, size_t iovcnt);

    /** @brief Flush file to syncronize */
    void flush();

private:
    bool isWritable();

};

#endif
