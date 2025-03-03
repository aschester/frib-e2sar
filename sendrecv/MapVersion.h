/**
 * @file MapVersion.h
 * @brief Map version string to NSCLDAQ format version.
 */

#ifndef MAPVERSION_H
#define MAPVERSION_H

#include <stdexcept>

#include <NSCLDAQFormatFactorySelector.h>

/**
 * @brief Map the version we get from the command line to a factory version.
 * @param fmtIn Format the user requested.
 * @throw std::invalid_argument Bad format version.
 * @return Factory version ID (from the enum).
 */
ufmt::FormatSelector::SupportedVersions
mapVersion(int fmtIn)
{
    switch (fmtIn) {
    case 12:
	return ufmt::FormatSelector::v12;
    case 11:
	return ufmt::FormatSelector::v11;
    case 10:
	throw std::invalid_argument("NSCLDAQ 10 is not currently supported");
    default:
	throw std::invalid_argument("Invalid DAQ format version specifier");
    }
}

#endif
