/**
 * @file minimizerSeeding.h
 * @brief links libMA to the minimiap 2 code...
 * @author Markus Schmidt
 */
#ifndef MINIMIZER_SEEDING_H
#define MINIMIZER_SEEDING_H

#include "container/minimizer_index.h"
#include "container/pack.h"
#include "container/seed.h"
#include "module/module.h"
#include "util/system.h"

#ifdef WITH_ZLIB
namespace libMA
{

/**
 * @brief Computes a maximally covering set of seeds.
 * @details
 * Can use either the extension scheme by Li et Al. or ours.
 * @ingroup module
 */
class MinimizerSeeding : public Module<Seeds, false, minimizer::Index, NucSeq, Pack>
{
  public:
    /**
     * @brief Initialize a MinimizerSeeding Module
     * @details
     */
    MinimizerSeeding( const ParameterSetManager& rParameters )
    {} // constructor

    std::shared_ptr<Seeds> execute( std::shared_ptr<minimizer::Index> pMMIndex, std::shared_ptr<NucSeq> pQuerySeq,
                                    std::shared_ptr<Pack> pPack )
    {
        auto sStr = pQuerySeq->toString( );
        return pMMIndex->seed_one( sStr, pPack );
    } // method
}; // class

} // namespace libMA

#ifdef WITH_PYTHON
/**
 * @brief exports the Segmentation @ref Module "module" to python.
 * @ingroup export
 */
void exportMinimizerSeeding( py::module& rxPyModuleId );
#endif
#endif

#endif