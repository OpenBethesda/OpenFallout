#ifndef APPS_OPENFALLOUT_OPTIONS_H
#define APPS_OPENFALLOUT_OPTIONS_H

#include <components/esm3/missingcontent.hpp>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>

namespace OpenMW
{
    boost::program_options::options_description makeOptionsDescription();

    // The answer of --missing-content (ask when it is not given); a word that is not an answer throws
    ESM::MissingContentAnswer readMissingContentAnswer(const boost::program_options::variables_map& variables);
}

#endif
